#include "data.h"
#include "interface.h"
#include "window.h"
#include "png.h"
#include "demosaic.h"
#include "algebra.h"
#include "matrix.h"

//struct WindowView : ImageView { Window window {this, int2(1024, 768)}; WindowView(Image&& image) : ImageView(move(image)) {} };
struct WindowCycleView : WidgetCycle {
    ImageView images[2];
    Window window {this, int2(1024, 768)};
    WindowCycleView(Image&& a, Image&& b) : WidgetCycle(toWidgets<ImageView>(images)), images{move(a), move(b)} {} };


Image4f parseIT8(ref<byte> it8) {
    TextData s(it8);
    Image4f target (22, 12);
    s.until("BEGIN_DATA\r\n");
    assert_(s);
    for(int i: range(12)) for(int j: range(1, 22 +1)) {
        s.skip(char('A'+i)+str(j));
        s.whileAny(' '); float x = s.decimal() / 100;
        s.whileAny(' '); float y = s.decimal() / 100;
        s.whileAny(' '); float z = s.decimal() / 100;
        s.until("\r\n");
        target(/*1+*/ (j-1), /*1+*/ i) = {x,y,z,0};
    }
    for(int j: range(23 +1)) {
        s.skip("GS"+str(j));
        s.whileAny(' '); float unused x = s.decimal() / 100;
        s.whileAny(' '); float unused y = s.decimal() / 100;
        s.whileAny(' '); float unused z = s.decimal() / 100;
        s.until("\r\n");
        //target(j, 1+12+1) = target(j, 1+12+1+1) = {x,y,z,0};
    }
    /*v4sf GS11 = target(11, 1+12+1);
    for(int j: range(24)) target(j, 0) = target(j, 1+12) = GS11;
    for(int i: range(13)) target(0, i) = target(23, i) = GS11;*/
    s.skip("END_DATA\r\n");
    assert_(!s);
    return target;
}

Image4f convert(Image4f&& target, const Image4f& source, mat3 matrix) {
    assert_(target.size == source.size);
    v4sf columns[3];
    for(size_t j: range(3)) for(size_t i: range(3)) { columns[j][i] = matrix(i, j); columns[j][3] = 0; } // Loads matrix in registers
    for(uint y: range(target.height)) for(uint x: range(target.width)) {
        v4sf v = source(x, y);
        target(x, y) = columns[0] * float4(v[0]) + columns[1] * float4(v[1]) + columns[2] * float4(v[2]);
    }
    return move(target);
}
Image4f convert(const Image4f& source, mat3 matrix) { return convert(source.size, source, matrix); }

static mat3 sRGB {vec3(0.0557, -0.9689, 3.2406), vec3(-0.2040, 1.8758, -1.5372), vec3(1.0570, 0.0415, -0.4986) }; // BGR

Image4f downsample(Image4f&& target, const Image4f& source) {
    assert_(target.size == source.size/2, target.size, source.size);
    for(uint y: range(target.height)) for(uint x: range(target.width))
        target(x,y) = (source(x*2+0,y*2+0) + source(x*2+1,y*2+0) + source(x*2+0,y*2+1) + source(x*2+1,y*2+1)) / float4(4);
    return move(target);
}
inline Image4f downsample(const Image4f& source) { return downsample(source.size/2, source); }

Image4f upsample(Image4f&& target, const Image4f& source) {
    assert_(target.size > source.size, target.size, source.size);
    for(uint y: range(target.size.y)) for(uint x: range(target.size.x))
        target(x,y) = source(x*source.size.x/target.size.x, y*source.size.y/target.size.y);
    return move(target);
}

// -- Template match --

static double SSE(const Image4f& a, const Image4f& b, int2 offset, int2 size) {
    double energy = 0;
    for(int y: range(size.y)) {
        for(int x: range(size.x)) {
            energy += sq3(a(x+offset.x, y+offset.y) - b(x*b.size.x/size.x, y*b.size.y/size.y))[0]; // SSE of nearest samples
        }
    }
    energy /= size.x*size.y;
    return energy;
}

int2 templateMatch(const Image4f& A, const Image4f& b, int2& size) {
    array<Image4f> mipmap;
    mipmap.append(share(A));
    size = b.size;
    while(mipmap.last().size > size*3) mipmap.append(downsample(mipmap.last()));
    int2 offset = mipmap.last().size / 2 - size / 2;
    const int searchWindowHalfLength = 4;
    for(const Image4f& a: mipmap.reverse()) {
        real bestSSE = inf;
        int2 levelBestOffset = offset, levelBestSize = size;
        for(int y: range(offset.y-searchWindowHalfLength, offset.y+searchWindowHalfLength +1)) {
            for(int x: range(offset.x-searchWindowHalfLength, offset.x+searchWindowHalfLength +1)) {
                for(int sx: range(size.x-searchWindowHalfLength, size.x+searchWindowHalfLength +1)) {
                    int2 levelSize = int2(sx, sx*size.y/size.x);
                    float SSE = ::SSE(a, b, int2(x,y), levelSize);
                    if(SSE < bestSSE) {
                        bestSSE = SSE;
                        levelBestOffset = int2(x, y);
                        levelBestSize = levelSize;
                    }
                }
            }
        }
        offset = levelBestOffset;
        size = levelBestSize;
        log(offset, size, a.size);
        if(a.size == A.size) return offset;
        size *= 2;
        offset *= 2;
    }
    error("");
}

