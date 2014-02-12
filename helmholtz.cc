#include "thread.h"
#include "algebra.h"
#include "math.h"
#include "time.h"
#include "window.h"
#include "interface.h"
#include "plot.h"
#include "png.h"

real maxabs(const ref<real>& v) { real y=0; for(real x: v) if(abs(x)>y) y=abs(x); return y; }

// 2D field as a vector
struct Vector2D : Vector {
    uint m, n;
    Vector2D():m(0),n(0){}
    Vector2D(Vector&& v, uint m, uint n):Vector(move(v)),m(m),n(n){}
    Vector2D(uint m, uint n):Vector(m*n),m(m),n(n){}
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
    enum GridType { Regular, Irregular } type; // Spatial discretisation
    Grid(uint Nx, uint Ny, GridType type) : Nx(Nx), Ny(Ny), X(Nx), Y(Ny), type(type) {
        if(type==Regular) {
            for(uint i: range(Nx)) X[i] = i / (Nx-1);
            for(uint j: range(Ny)) Y[j] = j / (Ny-1);
        } else { // Refines near boundaries
            for(uint i: range(Nx)) X[i] = 1 - cos(i*PI/(Nx-1));
            for(uint j: range(Ny)) Y[j] = 1 - cos(j*PI/(Ny-1));
        }
    }
};

// Solves (H - L)u = S
struct Helmholtz {
    const real H;
    const uint Nx, Ny;
    const Vector& X; const Vector& Y;
    enum ConditionType { Dirichlet, Neumann } type; // Boundary conditions (homogeneous)
    enum { Top, Bottom, Left, Right };
    const vec4 boundaryValues; // [Top, Bottom, Left, Right] values for Dirichlet or Neumann boundary conditions
    UMFPACK A;
    Helmholtz(real H, const Grid& grid, ConditionType type, vec4 boundaryValues) :
        H(H), Nx(grid.Nx), Ny(grid.Ny), X(grid.X), Y(grid.Y), type(type), boundaryValues(boundaryValues) {

        Matrix2D M (Nx, Ny);
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
            real h = (xp-xm)*(yp-ym)*h;
            M(i,j, i-1,j)   =    -left;
            M(i,j, i+1,j)   =         -right;
            M(i,j, i,  j-1) =                     -top;
            M(i,j, i,  j+1) =                           -bottom;
            M(i,j, i,  j  ) = H+left+right+top+bottom;
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
            auto bord = [&](bool transpose, int i0, int i1, int i) {
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
                m(i,i0, i  ,i0) = h+left+right+bottom;
            };
            for(uint i: range(1,Nx-1)) {
                bord(false, 0,    1,      i);
                bord(false, Ny-1, Ny-1-1, i);
            }
            for(uint j: range(1,Ny-1)) {
                bord(true,  0,    1,      j);
                bord(true,  Nx-1, Nx-1-1, j);
            }
            // Dirichlet au coins
            M(0, 0,    0, 0)          = 1;
            M(Nx-1, 0,    Nx-1, 0)    = 1;
            M(0,    Ny-1, 0, Ny-1)    = 1;
            M(Nx-1, Ny-1, Nx-1, Ny-1) = 1;
        }
        // LU Factorization
        A = M;
    }

    /// Solves (H - L)u = S
    Vector2D solve(const Vector2D& S) {
        Vector2D b (Nx,Ny);
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
            // Homogeneous Neumann needs Dirichlet corners to set the constant
            b(0,    0)    = 0;
            b(Nx-1, 0)    = 0;
            b(0, Ny-1)    = 0;
            b(Nx-1, Ny-1) = 0;
        }
        return Vector2D(A.solve(b), Nx, Ny);
    }
};

// Solves D.u = 0, dt u + (u.D)u = -Dp + (1/Re)Lu
struct Chorin {
    const uint N, Nx=N, Ny=N;
    const real U = 1; // Boundary speed
    const real L = 1; // Domain size
    const real nu = 1; // Fluid viscosity
    const real Re = U*L/nu; // Reynolds
    const real dt = 1; // Time step
    Grid grid {Nx,Ny, Grid::Regular};
    const Vector& X = grid.X; const Vector& Y = grid.Y;
    Chorin(uint N):N(N) {}

