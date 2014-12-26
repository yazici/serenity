/// \file blend.cc Automatic exposure blending
#include "source-view.h"
#include "serialization.h"
#include "image-folder.h"
#include "split.h"
#include "operation.h"
#include "align.h"
#include "weight.h"
#include "multiscale.h"
#include "jpeg-encoder.h"
#include "layout.h"

#if 0
struct ExposureBlendAnnotate : Application {
	Folder folder {"Pictures/ExposureBlend", home()};
	PersistentValue<map<String, String>> imagesAttributes {folder,"attributes"};
	ImageFolder source { folder };

	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	sRGBOperation sRGB {source};
	ImageSourceView view {sRGB, &index};
	Window window {&view, -1, [this]{ return view.title()+" "+imagesAttributes.value(source.elementName(index)); }};

	ExposureBlendAnnotate() {
		for(char c: range('0','9'+1)) window.actions[Key(c)] = [this, c]{ setCurrentImageAttributes("#"_+c); };
		for(char c: range('a','z'+1)) window.actions[Key(c)] = [this, c]{ setCurrentImageAttributes("#"_+c); };
	}
	void setCurrentImageAttributes(string currentImageAttributes) {
		imagesAttributes[source.elementName(index)] = copyRef(currentImageAttributes);
	}
};
registerApplication(ExposureBlendAnnotate, annotate);
#endif

struct ExposureBlend {
	Folder folder {"Documents/Pictures/ExposureBlend", home()};
	PersistentValue<map<String, String>> imagesAttributes {folder,"attributes"};

	ImageFolder source { folder };
	ImageOperationT<Intensity> intensity {source};
	ImageOperationT<Normalize> normalize {intensity};
	//DifferenceSplit split {normalize};
	AllImages split {source};

	GroupImageOperation splitNormalize {normalize, split};
	ImageGroupTransformOperationT<Align> transforms {splitNormalize};

	GroupImageOperation splitSource {source, split};
	SampleImageGroupOperation alignSource {splitSource, transforms};

	ImageGroupOperationT<Exposure> exposure {alignSource};

	ImageGroupOperationT<SelectMaximum> selectionWeights {exposure};
	BinaryImageGroupOperationT<Multiply> applySelectionWeights {selectionWeights, alignSource};
	ImageGroupFoldT<Sum> select {applySelectionWeights};

	ImageGroupOperationT<SmoothStep> stepExposure {exposure};
	ImageGroupOperationT<NormalizeSum> weights {stepExposure};

	BinaryImageGroupOperationT<Multiply> applyWeights {weights, alignSource};
	ImageGroupFoldT<Sum> direct {applyWeights};

	ImageGroupOperationT<WeightFilterBank> weightBands {weights}; // Splits each weight selection in bands
	ImageGroupOperationT<NormalizeSum> normalizeWeightBands {weightBands}; // Normalizes weight selection for each band

	ImageGroupForwardComponent multiscale {alignSource, sumBands};
	ImageGroupOperationT<FilterBank> splitBands {multiscale.source};
	BinaryImageGroupOperationT<Multiply> weightedBands {normalizeWeightBands, splitBands}; // Applies weights to each band
	ImageGroupOperationT<Sum> sumBands {weightedBands}; // Sums bands
	ImageGroupFoldT<Sum> blend {multiscale}; // Sums images
};

struct Transpose : OperatorT<Transpose> { /*string name() const override { return  "Transpose"; }*/ };
/// Swaps component and group indices
struct TransposeOperation : GenericImageOperation, ImageGroupSource, OperatorT<Transpose> {
	ImageGroupSource& source;
	TransposeOperation(ImageGroupSource& source) : GenericImageOperation(source, *this), source(source) {}

	size_t outputs() const override { return source.groupSize(0); /*Assumes all groups have same size*/ }
	size_t groupSize(size_t) const { return source.outputs(); }
	array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 size, string parameters = "") {
		array<SourceImage> outputs;
		assert_(source.outputs(), source.toString());
		for(size_t outputIndex: range(source.outputs())) {
			auto inputs = source.images(groupIndex, outputIndex, size, parameters);
			assert_(componentIndex < inputs.size);
			outputs.append( move(inputs[componentIndex]) );
		}
		return outputs;
	}
};

struct ExposureBlendPreview : ExposureBlend, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	sRGBOperation sRGB [3] {blend, direct, select};
	array<ImageSourceView> sRGBView = apply(mref<sRGBOperation>(sRGB),
											[&](ImageRGBSource& source) -> ImageSourceView { return {source, &index}; });
	VBox views {toWidgets(sRGBView), VBox::Share, VBox::Expand};
	Window window {&views, -1, [this]{ return views.title(); }};
};
registerApplication(ExposureBlendPreview);

#if 0
struct ExposureBlendTest : ExposureBlend, Application {
	ExposureBlendTest() {
		for(size_t groupIndex=0; split.nextGroup(); groupIndex++) {
			auto names = apply(split(groupIndex), [this](const size_t index) { return copy(source.elementName(index)); });
			auto set = apply(names, [this](string name) { return copy(imagesAttributes.at(name)); });
			log(names);
			log(set);
			for(const auto& e: set) assert_(e == set[0]);
		}
	}
};
registerApplication(ExposureBlendTest, test);
#endif

struct ExposureBlendExport : ExposureBlend, Application {
	sRGBOperation sRGB {blend};
	ExposureBlendExport() {
		Folder output ("Export", folder, true);
		for(size_t index: range(sRGB.count(-1))) {
			String name = sRGB.elementName(index);
			Time correctionTime;
			SourceImageRGB image = sRGB.image(index);
			correctionTime.stop();
			Time compressionTime;
			writeFile(name, encodeJPEG(image), output, true);
			compressionTime.stop();
			log(str(100*(index+1)/sRGB.count(-1))+'%', '\t',index+1,'/',sRGB.count(-1),
				'\t',sRGB.elementName(index),
				'\t',correctionTime, compressionTime);
		}
	}
};
registerApplication(ExposureBlendExport, export);

#if 0
struct ExposureBlendSelect : ExposureBlend, Application {
	sRGBOperation sRGB {select};
	ExposureBlendSelect() {
		Folder output ("Export", folder, true);
		for(size_t index: range(sRGB.count(-1))) {
			String name = sRGB.elementName(index);
			Time correctionTime;
			SourceImageRGB image = sRGB.image(index/*, int2(2048,1536), true*/);
			correctionTime.stop();
			Time compressionTime;
			writeFile(name+".select", encodeJPEG(image, 75), output, true);
			compressionTime.stop();
			log(str(100*(index+1)/sRGB.count(-1))+'%', '\t',index+1,'/',sRGB.count(-1),
				'\t',sRGB.elementName(index),
				'\t',correctionTime, compressionTime);
		}
	}
};
registerApplication(ExposureBlendSelect, select);
#endif