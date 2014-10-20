/// \file blend.cc Automatic exposure blending
#include "serialization.h"
#include "image-folder.h"
#include "process.h"
#include "normalize.h"
#include "difference.h"
#include "source-view.h"

/// Returns groups of consecutive pairs
struct ConsecutivePairs : virtual GroupSource {
	Source& source;
	ConsecutivePairs(Source& source) : source(source) {}
	size_t count(size_t unused need=0) override { return source.count()-1; }
	int64 time(size_t index) override { return max(apply(operator()(index), [this](size_t index) { return source.time(index); })); }
	array<size_t> operator()(size_t groupIndex) override {
		assert_(groupIndex < count());
		array<size_t> pair;
		pair.append(groupIndex);
		pair.append(groupIndex+1);
		return pair;
	}
};

/// Returns images from an image source grouped by a group source
struct ProcessedImageGroupSource : ImageGroupSource {
	ImageSource& source;
	GroupSource& groups;
	ProcessedImageGroupSource(ImageSource& source, GroupSource& groups) : source(source), groups(groups) {}
	size_t count(size_t need=0) override { return groups.count(need); }
	String name() const override { return source.name(); }
	int outputs() const override { return source.outputs(); }
	const Folder& folder() const override { return source.folder(); }
	int2 maximumSize() const override { return source.maximumSize(); }
	int64 time(size_t groupIndex) override { return max(apply(groups(groupIndex), [this](size_t index) { return source.time(index); })); }
	virtual String elementName(size_t groupIndex) const override {
		return str(apply(groups(groupIndex), [this](const size_t imageIndex) { return source.elementName(imageIndex); }));
	}
	int2 size(size_t groupIndex) const override {
		auto sizes = apply(groups(groupIndex), [this](size_t imageIndex) { return source.size(imageIndex); });
		for(auto size: sizes) assert_(size == sizes[0]);
		return sizes[0];
	}

	virtual array<SourceImage> images(size_t groupIndex, int outputIndex, int2 size=0, bool noCacheWrite = false) {
		return apply(groups(groupIndex), [&](const size_t imageIndex) { return source.image(imageIndex, outputIndex, size, noCacheWrite); });
	}
};

/// Splits sequence in groups separated when difference between consecutive images is greater than a threshold
struct DifferenceSplit : GroupSource {
	static constexpr float threshold = 0.18;
	ImageSource& source;
	ConsecutivePairs pairs {source};
	ProcessedImageGroupSource sourcePairs {source, pairs};
	Subtract subtract;
	ProcessedGroupImageSource difference{sourcePairs, subtract};
	array<array<size_t>> groups;

	DifferenceSplit(ImageSource& source) : source(source) {}

	bool nextGroup() {
		size_t startIndex = groups ? groups.last().last()+1 : 0;
		if(startIndex == source.count()) return false;
		size_t endIndex = startIndex+1; // Included
		for(; endIndex < difference.count(); endIndex++) {
			SourceImage image = difference.image(endIndex, 0, difference.maximumSize()/32);
			float meanEnergy = parallel::energy(image)/image.ref::size;
			log(endIndex, difference.elementName(endIndex), meanEnergy);
			if(meanEnergy > threshold) break;
		}
		assert_(endIndex < source.count(), endIndex, source.count());
		array<size_t> group;
		for(size_t index: range(startIndex, endIndex+1/*included*/)) group.append( index );
		log(group);
		groups.append(move(group));
		return true;
	}

	size_t count(size_t need) override { while(groups.size < need && nextGroup()) {} return groups.size; }

	/// Returns image indices for group index
	array<size_t> operator()(size_t groupIndex) override {
		assert_(groupIndex < count(groupIndex+1), groupIndex, groups.size);
		return copy(groups[groupIndex]);
	}

	int64 time(size_t groupIndex) override { return max(apply(operator()(groupIndex), [this](size_t index) { return source.time(index); })); }
};

struct Mean : ImageGroupOperation1, OperationT<Mean> {
	string name() const override { return "[mean]"; }
	virtual int outputs() const { return 1; }
	virtual void apply(const ImageF& Y, ref<ImageF> X) const {
		parallel::apply(Y, [&](size_t index) { return sum(::apply(X, [index](const ImageF& x) { return x[index]; }))/X.size; });
	}
};