Image4f mix(const Image4f& a, const Image4f& b) {
    assert_(a.size == b.size);
    Image4f target(a.size);
    for(size_t i: range(target.Ref::size)) target[i] = mix(a[i], b[i], b[i][3]);
    return target;
}

struct IT8 {
    string fileName = arguments()[1];
    string name = section(fileName,'.');
    Image preview;
    Image target;
    IT8() {
        string it8ChargeFileName = arguments()[0];
        Image4f it8 = parseIT8(readFile(it8ChargeFileName)); // Parses IT8 spot colors (XYZ)

        ImageF bayerCFA = fromRaw16(cast<uint16>(Map(fileName)), 4096, 3072); // Converts 16bit integer to 32bit floating point (normalized by maximum value)
        Image4f rawRGB = demosaic(bayerCFA); // raw RGB
        Image4f it8sRGB = convert(it8, sRGB); // Converts IT8 to an RGB colorspace (sRGB) close to raw RGB for template match
        int2 size; int2 offset = templateMatch(rawRGB, it8sRGB, size); // Locates IT8 chart within image
        Image4f chart = cropShare(rawRGB, offset, size); // Extracts IT8 chart

        Image4f spots (it8.size); // Acquired raw RGB for each IT8 spot
        Image4f spotsView = upsample(size, it8sRGB);
        spotsView.alpha = true;
        // Extracts mean raw RGB for each IT8 spot
        for(int Y: range(it8.size.y))for(int X: range(it8.size.x)) {
            int x0 = X*chart.size.x/it8.size.x, x3 = (X+1)*chart.size.x/it8.size.x;
            // Reduces spot size to calibrate under slight misalignment
            int x1 = x0+(x3-x0)/4, x2 = x3-(x3-x0)/4;
            int y0 = Y*chart.size.y/it8.size.y, y3 = (Y+1)*chart.size.y/it8.size.y;
            int y1 = y0+(y3-y0)/4, y2 = y3-(y3-y0)/4;
            for(int y: range(y1, y2))for(int x: range(x1, x2)) spotsView(x, y)[3] = 1./2; // For spots visualization (alpha=0 elsewhere)
            v4sf sum = float4(0);
            for(int y: range(y1, y2))for(int x: range(x1, x2)) sum += chart(x, y);
            v4sf mean = sum / float4((y2-y1)*(x2-x1));
            spots(X, Y) = mean;
        }

        // -- Linear least square determination of coefficients for linear conversion of raw RGB to XYZ
        mat3 rawRGBtoXYZ;
        for(size_t outputIndex: range(3)) { // XYZ
            const size_t unknownCount = 3, constraintCount = spots.Ref::size;
            Matrix A (constraintCount, unknownCount); Vector b (constraintCount);

            for(const size_t constraintIndex: range(constraintCount)) { // Each IT8 spot defines a constraint for linear least square regression
                for(size_t inputIndex: range(3)) // RGB
                    A(constraintIndex, inputIndex) = spots[constraintIndex][inputIndex];
                b[constraintIndex] = it8[constraintIndex][outputIndex]; // XYZ
            }

            // -- Solves linear least square system
            Matrix At = transpose(A);
            Matrix AtA = At * A;
            Vector Atb = At * b;
            Vector x = solve(move(AtA),  Atb); // Solves AtA = Atb
            Vector r = A*x - b; // Residual
            //log(r);
            for(float v: r) if(isNaN(v)) error("No solution found");
            for(size_t inputIndex: range(3)) // RGB
                rawRGBtoXYZ(outputIndex, inputIndex) = x[inputIndex]; // Inserts solution in conversion matrix
        }
        mat3 rawRGBtosRGB = sRGB * rawRGBtoXYZ;
        preview = convert(mix(convert(chart, rawRGBtosRGB), spotsView));
        target = convert(convert(rawRGB, rawRGBtosRGB));
    }
};

struct Preview : IT8, WindowCycleView, Application { Preview() : WindowCycleView(share(preview), share(target)) {} };
registerApplication(Preview);
struct Export : IT8, Application { Export() { writeFile(name+".png", encodePNG(target)); } };
registerApplication(Export, export);
