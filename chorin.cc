#include "thread.h"
#include "algebra.h"
#include "math.h"
#include "time.h"
#include "data.h"
#include "window.h"
#include "interface.h"
#include "graphics.h"
#include "png.h"

real maxabs(const ref<real>& v) { real y=0; for(real x: v) if(abs(x)>y) y=abs(x); return y; }

// 2D field as a vector
struct Vector2D : Vector {
    uint m, n;
    Vector2D():m(0),n(0){}
    Vector2D(Vector&& v, uint m, uint n):Vector(move(v)),m(m),n(n){}
    Vector2D(uint m, uint n, real value):Vector(m*n,value),m(m),n(n){}
    real& operator ()(uint i, uint j) { return at(j*m+i); }
    const real& operator ()(uint i, uint j) const { return at(j*m+i); }
};
inline Vector2D operator-(const Vector2D& a, const Vector2D& b) { assert(a.m==b.m && a.n==b.n); return Vector2D((const vector&)a-b,a.m,a.n); }
inline String str(const Vector2D& v) {
    String s;
    for(uint i: range(v.m)) {
        for(uint j: range(v.n)) {
            s<<ftoa(v(i,j),2,6);
        }
        if(i<v.m-1) s<<'\n';
    }
    return s;
}

// 2D field matrix (4D tensor) as a matrix
struct Matrix2D : Matrix {
    const uint m, n;
    Matrix2D(uint m, uint n):Matrix(m*n),m(m),n(n){}
    real operator ()(uint i, uint j, uint k, uint l) const { return Matrix::operator ()(j*m+i, l*m+k); }
    real& operator ()(uint i, uint j, uint k, uint l) { return Matrix::operator ()(j*m+i, l*m+k); }
};
inline String str(const Matrix2D& M) {
    String s;
    s << repeat("-"_,M.m*(M.n*5+3)) << '\n';
    for(uint i: range(M.m)) {
        for(uint j: range(M.n)) {
            for(uint k: range(M.m)) {
                for(uint l: range(M.n)) {
                    s<<ftoa(M(i,j,k,l),1,5);
                }
                s<<" | "_;
            }
            if(i<M.m-1 || j<M.m-1) s<<'\n';
        }
        s << repeat("-"_,M.m*(M.n*5+3)) << '\n';
    }
    return s;
}

inline String str(const Matrix2D& M, const Vector2D& v) {
    String s;
    s << repeat("-"_,M.m*(M.n*5+2)) << '\n';
    for(uint i: range(M.m)) {
        for(uint j: range(M.n)) {
            for(uint k: range(M.m)) {
                for(uint l: range(M.n)) {
                    s << ftoa(M(i,j,k,l),1,5);
                }
                s<<" |"_;
            }
            s << ftoa(v(i,j),1,5);
            s <<'\n';
        }
        s << repeat("-"_,M.m*(M.n*5+2));
        if(i<M.m-1) s << '\n';
    }
    return s;
}

struct Grid {
    const uint Nx, Ny;
    const Vector X, Y;
    Grid(uint Nx, uint Ny, real borderRefinementWeight=0) : Nx(Nx), Ny(Ny), X(Nx), Y(Ny) {
        real w = borderRefinementWeight;
        for(uint i: range(Nx)) {
            real regular = (real) i / (Nx-1);
            real cosinus = (1 - cos(PI*i/(Nx-1))) / 2;
            X[i] = (1-w)*regular + w*cosinus;
        }
        for(uint j: range(Ny)) {
            real regular = (real) j / (Ny-1);
            real cosinus = (1 - cos(PI*j/(Ny-1))) / 2;
            Y[j] = (1-w)*regular + w*cosinus;
        }
        log("1/(min dx)",1/(X[1]-X[0]), "1/(max dx)",1/(X[Nx/2+1]-X[Nx/2]));
    }