struct Transform {
	vec2 offset;
	/// Returns normalized source coordinates for the given normalized target coordinates
	vec2 operator()(vec2 target) const { return target + offset; }
	/// Returns scaled source coordinates for the given scaled target coordinates
	vec2 operator()(int2 target, int2 size) const { return operator()(vec2(target)/vec2(size))*vec2(size); }
};
// FIXME
static constexpr vec2 maximumImageSize = vec2(4000,3000);
String str(const Transform& o) { return str(o.offset*maximumImageSize); }
template<> Transform parse<Transform>(TextData& s) { return {parse<vec2>(s)/maximumImageSize}; }

bool operator ==(const Transform& a, const Transform& b) { return a.offset == b.offset; }

/// Composes transforms
Transform operator *(const Transform& a, const Transform& b) {
	return {a.offset+b.offset};
}

struct TransformGroupSource : Source {
	virtual array<Transform> operator()(size_t groupIndex);
};

/// Evaluates transforms for a group of images
struct ImageTransformGroupOperation : virtual Operation {
	virtual array<Transform> operator()(ref<ImageF>) const = 0;
};

struct ProcessedImageTransformGroupSource : TransformGroupSource {
	ImageGroupSource& source;
	ImageTransformGroupOperation& operation;
	Folder cacheFolder {operation.name(), source.folder(), true};
	ProcessedImageTransformGroupSource(ImageGroupSource& source, ImageTransformGroupOperation& operation) :
		source(source), operation(operation) {}

	virtual size_t count(size_t need=0) { return source.count(need); }
	virtual int64 time(size_t index) { return max(operation.time(), source.time(index)); }

	array<Transform> operator()(size_t groupIndex) override {
		assert_(source.outputs()==1);
		int2 size = source.size(groupIndex)/32;
		array<SourceImage> images = source.images(groupIndex, 0, size);
		return parseArray<Transform>(cache(cacheFolder, source.elementName(groupIndex), strx(size), source.time(groupIndex), [&]() {
			return str(operation(apply(images, [](const SourceImage& x){ return share(x); })));
		}, false));
	}
};

/// Evaluates residual energy between A and transform B
float residualEnergy(const ImageF& A, const ImageF& B, Transform transform) {
	assert_(A.size == B.size);
	int2 margin = int2(round(abs(transform.offset)*vec2(B.size)));
	int2 size = A.size - 2*margin;
	assert_(size > int2(16), transform, margin, A.size);
	float energy = sumXY(size, [&A, &B, transform](int x, int y) {
		int2 a = int2(round(abs(transform.offset)*vec2(B.size)))+int2(x,y);
		int2 b = int2(round(transform(a, B.size)));
		assert_(a >= int2(0) && a < A.size, a, A.size, b, B.size, transform.offset, int2(round(transform.offset*vec2(B.size))));
		assert_(b >= int2(0) && b < B.size);
		return sq(A(a) - B(b));
	}, 0.f);
	energy /= size.x*size.y;
	//energy -= size.x*size.y; // Advantages more overlap
	log(transform.offset, energy);
	return energy;
}

/// Evaluates first \a levelCount mipmap levels (shares source as first element)
array<ImageF> mipmap(const ImageF& source, int levelCount) {
	array<ImageF> mipmap; mipmap.append( share(source) );
	for(int unused level: range(levelCount)) mipmap.append(downsample(mipmap.last()));
	return mipmap;
}

/// Aligns images
struct Align : ImageTransformGroupOperation, OperationT<Align> {
	string name() const override { return "[align]"; }

	// Evaluates residual energy at integer offsets
	virtual array<Transform> operator()(ref<ImageF> images) const override {
		for(auto& image: images) assert_(image.size == images[0].size);
		const int levelCount = log2(uint(images[0].size.x/16)); // / (16, 12)
		array<ImageF> A = mipmap(images[0], levelCount);
		array<Transform> transforms;
		transforms.append();
		for(const ImageF& image : images.slice(1)) { // Compares each image with first one (TODO: full regression)
			array<ImageF> B = mipmap(image, levelCount);
			Transform bestTransform {vec2(0,0)};
			for(int level: range(levelCount)) { // From coarsest (last) to finest (first)
				const ImageF& a = A[levelCount-1-level];
				const ImageF& b = B[levelCount-1-level];
				Transform levelBestTransform = bestTransform;
				float bestResidualEnergy = residualEnergy(a, b, levelBestTransform);
				for(;;) { // Integer walk toward minimum at this scale
					// FIXME: Compare image subsets
					Transform stepBestTransform = levelBestTransform;
					for(int2 offset: {int2(-1,0), int2(1,0),  int2(0,-1),int2(0,1)}) { // Evaluate single steps along each translation axis
						Transform transform = levelBestTransform * Transform{vec2(offset)/vec2(b.size)};
						float energy = residualEnergy(a, b, transform);
						if(energy < bestResidualEnergy) {
							bestResidualEnergy = energy;
							stepBestTransform = transform;
						}
					}
					if(stepBestTransform==levelBestTransform) break;
					else levelBestTransform = stepBestTransform;
				}
				bestTransform = levelBestTransform;
			}
			transforms.append(bestTransform);
		}
		log(transforms);
		return transforms;
	}
};

