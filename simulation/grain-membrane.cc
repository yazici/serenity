// TODO: Face contacts
#include "simulation.h"
#include "parallel.h"
#include "grain.h"
#include "membrane.h"

#if MEMBRANE_FACE
static inline vXsf pointTriangleDistance(vXsf Ox, vXsf Oy, vXsf Oz, vXsf e0x, vXsf e0y, vXsf e0z,
                                         vXsf e1x, vXsf e1y, vXsf e1z, vXsf& u, vXsf& v, vXsf& Rx, vXsf& Ry, vXsf& Rz) {
 const vXsf Nx = e0y*e1z - e1y*e0z;
 const vXsf Ny = e0z*e1x - e1z*e0x;
 const vXsf Nz = e0x*e1y - e1x*e0y;
 const vXsf Px = Ny*e1z - e1y*Nz;
 const vXsf Py = Nz*e1x - e1z*Nx;
 const vXsf Pz = Nx*e1y - e1x*Ny;
 const vXsf det = Px * e0x + Py * e0y + Pz * e0z;
 const vXsf invDet = rcp(det);
 const vXsf u1 = max(_0f, invDet * (Px*Ox + Py*Oy + Pz*Oz));
 const vXsf Qx = Oy*e0z - e0y*Oz;
 const vXsf Qy = Oz*e0x - e0z*Ox;
 const vXsf Qz = Ox*e0y - e0x*Oy;
 const vXsf v1 = max(_0f, invDet * (Qx*Nx + Qy*Ny + Qz*Nz));
 // Rotates u1,v1 to e0+e1, e1-e0
 const vXsf u2 = min(_1f, (u1+v1)); // Clamps to third (diagonal // e1-e0) edge
 const vXsf v2 =  max(-_1f, min(_1f, (-u1+v1))); // Clamps orthogonally to diagonal edge
 // Rotates back to e0,e1
 u = (u2-v2) * floatX(1./2);
 v = (u2+v2) * floatX(1./2);
 const vXsf X = u*e0x + v*e1x;
 const vXsf Y = u*e0y + v*e1y;
 const vXsf Z = u*e0z + v*e1z;
 Rx = Ox-X;
 Ry = Oy-Y;
 Rz = Oz-Z;
 return sq(Rx) + sq(Ry) + sq(Rz);
}
static inline vXsf pointTriangleDistance(vXsf Ox, vXsf Oy, vXsf Oz, vXsf e0x, vXsf e0y, vXsf e0z,
                                         vXsf e1x, vXsf e1y, vXsf e1z) {
 vXsf u, v; vXsf Rx, Ry, Rz;
 return pointTriangleDistance(Ox, Oy, Oz, e0x, e0y, e0z, e1x, e1y, e1z, u, v, Rx, Ry, Rz);
}

template<int I> static inline void filter(const uint start, const uint size,
                                          const int* const gmA, const int* const gmB,
                                          const float* const gPx, const float* const gPy, const float* const gPz,
                                          const float* const mPx, const float* const mPy, const float* const mPz,
                                          const vXsi margin, const vXsi stride,
                                          const vXsf sqRadius,
                                          float* const gmL, atomic& contactCount, int* const gmContact) {
 const vXsi c0 = -stride, c1 = -stride-intX(1), c2 = intX(-1);
 for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
  const vXsi A = load(gmA, i), B = load(gmB, i);
  const vXsf Ax = gather(gPx, A), Ay = gather(gPy, A), Az = gather(gPz, A);
  const vXsf V0x = gather(mPx, B), V0y = gather(mPy, B), V0z = gather(mPz, B);
  const vXsf Rx = Ax-V0x, Ry = Ay-V0y, Rz = Az-V0z;
  const vXsi rowIndex = (B-margin)/stride;
  vXsi e0, e1;
  if(I == 0) { // (.,0,1)
   e0 = rowIndex%intX(2) +c0;
   e1 = rowIndex%intX(2) +c1;
  } else { // (.,1,2)
   e0 = rowIndex%intX(2) +c1;
   e1 = c2;
  }
  const vXsf e0x = gather(mPx, B+e0) - V0x;
  const vXsf e0y = gather(mPy, B+e0) - V0y;
  const vXsf e0z = gather(mPz, B+e0) - V0z;
  const vXsf e1x = gather(mPx, B+e1) - V0x;
  const vXsf e1y = gather(mPy, B+e1) - V0y;
  const vXsf e1z = gather(mPz, B+e1) - V0z;
  const maskX contact = lessThan(pointTriangleDistance(Rx, Ry, Rz, e0x, e0y, e0z, e1x, e1y, e1z), sqRadius);
  maskStore(gmL+i, ~contact, _0f);
  const uint index = contactCount.fetchAdd(countBits(contact));
  compressStore(gmContact+index, contact, intX(i)+_seqi);
 }
}