    real sample(const Vector2D& U, real x, real y) {
        uint i=0; while(i+1<X.size && X[i+1] <= x) i++;
        uint j=0; while(j+1<Y.size && Y[j+1] <= y) j++;
        real u = i+1<X.size ? (x-X[i])/(X[i+1]-X[i]) : 0;
        real v = j+1<Y.size ? (y-Y[j])/(Y[j+1]-Y[j]) : 0;\
        //log(x,y,i,j,U(i,j));
        return (1-v) * ((1-u) * U(i,j  ) + (u? u * U(i+1,j  ) :0)) +
               (v? v * ((1-u) * U(i,j+1) + (u? u * U(i+1,j+1) :0)) :0);
    }
};

// Solves (H - L)u = S
struct Helmholtz {
    const real H;
    const uint Nx, Ny;
    const Vector& X; const Vector& Y;
    enum ConditionType { Dirichlet, Neumann } type; // Boundary conditions (homogeneous)
    enum { Top, Bottom, Left, Right };
    vec4 boundaryValues; // [Top, Bottom, Left, Right] values for Dirichlet or Neumann boundary conditions
    //enum { TopLeft, TopRight, BottomLeft, BottomRight };
    //vec4 cornerValues; // [TopLeft, TopRight, BottomLeft, BottomRight] values for Neumann boundary conditions (unsupported)
    Matrix2D M {Nx, Ny}; // DEBUG
    UMFPACK A;
    Helmholtz(real H, const Grid& grid, ConditionType type, vec4 boundaryValues=0) :
        H(H), Nx(grid.Nx), Ny(grid.Ny), X(grid.X), Y(grid.Y), type(type), boundaryValues(boundaryValues) {

        //Matrix2D M (Nx, Ny);
        // Interior matrix
        for(uint i: range(1,Nx-1)) for(uint j: range(1,Ny-1)) {
            real xm = (X[i-1]+X[i])/2;
            real xp = (X[i]+X[i+1])/2;
            real ym = (Y[j-1]+Y[j])/2;
            real yp = (Y[j]+Y[j+1])/2;
            real left = (yp-ym)/(X[i]-X[i-1]);
            real right = (yp-ym)/(X[i+1]-X[i]);
            real top = (xp-xm)/(Y[j]-Y[j-1]);
            real bottom = (xp-xm)/(Y[j+1]-Y[j]);
            real h = (xp-xm)*(yp-ym)*H;
            M(i,j, i-1,j)   =    -left;
            M(i,j, i+1,j)   =         -right;
            M(i,j, i,  j-1) =                     -top;
            M(i,j, i,  j+1) =                           -bottom;
            M(i,j, i,  j  ) = h+left+right+top+bottom;
        }
        // Borders
        if(type==Dirichlet) {
            for(uint i: range(Nx)) {
                M(i, 0,    i, 0)    = 1;
                M(i, Ny-1, i, Ny-1) = 1;
            }
            for(uint j: range(Ny)) {
                M(0,   j, 0,   j) = 1;
                M(Nx-1,j, Nx-1,j) = 1;
            }
        }
        if(type==Neumann) {
            auto border = [&](bool transpose, int i0, int i1, int i) {
                const auto& x = !transpose ? X : Y;
                const auto& y = !transpose ? Y : X;
                real xm = (x[i-1]+x[i])/2;
                real xp = (x[i]+x[i+1])/2;
                real y12 = (y[i0]+y[i1])/2;
                real left  = (y12-y[i0])/(x[i]-x[i-1]);
                real right = (y12-y[i0])/(x[i+1]-x[i]);
                real bottom = (xp-xm)/(y[i1]-y[i0]);
                real h = (xp-xm)*(y12-y[i0])*H;
                auto m = [&](int i, int j, int k, int l) -> real& { return !transpose ? M(i,j,k,l) : M(j,i,l,k); };
                m(i,i0, i-1,i0) =  -left; // -Laplacian
                m(i,i0, i+1,i0) =       -right; // -Laplacian
                m(i,i0, i,  i1) =                   -bottom; // Flux
                m(i,i0, i,  i0) = h+left+right+bottom;
            };
            for(uint i: range(1,Nx-1)) {
                border(false, 0,    1,      i);
                border(false, Ny-1, Ny-1-1, i);
            }
            for(uint j: range(1,Ny-1)) {
                border(true,  0,    1,      j);
                border(true,  Nx-1, Nx-1-1, j);
            }
            auto corner = [&](int x0, int x1, int y0, int y1) {
                real x12 = (X[x0]+X[x1])/2;
                real y12 = (Y[y0]+Y[y1])/2;
                real right = (y12-Y[y0])/(x12-X[x0]);
                real bottom = (x12-X[x0])/(y12-Y[y0]);
                real h = (x12-X[x0])*(y12-Y[y0])*H;
                M(x0,y0, x1, y0) =  -right;       // -Laplacian
                M(x0,y0, x0, y1) =       -bottom; // -Laplacian
                M(x0,y0, x0, y0) = h+right+bottom;
            };
            M(0, 0,    0, 0)          = 1; // Dirichlet conditions on top-left corner
            //corner(0,    1,      0,         1);
            corner(Nx-1, Nx-1-1, 0,         1);
            corner(0,    1,      Ny-1, Ny-1-1);
            corner(Nx-1, Nx-1-1, Ny-1, Ny-1-1);
        }
#if 1
        // Asserts valid system
        for(auto& column: M.columns) for(auto& element: column) assert_(isNumber(element.value), element.value, H, '\n', X, '\n', Y, '\n', M);
#endif
        // LU Factorization (FIXME: symetric ? -> Cholesky)
        A = M;
    }

