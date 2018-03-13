#include "window.h"
#include "image-render.h"
#include "dng.h"
#include "math.h"
#include "algorithm.h"
#include "matrix.h"

inline vec4 rotationFromTo(const vec3 v1, const vec3 v2) { return normalize(vec4(cross(v1, v2), sqrt(dotSq(v1)*dotSq(v2)) + dot(v1, v2))); }
inline vec4 qmul(vec4 p, vec4 q) { return vec4(p.w*q.xyz() + q.w*p.xyz() + cross(p.xyz(), q.xyz()), p.w*q.w - dot(p.xyz(), q.xyz())); }
inline vec3 qapply(vec4 p, vec3 v) { return qmul(p, qmul(vec4(v, 0), vec4(-p.xyz(), p.w))).xyz(); }

generic ImageT<T> downsample(const ImageT<T>& source, int times) {
    assert_(times>0);
    ImageT<T> target = unsafeShare(source);
    for(auto_: range(times)) target=downsample(target);
    return target;
}

generic ImageT<T> upsample(const ImageT<T>& source, int times) {
    assert_(times>0);
    ImageT<T> target = unsafeShare(source);
    for(auto_: range(times)) target=upsample(target);
    return target;
}

static inline ImageF disk(int size) {
    ImageF target = ImageF(uint2(size));
    const float C = (size-1.f)/2, R² = sq(size/2-1.f);
    for(int y: range(target.size.y)) for(int x: range(target.size.x)) {
        const float r² = sq(x-C)+sq(y-C);
        target(x,y) = float(r²<R²); // FIXME: antialiasing
    }
    return target;
}

static inline void negate(const ImageF& Y, const ImageF& X) { for(size_t i: range(Y.ref::size)) Y[i] = 1-X[i]; }
static inline ImageF negate(const ImageF& X) { ImageF Y(X.size); negate(Y,X); return Y; }

inline double SSE(const ImageF& A, const ImageF& B, int2 centerOffset=0_0) {
 const int2 offset = centerOffset + (int2(A.size) - int2(B.size))/2;
 const uint2 aOffset (max(int2(0),+offset));
 const uint2 bOffset (max(int2(0),-offset));
 const uint2 size = min(A.size-aOffset, B.size-bOffset);
 float SSE = 0;
 const float* a = A.data+aOffset.y*A.stride+aOffset.x;
 const float* b = B.data+bOffset.y*B.stride+bOffset.x;
 for(uint y: range(size.y)) {
     const float* aLine = a+y*A.stride;
     const float* bLine = b+y*B.stride;
     for(uint x: range(size.x)) {
         SSE += sq(aLine[x] - bLine[x]);
     }
 }
 return SSE;
}

generic int2 argmax(const uint2 Asize, const uint2 Bsize, T function, int2 window=0_0, const int2 initialOffset=0_0) {
    if(!window) window = abs(int2(Asize-Bsize)); // Full search
    int2 bestOffset = 0_0; float bestSimilarity = -inff; //-SSE..0
    for(int y: range(-window.y/2, window.y/2)) for(int x: range(-window.x/2, window.x/2)) {
        const int2 offset = initialOffset + int2(x, y);
        const float similarity = function(offset);
        if(similarity > bestSimilarity) { bestSimilarity = similarity; bestOffset = offset; }
    }
    return bestOffset;
}

template<Type F, Type... Args> int2 argmax(F function, const ImageF& A, const ImageF& B, const Args&... args) {
    return ::argmax(A.size, B.size, [&](const int2 offset){ return function(offset, A, B, args...); });
}

template<Type F, Type... Images> int2 argmaxCoarse(const int L, F function, const Images&... images) {
    return ::argmax(function, downsample(images, L)...)*int(1<<L);
}

static int2 argmaxSSE(const ImageF& A, const ImageF& B, const int L=0) {
     return argmaxCoarse(L, [&](const int2 offset, const ImageF& A, const ImageF& B){ return -SSE(A, B, offset); }, A, B);
}

