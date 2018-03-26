#include "thread.h"
#include "window.h"
#include "image-render.h"
#include "jpeg.h"
#include "algorithm.h"
#include "mwc.h"
#include "matrix.h"
#include "jacobi.h"

template<> inline String str(const Matrix& A) {
    array<char> s;
    for(uint i: range(A.M)) {
        if(A.N==1) s.append("\t"+fmt(A(i,0), 4u));
        else {
            for(uint j: range(A.N)) {
                s.append("\t"+fmt(A(i,j), 4u));
            }
            if(i<A.M-1) s.append('\n');
        }
    }
    return move(s);
}

inline vec2 normal(vec2 a) { return vec2(-a.y, a.x); }
inline float cross(vec2 a, vec2 b) { return a.y*b.x - a.x*b.y; }

typedef ref<float> vector;

struct Test : Widget {
    Image target;
    unique<Window> window = nullptr;

    Test() {
        const ImageF I = luminance(decodeImage(Map("test.jpg")));
        Array<uint, 256> histogram; histogram.clear(0);
        const float maxX = ::max(I);
        for(const float x: I) histogram[int((histogram.size-1)*x/maxX)]++;

        const uint totalCount = I.ref::size;
        uint64 totalSum = 0;
        for(uint t: range(histogram.size)) totalSum += t*histogram[t];
        uint backgroundCount = 0;
        uint backgroundSum = 0;
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
            if(variance >= maximumVariance) {
                maximumVariance=variance;
                thresholdIndex = t;
            }
        }
        const float threshold = float(thresholdIndex)/float(histogram.size-1) * maxX;

        // Floodfill
        buffer<uint2> stack (I.ref::size, 0);
        stack.append(I.size/2u); // FIXME: Select largest region: floodfill from every unconnected seeds, keep largest region
        ImageT</*bool*/float> R (I.size); R.clear(0);
        while(stack) {
            const uint2& p0 = stack.pop();
            for(int2 dp: {int2(0,-1),int2(-1,0),int2(1,0),int2(0,1)}) { // 4-way connectivity
                uint2 p = uint2(int2(p0)+dp);
                if(anyGE(p,I.size)) continue;
                if(I(p) <= threshold) continue;
                if(R(p)) continue; // Already marked
                R(p) = 1;
                stack.append(p);
            }
        }

        // Mean
        buffer<vec2> X (R.ref::size, 0);
        vec2 Σ = 0_;
        for(const uint iy: range(I.size.y)) for(const uint ix: range(I.size.x)) {
            vec2 x(ix, I.size.y-1-iy); // Flips Y axis from Y top down to Y bottom up
            if(R(ix,iy)) {
                X.append(x);
                Σ += x;
            }
        }
        const vec2 μ = Σ / float(X.size);
        for(vec2& x: X) x -= μ;

        // PCA
        Random random;
        vec2 r = normalize(random.next<vec2>());
        for(auto_: range(4)) {
            vec2 Σ = 0_;
            for(vec2 x: X) Σ += dot(r,x)*x;
            r = normalize(Σ);
        }
        const vec2 e0 = r;
        const vec2 e1 = normal(e0);
        const mat2 V (e0, e1);

        // Initial corner estimation (maximize area of quadrant in eigenspace)
        vec2 C[4] = {0_,0_,0_,0_}; // eigenspace
        for(const vec2& x : X) {
            const vec2 Vx = V * x;
            static constexpr int quadrantToWinding[2][2] = {{0,1},{3,2}};
            vec2& c = C[quadrantToWinding[Vx.x>0][Vx.y>0]];
            if(abs(Vx.x*Vx.y) > abs(c.x*c.y)) c = Vx;
        }

        // Iterative corner optimization (maximize total area)
        for(uint i: range(4)) {
            const vec2 C3 = C[(i+3)%4];
            vec2& C0 = C[(i+0)%4];
            const vec2 C1 = C[(i+1)%4];
            float A0 = cross(C1-C0, C3-C0);
            for(const vec2& x : X) {
                const vec2 Vx = V * x;
                const float A = cross(C1-Vx, C3-Vx);
                if(A > A0) {
                    A0 = A;
                    C0 = Vx;
                }
            }
        }

        static constexpr uint N = 4;

        const float y = 297./210;
        const ref<vec2> modelC = {{-1,-y},{1,-y},{1,y},{-1,y}}; // FIXME: normalize origin and average distance ~ √2

        mat2 U = V.inverse();
        mat3 K;
        const float focalLength = 4.2, pixelPitch = 0.0014;
        K(0,0) = 2/(I.size.x*pixelPitch/focalLength);
        K(1,1) = 2/(I.size.y*pixelPitch/focalLength);
        const float near = K(1,1);
        const mat3 K¯¹ = K.¯¹();
        const buffer<vec2> X´ = apply(ref<vec2>(C), [&](vec2 x){ return K¯¹*(2.f*(μ+U*x)/vec2(I.size)-vec2(1)); });

#if 0
        const ref<vec2> TX = modelC;
        const ref<vec2> TX´ = X´;
#else
        Array<vec2, 4> TX; mat4 T; {
            const ref<vec2> X = modelC;
            const vec2 μ = ::mean(X);
            TX.apply([=](const vec2 x){ return x-μ; }, X);
            const float μD = ::mean<float>(apply(X,[=](const vec2 x){ return ::dotSq(x); }));
            TX.apply([=](const vec2 x){ return x*sqrt(3/μD); }, TX);
            T = mat4().scale(vec3(sqrt(3/μD))).translate(vec3(-μ,0));
        }