    /// Solves (H - L)u = S
    Vector2D solve(const Vector2D& S) {
        Vector2D b (Nx,Ny,nan);
        // Interior
        for(uint i: range(1,Nx-1)) for(uint j: range(1,Ny-1)) {
            real xm = (X[i-1]+X[i])/2;
            real xp = (X[i]+X[i+1])/2;
            real ym = (Y[j-1]+Y[j])/2;
            real yp = (Y[j]+Y[j+1])/2;
            b(i,j) = (xp-xm)*(yp-ym) * S(i,j);
        }
        if(type==Dirichlet) {
            for(uint i: range(Nx)) {
                b(i, 0   ) = boundaryValues[Top];
                b(i, Ny-1) = boundaryValues[Bottom];
            }
            for(uint j: range(Ny)) {
                b(0, j)    = boundaryValues[Left];
                b(Nx-1, j) = boundaryValues[Right];
            }
        }
        if(type==Neumann) {
            auto bord = [&](bool transpose, int i0, int i1, int i) {
                const auto& x = !transpose ? X : Y;
                const auto& y = !transpose ? Y : X;
                real xm = (x[i-1]+x[i])/2;
                real xp = (x[i]+x[i+1])/2;
                real y12 = (y[i0]+y[i1])/2;
                real s = !transpose ? S(i,i0) : S(i0,i);
                real f = boundaryValues[ !transpose ? (i0==0 ? Top : Bottom) : (i0==0 ? Left : Right) ];
                (!transpose ? b(i,i0) : b(i0,i)) = (xp-xm) * ((y12-y[i0]) * s - f);
            };
            for(uint i: range(1,Nx-1)) {
                bord(false, 0,    1,      i);
                bord(false, Ny-1, Ny-1-1, i);
            }
            for(uint j: range(1,Ny-1)) {
                bord(true,  0,    1,      j);
                bord(true,  Nx-1, Nx-1-1, j);
            }
            auto corner = [&] (int x0, int x1, int y0, int y1) {
                real x12 = (X[x0]+Y[x1])/2;
                real y12 = (Y[y0]+Y[y1])/2;
                // FIXME: non-zero flux conditions
                b(x0, y0) = (x12-X[x0]) * (y12-Y[y0]) * S(x0,y0);
            };
            b(0,       0) = 0; // Sets top-left corner to zero
            //corner(0,    1,      0,         1);
            corner(Nx-1, Nx-1-1, 0,         1);
            corner(0,    1,      Ny-1, Ny-1-1);
            corner(Nx-1, Nx-1-1, Ny-1, Ny-1-1);
        }
        if(type==Neumann && Nx*Ny<=25) log(M, b);
        return Vector2D(A.solve(b), Nx, Ny);
    }
};