generic ImageF apply(const uint2 Asize, const uint2 Bsize, T function, int2 window=0_0, const int2 initialOffset=0_0) {
    if(!window) window = abs(int2(Asize-Bsize)); // Full search
    ImageF target = ImageF( ::max(Asize, Bsize) );
    target.clear(-inff);
    for(int y: range(-window.y/2, (window.y+1)/2)) for(int x: range(-window.x/2, (window.x+1)/2))
        target(target.size.x/2+x, target.size.y/2+y) = function(initialOffset + int2(x, y));
    return ::move(target);
}

template<Type F, Type... Args> ImageF apply(F function, const ImageF& A, const ImageF& B, const Args&... args) {
    return ::apply(A.size, B.size, [&](const int2 offset){ return function(offset, A, B, args...); });
}

template<Type F, Type... Images> ImageF applyCoarse(const int L, F function, const Images&... images) {
    return upsample(::apply(function, downsample(images, L)...), L);
}

generic void apply(const ImageF& A, const ImageF& B, int2 centerOffset, T f) {
 const int2 offset = centerOffset + (int2(A.size) - int2(B.size))/2;
 const uint2 aOffset (max(int2(0),+offset));
 const uint2 bOffset (max(int2(0),-offset));
 const uint2 size = min(A.size-aOffset, B.size-bOffset);
 const uint a = aOffset.y*A.stride+aOffset.x;
 const uint b = bOffset.y*B.stride+bOffset.x;
 for(uint y: range(size.y)) {
     const uint yLine = y*size.x;
     const uint aLine = a+y*A.stride;
     const uint bLine = b+y*B.stride;
     for(uint x: range(size.x)) {
         f(yLine+x, aLine+x, bLine+x);
     }
 }
}

inline void multiply(const ImageF& Y, const ImageF& A, const ImageF& B, int2 centerOffset=0_0) {
    assert_(Y.size == ::min(A.size, B.size));
    apply(A, B, centerOffset, [&](const uint y, const uint a, const uint b){ Y[y]=A[a]*B[b]; });
}
inline ImageF multiply(const ImageF& A, const ImageF& B, int2 centerOffset=0_0) {
    ImageF Y(::min(A.size, B.size));
    multiply(Y,A,B,centerOffset);
    return Y;
}

inline void opGt(const ImageF& Y, const ImageF& X, float threshold) { for(uint i: range(Y.ref::size)) Y[i] = float(X[i]>threshold); }
inline ImageF operator>(const ImageF& X, float threshold) { ImageF Y(X.size); ::opGt(Y,X,threshold); return Y; }

static inline vec4 principalCone(const ImageF& disk) {
    auto V = [&](uint ix, uint iy) {
            const float x = +((float(ix)/disk.size.x)*2-1);
            const float y = -((float(iy)/disk.size.y)*2-1);
            const float r² = sq(x)+sq(y);
            assert_(r² < 1);
            const float z = sqrt(1-r²);
            assert_(z > 0);
            const vec3 v = vec3(x,y,z);
            return v;
    };
    auto Ɐ = [&](function<void(float, float, vec3)> f) {
        for(uint iy: range(disk.size.y)) for(uint ix: range(disk.size.x)) { // ∫dA
            const float I = disk(ix, iy);
            if(I == 0) continue; // Mask
            const vec3 v = V(ix, iy);
            const float dΩ_dA = 1/v.z; // dΩ = sinθ dθ dφ, dA = r dr dφ, r=sinθ, dΩ/dA=dθ/dr=1/cosθ, cosθ=z
            f(dΩ_dA, I, v);
        }
    };
    float ΣN = 0, ΣⱯI = 0; Ɐ([&](float dΩ_dA, float I, vec3){ ΣN += dΩ_dA; ΣⱯI += dΩ_dA*I; });
    const float μI = ΣⱯI/ΣN;
    float ΣI = 0; vec3 ΣIv = 0_; Ɐ([&](float dΩ_dA, float I, vec3 v){ I-=μI; if(I>0) { ΣI += I; ΣIv += dΩ_dA*I*v; }});
    if(ΣI == 0) {
        const uint i = argmax(disk);
        return vec4(V(i%disk.size.x,i/disk.size.y), 0);
    }
    const vec3 μ = normalize(ΣIv); // Spherical mean of weighted directions
    const float p = length(ΣIv)/ΣI; // Polarisation
    const float Ω = p; // lim[p->0] Ω = p ; Ω=∫dΩ=2π[1-cosθ]~πθ², p=∫dΩv|z=π/2[1-cos2θ]~πθ²
    log(disk.size, disk.ref::size, ΣI, ΣIv, μ, p, Ω);
    return vec4(μ, Ω);
}

