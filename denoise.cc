/// \file denoise.cc Noise reduction
#include "image.h"
#include "parallel.h"
#include "thread.h"
#include "raw.h"
#include "demosaic.h"
#include "IT8.h"

/// Noise reduction using non-local means
// FIXME: CFA
Image4f NLM(Image4f&& target, const Image4f& source, const int pHL  = 1 /*-5 Patch radius*/, const int wHL = 1 /*-17 Window radius*/) {
	constexpr float h2 = sq(1./256); // ~ sigma/2 (sigma ~ 2-25) FIXME
	int nX=source.size.x, nY=source.size.y;
	chunk_parallel(nY, [&](uint, int Y) {
		if(Y<wHL+pHL || Y>=nY-wHL-pHL) {
			for(int c: range(3)) for(int X: range(nX))  target(X, Y)[c] = source(X,Y)[c]; // FIXME
			return;
		}
		for(int c: range(3)) for(int X: range(0, wHL+pHL)) target(X, Y)[c] = source(X,Y)[c]; // FIXME
		for(int c: range(3)) for(int X: range(nX-wHL-pHL, nX)) target(X, Y)[c] = source(X,Y)[c]; // FIXME
		for(int X: range(wHL+pHL, nX-wHL-pHL)) {
			float weights[sq(wHL+1+wHL)];
			float weightSum = 0;
			for(int dY=-wHL; dY<=wHL; dY++) {
				for(int dX=-wHL; dX<=wHL; dX++) {
					float d = 0; // Patch distance
					for(int c: range(3)) {
						for(int dy=-pHL; dy<=pHL; dy++) {
							for(int dx=-pHL; dx<=pHL; dx++) {
								d += sq( source(X+dx, Y+dy)[c] - source(X+dX+dx, Y+dY+dy)[c] );
							}
						}
					}
					float weight = exp(-sq(d)/h2);
					weights[(wHL+dY)*(wHL+1+wHL)+(wHL+dX)] = weight;
					weightSum += weight;
				}
			}
			for(int c: range(3)) {
				float sum = 0;
				for(int dY=-wHL; dY<=wHL; dY++) {
					for(int dX=-wHL; dX<=wHL; dX++) {
						float weight = weights[(wHL+dY)*(wHL+1+wHL)+(wHL+dX)];
						sum += weight * source(X+dX, Y+dY)[c];
					}
				}
				target(X, Y)[c] = sum / weightSum;
			}
		}
	});
	return move(target);
}
Image4f NLM(const Image4f& source) { return NLM(source.size, source); }

/// Removes hot pixel
ImageF cool(ImageF&& image) {
	for(size_t y: range(2,image.size.y-2)) {
		for(size_t x: range(2,image.size.y-2)) {
			float& pixel = image(x,y);
			float mean = (image(x,y-2) + image(x-2,y) + image(x+2,y) + image(x,y+2))/4;
			if(pixel > 1./16 && pixel > mean+1./32) pixel = mean;
		}
	}
	return move(image);
}

v4sf SSD(ref<v4sf> X, v4sf mean) {
	v4sf sum = float4(0);
	for(v4sf x: X) sum += sq(x-mean);
	return sum;
}

struct Denoise : Application {
	string fileName = arguments()[0];
	map<String, Image> images;
	Denoise() {
		string name = section(fileName,'.');
		mat4 rawRGBtosRGB =mat4(sRGB) * IT8(demosaic(Raw(Map("scene_daylight_211ms_c2.raw16"))), readFile("R100604.txt")).rawRGBtoXYZ;
		Raw raw {Map(fileName)};
		Image4f image = demosaic(raw);
		v4sf mean3 = ::mean(image);
		float mean = sum3(mean3)/3;
		log(withName(raw.exposure*1e3, raw.gain, raw.gainDiv, raw.temperature, mean, log2(mean), 10*log10(mean)));
		float stddev = sqrt(sum3(SSD(image, mean3))/3);
		log(withName(stddev, log2(stddev), 10*log10(stddev)));
		log(int(round((log2(mean)+10)/raw.exposure)), "LSB10/s");
		log(int(round((log2(stddev))/raw.exposure)), "LSB10/s");
		images.insert(name+".source", convert(convert(demosaic(raw), rawRGBtosRGB)));
		images.insert(name+".NLM", convert(convert(NLM(demosaic(cool(move(raw)))), rawRGBtosRGB)));
	}
};

#include "view.h"
struct Preview : Denoise, WindowCycleView { Preview() : WindowCycleView(images) {} };
registerApplication(Preview);

#include "png.h"
struct Export : Denoise {
	Export() {
		for(auto image: images) {
			log(image.key);
			writeFile(image.key+".png", encodePNG(image.value), currentWorkingDirectory(), true);
		}
	}
};
registerApplication(Export, export);
