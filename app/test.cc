#include "thread.h"
#if 1
#include "window.h"
#include "image-render.h"
#include "jpeg.h"
#include "algorithm.h"

#if 0
static inline ImageF dxx(const ImageF& I) {
    ImageF dxx (I.size);
    for(int y: range(dxx.size.y))
        for(int x: range(dxx.size.x))
            dxx(x,y) = (1.f/4)*I(::max(0,x-1),y) + (-2.f/4)*I(x,y) + (1.f/4)*I(::min(int(I.size.x)-1,x+1),y);
    return dxx;
}

static inline ImageF dyy(const ImageF& I) {
    ImageF dyy (I.size);
    for(int y: range(dyy.size.y))
        for(int x: range(dyy.size.x))
            dyy(x,y) = (1.f/4)*I(x,::max(0,y-1)) + (-2.f/4)*I(x,y) + (1.f/4)*I(x,::min(int(I.size.y)-1,y+1));
    return dyy;
}

static inline ImageF dxy(const ImageF& I) {
    ImageF dxy (I.size);
    const float c = 1/(4*sqrt(2.));
    for(int y: range(dxy.size.y))
        for(int x: range(dxy.size.x))
            dxy(x,y) = +c*I(::max(0,x-1),::max(0,              y-1)) + -c*I(::min(int(I.size.x)-1,x+1),::max(0,              y-1))
                     + -c*I(::max(0,x-1),::min(int(I.size.y)-1,y+1)) + +c*I(::min(int(I.size.x)-1,x+1),::min(int(I.size.y)-1,y+1));
    return dxy;
}

static inline ImageF detH(const ImageF& I) {
    ImageF dxx = ::dxx(I);
    ImageF dyy = ::dyy(I);
    ImageF dxy = ::dxy(I);
    ImageF detH (I.size);
    for(uint i: range(detH.ref::size)) detH[i] = dxx[i]*dyy[i] - dxy[i]*dxy[i];
    return detH;
}
#endif

struct ratio { uint64 num, den; };
static inline bool operator <=(ratio a, ratio b) { return a.num*b.den <= b.num*a.den; }
template<> String str(const ratio& o) { return str(o.num)+'/'+str(o.den); }
//template<> String str(const ratio& o) { return str(o.num/gcd(o.num,o.den))+'/'+str(o.den/gcd(o.num,o.den)); }

struct Test : Widget {
    Image preview;
    unique<Window> window = nullptr;

    Test() {
        const ImageF X = luminance(decodeImage(Map("test.jpg")));
        Array(uint, histogram, 256); histogram.clear(0);
        const float maxX = ::max(X);
        for(const float x: X) histogram[int(255*x/maxX)]++;

        const uint totalCount = X.ref::size;
        uint64 totalSum = 0;
        for(uint t: range(histogram.size)) totalSum += t*histogram[t];
        uint backgroundCount = 0;
        uint backgroundSum = 0;
        //ratio maximumVariance {0,1};
        float maximumVariance = 0;
        uint thresholdIndex = 0;
        for(uint t: range(histogram.size)) {
            backgroundCount += histogram[t];
            if(backgroundCount == 0) continue;
            backgroundSum += t*histogram[t];
            uint foregroundCount = totalCount - backgroundCount;
            uint64 foregroundSum = totalSum - backgroundSum;
            if(foregroundCount == 0) break;
            const float foregroundMean = float(foregroundSum)/float(foregroundCount);
            const float backgroundMean = float(backgroundSum)/float(backgroundCount);
            const float variance = float(foregroundCount)*float(backgroundCount)*sq(foregroundMean - backgroundMean);
            // variance = ω0*ω1*(μ0-μ1)² = ω0*ω1*(Σ0/ω0-Σ1/ω1)² = ω0*ω1*(Σ0*ω1-Σ1*ω0)²/(ω0*ω1)² = (Σ0*ω1-Σ1*ω0)²/(ω0*ω1)
            //const ratio variance {sq(foregroundSum*backgroundCount-backgroundSum*foregroundCount),(uint64)foregroundCount*backgroundCount};
            if(variance >= maximumVariance) {
                maximumVariance=variance;
                thresholdIndex = t;
            }
        }
        log(thresholdIndex);
        const float threshold = thresholdIndex * maxX;

        preview = sRGB(X > threshold);
        window = ::window(this, int2(preview.size), mainThread, 0);
        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview);
    }
} static test;

#elif 1
#include "dng.h"

// Splits CFA R,GG,B quads into BGR components, and normalizes min/max levels, yields RGGB intensity image
static Image3f BGGRtoBGR(const DNG& source) {
    Image3f target(source.size/2u);
    for(uint y: range(target.size.y)) for(uint x: range(target.size.x)) {
        const int B  = ::max(0, source(x*2+0,y*2+0)-source.blackLevel);
        const int G1 = ::max(0, source(x*2+1,y*2+0)-source.blackLevel);
        const int G2 = ::max(0, source(x*2+0,y*2+1)-source.blackLevel);
        const int R  = ::max(0, source(x*2+1,y*2+1)-source.blackLevel);
        const float rcp = 4095-source.blackLevel;
        const float b = rcp*B;
        const float g = (rcp/2)*(G1+G2);
        const float r = rcp*R;
        target(x,y) = bgr3f(b,g,r);
    }
    return target;
}

struct Test : Widget {
    Image preview;
    unique<Window> window = nullptr;

    Test() {
        const Image3f background = BGGRtoBGR(parseDNG(Map("IMG_0752.dng")));
        const Image3f shadow = BGGRtoBGR(parseDNG(Map("IMG_0751.dng")));
        const Image3f ratio (shadow.size);
        for(const uint i: range(ratio.ref::size)) ratio[i] = shadow[i] / background[i];
        preview = sRGB(ratio);
        writeFile("ratio.png", encodePNG(preview));
        window = ::window(this, int2(preview.size), mainThread, 0);
        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview);
    }
} static test;

#else

#include "vector.h"
#include "math.h"
struct Test {
    Test() {
        const vec3 A (0.22, 0.25, 0.94);
        const vec3 B (0.20, 0.34, 0.92);
        log(acos(dot(A,B)/sqrt(dotSq(A)*dotSq(B)))*180/π);
    }
} static test;
#endif