        Array<vec2, 4> TX´; mat4 T´; {
            const ref<vec2> X = X´;
            const vec2 μ = ::mean<vec2>(X);
            TX´.apply([=](const vec2 x){ return x-μ; }, X);
            const float μD = ::mean<float>(apply(X,[=](const vec2 x){ return ::dotSq(x); }));
            TX´.apply([=](const vec2 x){ return x*sqrt(3/μD); }, TX´);
            T´ = mat4().scale(vec3(sqrt(3/μD))).translate(vec3(-μ,0));
        }
#endif

        // DLT: Ah = 0
        Matrix A(N*2, 9);
        for(uint i: range(N)) {
            const uint I = i*2;
            A(I+0, 0) = -TX[i].x;
            A(I+0, 1) = -TX[i].y;
            A(I+0, 2) = -1;
            A(I+0, 3) = 0; A(I+0, 4) = 0; A(I+0, 5) = 0;
            A(I+1, 0) = 0; A(I+1, 1) = 0; A(I+1, 2) = 0;
            A(I+1, 3) = -TX[i].x;
            A(I+1, 4) = -TX[i].y;
            A(I+1, 5) = -1;
            A(I+0, 6) = TX´[i].x*TX[i].x;
            A(I+0, 7) = TX´[i].x*TX[i].y;
            A(I+0, 8) = TX´[i].x;
            A(I+1, 6) = TX´[i].y*TX[i].x;
            A(I+1, 7) = TX´[i].y*TX[i].y;
            A(I+1, 8) = TX´[i].y;
        }
        const USV usv = SVD(A);
        const vector h = usv.V[usv.V.N-1];
        mat3 H;
        for(int i: range(usv.V.M)) H(i/3, i%3) = h[i] / h[8];
        H = mat3(vec3(1/sqrt(::length(H[0])*::length(H[1])))) * H; // Normalizes by geometric mean of the 2 rotation vectors

        mat4 Rt;
        Rt[0] = vec4(H[0], 0);
        Rt[1] = vec4(H[1], 0);
        Rt[2] = vec4(cross(H[0],H[1]), 0);
        Rt[3] = vec4(H[2].xy(), 0, 1);

        // FIXME
        Rt[0].w = Rt[0].z;
        Rt[1].w = Rt[1].z;
        Rt[2].w = Rt[2].z;
        Rt[3].w = H[2].z;

        const mat4 view = mat4(T´.¯¹())*Rt*mat4(T);
        log("view\n"+str(view));

        const float far = 1000/focalLength*near; //mm
        target = sRGB(R, 128);
#if 0
        mat4 K4;
        K4[0] = vec4(K[0], 0);
        K4[1] = vec4(K[1], 0);
        K4[2] = vec4(0);
        K4[3] = vec4(K[2].xy(), 0, 1);
        K4(2,2) = - (far+near) / (far-near);
        K4(2,3) = - 2*far*near / (far-near);
        K4(3,2) = - 1;
        K4(3,3) = 0;
#else
        const mat4 projection = perspective(near, far).scale(vec3(float(target.size.y)/float(target.size.x), 1, 1));
        const mat4 K4 = projection;
#endif
        log("K\n"+str(K));
        log("K4\n"+str(K4));
        const mat4 view´ = K4.¯¹()*mat4(K)*view;
        log("view´\n"+str(view´));

        const mat4 P = K4*view´;

        const mat4 NDC = mat4()
                .scale(vec3(vec2(target.size)/2.f, 1))
                .translate(vec3(1)); // -1, 1 -> 0, 2
        const mat4 flipY = mat4().translate(vec3(0, target.size.y-1, 0)).scale(vec3(1, -1, 1)); // Flips Y axis from Y bottom up to Y top down for ::line
        const mat4 M = flipY*NDC*P;

        line(target, (M*vec3(modelC[0], 0)).xy(), (M*vec3(modelC[1], 0)).xy(), bgr3f(1));
        line(target, (M*vec3(modelC[1], 0)).xy(), (M*vec3(modelC[2], 0)).xy(), bgr3f(1));
        line(target, (M*vec3(modelC[2], 0)).xy(), (M*vec3(modelC[3], 0)).xy(), bgr3f(1));
        line(target, (M*vec3(modelC[3], 0)).xy(), (M*vec3(modelC[0], 0)).xy(), bgr3f(1));

        const float z = 0.1;
        line(target, (M*vec3(modelC[0], 0)).xy(), (M*vec3(modelC[0], z)).xy(), bgr3f(1));
        line(target, (M*vec3(modelC[1], 0)).xy(), (M*vec3(modelC[1], z)).xy(), bgr3f(1));
        line(target, (M*vec3(modelC[2], 0)).xy(), (M*vec3(modelC[2], z)).xy(), bgr3f(1));
        line(target, (M*vec3(modelC[3], 0)).xy(), (M*vec3(modelC[3], z)).xy(), bgr3f(1));

        line(target, (M*vec3(modelC[0], z)).xy(), (M*vec3(modelC[1], z)).xy(), bgr3f(1));
        line(target, (M*vec3(modelC[1], z)).xy(), (M*vec3(modelC[2], z)).xy(), bgr3f(1));
        line(target, (M*vec3(modelC[2], z)).xy(), (M*vec3(modelC[3], z)).xy(), bgr3f(1));
        line(target, (M*vec3(modelC[3], z)).xy(), (M*vec3(modelC[0], z)).xy(), bgr3f(1));

        if(1) {
            window = ::window(this, int2(target.size), mainThread, 0);
            window->show();
        }
    }
    void render(RenderTarget2D& renderTarget_, vec2, vec2) override {
        const Image& renderTarget = (ImageRenderTarget&)renderTarget_;
        copy(renderTarget, target);
    }
} static test;