// Solves D.u = 0, dt u + (u.D)u = -Dp + (1/Re)Lu
struct Chorin {
    const uint N, Nx=N, Ny=N;
    const real U = 1; // Boundary speed
    const real L = 1; // Domain size
    const real nu = 1e-3; // Fluid viscosity
    const real Re = U*L/nu; // Reynolds
    const real dt = L/(max(Nx,Ny)*U); // Time step
    Grid grid {Nx,Ny, 0};
    const Vector& X = grid.X; const Vector& Y = grid.Y;
    Chorin(uint N):N(N) {}

    Helmholtz prediction {Re/dt, grid, Helmholtz::Dirichlet}; // Dirichlet conditions with top u = U
    Vector2D predict(const Vector2D& U, const Vector2D& Ux, const Vector2D& Uy, int axis) {
        Vector2D S(Nx, Ny, nan);
        for(uint i: range(1,Nx-1)) for(uint j: range(1,Ny-1)) {
            real u = U(i,j), ux = Ux(i,j), uy = Uy(i,j);
            real dxU = ( U(i+1,j) - U(i-1,j) ) / ( X[i+1] - X[i-1] );
            real dyU = ( U(i,j+1) - U(i,j-1) ) / ( Y[j+1] - Y[j-1] );
            real dxP = ( P(i+1,j) - P(i-1,j) ) / ( X[i+1] - X[i-1] );
            real dyP = ( P(i,j+1) - P(i,j-1) ) / ( Y[j+1] - Y[j-1] );
            S(i,j) = Re*(u/dt - (ux*dxU + uy*dyU) - (axis==1? dyP : dxP)); // b = Lx*Ly*S, S = u/dt - (u.D)u - DP
        }
        // Dirichlet conditions are set with Helmholtz::boundaryValues
        return prediction.solve(S);
    }

    Helmholtz poisson {0, grid, Helmholtz::Neumann, 0}; // Poisson with homogeneous Neumann conditions (Dp=0)
    Vector2D correct(const Vector2D& Ux, const Vector2D& Uy) {
        Vector2D S(Nx, Ny, nan);
        for(uint i: range(1,Nx-1)) for(uint j: range(1,Ny-1)) {
            real dxUx = ( Ux(i+1,j) - Ux(i-1,j) ) / ( X[i+1] - X[i-1] );
            real dyUy = ( Uy(i,j+1) - Uy(i,j-1) ) / ( Y[j+1] - Y[j-1] );
            S(i,j) = - (dxUx + dyUy); // -Lu = S
        }
        auto border = [&](bool transpose, int i0, int i1, int i) {
            const auto& x = !transpose ? X : Y;
            const auto& y = !transpose ? Y : X;
            auto ux = [&](int i, int j) -> real { return !transpose ? Ux(i,j) : Ux(j,i); };
            auto uy = [&](int i, int j) -> real { return !transpose ? Uy(i,j) : Uy(j,i); };
            real dxUx = (ux(i+1,i0) - ux(i-1,i0)) / (x[i+1] - x[i-1]);
            real dyUy = (uy(i,  i1) - uy(i,  i0)) / (y[i1 ] - y[i0 ]);
            (!transpose ? S(i,i0) : S(i0,i)) = -(dxUx + dyUy); // -Lu = S
        };
        for(uint i: range(1,Nx-1)) {
            border(false, 0,    1,      i);
            border(false, Ny-1, Ny-1-1, i);
        }
        for(uint j: range(1,Ny-1)) {
            border(true,  0,    1,      j);
            border(true,  Nx-1, Nx-1-1, j);
        }
        // Homogenous Neumann conditions (zero flux)
        auto corner = [&] (int x0, int x1, int y0, int y1) {
            real x12 = (X[x0]+Y[x1])/2;
            real y12 = (Y[y0]+Y[y1])/2;
            real dxUx = (Ux(x1, y0) - Ux(x0, y0)) / (x12 - X[x0]);
            real dyUy = (Uy(x0, y1) - Uy(x0, y0)) / (y12 - Y[y0]);
            S(x0, y0) = - (dxUx + dyUy);
        };
        corner(0,    1,      0,         1);
        corner(Nx-1, Nx-1-1, 0,         1);
        corner(0,    1,      Ny-1, Ny-1-1);
        corner(Nx-1, Nx-1-1, Ny-1, Ny-1-1);
        return poisson.solve(S);
    }

