#include "render.h"
#include "time.h"
#include "serialization.h"
#include "parallel.h"

LayoutRender::LayoutRender(Layout&& _this, const float _mmPx, const float _inchPx) : Layout(move(_this)) {
	assert_((_mmPx>0) ^ (_inchPx>0));
	const float mmPx = _mmPx ? _mmPx : _inchPx/inchMM;
	assert_(mmPx);

	// -- Renders each element
	//Time loadTime;
	buffer<Image> images = apply(elements, [=](const Element& e) { return e.size(mmPx)>int2(0) ? e.image(mmPx) : Image(); });
	//log(loadTime);

	if(0) {  // -- Evaluates resolution
		const float inchPx = _inchPx ? _inchPx : _mmPx*inchMM;
		assert_(inchPx);
		float minScale = inf, maxScale = 0;
		for(size_t elementIndex: range(elements.size)) {
			float scale = images[elementIndex].size.x / elements[elementIndex]->size(mmPx).x;
			minScale = min(minScale, scale);
			maxScale = max(maxScale, scale);
		}
		log("@"+str(mmPx)+"ppmm "+str(inchPx)+"ppi: \t "+
			"min: "+str(minScale)+"x "+str(minScale*mmPx)+"ppmm "+str(minScale*inchPx)+"ppi \t"+
			"max: "+str(maxScale)+"x "+str(maxScale*mmPx)+"ppmm "+str(maxScale*inchPx)+"ppi");
	}

	// -- Evaluates each elements dominant color (or use user argument overrides)
	buffer<v4sf> innerBackgroundColors = apply(images.size, [&](const size_t elementIndex) {
		const Image& iSource = images[elementIndex];
		int hueHistogram[0x100] = {}; mref<int>(hueHistogram).clear(0); // 1½K: 32bit / 0xFF -> 4K² images
		int intensityHistogram[0x100] = {}; mref<int>(intensityHistogram).clear(0);
		for(byte4 c: iSource) {
			const int B = c.b, G = c.g, R = c.r;
			const int M = max(max(B, G), R);
			const int m = min(min(B, G), R);
			const int C = M - m;
			const int I = (B+G+R)/3;
			intensityHistogram[I]++;
			if(C) {
				int H;
				if(M==R) H = ((G-B)*43/C+0x100)%0x100; // 5-6 0-1
				else if(M==G) H = (B-R)*43/C+85; // 1-3
				else if(M==B) H = (R-G)*43/C+171; // 3-5
				else ::error(B, G, R);
				hueHistogram[H] += C;
			}
		}
		int H = argmax(ref<int>(hueHistogram));
		if(arguments.contains("hue"_)) H = parse<float>(arguments.at("hue"_)) * 0xFF;
		int C = parse<float>(arguments.value("chroma"_, "1/4"_)) * 0xFF;
		int X = C * (0xFF - abs((H%85)*6 - 0xFF)) / 0xFF;
		int I = argmax(ref<int>(intensityHistogram));
		if(arguments.contains("intensity"_)) I = parse<float>(arguments.at("intensity"_)) * 0xFF;
		int R,G,B;
		if(H < 43) R=C, G=X, B=0;
		else if(H < 85) R=X, G=C, B=0;
		else if(H < 128) R=0, G=C, B=X;
		else if(H < 171) R=0, G=X, B=C;
		else if(H < 213) R=X, G=0, B=C;
		else if(H < 256) R=C, G=0, B=X;
		else ::error(H);
		int m = max(0, I - (R+G+B)/3);
		// Limits intensity within sRGB
		m = min(m, 0xFF-R);
		m = min(m, 0xFF-G);
		m = min(m, 0xFF-B);
		extern float sRGB_reverse[0x100];
		return v4sf{sRGB_reverse[m+B], sRGB_reverse[m+G], sRGB_reverse[m+R], 0};
	});

	ImageF target(int2(round(size * mmPx)));
	target.clear(float4(0));

#if 0 // TODO: per row column transition
	// -- Transitions exterior borders to background color
	const int iX = floor(outerMM.x*mmPx);
	const int iY = floor(outerMM.y*mmPx);
	v4sf outerBackgroundColor = float4(1) * ::mean(innerBackgroundColors);
	vec2 margin = (outerMM/*-innerMM*/)*mmPx; // Transition on inner margin size, outer outer margin is constant
	vec2 innerPx = 0;//innerMM*mmPx;
	int2 size = target.size;
	// Outer background vertical sides
	if(iX > 0) parallel_chunk(max(0, iY), min(size.y, size.y-iY), [&](uint, int Y0, int DY) {
		for(int y: range(Y0, Y0+DY)) {
			v4sf* line = target.begin() + y*target.stride;
			for(int x: range(iX)) {
				float w = constantMargin ? (x>=margin.x ? 1 - (float(x)-margin.x) / float(innerPx.x) : 1) : float(iX-x) / iX;
				assert(w >= 0 && w <= 1);
				v4sf c = float4(w) * outerBackgroundColor;;
				line[x] += c;
				line[size.x-1-x] += c;
			}
		}
	});
	// Outer background horizontal sides
	if(iY > 0) parallel_chunk(iY, [&](uint, int Y0, int DY) {
		for(int y: range(Y0, Y0+DY)) {
			v4sf* line0 = target.begin() + y*target.stride;
			v4sf* line1 = target.begin() + (size.y-1-y)*target.stride;
			float w = constantMargin ? (y>=margin.y ? 1 - float(y-margin.y) / float(innerPx.y) : 1) : float(iY-y) / iY;
			assert(w >= 0 && w <= 1);
			for(int x: range(max(0, iX), min(size.x, size.x-iX))) {
				v4sf c = float4(w) * outerBackgroundColor;
				line0[x] += c;
				line1[x] += c;
			}
		}
	});
	// Outer background corners
	if(iX > 0 && iY > 0) parallel_chunk(iY, [&](uint, int Y0, int DY) {
		for(int y: range(Y0, Y0+DY)) {
			v4sf* line0 = target.begin() + y*target.stride;
			v4sf* line1 = target.begin() + (target.size.y-1-y)*target.stride;
			float yw = constantMargin ? (y>=margin.y ? float(y-margin.y) / float(innerPx.y) : 0) : 1 - float(iY-y) / iY;
			for(int x: range(iX)) {
				float xw =  constantMargin ? (x>=margin.x ? float(x-margin.x) / float(innerPx.x) : 0) : 1 - float(iX-x) / iX;
				float w = (1-xw)*yw + xw*(1-yw) + (1-xw)*(1-yw);
				assert(w >= 0 && w <= 1);
				v4sf c = float4(w) * outerBackgroundColor;
				line0[x] += c;
				line0[size.x-1-x] += c;
				line1[x] += c;
				line1[size.x-1-x] += c;
			}
		}
	});
#endif

	// -- Copies source images to target
	for(size_t elementIndex: range(elements.size)) {
		const Element& element = elements[elementIndex];
		float x0 = element.min.x, x1 = element.max.x;
		float y0 = element.min.y, y1 = element.max.y;
		int ix0 = round(x0*mmPx), iy0 = round(y0*mmPx);
		int ix1 = round(x1*mmPx), iy1 = round(y1*mmPx);
		int2 size(ix1-ix0, iy1-iy0);
		if(!(size>int2(0))) continue;
		assert(size>0);

		const Image& image = images[elementIndex];
		//if(size == image.size) log(size); else log(image.size, "→", size);
		// TODO: single pass linear resize, float conversion (or decode to float and resize) + direct output to target
		Image iSource = size == image.size ? share(image) : resize(size, image);
		ImageF source (size);
		parallel_chunk(iSource.Ref::size, [&](uint, size_t I0, size_t DI) {
			extern float sRGB_reverse[0x100];
			for(size_t i: range(I0, I0+DI)) source[i] = {sRGB_reverse[iSource[i][0]], sRGB_reverse[iSource[i][1]], sRGB_reverse[iSource[i][2]],
														 image.alpha ? float(iSource[i][2])/0xFF : 1};
		});

		//log(strx(int2(ix0, iy0)), strx(int2(ix1, iy1)), strx(target.size));
#if 0
		// -- Margins
		int currentRowIndex = element.index[0];
		int columnIndex = element.index[1];
		float leftMarginMM = (constantMargin || columnIndex) ? columnInnerMarginsMM[columnIndex-1] : rowOuterMarginsMM[currentRowIndex];
		float aboveMarginMM = (constantMargin || currentRowIndex) ? rowInnerMarginsMM[currentRowIndex-1] : columnOuterMarginsMM[columnIndex];
		size_t nextColumnIndex=columnIndex+1;
		while(nextColumnIndex < rows.size && rows[currentRowIndex][nextColumnIndex]==-1) nextColumnIndex++;
		bool lastColumn = nextColumnIndex==rows[currentRowIndex].size;
		float rightMarginMM = (!lastColumn || constantMargin) ? columnInnerMarginsMM[nextColumnIndex-1] : rowOuterMarginsMM[currentRowIndex];
		size_t nextRowIndex=currentRowIndex+1;
		while(nextRowIndex < rows.size && rows[nextRowIndex][columnIndex]==-2) nextRowIndex++;
		bool lastRow = nextRowIndex==rows.size;
		float belowMarginMM = (!lastRow || constantMargin) ? rowInnerMarginsMM[nextRowIndex-1] : columnOuterMarginsMM[columnIndex];

		// min,size,max pixel coordinates
		int iw0 = ix0-round((x0-leftMarginMM)*mmPx), ih0 = iy0-round((y0-aboveMarginMM)*mmPx); // Previous border sizes
		int iw1 = round((x1+rightMarginMM)*mmPx) - ix1, ih1 = round((y1+belowMarginMM)*mmPx) - iy1; // Next border sizes

		if(0) { // -- Extends images over margins with a mirror transition
			// Clamps border transition size to image size to mirror once
			if(iw0 > size.x) iw0 = size.x; if(iw1 > size.x) iw1 = size.x;
			if(ih0 > size.y) ih0 = size.y; if(ih1 > size.y) ih1 = size.y;
			for(int y: range(min(ih0, iy0))) {
				mref<v4sf> line = target.slice((iy0-y-1)*target.stride, target.width);
				for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4((1-x/float(iw0))*(1-y/float(ih0))) * source(x, y); // Left
				for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] += float4(1-y/float(ih0)) * source(x-ix0, y); // Center
				for(int x: range(max(0, ix1), min(ix1+iw1, target.size.x))) line[x] += float4(1-(x-ix1)/float(iw1)*(1-y/float(ih0))) * source(size.x-1-x, y); // Right
			}
			parallel_chunk(max(0, iy0), min(iy0+size.y, target.size.y), [&](uint, int Y0, int DY) { // Center
				for(int y: range(Y0, Y0+DY)) {
					v4sf* line = target.begin() + y*target.stride;
					for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4(1-x/float(iw0)) * source(x, y-iy0); // Left
					v4sf* sourceLine = source.begin() + (y-iy0)*source.stride;
					for(int x: range(max(0, ix0), min(target.size.x, ix0+size.x))) line[x] = sourceLine[x-ix0]; // Copy image
					for(int x: range(max(0, ix1), min(ix1+iw1, target.size.x))) line[x] += float4(1-(x-ix1)/float(iw1)) * source(size.x-1-x, y-iy0); // Right
				}
			});
			for(int y: range(max(0, iy1), min(iy1+ih1, target.size.y))) {
				v4sf* line = target.begin() + y*target.stride;
				for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4((1-x/float(iw0))*(1-(y-iy1)/float(ih1))) * source(x-ix0, size.y-1-(y-iy1)); // Left
				for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] += float4(1-(y-iy1)/float(ih1)) * source(x-ix0, size.y-1-(y-iy1)); // Center
				for(int x: range(max(0, ix1), min(ix1+iw1, target.size.x))) line[x] += float4(1-(x-ix1)/float(iw1)*(1-(y-iy1)/float(ih1))) * source(size.x-1-x, size.y-1-(y-iy1)); // Right
			}
		} else if(1) { // -- Blends inner background over margins with a linear transition
			v4sf innerBackgroundColor = innerBackgroundColors[elementIndex];
			for(int y: range(min(ih0, iy0))) {
				mref<v4sf> line = target.slice((iy0-y-1)*target.stride, target.width);
				for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4((1-x/float(iw0))*(1-y/float(ih0))) * innerBackgroundColor; // Left
				for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] += float4(1-y/float(ih0)) * innerBackgroundColor; // Center
				for(int x: range(max(0, ix1), min(ix1+iw1, target.size.x))) line[x] += float4((1-(x-ix1)/float(iw1))*(1-y/float(ih0))) * innerBackgroundColor; // Right
			}
			parallel_chunk(max(0, iy0), min(iy0+size.y, target.size.y), [&](uint, int Y0, int DY) { // Center
				for(int y: range(Y0, Y0+DY)) {
					v4sf* line = target.begin() + y*target.stride;
					for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4(1-x/float(iw0)) * innerBackgroundColor; // Left
					v4sf* sourceLine = source.begin() + (y-iy0)*source.stride;
					for(int x: range(max(0, ix0), min(target.size.x, ix0+size.x))) line[x] = sourceLine[x-ix0]; // Copy image
					for(int x: range(max(0, ix1), min(ix1+iw1, target.size.x))) line[x] += float4(1-(x-ix1)/float(iw1)) * innerBackgroundColor; // Right
				}
			});
			for(int y: range(max(0, iy1), min(iy1+ih1, target.size.y))) {
				v4sf* line = target.begin() + y*target.stride;
				for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4((1-x/float(iw0))*(1-(y-iy1)/float(ih1))) * innerBackgroundColor; // Left
				for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] += float4(1-(y-iy1)/float(ih1)) * innerBackgroundColor; // Center
				for(int x: range(max(0, ix1), min(ix1+iw1, target.size.x))) line[x] += float4((1-(x-ix1)/float(iw1))*(1-(y-iy1)/float(ih1))) * innerBackgroundColor; // Right
			}
		}