/// Samples \a transform of \a source using nearest neighbour
void sample(const ImageF& target, const ImageF& source, Transform transform) {
	applyXY(target, [&](int x, int y) {
		int2 s = int2(round(transform(int2(x,y), source.size)));
		if(!(s >= int2(0) && s < source.size)) return 0.f;
		assert_(s >= int2(0) && s < source.size, s, source.size);
		return source(s);
	});
}
ImageF sample(ImageF&& target, const ImageF& source, Transform transform) { sample(target, source, transform); return move(target); }
ImageF sample(const ImageF& source, Transform transform) { return sample(source.size, source, transform); }

struct TransformSampleImageGroupSource : ImageGroupSource {
	ImageGroupSource& source;
	TransformGroupSource& transform;
	Folder cacheFolder {"[sample]", source.folder(), true};
	TransformSampleImageGroupSource(ImageGroupSource& source, TransformGroupSource& transform)
		: source(source), transform(transform) {}

	size_t count(size_t need=0) override { return source.count(need); }
	int64 time(size_t groupIndex) override { return max(source.time(groupIndex), transform.time(groupIndex)); }
	String name() const override { return str("[sample]", source.name()); }
	int outputs() const override { return source.outputs(); }
	const Folder& folder() const override { return cacheFolder; }
	int2 maximumSize() const override { return source.maximumSize(); }
	String elementName(size_t groupIndex) const override { return source.elementName(groupIndex); }
	int2 size(size_t groupIndex) const override { return source.size(groupIndex); }

	array<SourceImage> images(size_t groupIndex, int outputIndex, int2 size=0, bool noCacheWrite = false) override {
		auto images = source.images(groupIndex, outputIndex, size, noCacheWrite);
		auto transforms = transform(groupIndex);
		return apply(images.size, [&](size_t index) -> SourceImage { return sample(images[index], transforms[index]); });
	}
};

/// Evaluates transforms to align groups of images

struct ExposureBlend {
	Folder folder {"Pictures/ExposureBlend", home()};
	PersistentValue<map<String, String>> imagesAttributes {folder,"attributes"};
	ImageFolder source { folder };
	ProcessedSourceT<Intensity> intensity {source};
	ProcessedSourceT<BandPass> bandpass {intensity};
	ProcessedSourceT<Normalize> normalize {bandpass};
	DifferenceSplit split {normalize};
	ProcessedImageGroupSource sourceSplit {source, split};
	ProcessedImageGroupSource intensitySplit {normalize, split};

	Align align;
	ProcessedImageTransformGroupSource transforms {intensitySplit, align};

	TransformSampleImageGroupSource transformed {sourceSplit, transforms};

	Mean mean;
	ProcessedGroupImageSource unaligned {sourceSplit, mean};
	ProcessedGroupImageSource aligned {transformed, mean};
};

struct ExposureBlendPreview : ExposureBlend, Application {
	/*PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;*/
	size_t index = 0;

#if 0
	ImageSourceView views [2] {{unaligned, &index, window, source.maximumSize()/32}, {aligned, &index, window, source.maximumSize()/32}};
#else
	ImageSourceView views [2] {{unaligned, &index, window}, {aligned, &index, window}};
#endif
	WidgetToggle toggleView {&views[0], &views[1], 0};
	Window window {&toggleView, -1, [this]{ return toggleView.title()+" "+imagesAttributes.value(source.elementName(index)); }};

	ExposureBlendPreview() {
		for(char c: range('0','9'+1)) window.actions[Key(c)] = [this, c]{ setCurrentImageAttributes("#"_+c); };
	}
	void setCurrentImageAttributes(string currentImageAttributes) {
		imagesAttributes[source.elementName(index)] = String(currentImageAttributes);
	}
};
registerApplication(ExposureBlendPreview);

struct ExposureBlendTest : ExposureBlend, Application {
	ExposureBlendTest() {
		for(size_t groupIndex=0; split.nextGroup(); groupIndex++) {
			log(apply(split(groupIndex), [this](const size_t index) { return copy(imagesAttributes.at(source.elementName(index))); }));
		}
	}
};
registerApplication(ExposureBlendTest, test);