    // Current state vector
    Vector2D ux{Nx,Ny,0}, uy{Nx,Ny,0}, P{Nx,Ny,0};

    // Solves one iteration of Chorin
    void solve() {
        prediction.boundaryValues = vec4(U,0,0,0);
        Vector2D nux = predict(ux, ux,uy, 0);
        prediction.boundaryValues = 0;
        Vector2D nuy = predict(uy, ux,uy, 1);

        Vector2D psi = correct(nux, nuy);
        for(uint i: range(1,Nx-1)) for(uint j: range(1,Ny-1)) {
            real dxP = ( psi(i+1,j) - psi(i-1,j) ) / ( X[i+1] - X[i-1] );
            real dyP = ( psi(i,j+1) - psi(i,j-1) ) / ( Y[j+1] - Y[j-1] );
            nux(i,j) -= dxP;
            nuy(i,j) -= dyP;
            //P(i,j) += dt*psi(i, j);
        }
        // D P = 0 on borders => no correction => keep values from prediction
        ux = move(nux), uy = move(nuy);
        //for(uint i: range(1,Nx-1)) ux(i, 0) = 0;
    }

    // Solves for flow function L phi =  - dy u + dx v
    Vector2D flow(const Vector2D& Ux, const Vector2D& Uy) {
        Vector2D S(Nx, Ny, nan);
        for(uint i: range(1,Nx-1)) for(uint j: range(1,Ny-1)) {
            real dxUy = ( Uy(i+1,j) - Uy(i-1,j) ) / ( X[i+1] - X[i-1] );
            real dyUx = ( Ux(i,j+1) - Ux(i,j-1) ) / ( Y[j+1] - Y[j-1] );
            S(i,j) = - (dxUy - dyUx); // -Lu = S
        }
        auto border = [&](bool transpose, int i0, int i1, int i) {
            const auto& x = !transpose ? X : Y;
            const auto& y = !transpose ? Y : X;
            auto ux = [&](int i, int j) -> real { return !transpose ? Ux(i,j) : Ux(j,i); };
            auto uy = [&](int i, int j) -> real { return !transpose ? Uy(i,j) : Uy(j,i); };
            real dxUy = (uy(i+1,i0) - uy(i-1,i0)) / (x[i+1] - x[i-1]);
            real dyUx = (ux(i,  i1) - ux(i,  i0)) / (y[i1 ] - y[i0 ]);
            (!transpose ? S(i,i0) : S(i0,i)) = -(dxUy - dyUx); // -Lu = S
        };
        for(uint i: range(1,Nx-1)) {
            border(false, 0,    1,      i);
            border(false, Ny-1, Ny-1-1, i);
        }
        for(uint j: range(1,Ny-1)) {
            border(true,  0,    1,      j);
            border(true,  Nx-1, Nx-1-1, j);
        }
        // Homogenous Neumann conditions (zero flux)
        auto corner = [&] (int x0, int x1, int y0, int y1) {
            real x12 = (X[x0]+Y[x1])/2;
            real y12 = (Y[y0]+Y[y1])/2;
            real dxUy = (Uy(x1, y0) - Uy(x0, y0)) / (x12 - X[x0]);
            real dyUx = (Ux(x0, y1) - Ux(x0, y0)) / (y12 - Y[y0]);
            S(x0, y0) = - (dxUy - dyUx);
        };
        corner(0,    1,      0,         1);
        corner(Nx-1, Nx-1-1, 0,         1);
        corner(0,    1,      Ny-1, Ny-1-1);
        corner(Nx-1, Nx-1-1, Ny-1, Ny-1-1);
        //error("flow\n", poisson.M, S);
        return poisson.solve(S);
    }
    // Returns flow field for current state
    Vector2D flow() { return flow(ux, uy); }

#if ADVECT
    Vector2D advect(const Vector2D& U, const Vector2D& Ux, const Vector2D& Uy) {
        Vector2D S(Nx, Ny, nan);
        for(uint i: range(0,Nx-1)) for(uint j: range(1,Ny-1)) {
            real u = U(i,j), ux = Ux(i,j), uy = Uy(i,j);
            real dxU = ( U(i+1,j) - U(i-1,j) ) / ( X[i+1] - X[i-1] );
            real dyU = ( U(i,j+1) - U(i,j-1) ) / ( Y[j+1] - Y[j-1] );
            S(i,j) = u - dt*(ux*dxU + uy*dyU);
        }
        for(uint i: range(Nx)) {
            S(i, 0   ) = U(i,    0);
            S(i, Ny-1) = U(i, Ny-1);
        }
        for(uint j: range(Ny)) {
            S(0, j)    = U(0,    j);
            S(Nx-1, j) = U(Nx-1, j);
        }
        return S;
    }
    // Advects U with the current velocity field
    Vector2D advect(const Vector2D& U) { return advect(U, ux, uy); }
#endif