    Helmholtz prediction {Re/dt, grid, Helmholtz::Dirichlet, vec4(U,0,0,0)}; // Dirichlet conditions with top u = U
    Vector2D predict(const Vector2D& U, const Vector2D& Ux, const Vector2D& Uy) {
        Vector2D S(Nx, Ny);
        for(uint i: range(1,Nx-1)) for(uint j: range(1,Ny-1)) {
            real u = U(i,j), ux = Ux(i,j), uy = Uy(i,j);
            real dxU = ( U(i+1,j) - U(i-1,j) ) / ( X[i+1] - X[i-1] );
            real dyU = ( U(i,j+1) - U(i,j-1) ) / ( Y[j+1] - X[j-1] );
            S(i,j) = Re*(u/dt - (ux*dxU + uy*dyU)); // b = Lx*Ly*S
        }
        // Dirichlet conditions are set with Helmholtz::boundaryValues
        return prediction.solve(S);
    }

    Helmholtz correction {0, grid, Helmholtz::Neumann, 0}; // Poisson with homogeneous Neumann conditions (Dp=0)
    Vector2D correct(const Vector2D& ux, const Vector2D& uy) {
        Vector2D S(Nx, Ny);
        for(uint i: range(1,Nx-1)) for(uint j: range(1,Ny-1)) {
            real dxU = ( ux(i+1,j) - ux(i-1,j) ) / ( X[i+1] - X[i-1] );
            real dyU = ( uy(i,j+1) - uy(i,j-1) ) / ( Y[j+1] - X[j-1] );
            S(i,j) = /*(1/dt)**/(dxU + dyU);
        }
        // Neumann conditions are set with Helmholtz::boundaryValues
        return correction.solve(S);
    }

    // Current state vector
    Vector2D ux {Nx,Ny}, uy{Nx,Ny};

    // Solves one iteration of Chorin
    void solve() {
        Vector2D nux = predict(ux, ux,uy);
        Vector2D nuy = predict(uy, ux,uy);
        Vector2D psi = correct(nux, nuy);
        for(uint i: range(1,Nx-1)) for(uint j: range(1,Ny-1)) {
            real dxP = ( psi(i+1,j) - psi(i-1,j) ) / ( X[i+1] - X[i-1] );
            real dyP = ( psi(i,j+1) - psi(i,j-1) ) / ( Y[j+1] - X[j-1] );
            ux(i,j) = nux(i,j) - /*dt**/dxP;
            uy(i,j) = nuy(i,j) - /*dt**/dyP;
        }
    }
};


/// Displays scalar fields as blue,green,red components
struct FieldView : Widget {
    const Vector& X; const Vector& Y;
    const Vector2D& B; const Vector2D& G; const Vector2D& R;
    FieldView(const Grid& grid, const Vector2D& B,const Vector2D& G, const Vector2D& R):X(grid.X),Y(grid.Y),B(B),G(G),R(R){}
    int2 sizeHint() { return int2(720/2); }
    void render(const Image& target) override {
        vec3 cMax (maxabs(B), maxabs(G), maxabs(R));
        for(uint i: range(X.size-1)) for(uint j: range(Y.size-1)) {
            for(uint y: range(round(target.size().y*Y[j]),round(target.size().y*Y[j+1]))) {
                for(uint x: range(round(target.size().x*X[i]),round(target.size().x*X[i+1]))) {
                    real u = ((x+1./2)/target.size().x-X[i])/(X[i+1]-X[i]);
                    real v = ((y+1./2)/target.size().y-Y[j])/(Y[j+1]-Y[j]);
                    vec3 c;
                    for(uint i : range(3)) {
                        const Vector2D& C = *(ref<const Vector2D*>{&B,&G,&R}[i]);
                        c[i] = (1-v) * ((1-u) * C(i,j  ) + u * C(i+1,j  )) +
                                v    * ((1-u) * C(i,j+1) + u * C(i+1,j+1));
                    }
                    int3 linear = int3(round(float(0xFFF)*((vec3(1)+c/cMax)/float(2))));
                    extern uint8 sRGB_forward[0x1000];
                    target(x,y) = byte4(sRGB_forward[linear.x], sRGB_forward[linear.y], sRGB_forward[linear.z], 0xFF);
                }
            }
        }
    }
};

struct Application {
    const uint N = 8;
    Chorin chorin {N};
    uint t = 0;
    FieldView fieldView {chorin.grid, chorin.ux, chorin.uy, chorin.ux};
    Window window {&fieldView, int2(1280,720), str(N)};
    Application() {
        chorin.solve();
        if(arguments().contains("video"_)) {
            writeFile("Helmholtz.png"_,encodePNG(renderToImage(fieldView, window.size)), home());
        } else {
            window.oxygenBackground = false;
            window.localShortcut(Escape).connect([]{exit();});
            window.frameSent.connect([this](){
                chorin.solve();
                window.render();
                window.setTitle(str(t++));
            });
            window.show();
        }
    }
} app;