template<int I> static inline void evaluate(const int start, const int size,
                                            const int* pContacts, const int unused contactCount,
                                            const int* gmA, const int* gmB,
                                            const float* grainPx, const float* grainPy, const float* grainPz,
                                            const float* mPx, const float* mPy, const float* mPz,
                                            const int margin, const int stride,
                                            const vXsf Gr,
                                            float* const pLocalAx, float* const pLocalAy, float* const pLocalAz,
                                            float* const pLocalBu, float* const pLocalBv,
                                            const vXsf K, const vXsf Kb,
                                            const vXsf staticFrictionStiffness, const vXsf frictionCoefficient,
                                            const vXsf staticFrictionLength, const vXsf staticFrictionSpeed, const vXsf staticFrictionDamping,
                                            const float* AVx, const float* AVy, const float* AVz,
                                            const float* pBVx, const float* pBVy, const float* pBVz,
                                            const float* pAAVx, const float* pAAVy, const float* pAAVz,
                                            const float* ArotationX, const float* ArotationY, const float* ArotationZ,
                                            const float* ArotationW,
                                            float* const pFx, float* const pFy, float* const pFz,
                                            float* const pTAx, float* const pTAy, float* const pTAz,
                                            float* const pU, float* const pV) {
 const vXsi marginX = intX(margin);
 const vXsi strideX = intX(stride);
 const vXsi c0 = intX(-stride), c1 = intX(-stride-1), c2 = intX(-1);
 for(int i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const vXsi contacts = load(pContacts, i);
  const vXsi A = gather(gmA, contacts), B = gather(gmB, contacts);
  const vXsf Ax = gather(grainPx, A), Ay = gather(grainPy, A), Az = gather(grainPz, A);
  const vXsf B0x = gather(mPx, B), B0y = gather(mPy, B), B0z = gather(mPz, B);
  const vXsf Ox = Ax-B0x, Oy = Ay-B0y, Oz = Az-B0z;
  const vXsi rowIndex = (B-marginX)/strideX;
  vXsi e0, e1;
  if(I == 0) { // (.,0,1)
   e0 = rowIndex%intX(2) +c0;
   e1 = rowIndex%intX(2) +c1;
  } else { // (.,1,2)
   e0 = rowIndex%intX(2) +c1;
   e1 = c2;
  }
  const vXsf B1x = gather(mPx, B+e0);
  const vXsf B1y = gather(mPy, B+e0);
  const vXsf B1z = gather(mPz, B+e0);
  const vXsf B2x = gather(mPx, B+e1);
  const vXsf B2y = gather(mPy, B+e1);
  const vXsf B2z = gather(mPz, B+e1);
  const vXsf e0x = B1x - B0x;
  const vXsf e0y = B1y - B0y;
  const vXsf e0z = B1z - B0z;
  const vXsf e1x = B2x - B0x;
  const vXsf e1y = B2y - B0y;
  const vXsf e1z = B2z - B0z;
  vXsf U, V, Rx, Ry, Rz;
  const vXsf d2 = pointTriangleDistance(Ox, Oy, Oz, e0x, e0y, e0z, e1x, e1y, e1z, U, V, Rx, Ry, Rz);
  const vXsf R = sqrt(d2);
  const vXsf depth = Gr - R;
  // Tension
  const vXsf Fk = K * sqrt(depth) * depth;
  // Relative velocity
  const vXsf AAVx = gather(pAAVx, A), AAVy = gather(pAAVy, A), AAVz = gather(pAAVz, A);
  const vXsf BV0x = gather(pBVx, B);
  const vXsf BV0y = gather(pBVy, B);
  const vXsf BV0z = gather(pBVz, B);
  const vXsf BV1x = gather(pBVx, B+e0);
  const vXsf BV1y = gather(pBVy, B+e0);
  const vXsf BV1z = gather(pBVz, B+e0);
  const vXsf BV2x = gather(pBVx, B+e1);
  const vXsf BV2y = gather(pBVy, B+e1);
  const vXsf BV2z = gather(pBVz, B+e1);
  const vXsf W = _1f-U-V;
  const vXsf BVx = W*BV0x + U*BV1x + V*BV2x;
  const vXsf BVy = W*BV0y + U*BV1y + V*BV2y;
  const vXsf BVz = W*BV0z + U*BV1z + V*BV2z;
  const vXsf Nx = Rx/R, Ny = Ry/R, Nz = Rz/R;
  const vXsf RAx = - Gr * Nx, RAy = - Gr * Ny, RAz = - Gr * Nz;
  const vXsf RVx = gather(AVx, A) + (AAVy*RAz - AAVz*RAy) - BVx;
  const vXsf RVy = gather(AVy, A) + (AAVz*RAx - AAVx*RAz) - BVy;
  const vXsf RVz = gather(AVz, A) + (AAVx*RAy - AAVy*RAx) - BVz;
  // Damping
  const vXsf normalSpeed = Nx*RVx+Ny*RVy+Nz*RVz;
  const vXsf Fb = - Kb * sqrt(sqrt(depth)) * normalSpeed;
  // Normal force
  const vXsf Fn = Fk + Fb;
  const vXsf NFx = Fn * Nx;
  const vXsf NFy = Fn * Ny;
  const vXsf NFz = Fn * Nz;

#if 0
  if(1) {
   forces.reserve(align(8, forces.size+simd));
   for(size_t k: range(simd)) { // FIXME: ignore pad
    forces.append(vec3(extract(Ax-Rx, k), extract(Ay-Ry, k), extract(Az-Rz, k)),
                  vec3(extract(/*NFx*/Fk * Nx, k), extract(/*NFy*/Fk * Ny, k), extract(/*NFz*/Fk * Nz, k)));
   }
  }
#endif
#if 0
   for(int k: range(simd)) {
    if(!(extract(/*sqrt(Fx*Fx + Fy*Fy + Fz*Fz)*/Fk, k) < 10000*N
         //&& extract(sqrt(Vx*Vx + Vy*Vy + Vz*Vz), k) < 100*m/s
         /* && extract(Pz, k) < membrane->height*/)) { log(
        "sqrt(Fx*Fx + Fy*Fy + Fz*Fz)[k] < ?*N && "
        "sqrt(Vx*Vx + Vy*Vy + Vz*Vz)[k] < ?*m/s && "
        "Pz[k] < membrane->height)\n",
        //sqrt(Fx*Fx + Fy*Fy + Fz*Fz)[k] < 10000*N,
        //sqrt(Vx*Vx + Vy*Vy + Vz*Vz)[k] < 100*m/s,
        //Pz[k] < membrane->height,
        "I", i+k, contactCount,"\n"
        //"X", Px[k], Py[k], Pz[k],"\n"
        "V", /*RVx[k], RVy[k], RVz[k],*/ extract(sqrt(RVx*RVx + RVy*RVy + RVz*RVz), k),"m/s\n"
                                                                                       "F", /*Fx[k], Fy[k], Fz[k],*/ extract(/*sqrt(Fx*Fx + Fy*Fy + Fz*Fz)*/Fk, k),"\n"
                                                                                                                                                                   "depth", extract(depth, k),"m", extract(floatX(100)*depth/Gr, k),"%","\n",
        "O", extract(Ox, k), extract(Oy, k), extract(Oz, k),"\n"
                                                            "e0", extract(e0x, k), extract(e0y, k), extract(e0z, k), "\n",
        "e1", extract(e1x, k), extract(e1y, k), extract(e1z, k), "\n",
        "U,V", extract(U, k), extract(V, k), "\n",
        "R", extract(Rx, k), extract(Ry, k), extract(Rz, k), "\n"
        /*"//",
          "H", (membrane->height-grain->radius) /m,
          "X",  Px[k] /m, Py[k] /m, Pz[k] /m,
          "V", Vx[k] /(m/s), Vy[k] /(m/s), Vz[k] /(m/s),
          "F", Fx[k] /N, Fy[k] /N, Fz[k] /N*/);
     //highlightGrains.append(i+k);
     fail=true; return;
    }
   }
#endif

  // Dynamic friction
  // Tangent relative velocity
  const vXsf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  const vXsf TRVx = RVx - RVn * Nx;
  const vXsf TRVy = RVy - RVn * Ny;
  const vXsf TRVz = RVz - RVn * Nz;
  const vXsf tangentRelativeSpeed = sqrt(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  const maskX div0 = greaterThan(tangentRelativeSpeed, _0f);
  const vXsf Fd = - frictionCoefficient * Fn / tangentRelativeSpeed;
  const vXsf FDx = mask3_fmadd(Fd, TRVx, _0f, div0);
  const vXsf FDy = mask3_fmadd(Fd, TRVy, _0f, div0);
  const vXsf FDz = mask3_fmadd(Fd, TRVz, _0f, div0);

  // Gather static frictions
  const vXsf oldLocalAx = gather(pLocalAx, contacts);
  const vXsf oldLocalAy = gather(pLocalAy, contacts);
  const vXsf oldLocalAz = gather(pLocalAz, contacts);
  const vXsf oldlocalBu = gather(pLocalBu, contacts);
  const vXsf oldlocalBv = gather(pLocalBv, contacts);

  const vXsf QAx = gather(ArotationX, A);
  const vXsf QAy = gather(ArotationY, A);
  const vXsf QAz = gather(ArotationZ, A);
  const vXsf QAw = gather(ArotationW, A);
  const vXsf X1 = QAw*RAx + RAy*QAz - QAy*RAz;
  const vXsf Y1 = QAw*RAy + RAz*QAx - QAz*RAx;
  const vXsf Z1 = QAw*RAz + RAx*QAy - QAx*RAy;
  const vXsf W1 = - (RAx * QAx + RAy * QAy + RAz * QAz);
  const vXsf newLocalAx = QAw*X1 - (W1*QAx + QAy*Z1 - Y1*QAz);
  const vXsf newLocalAy = QAw*Y1 - (W1*QAy + QAz*X1 - Z1*QAx);
  const vXsf newLocalAz = QAw*Z1 - (W1*QAz + QAx*Y1 - X1*QAy);

  const vXsf newlocalBu = U;
  const vXsf newlocalBv = V;

  const maskX reset = equal(oldLocalAx, _0f);
  vXsf localAx = blend(reset, oldLocalAx, newLocalAx);
  const vXsf localAy = blend(reset, oldLocalAy, newLocalAy);
  const vXsf localAz = blend(reset, oldLocalAz, newLocalAz);
  const vXsf localBu = blend(reset, oldlocalBu, newlocalBu);
  const vXsf localBv = blend(reset, oldlocalBv, newlocalBv);

  const vXsf QX = QAw*localAx - (localAy*QAz - QAy*localAz);
  const vXsf QY = QAw*localAy - (localAz*QAx - QAz*localAx);
  const vXsf QZ = QAw*localAz - (localAx*QAy - QAx*localAy);
  const vXsf QW = localAx * QAx + localAy * QAy + localAz * QAz;
  const vXsf FRAx = QAw*QX + QW*QAx + QAy*QZ - QY*QAz;
  const vXsf FRAy = QAw*QY + QW*QAy + QAz*QX - QZ*QAx;
  const vXsf FRAz = QAw*QZ + QW*QAz + QAx*QY - QX*QAy;
  const vXsf localBw = _1f - localBu - localBv;
  const vXsf gBx = localBw * B0x + localBu * B1x + localBv * B2x;
  const vXsf gBy = localBw * B0y + localBu * B1y + localBv * B2y;
  const vXsf gBz = localBw * B0z + localBu * B1z + localBv * B2z;

  const vXsf gAx = Ax + FRAx;
  const vXsf gAy = Ay + FRAy;
  const vXsf gAz = Az + FRAz;
  const vXsf Dx = gBx - gAx;
  const vXsf Dy = gBy - gAy;
  const vXsf Dz = gBz - gAz;
  const vXsf Dn = Nx*Dx + Ny*Dy + Nz*Dz;
  // Tangent offset
  const vXsf TOx = Dx - Dn * Nx;
  const vXsf TOy = Dy - Dn * Ny;
  const vXsf TOz = Dz - Dn * Nz;
  const vXsf tangentLength = sqrt(TOx*TOx+TOy*TOy+TOz*TOz);
  const vXsf Ks = staticFrictionStiffness * Fn;
  const vXsf Fs = Ks * tangentLength;
  // Spring direction
  const vXsf SDx = TOx / tangentLength;
  const vXsf SDy = TOy / tangentLength;
  const vXsf SDz = TOz / tangentLength;
  const maskX hasTangentLength = greaterThan(tangentLength, _0f);
  const vXsf sfFb = staticFrictionDamping * (SDx * RVx + SDy * RVy + SDz * RVz);
  const maskX hasStaticFriction = greaterThan(staticFrictionLength, tangentLength)
    & greaterThan(staticFrictionSpeed, tangentRelativeSpeed);
  const vXsf sfFt = maskSub(Fs, hasTangentLength, sfFb);
  const vXsf FTx = mask3_fmadd(sfFt, SDx, FDx, hasStaticFriction & hasTangentLength);
  const vXsf FTy = mask3_fmadd(sfFt, SDy, FDy, hasStaticFriction & hasTangentLength);
  const vXsf FTz = mask3_fmadd(sfFt, SDz, FDz, hasStaticFriction & hasTangentLength);

  // Resets contacts without static friction
  localAx = blend(hasStaticFriction, _0f, localAx); // FIXME use 1s (NaN) not 0s to flag resets

  // Scatter static frictions
  scatter(pLocalAx, contacts, localAx);
  scatter(pLocalAy, contacts, localAy);
  scatter(pLocalAz, contacts, localAz);
  scatter(pLocalBu, contacts, localBu);
  scatter(pLocalBv, contacts, localBv);

  store(pFx, i, NFx + FTx);
  store(pFy, i, NFy + FTy);
  store(pFz, i, NFz + FTz);
  store(pTAx, i, RAy*FTz - RAz*FTy);
  store(pTAy, i, RAz*FTx - RAx*FTz);
  store(pTAz, i, RAx*FTy - RAy*FTx);
  store(pU, i, U);
  store(pV, i, V);
 }
}

template<int I> static inline float sum(const uint size, const int* const contacts,
                                        const int* const A, const int* const B,
                                        const float* const pFx, const float* const pFy, const float* const pFz,
                                        const float* const pTAx, const float* const pTAy, const float* const pTAz,
                                        float* const gFx, float* const gFy, float* const gFz,
                                        float* const gTx, float* const gTy, float* const gTz,
                                        const float* const gPx, const float* const gPy,
                                        const float* const U, const float* const V,
                                        float* const mFx, float* const mFy, float* const mFz,
                                        const int margin, const int stride) {
 float radialForce = 0;
 for(uint i = 0; i < size; i++) { // Scalar scatter add
  uint index = contacts[i];
  uint a = A[index];
  uint b = B[index];
  float Fx = pFx[i];
  float Fy = pFy[i];
  float Fz = pFz[i];
  gFx[a] += Fx;
  gFy[a] += Fy;
  gFz[a] += Fz;
  gTx[a] += pTAx[i];
  gTy[a] += pTAy[i];
  gTz[a] += pTAz[i];
  float Px = gPx[a], Py = gPy[a];
  float L = sqrt(Px*Px + Py*Py);
  radialForce += (Px * Fx + Py * Fy) / L;
  float u = U[i];
  float v = V[i];
  float w = 1-u-v;
  mFx[b] -= w*Fx;
  mFy[b] -= w*Fy;
  mFz[b] -= w*Fz;
  const int rowIndex = (b-margin)/stride;
  int e0, e1;
  if(I == 0) { // (.,0,1)
   e0 = rowIndex%2 -stride;
   e1 = rowIndex%2 -stride-1;
  } else { // (.,1,2)
   e0 = rowIndex%2 -stride-1;
   e1 = -1;
  }
  mFx[b+e0] -= u*Fx;
  mFy[b+e0] -= u*Fy;
  mFz[b+e0] -= u*Fz;
  mFx[b+e1] -= v*Fx;
  mFy[b+e1] -= v*Fy;
  mFz[b+e1] -= v*Fz;
 }
 if(!isNumber(radialForce)) {
  log("isNumber(radialForce)");
  fail = true;
  return 0;
 }
 return -radialForce;
}
#else

static inline void evaluateGrainMembrane(const int start, const int size,
                                         const int* pContacts, const int unused contactCount,
                                         const int* gmA, const int* gmB,
                                         const float* grainPx, const float* grainPy, const float* grainPz,
                                         const float* membranePx, const float* membranePy, const float* membranePz,
                                         const float Gr,
                                         float* const LocalAx, float* const LocalAy, float* const LocalAz,
                                         const vXsf K, const vXsf Kb,
                                         const vXsf staticFrictionStiffness, const vXsf dynamicFrictionCoefficient,
                                         const vXsf staticFrictionLength, const vXsf staticFrictionSpeed,
                                         const vXsf staticFrictionDamping,
                                         const float* AVx, const float* AVy, const float* AVz,
                                         const float* pBVx, const float* pBVy, const float* pBVz,
                                         const float* pAAVx, const float* pAAVy, const float* pAAVz,
                                         const float* ArotationX, const float* ArotationY, const float* ArotationZ,
                                         const float* ArotationW,
                                         float* const pFx, float* const pFy, float* const pFz,
                                         float* const pTAx, float* const pTAy, float* const pTAz) {
 for(int i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const vXsi contacts = load(pContacts, i);
  const vXsi A = gather(gmA, contacts), B = gather(gmB, contacts);
  const vXsf Ax = gather(grainPx, A), Ay = gather(grainPy, A), Az = gather(grainPz, A);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  const vXsf Bx = gather(membranePx, B), By = gather(membranePy, B), Bz = gather(membranePz, B);
  const vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  const vXsf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
  const vXsf depth = Gr - length;
  const vXsf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
  const vXsf RAx = - Gr  * Nx, RAy = - Gr * Ny, RAz = - Gr * Nz;

  // Tension
  const vXsf Fk = K * sqrt(depth) * depth;
  // Relative velocity
  const vXsf AAVx = gather(pAAVx, A), AAVy = gather(pAAVy, A), AAVz = gather(pAAVz, A);
  const vXsf BVx = gather(pBVx, B), BVy = gather(pBVy, B), BVz = gather(pBVz, B);
  const vXsf RVx = gather(AVx, A) + (AAVy*RAz - AAVz*RAy) - BVx;
  const vXsf RVy = gather(AVy, A) + (AAVz*RAx - AAVx*RAz) - BVy;
  const vXsf RVz = gather(AVz, A) + (AAVx*RAy - AAVy*RAx) - BVz;
  // Damping
  const vXsf normalSpeed = Nx*RVx+Ny*RVy+Nz*RVz;
  const vXsf Fb = - Kb * sqrt(sqrt(depth)) * normalSpeed ; // Damping
  // Normal force
  const vXsf Fn = Fk + Fb;
  const vXsf NFx = Fn * Nx;
  const vXsf NFy = Fn * Ny;
  const vXsf NFz = Fn * Nz;

  if(1) {
   forces.reserve(align(8, forces.size+simd));
   for(size_t k: range(simd)) // FIXME: ignore pad
    forces.append(vec3(extract(Ax-Rx, k), extract(Ay-Ry, k), extract(Az-Rz, k)),
                  vec3(extract(NFx, k), extract(NFy, k), extract(NFz, k)));
  }

  // Dynamic friction
  // Tangent relative velocity
  const vXsf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  const vXsf TRVx = RVx - RVn * Nx;
  const vXsf TRVy = RVy - RVn * Ny;
  const vXsf TRVz = RVz - RVn * Nz;
  const vXsf tangentRelativeSpeed = sqrt(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  const maskX div0 = greaterThan(tangentRelativeSpeed, _0f);
  const vXsf Fd = - dynamicFrictionCoefficient * Fn / tangentRelativeSpeed;
  const vXsf FDx = mask3_fmadd(Fd, TRVx, _0f, div0);
  const vXsf FDy = mask3_fmadd(Fd, TRVy, _0f, div0);
  const vXsf FDz = mask3_fmadd(Fd, TRVz, _0f, div0);

  // Gather static frictions
  const vXsf oldLocalAx = gather(LocalAx, contacts);
  const vXsf oldLocalAy = gather(LocalAy, contacts);
  const vXsf oldLocalAz = gather(LocalAz, contacts);

  const vXsf QAx = gather(ArotationX, A);
  const vXsf QAy = gather(ArotationY, A);
  const vXsf QAz = gather(ArotationZ, A);
  const vXsf QAw = gather(ArotationW, A);
  const vXsf X1 = QAw*RAx + RAy*QAz - QAy*RAz;
  const vXsf Y1 = QAw*RAy + RAz*QAx - QAz*RAx;
  const vXsf Z1 = QAw*RAz + RAx*QAy - QAx*RAy;
  const vXsf W1 = - (RAx * QAx + RAy * QAy + RAz * QAz);
  const vXsf newLocalAx = QAw*X1 - (W1*QAx + QAy*Z1 - Y1*QAz);
  const vXsf newLocalAy = QAw*Y1 - (W1*QAy + QAz*X1 - Z1*QAx);
  const vXsf newLocalAz = QAw*Z1 - (W1*QAz + QAx*Y1 - X1*QAy);

  const maskX reset = equal(oldLocalAx, _0f);
  vXsf localAx = blend(reset, oldLocalAx, newLocalAx);
  const vXsf localAy = blend(reset, oldLocalAy, newLocalAy);
  const vXsf localAz = blend(reset, oldLocalAz, newLocalAz);

  const vXsf X = QAw*localAx - (localAy*QAz - QAy*localAz);
  const vXsf Y = QAw*localAy - (localAz*QAx - QAz*localAx);
  const vXsf Z = QAw*localAz - (localAx*QAy - QAx*localAy);
  const vXsf W = localAx * QAx + localAy * QAy + localAz * QAz;
  const vXsf FRAx = QAw*X + W*QAx + QAy*Z - Y*QAz;
  const vXsf FRAy = QAw*Y + W*QAy + QAz*X - Z*QAx;
  const vXsf FRAz = QAw*Z + W*QAz + QAx*Y - X*QAy;

  const vXsf gAx = Ax + FRAx;
  const vXsf gAy = Ay + FRAy;
  const vXsf gAz = Az + FRAz;
  const vXsf gBx = Bx;
  const vXsf gBy = By;
  const vXsf gBz = Bz;
  const vXsf Dx = gBx - gAx;
  const vXsf Dy = gBy - gAy;
  const vXsf Dz = gBz - gAz;
  const vXsf Dn = Nx*Dx + Ny*Dy + Nz*Dz;
  // Tangent offset
  const vXsf TOx = Dx - Dn * Nx;
  const vXsf TOy = Dy - Dn * Ny;
  const vXsf TOz = Dz - Dn * Nz;
  const vXsf tangentLength = sqrt(TOx*TOx+TOy*TOy+TOz*TOz);
  const vXsf Ks = staticFrictionStiffness * Fn;
  const vXsf Fs = Ks * tangentLength; // 0.1~1 fN
  // Spring direction
  const vXsf SDx = TOx / tangentLength;
  const vXsf SDy = TOy / tangentLength;
  const vXsf SDz = TOz / tangentLength;
  const maskX hasTangentLength = greaterThan(tangentLength, _0f);
  const vXsf sfFb = staticFrictionDamping * (SDx * RVx + SDy * RVy + SDz * RVz);
  const maskX hasStaticFriction = greaterThan(staticFrictionLength, tangentLength)
    & greaterThan(staticFrictionSpeed, tangentRelativeSpeed);
  const vXsf sfFt = maskSub(Fs, hasTangentLength, sfFb);
  const vXsf FTx = mask3_fmadd(sfFt, SDx, FDx, hasStaticFriction & hasTangentLength);
  const vXsf FTy = mask3_fmadd(sfFt, SDy, FDy, hasStaticFriction & hasTangentLength);
  const vXsf FTz = mask3_fmadd(sfFt, SDz, FDz, hasStaticFriction & hasTangentLength);
  // Resets contacts without static friction
  localAx = blend(hasStaticFriction, _0f, localAx); // FIXME use 1s (NaN) not 0s to flag resets
  store(pFx, i, NFx + FTx);
  store(pFy, i, NFy + FTy);
  store(pFz, i, NFz + FTz);
  store(pTAx, i, RAy*FTz - RAz*FTy);
  store(pTAy, i, RAz*FTx - RAx*FTz);
  store(pTAz, i, RAx*FTy - RAy*FTx);

  // Scatter static frictions
  scatter(LocalAx, contacts, localAx);
  scatter(LocalAy, contacts, localAy);
  scatter(LocalAz, contacts, localAz);
 }
}
#endif

void Simulation::stepGrainMembrane() {
 if(!grain->count || !membrane->count) return;
 //grainMembraneGlobalMinD = 0; // DEBUG
 if(grainMembraneGlobalMinD <= 0)  {
  grainLattice();

  const int X = lattice.size.x, Y = lattice.size.y;
  const int* latticeNeighbours[3*3] = {
   lattice.base.data+(-X*Y-X-1),
   lattice.base.data+(-X*Y-1),
   lattice.base.data+(-X*Y+X-1),

   lattice.base.data+(-X-1),
   lattice.base.data+(-1),
   lattice.base.data+(X-1),

   lattice.base.data+(X*Y-X-1),
   lattice.base.data+(X*Y-1),
   lattice.base.data+(X*Y+X-1)
  };

  const float verletDistance = 2*grain->radius/sqrt(3.); //FIXME: - grain->radius/*Face bounding radius*/;
  assert_(verletDistance > grain->radius + 0);

  for(size_t I: range(2)) { // Two faces / vertex
   auto& gm = grainMembrane[I];
   swap(gm.oldA, gm.A);
   swap(gm.oldB, gm.B);
   swap(gm.oldLocalAx, gm.localAx);
   swap(gm.oldLocalAy, gm.localAy);
   swap(gm.oldLocalAz, gm.localAz);
#if MEMBRANE_FACE
   swap(gm.oldLocalBu, gm.localBu);
   swap(gm.oldLocalBv, gm.localBv);
#endif

   static constexpr size_t averageVerletCount = 16;
   const size_t GWcc = align(simd, max(2048ul, grain->count * averageVerletCount +1));
   if(GWcc > gm.A.capacity) {
    memoryTime.start();
    gm.A = buffer<int>(GWcc, 0);
    gm.B = buffer<int>(GWcc, 0);
    gm.localAx = buffer<float>(GWcc, 0);
    gm.localAy = buffer<float>(GWcc, 0);
    gm.localAz = buffer<float>(GWcc, 0);
#if MEMBRANE_FACE
    gm.localBu = buffer<float>(GWcc, 0);
    gm.localBv = buffer<float>(GWcc, 0);
#endif
    memoryTime.stop();
   }
   gm.A.size = gm.A.capacity;
   gm.B.size = gm.B.capacity;

   atomic contactCount;
   auto search = [&](uint, uint rowIndex) {
    const float* const mPx = membrane->Px.data, *mPy = membrane->Py.data, *mPz = membrane->Pz.data;
    const float* const gPx = grain->Px.data+simd, *gPy = grain->Py.data+simd, *gPz = grain->Pz.data+simd;
    int* const gmA = gm.A.begin(), *gmB = gm.B.begin();
    const vXsf sqVerletDistance = floatX(sq(verletDistance));
    const int W = membrane->W;
    const int stride = membrane->stride;
    const int base = membrane->margin+rowIndex*stride;
    const vXsf scale = floatX(lattice.scale);
    const vXsf minX = floatX(lattice.min.x), minY = floatX(lattice.min.y), minZ = floatX(lattice.min.z);
    const vXsi sizeX = intX(lattice.size.x), sizeYX = intX(lattice.size.y * lattice.size.x);
#if MEMBRANE_FACE
    int e0, e1;
    if(I==0) { // (.,0,1)
     e0 = -stride+rowIndex%2;
     e1 = e0-1;
    } else { // (.,1,2)
     e0 = -stride+rowIndex%2-1;
     e1 = -1;
    }
#endif
    for(int j=0; j<W; j+=simd) {
     const int b = base+j;
     const vXsi B = intX(b)+_seqi;
     const vXsf B0x = load(mPx, b);
     const vXsf B0y = load(mPy, b);
     const vXsf B0z = load(mPz, b);
#if MEMBRANE_FACE
     const vXsf B1x = loadu(mPx, b+e0);
     const vXsf B1y = loadu(mPy, b+e0);
     const vXsf B1z = loadu(mPz, b+e0);
     const vXsf B2x = loadu(mPx, b+e1);
     const vXsf B2y = loadu(mPy, b+e1);
     const vXsf B2z = loadu(mPz, b+e1);
     const vXsf e0x = B1x-B0x;
     const vXsf e0y = B1y-B0y;
     const vXsf e0z = B1z-B0z;
     const vXsf e1x = B2x-B0x;
     const vXsf e1y = B2y-B0y;
     const vXsf e1z = B2z-B0z;
#else
     const vXsf Mx = B0x;
     const vXsf My = B0y;
     const vXsf Mz = B0z;
#endif
#if 0
     // FIXME: duplicate contacts
#define search(N) { \
     const vXsf Mx = B##N##x; \
     const vXsf My = B##N##y; \
     const vXsf Mz = B##N##z;\
     const vXsi index = convert(scale*(Mz-minZ)) * sizeYX \
       + convert(scale*(My-minY)) * sizeX \
       + convert(scale*(Mx-minX)); \
     for(int n: range(3*3)) for(int i: range(3)) { \
       const vXsi A = gather(latticeNeighbours[n]+i, index); \
       const vXsf Ax = gather(gPx, A), Ay = gather(gPy, A), Az = gather(gPz, A); \
       const vXsf Rx = Ax-B0x, Ry = Ay-B0y, Rz = Az-B0z; \
       maskX mask = notEqual(A, _1i) & lessThan( \
          pointTriangleDistance(Rx, Ry, Rz, e0x, e0y, e0z, e1x, e1y, e1z), sqVerletDistance); \
       uint targetIndex = contactCount.fetchAdd(countBits(mask)); \
       compressStore(gmA+targetIndex, mask, A); \
       compressStore(gmB+targetIndex, mask, B); \
      } \
    }
     search(0) search(1) search(2)
#undef search
#else
     // FIXME: Assumes triangle is small enough
     const vXsf Mx = (B0x+B1x+B2x)*floatX(1./3);
     const vXsf My = (B0y+B1y+B2y)*floatX(1./3);
     const vXsf Mz = (B0z+B1z+B2z)*floatX(1./3);
     const vXsi index = convert(scale*(Mz-minZ)) * sizeYX
       + convert(scale*(My-minY)) * sizeX
       + convert(scale*(Mx-minX));
     for(int n: range(3*3)) for(int i: range(3)) {
      const vXsi A = gather(latticeNeighbours[n]+i, index);
      const vXsf Ax = gather(gPx, A), Ay = gather(gPy, A), Az = gather(gPz, A);
      const vXsf Rx = Ax-B0x, Ry = Ay-B0y, Rz = Az-B0z;
      maskX mask = notEqual(A, _1i) & lessThan(
         pointTriangleDistance(Rx, Ry, Rz, e0x, e0y, e0z, e1x, e1y, e1z), sqVerletDistance);
      uint targetIndex = contactCount.fetchAdd(countBits(mask));
      compressStore(gmA+targetIndex, mask, A);
      compressStore(gmB+targetIndex, mask, B);
     }
#endif
   }
  };
  grainMembraneSearchTime += parallel_for(1, membrane->H, search);
  //if(!contactCount) continue;
  assert_(contactCount.count <= gm.A.capacity, contactCount.count, gm.A.capacity);
  gm.A.size = contactCount;
  gm.B.size = contactCount;
  gm.localAx.size = contactCount;
  gm.localAy.size = contactCount;
  gm.localAz.size = contactCount;
#if MEMBRANE_FACE
  gm.localBu.size = contactCount;
  gm.localBv.size = contactCount;
  //gm.localBt.size = contactCount;
#endif

  grainMembraneRepackFrictionTime.start();
  if(gm.A.size) parallel_chunk(gm.A.size, [&](uint, size_t start, size_t size) {
   for(size_t i=start; i<start+size; i++) {
    int a = gm.A[i];
    int b = gm.B[i];
    for(int j: range(gm.oldA.size)) {
     if(gm.oldA[j] == a && gm.oldB[j] == b) {
      gm.localAx[i] = gm.oldLocalAx[j];
      gm.localAy[i] = gm.oldLocalAy[j];
      gm.localAz[i] = gm.oldLocalAz[j];
#if MEMBRANE_FACE
      gm.localBu[i] = gm.oldLocalBu[j];
      gm.localBv[i] = gm.oldLocalBv[j];
#endif
      goto break_;
     }
    } /*else*/ {
     gm.localAx[i] = 0;
    }
break_:;
   }
  });
  grainMembraneRepackFrictionTime.stop();

  assert(align(simd, gm.A.size+1) <= gm.A.capacity, gm.A.size, gm.A.capacity);
  for(size_t i=gm.A.size; i<align(simd, gm.A.size +1); i++) gm.A.begin()[i] = -1;
  assert(align(simd, gm.B.size+1) <= gm.B.capacity);
  for(size_t i=gm.B.size; i<align(simd, gm.B.size +1); i++) gm.B.begin()[i] = membrane->stride+1;
 }
 grainMembraneGlobalMinD = verletDistance - (grain->radius+0);
 if(grainMembraneGlobalMinD < 0) log("grainMembraneGlobalMinD", grainMembraneGlobalMinD);

 /*if(processState > ProcessState::Pour) // Element creation resets verlet lists
   log("grain-membrane", grainMembraneSkipped);*/
 grainMembraneSkipped=0;
} else grainMembraneSkipped++;

// Filters verlet lists, packing contacts to evaluate
for(size_t I: range(1+MEMBRANE_FACE)) { // Face type
 auto& gm = grainMembrane[I];
 if(align(simd, gm.A.size) > gm.contacts.capacity) {
  gm.contacts = buffer<int>(align(simd, gm.A.size));
 }
 gm.contacts.size = 0;

 atomic contactCount;
 auto filter = [this, &gm, I, &contactCount](uint, uint start, uint size) {
#if MEMBRANE_FACE
#define filter(I) \
 ::filter<I>(start, size, \
 gm.A.data, gm.B.data, \
 grain->Px.data+simd, grain->Py.data+simd, grain->Pz.data+simd, \
 membrane->Px.data, membrane->Py.data, membrane->Pz.data, \
 intX(membrane->margin), intX(membrane->stride), \
 floatX(sq(grain->radius)), \
 gm.localAx.begin(), contactCount, gm.contacts.begin())
  if(I==0) filter(0); else filter(1);
#undef filter
#else
  const int* const gmA = gm.A.data, *gmB = gm.B.data;
  const float* const gPx = grain->Px.data+simd, *gPy = grain->Py.data+simd, *gPz = grain->Pz.data+simd;
  const float* const mPx = membrane->Px.data, *mPy = membrane->Py.data, *mPz = membrane->Pz.data;
  float* const gmL = gm.localAx.begin();
  int* const gmContact = gm.contacts.begin();
  const vXsf sqRadius = floatX(sq(grain->radius));
  for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
   vXsi A = load(gmA, i), B = load(gmB, i);
   vXsf Ax = gather(gPx, A), Ay = gather(gPy, A), Az = gather(gPz, A);
   vXsf Bx = gather(mPx, B), By = gather(mPy, B), Bz = gather(mPz, B);
   vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
   vXsf sqDistance = Rx*Rx + Ry*Ry + Rz*Rz;
   maskX contact = lessThan(sqDistance, sqRadius);
   maskStore(gmL+i, ~contact, _0f);
   uint index = contactCount.fetchAdd(countBits(contact));
   compressStore(gmContact+index, contact, intX(i)+_seqi);
  }
#endif
 };
 if(gm.A.size/simd) grainMembraneFilterTime += parallel_chunk(gm.A.size/simd, filter);
 // The partial iteration has to be executed last so that invalid contacts are trailing
 // and can be easily trimmed
 if(gm.A.size%simd != 0) filter(0, gm.A.size/simd, 1u);
 gm.contacts.size = contactCount;
 while(contactCount.count > 0 && gm.contacts[contactCount-1] >= (int)gm.A.size)
  contactCount.count--; // Trims trailing invalid contacts
 gm.contacts.size = contactCount;
 if(!gm.contacts.size) continue;
 for(size_t i=gm.contacts.size; i<align(simd, gm.contacts.size); i++)
  gm.contacts.begin()[i] = gm.A.size;

 // Evaluates forces from (packed) intersections (SoA)
 size_t GWcc = align(simd, gm.contacts.size); // Grain-Membrane contact count
 if(GWcc > gm.Fx.capacity) {
  memoryTime.start();
  gm.Fx = buffer<float>(GWcc);
  gm.Fy = buffer<float>(GWcc);
  gm.Fz = buffer<float>(GWcc);
  gm.TAx = buffer<float>(GWcc);
  gm.TAy = buffer<float>(GWcc);
  gm.TAz = buffer<float>(GWcc);
  gm.U = buffer<float>(GWcc);
  gm.V = buffer<float>(GWcc);
  memoryTime.stop();
 }
 gm.Fx.size = GWcc;
 gm.Fy.size = GWcc;
 gm.Fz.size = GWcc;
 gm.TAx.size = GWcc;
 gm.TAy.size = GWcc;
 gm.TAz.size = GWcc;
 gm.U.size = GWcc;
 gm.V.size = GWcc;
 const float E = 1/((1-sq(grain->poissonRatio))/grain->elasticModulus+(1-sq(Membrane::poissonRatio))/Membrane::elasticModulus);
 const float R = 1/(grain->curvature+Membrane::curvature);
 const float K = 4./3*E*sqrt(R);
 const float mass = 1/(1/grain->mass+1/membrane->mass);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
 grainMembraneEvaluateTime += parallel_chunk(GWcc/simd, [&](uint, size_t start, size_t size) {
  #if MEMBRANE_FACE
  #define evaluate(I) \
   evaluate<I>(start, size, \
   gm.contacts.data, gm.contacts.size, \
   gm.A.data, gm.B.data, \
   grain->Px.data+simd, grain->Py.data+simd, grain->Pz.data+simd, \
   membrane->Px.data, membrane->Py.data, membrane->Pz.data, \
   membrane->margin, membrane->stride, \
   floatX(grain->radius), \
   gm.localAx.begin(), gm.localAy.begin(), gm.localAz.begin(), \
   gm.localBu.begin(), gm.localBv.begin(), \
   floatX(K), floatX(Kb), \
   floatX(staticFrictionStiffness), floatX(dynamicGrainMembraneFrictionCoefficient), \
   floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping), \
   grain->Vx.data+simd, grain->Vy.data+simd, grain->Vz.data+simd, \
   membrane->Vx.data, membrane->Vy.data, membrane->Vz.data, \
   grain->AVx.data+simd, grain->AVy.data+simd, grain->AVz.data+simd, \
   grain->Rx.data+simd, grain->Ry.data+simd, grain->Rz.data+simd, grain->Rw.data+simd, \
   gm.Fx.begin(), gm.Fy.begin(), gm.Fz.begin(), \
   gm.TAx.begin(), gm.TAy.begin(), gm.TAz.begin(), \
   gm.U.begin(), gm.V.begin())
   if(I==0) evaluate(0); else evaluate(1);
  #else
   evaluateGrainMembrane(start, size,
                         gm.contacts.data, gm.contacts.size,
                         gm.A.data, gm.B.data,
                         grain->Px.data+simd, grain->Py.data+simd, grain->Pz.data+simd,
                         membrane->Px.data, membrane->Py.data, membrane->Pz.data,
                         grain->radius,
                         gm.localAx.begin(), gm.localAy.begin(), gm.localAz.begin(),
                         floatX(K), floatX(Kb),
                         floatX(staticFrictionStiffness), floatX(dynamicGrainMembraneFrictionCoefficient),
                         floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping),
                         grain->Vx.data+simd, grain->Vy.data+simd, grain->Vz.data+simd,
                         membrane->Vx.data, membrane->Vy.data, membrane->Vz.data,
                         grain->AVx.data+simd, grain->AVy.data+simd, grain->AVz.data+simd,
                         grain->Rx.data+simd, grain->Ry.data+simd, grain->Rz.data+simd, grain->Rw.data+simd,
                         gm.Fx.begin(), gm.Fy.begin(), gm.Fz.begin(),
                         gm.TAx.begin(), gm.TAy.begin(), gm.TAz.begin());
  #endif
}/*, 1 *//*DEBUG: forces*/);
 if(fail) return;
 grainMembraneContactSizeSum += gm.contacts.size;
 grainMembraneSumTime.start();