#else
		parallel_chunk(max(0, iy0), min(iy0+size.y, target.size.y), [&](uint, int Y0, int DY) { // Center
			for(int y: range(Y0, Y0+DY)) {
				v4sf* line = target.begin() + y*target.stride;
				v4sf* sourceLine = source.begin() + (y-iy0)*source.stride;
				for(int x: range(max(0, ix0), min(target.size.x, ix0+size.x))) line[x] = sourceLine[x-ix0]; // Copy image
			}
		});
#endif
	}

	{  int clip=0, nan = 0;
		for(size_t i : range(target.Ref::size)) {
			for(uint c: range(3)) {
				float v = target[i][c];
				if(v >= 0 && v <= 1) continue;
				if(v < 0) v = 0;
				else if(v > 1) v = 1;
				else {
					nan++;
					//if(!nan) log("NaN", v, i, c);
				}
				//if(!clip) log("Clip", v, i, c);
				clip++;
			}
		}
		//if(clip) log("Clip", clip);
	}

	if(0) { // -- Large gaussian blur approximated with repeated box convolution
		//log("Blur");
		//Time blurTime;
		ImageF blur(target.size);
		{
			ImageF transpose(target.size.y, target.size.x);
			const int R = min(target.size.x, target.size.y) / 8;
			//const int R = max(min(widths), min(heights))/4; //8
			box(transpose, target, R/*, outerBackgroundColor*/);
			box(blur, transpose, R/*, outerBackgroundColor*/);
			box(transpose, blur, R/*, outerBackgroundColor*/);
			box(blur, transpose, R/*, outerBackgroundColor*/);
		}
		if(0) for(const Element& element : elements) { // -- Copies source images over blur background
			float x0 = element.min.x, x1 = element.max.x;
			float y0 = element.min.y, y1 = element.max.y;
			int ix0 = round(x0*mmPx), iy0 = round(y0*mmPx);
			int ix1 = round(x1*mmPx), iy1 = round(y1*mmPx);
			if(ix1<=ix0 || iy1<=iy0) continue;
			parallel_chunk(max(0, iy0), min(target.size.y, iy1), [&](uint, int Y0, int DY) {
				for(int y: range(Y0, Y0+DY)) {
					v4sf* blurLine = blur.begin() + y*blur.stride;
					const v4sf* targetLine = target.begin() + y*target.stride;
					for(int x: range(max(0, int(ix0)), min(target.size.x, int(ix1)))) {
						// TODO: only blend if image.alpha
						blurLine[x] = mix(blurLine[x], targetLine[x], targetLine[x][3]);
					}
				}
			});
		}
		if(0) for(size_t elementIndex: range(elements.size)) { // -- Feathers
			if(images[elementIndex].alpha) continue;
			const Element& element = elements[elementIndex];
			float x0 = element.min.x, x1 = element.max.x;
			float y0 = element.min.y, y1 = element.max.y;
			int ix0 = round(x0*mmPx), iy0 = round(y0*mmPx);
			int ix1 = round(x1*mmPx), iy1 = round(y1*mmPx);
			int2 size(ix1-ix0, iy1-iy0);
			if(!(size.x>0 && size.y>0)) continue;
			assert(size.x>0 && size.y>0);

			// Element min,size,max pixel coordinates
			//int2 feather = int2(floor(clamp(vec2(0), innerMM/2.f, vec2(1))*mmPx));
			//int2 feather = int2(floor(min(innerMM.x, innerMM.y)/2.f*mmPx));
			//int2 feather = int2(floor(innerMM/2.f*mmPx));
			int2 feather = 1*mmPx;
			int iw0 = feather.x, ih0 = feather.y; // Previous border sizes
			int iw1 = feather.x, ih1 = feather.y; // Next border sizes

			// Clamps border transition size to image size to mirror once
			if(iw0 > size.x) iw0 = size.x; if(iw1 > size.x) iw1 = size.x;
			if(ih0 > size.y) ih0 = size.y; if(ih1 > size.y) ih1 = size.y;

			for(int y: range(min(min(ih0, iy0), target.size.y-iy0))) {
				mref<v4sf> sourceLine = target.slice((iy0+y)*target.stride, target.width);
				mref<v4sf> line = blur.slice((iy0-y-1)*target.stride, target.width);
				for(int x: range(min(iw0, ix0))) line[ix0-x-1] = mix(line[ix0-x-1], sourceLine[ix0+x], sourceLine[ix0+x][3]*(1-x/float(iw0))*(1-y/float(ih0))); // Left
				for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] = mix(line[x], sourceLine[x], sourceLine[x][3]*(1-y/float(ih0))); // Center
				for(int x: range(iw1)) line[ix1+x] = mix(line[ix1+x], sourceLine[ix1+x], sourceLine[ix1+x][3]*(1-x/float(iw1))*(1-y/float(ih0))); // Right
			}
			parallel_chunk(max(0, iy0), min(iy0+size.y, target.size.y), [&](uint, int Y0, int DY) { // Center
				for(int y: range(Y0, Y0+DY)) {
					v4sf* sourceLine = target.begin() + y*target.stride;
					v4sf* line = blur.begin() + y*target.stride;
					for(int x: range(min(iw0, ix0))) line[ix0-x-1] = mix(line[ix0-x-1], sourceLine[ix0+x], sourceLine[ix0+x][3]*(1-x/float(iw0))); // Left
					for(int x: range(iw1)) line[ix1+x] = mix(line[ix1+x], sourceLine[ix1-x-1], sourceLine[ix1-x-1][3]*(1-x/float(iw1))); // Right
				}
			});
			for(int y: range(max(0, iy1), min(iy1+min(ih1, iy1), target.size.y))) {
				v4sf* sourceLine = target.begin() + (iy1-1-(y-iy1))*target.stride;
				v4sf* line = blur.begin() + y*target.stride;
				for(int x: range(min(iw0, ix0))) line[ix0-x-1] = mix(line[ix0-x-1], sourceLine[ix0+x], sourceLine[ix0+x][3]*(1-x/float(iw0))*(1-(y-iy1)/float(ih1))); // Left
				for(int x: range(max(0,ix0), min(target.size.x, ix0+size.x))) line[x] = mix(line[x], sourceLine[x], sourceLine[x][3]*(1-(y-iy1)/float(ih1))); // Center
				for(int x: range(iw1)) line[ix1+x] = mix(line[ix1+x], sourceLine[ix1-x-1], sourceLine[ix1-x-1][3]*(1-x/float(iw1))*(1-(y-iy1)/float(ih1))); // Right
			}
		}
		target = move(blur);
		//log(blurTime);
	}

	// -- Convert back to 8bit sRGB
	Image iTarget (target.size);
	assert(target.Ref::size == iTarget.Ref::size);
	parallel_chunk(target.Ref::size, [&](uint, size_t I0, size_t DI) {
		extern uint8 sRGB_forward[0x1000];
		int clip = 0;
		for(size_t i: range(I0, I0+DI)) {
			int3 linear;
			for(uint c: range(3)) {
				float v = target[i][c];
				if(!(v >= 0 && v <= 1)) {
					if(v < 0) v = 0;
					else if(v > 1) v = 1;
					else v = 0; // NaN
					//if(!clip) log("Clip", v, i, c);
					clip++;
				}
				linear[c] = int(round(0xFFF*v));
			}
			iTarget[i] = byte4( sRGB_forward[linear[0]], sRGB_forward[linear[1]], sRGB_forward[linear[2]] );
		}
		//if(clip) log("Clip", clip);
		//assert(!clip);
	});
	this->target = move(iTarget);
}