// Sums CFA RGGB quads together, and normalizes min/max levels, yields RGGB intensity image
static ImageF sumBGGR(const DNG& source) {
    ImageF target(source.size/2u);
    for(uint y: range(target.size.y)) for(uint x: range(target.size.x)) {
        const int B = ::max(0, source(x*2+0,y*2+0)-source.blackLevel);
        const int G1 = ::max(0,source(x*2+1,y*2+0)-source.blackLevel);
        const int G2 = ::max(0,source(x*2+0,y*2+1)-source.blackLevel);
        const int R = ::max(0,source(x*2+1,y*2+1)-source.blackLevel);
        target(x,y) = float(B+G1+G2+R)/(4*(4095-source.blackLevel));
    }
    return target;
}

// Cached
struct CachedImageF : ImageF { Map map; };
static CachedImageF loadRaw(const string path) {
    if(!existsFile(path+".raw")) writeFile(path+".raw", cast<byte>(sumBGGR(parseDNG(Map(path), true))));
    DNG image = parseDNG(Map(path), false);
    Map map(path+".raw");
    return {ImageF(unsafeRef(cast<float>(map)), image.size), ::move(map)};
}

static const int3 diskSearch(const ImageF& image, const uint maxR, const uint L=7) {
    for(uint r = maxR;;) {
        const ImageF templateDisk = ::disk(maxR);
        const int2 center = argmaxSSE(negate(templateDisk), image, L);
        return int3(center, r);
    }
}

struct Sphere : Widget {
    Image preview;
    unique<Window> window = nullptr;

    Sphere() {
        Time decodeTime {true}; // FIXME: mmap cache
        const CachedImageF image = loadRaw("IMG_0658.dng");
        const CachedImageF low = loadRaw("IMG_0659.dng"); // Low exposure (only highlights)
        log(decodeTime);

        Time time {true};

        const int3 center = diskSearch(image, image.size.x/4);
        const ImageF templateDisk = ::disk(center[2]);

        const ImageF disk = multiply(templateDisk, image, center.xy());
        const ImageF light = multiply(templateDisk, low, center.xy());
        //Image preview = sRGB(light > 0.7f);
        //Image preview = sRGB(image);
        preview = sRGB(disk);

        log(time);
#if 0
        const vec4 lightCone = principalCone(light > 0.7f);
        const vec3 μ = lightCone.xyz();
        const int2 μ_xy = int2((vec2(μ.x,-μ.y)+vec2(1))/vec2(2)*vec2(light.size));
        if(1) {
            const int r = 0;
            if(!anyGE(μ_xy+int2(r),int2(preview.size)) && !anyLE(μ_xy,int2(r)))
                for(int dy: range(-r,r+1))for(int dx: range(-r,r+1)) preview(μ_xy.x+dx, μ_xy.y+dy) = byte4(0,0xFF,0,0xFF);
        }

        const float dx = 5, f = 4;
        const vec3 C = normalize(vec3(vec2(center.x, -center.y) / float(image.size.x) * dx, f));
        const vec4 Q = rotationFromTo(C, vec3(0,0,1));
        const float Ω = lightCone.w, θ = acos(1-Ω/(2*π));
        log(C, μ, qapply(Q,μ), Ω, θ*180/π);
#endif
        window = ::window(this, int2(preview.size), mainThread, 0);
        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview);
    }
} static app;