    // Samples velocity field at (x, y)
    vec2 velocity(real x, real y) { return vec2(grid.sample(ux, x, y), grid.sample(uy, x, y)); }
};

/// Displays scalar fields as blue,green,red components
struct FieldView : Widget {
    const Vector& X; const Vector& Y;
    const Vector2D& Ux; const Vector2D& Uy;
    const Vector2D& B; const Vector2D& G; const Vector2D& R;
    bool showGrid = false, showVectors = false;
    map<vec2, Text> labels;
    FieldView(const Grid& grid, const Vector2D& Ux, const Vector2D& Uy, const Vector2D& B, const Vector2D& G, const Vector2D& R)
        : X(grid.X), Y(grid.Y), Ux(Ux), Uy(Uy), B(B), G(G), R(R) {}
    int2 sizeHint() { return int2(720/2); }
    void render(const Image& target) override {\
        vec2 size (target.size());
        vec3 cMin (min(B), min(G), min(R));
        vec3 cMax (max(B), max(G), max(R));
        for(uint i: range(X.size-1)) for(uint j: range(Y.size-1)) {
            int y0 = round(size.y*Y[j]), y1 = round(size.y*Y[j+1]);
            int x0 = round(size.x*X[i]), x1 = round(size.x*X[i+1]);
            for(uint y: range(y0, y1)) for(uint x: range(x0, x1)) {
                real u = ((x+1./2)/size.x-X[i])/(X[i+1]-X[i]);
                real v = ((y+1./2)/size.y-Y[j])/(Y[j+1]-Y[j]);
                vec3 c = 0;
                for(uint componentIndex : range(3)) {
                    const Vector2D& C = *ref<const Vector2D*>{&B,&G,&R}[componentIndex];
                    c[componentIndex] = (1-v) * ((1-u) * C(i,j  ) + u * C(i+1,j  )) +
                                         v    * ((1-u) * C(i,j+1) + u * C(i+1,j+1));
                }
                vec3 normalized = (c-cMin)/(cMax-cMin);
                normalized = round(normalized*16.f)/16.f; // Quantize
                int3 linear = max(int3(0),int3(round(float(0xFFF)*normalized)));
                assert_(linear >= int3(0) && linear < int3(0x1000), linear, c, cMax);
                extern uint8 sRGB_forward[0x1000];
                target(x,y) = byte4(sRGB_forward[linear.x], sRGB_forward[linear.y], sRGB_forward[linear.z], 0xFF);
            }
        }
        if(showGrid) {
            for(real x: X) line(target, int2(size*vec2(x,0)), int2(size*vec2(x,1)));
            for(real y: Y) line(target, int2(size*vec2(0,y)), int2(size*vec2(1,y)));
        }
        if(showVectors) {
            const real uMax = max(maxabs(Ux), maxabs(Uy));
            const uint S = 4; // Downsampling factor
            const float cellSize = S*min(size.x/X.size, size.y/Y.size);
            for(uint x: range(X.size/S)) for(uint y: range(Y.size/S)) {
                uint N = 0; vec2 P = 0, U = 0;
                for(uint i: range(x*S,(x+1)*S)) for(uint j: range(y*S,(y+1)*S)) { //FIXME: weight by cell sizes
                    N++;
                    P += vec2(X[i], Y[j]);
                    U += vec2(Ux(i,j),Uy(i,j));
                }
                P /= N, U /= N;
                vec2 center = vec2(target.size()) * P;
                vec2 scale = cellSize * U / float(uMax);
                line(target, center + scale*vec2(0,    0   ), center + scale*vec2(1,1));
                line(target, center + scale*vec2(1./2, 1   ), center + scale*vec2(1,1));
                line(target, center + scale*vec2(1,    1./2), center + scale*vec2(1,1));
            }
        }
        for(auto label: labels) label.value.render(target, int2(round(size*label.key)));
    }
};

