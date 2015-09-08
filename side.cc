#include "side.h"
typedef __SIZE_TYPE__ size_t;
typedef uint16 __attribute((__vector_size__ (8))) v4hi;
typedef int __attribute((__vector_size__(16))) v4si;
inline v4si cvttps2dq(v4sf a) { return __builtin_ia32_cvttps2dq(a); }
inline v4sf dot2(v4sf a, v4sf b) { return __builtin_ia32_dpps(a,b,0b00111111); }

// AVX
#if /*__AVX__ &&*/ 1
typedef float v8sf __attribute((__vector_size__ (32)));
inline v8sf constexpr float6(float f) { return (v8sf){f,f,f,f,f,f,0,0}; }
//inline v8sf constexpr float8(float f) { return (v8sf){f,f,f,f,f,f,f,f}; }
#if __clang__
#define shuffle8(A,B, c0, c1, c2, c3, c4, c5, c6, c7) __builtin_shufflevector(A,B, c0, c1, c2, c3, c4, c5, c6, c7)
#else
#define shuffle8(A,B, c0, c1, c2, c3, c4, c5, c6, c7) (v8sf){A[c0],A[c1],A[c2],A[c3],B[c4],B[c5],B[c6],B[c7]}
#endif
#undef packed
#include <immintrin.h>
#define packed __attribute((packed))
// QtCreator
#include <avxintrin.h>
static inline float reduce8(v8sf x) {
    /* ( x3+x7, x2+x6, x1+x5, x0+x4 ) */
    const v4sf x128 = __builtin_ia32_vextractf128_ps256(x, 1) + _mm256_castps256_ps128(x);
    const __m128 x64 = x128 + _mm_movehl_ps/*__builtin_ia32_movhlps*/(x128, x128);
    const __m128 x32 = x64 + _mm_shuffle_ps/*__builtin_ia32_shufps*/(x64, x64, 0x55);
    return x32[0]; //_mm_cvtss_f32/*__builtin_ia32_vec_ext_v4sf*/(x32, 0);
}
typedef int v8si __attribute((__vector_size__ (32)));
static constexpr v8si _11111100 = {~0,~0,~0,~0,~0,~0,0,0};
static inline float reduce6(v8sf x) { return reduce8((v8sf)((v8si)x & _11111100)); }
inline v8sf sqrt8(v8sf x) { return _mm256_sqrt_ps(x); }
#include "avx_mathfun.h"
const char* build = "AVX";
#else
const char* build = "";
struct v8sf {
    v4sf a,b;
    struct Ref {
        v8sf& ref;
        int i;
        void operator=(float v) { if(i<4) ref.a[i] = v; else ref.b[i-4]=v; }
        operator float() { return i<4 ? ref.a[i] : ref.b[i-4]; }
    };
    Ref operator[](int i) { return {*this, i}; }
};
inline v8sf constexpr float6(float f) { return (v8sf){{f,f,f,f},{f,f,0,0}}; }
//inline v8sf constexpr float8(float f) { return (v8sf){f,f,f,f,f,f,f,f}; }
#define shuffle8(A,B, c0, c1, c2, c3, c4, c5, c6, c7) (v8sf){{A[c0],A[c1],A[c2],A[c3]},{B[c4],B[c5],B[c6],B[c7]}}
static inline float reduce6(v8sf x) { return x.a[0]+x.a[1]+x.a[2]+x.a[3]+x.b[0]+x.b[1]; } // FIXME
inline v4sf sqrt(v4sf a) { return __builtin_ia32_sqrtps(a); }
inline v8sf sqrt8(v8sf x) { return {sqrt(x.a),sqrt(x.b)}; }
inline void operator -=(v8sf& a, v8sf b) { a.a-=b.a, a.b-=b.b; }
inline void operator +=(v8sf& a, v8sf b) { a.a+=b.a, a.b+=b.b; }
inline v8sf operator +(v8sf a, v8sf b) { return {a.a+b.a, a.b+b.b}; }
inline v8sf operator -(v8sf a, v8sf b) { return {a.a-b.a, a.b-b.b}; }
inline v8sf operator *(v8sf a, v8sf b) { return {a.a*b.a, a.b*b.b}; }
inline v8sf operator /(v8sf a, v8sf b) { return {a.a/b.a, a.b/b.b}; }
inline float exp(float x) { return __builtin_expf(x); }
inline v8sf exp256_ps(v8sf a) { return (v8sf){exp(a[0]),exp(a[1]),exp(a[2]),exp(a[3]),exp(a[4]),exp(a[5]),0,0}; }
#endif
//static constexpr v8sf _00000000f = {0.f,0.f,0.f,0.f,0.f,0.f,0.f};
static constexpr v8sf _11111111f = {1.f,1.f,1.f,1.f,1.f,1.f,1.f};

