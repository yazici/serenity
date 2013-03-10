#include "process.h"
#include "algebra.h"
#include "time.h"

struct PoissonSolver {
    const uint Mx=32, My=32; // Spatial resolutions
    const float Lx=1, Ly=1; // Physical dimensions
    const float dx = Lx/Mx, dy = Ly/My; // Physical resolutions
    const float T0 = 0, T1 = 1; // Lateral boundary conditions
    const uint N = Mx*My; // Total sample count
    Matrix A{N,N}; // Poisson operator
    Vector b{N}; // Right-hand vector
    PLU LU; // Factorized operator
    Vector u; // Solution
    PoissonSolver() {
        for(uint x: range(Mx)) {
            for(uint y: range(My)) {
                uint i = y*Mx+x;
                if(x==0||x==Mx-1) { // Fixed value boundary condition (u=T1/T0)
                    A(i,i) = 1;
                    b[i] = (x==0 ? T1 : T0);
                }
                else if(y==0||y==My-1) { // Fixed derivative boundary condition (u'=0)
                    if(y==0) {
                        A(i,i+0*My) = -3/(2*dx); A(i,i+1*My) = 4/(2*dx); A(i,i+2*My) = -1/(2*dx);
                    } else {
                        A(i,i+0*My) = 3/(2*dx); A(i,i-1*My) = -4/(2*dx); A(i,i-2*My) = 1/(2*dx);
                    }
                    b[i] = 0; // No flux
                }
                else { // Poisson equation on interior points (using 2nd order finite differences)
                    A(i,i-1) = A(i,i+1) = 1/(dx*dx);
                    A(i,i-My) = A(i,i+My) = 1/(dy*dy);
                    A(i,i) = -2*(1/(dx*dx)+1/(dy*dy));

                    b[i] = 0; // No source
                }
            }
        }

        {
            ScopeTimer timer;
            LU = factorize(move(A));
            //assert(inverse(PLU)*A==identity(A.n), inverse(PLU)*A);
            u = solve(LU,b);
        }
    }
};
#if DEBUG
#include "window.h"
#include "display.h"
struct PoissonTest : PoissonSolver, Widget {
    Image image;
    Window window {this, int2(-1,-1), "Poisson"_};
    PoissonTest() {
        // Shows solution
        image = Image(Mx, My);
        for(uint x: range(Mx)) {
            for(uint y: range(My)) {
                uint i = y*Mx+x;
                image(x,y) = clip(0, int(u[i]*0xFF), 0xFF); // [0,1] -> [black,white]
                //image(x,y) = byte4(clip(0, int(-u(i)*0xFF), 0xFF), 0, clip(0, int(u(i)*0xFF), 0xFF), 0xFF); [-1,0,1] -> [blue,black,red]
            }
        }
        image = resize(image, 16*Mx, 16*My);

        // Shows operator
        //image = Image(N, N);
        //for(uint i: range(N)) for(uint j: range(N)) image(j,i) = A(i,j) == 0 ? 0 : 0xFF; // Operator coefficients [0,non zero] -> [black, white]
        //for(uint i: range(N)) for(uint j: range(N)) image(j,i) = LU.LU(i,j) == 0 ? 0 : 0xFF; // Operator coefficients [0,non zero] -> [black, white]

        window.localShortcut(Escape).connect(&exit);
    }
    int2 sizeHint() { return int2(16*Mx, 16*My); }
    void render(int2 position, int2) { blit(position, image); }
} test;
#else
PoissonSolver test[9]; //O_o: 8 times -> +10ms
#endif