struct Application {
    const uint N = 64;
    Chorin chorin {N};
    uint t = 0;
#if FLOW
    Vector2D flow;
#endif
#if ADVECT
    Vector2D textures[2] = {{N,N,nan},{N,N,nan}};
    FieldView fieldView {chorin.grid, chorin.ux, chorin.uy, textures[0], textures[1], flow};
#elif FLOW
    FieldView fieldView {chorin.grid, chorin.ux, chorin.uy, flow, flow, flow};
#else
    FieldView fieldView {chorin.grid, chorin.ux, chorin.uy, chorin.P, chorin.uy, chorin.ux};
#endif
    Window window {&fieldView, int2(1024,1024), str(N)};

    map<real, real> ux_y;
    map<real, real> uy_x;

    void step() {
        chorin.solve();
#if FLOW
        flow = chorin.flow();
#endif
#if ADVECT
        for(Vector2D& texture: textures) texture = chorin.advect(texture);
#endif
        {real x = 1./2; for(real y: ux_y.keys) fieldView.labels[vec2(x,y)] = str(y, ux_y[y], chorin.velocity(x,y).x); }
    }

    map<real, real> parseProfile(const string& name, const string& columnName) {
        map<real, real> profile;
        TextData s = readFile(name,home());
        auto keys = split(s.line(), ';');
        uint column = keys.indexOf(columnName);
        while(s) {
            auto values = split(s.line(),';');
            profile.insert(fromDecimal(values[0]), fromDecimal(values[column]));
        }
        return profile;
    }

    Application() {
        ux_y = parseProfile("profile_u_fct_y_cavity.csv"_, "u_Re1000"_);
        for(real& y: ux_y.keys) y=1-y; // Top-left origin
        uy_x = parseProfile("profile_v_fct_x_cavity.csv"_, "v_Re1000"_);
#if ADVECT
        for(uint i: range(chorin.Nx)) for(uint j: range(chorin.Ny)) textures[0](i,j) = chorin.X[i], textures[1](i,j) = chorin.Y[j];
#endif
        step();
        window.background = Window::NoBackground;
        window.actions[Escape] = []{exit();};
        window.frameSent
                = [this]{
            step();
            window.render();
            window.setTitle(str(t++));
        };
        window.actions[Return] = [this]{
            writeFile("Re="_+str(chorin.Re)+",t="_+str(t)+".png"_,encodePNG(renderToImage(fieldView, window.size)), home());
        };
        window.actions['g'] = [this]{ fieldView.showGrid = !fieldView.showGrid; };
        window.actions['v'] = [this]{ fieldView.showVectors = !fieldView.showVectors; };
        window.show();
    }
} app;