#if __AVX__ && __gcc__
__attribute__ ((target ("avx")))
#endif
void side(int W, const v4sf* position, const v4sf* velocity __attribute((unused)), v4sf* force, float pressure,
          float internodeLength, float tensionStiffness, float tensionDamping, float radius, int start, int size
          #if SINGLEGRID
          , const uint16* grainBase, int gX, int gY, v4sf scale, v4sf min
          #endif
          ) {
    int dy[6] {-W,            0,       W,         W,     0, -W};
    int dx[2][6] {{-1, -1, -1, 0, 1, 0},{0, -1, 0, 1, 1, 1}};
    v8sf P = float6(pressure/(2*3)); // area = length(cross)/2 / 3 vertices
    v8sf internodeLength6 = float6(internodeLength);
    //v8sf sqInternodeLength6 = float6(internodeLength*internodeLength);
    //v8sf tensionStiffness6 = float6(tensionStiffness);
    v8sf tensionStiffness_internodeLength6 = float6(tensionStiffness*internodeLength);
    v8sf __attribute((unused)) tensionDamping6 = float6(tensionDamping);
    float sqRadius = radius*radius;

#if SINGLEGRID
    // Side - Grain
    const uint16* grainNeighbours[3*3] = {
        grainBase-gX*gY-gX-1, grainBase-gX*gY-1, grainBase-gX*gY+gX-1,
        grainBase-gX-1, grainBase-1, grainBase+gX-1,
        grainBase+gX*gY-gX-1, grainBase+gX*gY-1, grainBase+gX*gY+gX-1
    };
    //const v4sf XYZ0{float(1), float(gX), float(gY*gX), 0.f};
    int gYX = gY*gX;
#endif

    for(int i=start; i<start+size; i++) { // FIXME: evaluate tension once per edge
        int D[6];
        int base = i*W;
        for(int e=0; e<6; e++) D[e] = base+dy[e]+dx[i%2][e];
        for(int j=1; j<W-1; j++) {
            int index = base+j;
            v4sf O = position[index];
            v8sf X, Y, Z;
            for(int e=0; e<6; e++) { // TODO: assert unrolled
                int a = j+D[e]; // Gather
                X[e] = position[a][0];
                Y[e] = position[a][1];
                Z[e] = position[a][2];

            }
            X -= float6(O[0]);
            Y -= float6(O[1]);
            Z -= float6(O[2]);
            v8sf sqLength = X*X+Y*Y+Z*Z;
            v8sf length = sqrt8(sqLength);
            v8sf x = length - internodeLength6;
            //v8sf x = sqLength - sqInternodeLength6;
#define DAMP 0
#if DAMP
            v4sf V = velocity[index];
            v8sf vX, vY, vZ;
            for(int e=0; e<6; e++) {
                int a = j+D[e]; // Gather
                vX[e] = velocity[a][0];
                vY[e] = velocity[a][1];
                vZ[e] = velocity[a][2];
            }
            vX -= float6(V[0]);
            vY -= float6(V[1]);
            vZ -= float6(V[2]);
            v8sf rV = (X*vX+Y*vY+Z*vZ);
            //v8sf T = (tensionStiffness6 * x - tensionDamping6 * rV) / length;
            v8sf T = (tensionStiffness6 * (exp256_ps(x)-_11111111f) - tensionDamping6 * rV) /  length;
#else
            //v8sf T = (tensionStiffness6 * x) / length;
            //v8sf T = (tensionStiffness6 * x);// / sqLength;
            v8sf T = (tensionStiffness_internodeLength6 * (exp256_ps(x/internodeLength6)-_11111111f))
                    /  length;
#endif
            v8sf Fx = T * X;
            v8sf Fy = T * Y;
            v8sf Fz = T * Z;


            /*if(dot2(O,O)[0] >= sqRadius) {
                v8sf X1 = shuffle8(X, X, 1, 2, 3, 4, 5, 0, 0, 0);
                v8sf Y1 = shuffle8(Y, Y, 1, 2, 3, 4, 5, 0, 0, 0);
                v8sf Z1 = shuffle8(Z, Z, 1, 2, 3, 4, 5, 0, 0, 0);
                Fx += P * (Y*Z1 - Y1*Z);
                Fy += P * (Z*X1 - Z1*X);
                Fz += P * (X*Y1 - X1*Y);
            }*/

            force[index][0] = reduce6(Fx);
            force[index][1] = reduce6(Fy);
            force[index][2] = reduce6(Fz);

#define PRESSURE 1
#if PRESSURE
            if(dot2(O,O)[0] >= sqRadius) {
                v8sf X1 = shuffle8(X, X, 1, 2, 3, 4, 5, 0, 0, 0);
                v8sf Y1 = shuffle8(Y, Y, 1, 2, 3, 4, 5, 0, 0, 0);
                v8sf Z1 = shuffle8(Z, Z, 1, 2, 3, 4, 5, 0, 0, 0);
                float Px = reduce6(P * (Y*Z1 - Y1*Z));
                float Py = reduce6(P * (Z*X1 - Z1*X));
                float Pz = reduce6(P * (X*Y1 - X1*Y));
                if(Px*O[0]+Py*O[1] < 0) { // Inward (not reversed membrane)
                    force[index][0] += Px;
                    force[index][1] += Py;
                    force[index][2] += Pz;
                }
            }
#endif
        }
        for(int j=W-1; j<=W; j++) {
            int index = base+j%W;
            v4sf O = position[index];
            // Pressure
            v8sf X, Y, Z;
            for(int e=0; e<6; e++) { // TODO: assert unrolled
                int a = base+dy[e]+(j+dx[i%2][e])%W;
                X[e] = position[a][0];
                Y[e] = position[a][1];
                Z[e] = position[a][2];
            }
            X -= float6(O[0]);
            Y -= float6(O[1]);
            Z -= float6(O[2]);
            //v8sf length = sqrt8(X*X+Y*Y+Z*Z);
            v8sf sqLength = X*X+Y*Y+Z*Z;
            v8sf length = sqrt8(sqLength);
            v8sf x = length - internodeLength6;
            //v8sf x = sqLength - sqInternodeLength6;
#if DAMP
            v4sf V = velocity[index];
            v8sf vX, vY, vZ;
            for(int e=0; e<6; e++) {
                int a = j+D[e]; // Gather
                vX[e] = velocity[a][0];
                vY[e] = velocity[a][1];
                vZ[e] = velocity[a][2];
            }
            vX -= float6(V[0]);
            vY -= float6(V[1]);
            vZ -= float6(V[2]);
            v8sf rV = (X*vX+Y*vY+Z*vZ);
            //v8sf T = (tensionStiffness6 * x - tensionDamping6 * rV) / length;
            v8sf T = (tensionStiffness6 * (exp256_ps(x)-_11111111f) - tensionDamping6 * rV) /  length;
#else
            v8sf T = (tensionStiffness_internodeLength6 * (exp256_ps(x/internodeLength6)-_11111111f))
                    /  length;
#endif
            //v8sf T = (tensionStiffness6 * x) / sqLength;
            v8sf Fx = T * X;
            v8sf Fy = T * Y;
            v8sf Fz = T * Z;

            /*if(dot2(O,O)[0] >= sqRadius) {
                v8sf X1 = shuffle8(X, X, 1, 2, 3, 4, 5, 0, 0, 0);
                v8sf Y1 = shuffle8(Y, Y, 1, 2, 3, 4, 5, 0, 0, 0);
                v8sf Z1 = shuffle8(Z, Z, 1, 2, 3, 4, 5, 0, 0, 0);
                Fx += P * (Y*Z1 - Y1*Z);
                Fy += P * (Z*X1 - Z1*X);
                Fz += P * (X*Y1 - X1*Y);
            }*/
            force[index][0] = reduce6(Fx);
            force[index][1] = reduce6(Fy);
            force[index][2] = reduce6(Fz);

#if PRESSURE
            if(dot2(O,O)[0] >= sqRadius) {
                v8sf X1 = shuffle8(X, X, 1, 2, 3, 4, 5, 0, 0, 0);
                v8sf Y1 = shuffle8(Y, Y, 1, 2, 3, 4, 5, 0, 0, 0);
                v8sf Z1 = shuffle8(Z, Z, 1, 2, 3, 4, 5, 0, 0, 0);
                float Px = reduce6(P * (Y*Z1 - Y1*Z));
                float Py = reduce6(P * (Z*X1 - Z1*X));
                float Pz = reduce6(P * (X*Y1 - X1*Y));
                if(Px*O[0]+Py*O[1] < 0) { // Inward (not reversed membrane)
                    force[index][0] += Px;
                    force[index][1] += Py;
                    force[index][2] += Pz;
                }
            }
#endif

#if SINGLEGRID
            v4si xyz = cvttps2dq(scale*(O-min));
            size_t offset = gYX * xyz[2] + gX * xyz[1] + xyz[0];
            //size_t offset = dot3(XYZ0, cvtdq2ps(cvttps2dq(scale*(O-min))))[0];
#define CHECK 0
#if CHECK
            array<size_t> contacts;
#endif
            for(size_t n=0; n<3*3; n++) {
                v4hi line = *(v4hi*)(grainNeighbours[n] + offset);
                if(line[0]) {
                    penalty(side, index, grain, line[0]-1);
#if CHECK
                    contacts.append(line[0]-1);
#endif
                }
                if(line[1]) {
                    penalty(side, index, grain, line[1]-1);
#if CHECK
                    contacts.append(line[1]-1);
#endif
                }
                if(line[2]) {
                    penalty(side, index, grain, line[2]-1);
#if CHECK
                    contacts.append(line[2]-1);
#endif
                }
            }
#if CHECK
            for(size_t b: range(grain.count)) // Asserts correct cull
                assert_(contact(side, index, grain, b).depth >= 0 || contacts.contains(b), index, b,
                        side.Vertex::position[index], grain.position[b],
                        Grain::radius/contact(side, index, grain, b).depth);
#endif
#endif
        }
    }
}
