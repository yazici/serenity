#include "window.h"
#include "layout.h"
#include "interface.h"
#include "pdf.h"
#include "jpeg.h"
#include "render.h"
#include "jpeg-encoder.h"

struct Mosaic {
	// Name
	string name = arguments() ? arguments()[0] : (error("Expected name"), string());
	array<Graphics> pages;
	Mosaic(const float inchPx = 90) {
		array<String> images = Folder(".").list(Files);
		images.filter([](string name){return !(endsWith(name,".png") || endsWith(name, ".jpg") || endsWith(name, ".JPG"));});
		// -- Parses page definitions
		array<array<array<Image>>> pages;
		for(TextData s= readFile(name); s;) {
			array<array<Image>> page;
			do {
				array<Image> row;
				for(string name; (name = s.whileNo(" \n"));) {
					string file = [&images](string name) {
						for(string file: images) if(startsWith(file, name)) return file;
						error("No such image"_, name, "in", images);
					}(name);
					Image image = decodeImage(readFile(file));
					image.alpha = false;
					assert_(!image.alpha);
					row.append(move(image));
					s.match(' ');
				}
				assert_(row);
				page.append(move(row));
				if(!s) break;
				s.skip('\n');
			} while(s && !s.match('\n')); // Blank line breaks page
			pages.append(move(page));
		}
		const float inchMM = 25.4;
		const vec2 pageSize = vec2(400,300)/*mm*/ * (inchPx/inchMM);
		for(auto& images: pages) {
			// -- Layout images
			const float W = pageSize.x, H = pageSize.y; // Container size
			const int m = images.size; // Row count
			buffer<int> n = apply(images, [](ref<Image> row){ return int(row.size); }); // Columns count
			buffer<buffer<float>> a = apply(images, [](ref<Image> row){ return apply(row, [](const Image& image){ return (float)image.width/image.height; }); });  // Elements aspect ratios
			buffer<float> A = apply(a, sum<float>);  // Row aspect ratio: Aᵢ = Σj aᵢⱼ
			float alpha = 1./2; // FIXME: Optimize under constraint
			float X = (W * sum(apply(A, [](float A){ return 1/A; })) - H) / (sum(apply(m, [&](size_t i){ return (n[i]+1)/A[i]; })) - alpha*(m+1)); // Border: X = ( W Σᵢ 1/Aᵢ - H) / (Σᵢ (nᵢ+1)/Aᵢ - alpha*m+1) [Y=alpha*X]
			float Y = alpha*X;
			buffer<float> h = apply(m, [&](size_t i){ return (W - (n[i]+1)*X)/A[i]; }); // Row height: hᵢ = (W - (nᵢ+1)X) / Aᵢ
			buffer<buffer<float>> w = apply(m, [&](size_t i){ return apply(n[i], [&](size_t j){ return a[i][j] * h[i]; }); }); // Elements width wᵢⱼ = aᵢⱼ hᵢ
			//log("W",W,"H",H, "X", X, "Y", Y, "h", h, "w", w);
			Graphics& page = this->pages.append();
#if 1     // -- Extend images over background
			page.bounds = Rect(round(pageSize));
			Image target(int2(page.bounds.size()));
			float y0 = Y;
			// TODO: linear
			// TODO: top white
			for(size_t i: range(m)) {
				float x0 = X;
				// TODO: left white
				for(size_t j: range(n[i])) {
					int ix0 = round(x0), iy0 = round(y0);
					int iw0 = ix0-round(x0-X), ih0 = iy0-round(y0-Y); // Previous border sizes
					float x1 = x0+w[i][j], y1 = y0+h[i];
					int ix1 = round(x1), iy1 = round(y1);
					int iw1 = round(x1+X)-ix1, ih1 = round(y1+Y)-iy1; // Next border sizes
					int2 size(ix1-ix0, iy1-iy0);
					Image source = resize(size, images[i][j]);
					for(int y: range(ih0)) {
						for(int x: range(size.x)) {
							bgr3i s = 0;
							for(int dx: range(-(y+1), (y+2))) for(int dy: range(y+3)) s += (bgr3i)source(clip(0, x+dx, size.x-1), dy).bgr();
							s = (ih0-1-y) * s / ( (y+3) * ((y+2) - -(y+1)) * (ih0-1));
							target(ix0+x, iy0-y-1) += byte4(s);
						}
					}
					for(int y: range(size.y)) {
						for(int x: range(iw0)) {
							bgr3i s = 0;
							for(int dy: range(-(x+1), (x+2))) for(int dx: range(x+3)) s += (bgr3i)source(dx, clip(0, y+dy, size.y-1)).bgr();
							s = (iw0-1-x) * s / ( (x+3) * ((x+2) - -(x+1)) * (iw0-1));
							target(ix0-x-1, iy0+y) += byte4(s);
						}
						for(int x: range(size.x)) target(ix0+x, iy0+y) = source(x, y);
						for(int x: range(iw1)) {
							bgr3i s = 0;
							for(int dy: range(-(x+1), (x+2))) for(int dx: range(x+3)) s += (bgr3i)source(size.x-1-dx, clip(0, y+dy, size.y-1)).bgr();
							s = (iw1-1-x) * s / ( (x+3) * ((x+2) - -(x+1)) * (iw1-1));
							target(ix1+x, iy0+y) += byte4(s);
						}
					}
					for(int y: range(ih1)) {
						for(int x: range(size.x)) {
							bgr3i s = 0;
							for(int dx: range(-(y+1), (y+2))) for(int dy: range(y+3)) s += (bgr3i)source(clip(0, x+dx, size.x-1), size.y-1-dy).bgr();
							s = (ih1-1-y) * s / ( (y+3) * ((y+2) - -(y+1)) * (ih1-1));
							target(ix0+x, iy1+y) += byte4(s);
						}
					}
					x0 += w[i][j] + X;
				}
				// TODO: right white
				y0 += h[i] + Y;
			}
			// TODO: bottom white
			page.blits.append(0, page.bounds.size(), move(target));
#else   // -- No background
			page.bounds = Rect(pageSize);
			Graphics& page = this->pages.append();
			page.bounds = Rect(pageSize);
			float y0 = Y;
			for(size_t i: range(m)) {
				float x = X;
				for(size_t j: range(n[i])) {
					assert_(w[i][j] > 0 && h[i] > 0, w[i][j], h[i]);
					page.blits.append(vec2(x,y0), vec2(w[i][j], h[i]), move(images[i][j]));
					x += w[i][j] + X;
				}
				assert_(round(x) == round(W), x, W);
				y0 += h[i] + Y;
			}
#endif
			assert_(round(y0) == round(H));
		}
	}
};

struct MosaicPreview : Mosaic, Application {
	Scroll<HList<GraphicsWidget>> pages {apply(Mosaic::pages, [](Graphics& o) { return GraphicsWidget(move(o)); })};
	Window window {&pages, int2(round(pages[0].bounds.size())), [this](){return unsafeRef(name);}};
};
registerApplication(MosaicPreview);

struct MosaicExport : Mosaic, Application {
	MosaicExport() : Mosaic(300) {
		//writeFile(name+".pdf"_, toPDF(pageSize, pages, 72/*PostScript point per inch*/ / inchPx /*px/inch*/), currentWorkingDirectory(), true);
		writeFile(name+".jpg"_, encodeJPEG(render(int2(round(pages[0].bounds.size())), pages[0])), currentWorkingDirectory(), true);
	}
};
registerApplication(MosaicExport, export);