#if MEMBRANE_FACE
#define sum(I) \
 this->radialForce += ::sum<I>(gm.contacts.size, \
 gm.contacts.data, gm.A.data, gm.B.data, \
 gm.Fx.data, gm.Fy.data, gm.Fz.data, \
 gm.TAx.data, gm.TAy.data, gm.TAz.data, \
 grain->Fx.begin()+simd, grain->Fy.begin()+simd, grain->Fz.begin()+simd, \
 grain->Tx.begin()+simd, grain->Ty.begin()+simd, grain->Tz.begin()+simd, \
 grain->Px.data+simd, grain->Py.data+simd, \
 gm.U.data, gm.V.data, \
 membrane->Fx.begin(), membrane->Fy.begin(), membrane->Fz.begin(), \
 membrane->margin, membrane->stride)
 if(I==0) sum(0); else sum(1);
#else
 float radialForce = 0;
 for(size_t i = 0; i < gm.contacts.size; i++) { // Scalar scatter add
  size_t index = gm.contacts[i];
  size_t a = gm.A[index];
  size_t b = gm.B[index];
  grain->Fx[simd+a] += gm.Fx[i];
  grain->Fy[simd+a] += gm.Fy[i];
  grain->Fz[simd+a] += gm.Fz[i];
  grain->Tx[simd+a] += gm.TAx[i];
  grain->Ty[simd+a] += gm.TAy[i];
  grain->Tz[simd+a] += gm.TAz[i];
  vec2 N = grain->position(a).xy();
  N /= length(N);
  radialForce += dot(N, vec2(gm.Fx[i], gm.Fy[i]));
  membrane->Fx[b] -= gm.Fx[i];
  membrane->Fy[b] -= gm.Fy[i];
  membrane->Fz[b] -= gm.Fz[i];
 }
 this->radialForce += -radialForce;
#endif
}
grainMembraneSumTime.stop();
radialSumStepCount++;
}
