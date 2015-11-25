#include "simulation.h"

/*/// For each index A, maps B to packed A-B contact index
buffer<int2> contactMap(size_t ACount, size_t maxBforA, size_t ABCount,
                                         ref<int> ABA, ref<int> ABB) {
 buffer<int2> contactMap {ACount * maxBforA};
 contactMap.clear(int2(0,0));
 for(size_t index: range(ABCount)) {
  size_t a = ABA[index];
  size_t base = a*maxBforA;
  size_t i = 0;
  while(i<maxBforA && contactMap[base+i]) i++; // Finds next available slot
  assert_(i<maxBforA);
  contactMap[base+i] = int2(ABB[index]+1, int(index));
 }
 return contactMap;
}*/

bool Simulation::stepGrainWire() {
 if(!wire.count) return true;

 //buffer<int2> contactMap = ::contactMap(grain.count, maxWireforGrain);
 //grainWireCount = 0;

 unique<Grid> wireGrid = generateGrid(wire, Grain::radius+Wire::radius);
 if(!wireGrid) return false;

 const int X = wireGrid->size.x, Y = wireGrid->size.y;
 const uint16* wireNeighbours[3*3] = {
  wireGrid->base+(-X*Y-X-1)*Grid::cellCapacity,
  wireGrid->base+(-X*Y-1)*Grid::cellCapacity,
  wireGrid->base+(-X*Y+X-1)*Grid::cellCapacity,

  wireGrid->base+(-X-1)*Grid::cellCapacity,
  wireGrid->base+(-1)*Grid::cellCapacity,
  wireGrid->base+(X-1)*Grid::cellCapacity,

  wireGrid->base+(X*Y-X-1)*Grid::cellCapacity,
  wireGrid->base+(X*Y-1)*Grid::cellCapacity,
  wireGrid->base+(X*Y+X-1)*Grid::cellCapacity
 };

 {
  size_t averageGrainWireContactCount = 8;
  buffer<float> grainWireLocalAx {grain.count * averageGrainWireContactCount};
  buffer<float> grainWireLocalAy {grain.count * averageGrainWireContactCount};
  buffer<float> grainWireLocalAz {grain.count * averageGrainWireContactCount};
  buffer<float> grainWireLocalBx {grain.count * averageGrainWireContactCount};
  buffer<float> grainWireLocalBy {grain.count * averageGrainWireContactCount};
  buffer<float> grainWireLocalBz {grain.count * averageGrainWireContactCount};

  // TODO: Verlet
  size_t firstAContactIndex = 0; // Index of first contact with A in old grainWire[Local]A|B list
  for(size_t a: range(grain.count)) { // TODO: SIMD
   size_t offset = wireGrid->index(grain.Px[a], grain.Py[a], grain.Pz[a]); // offset
   for(size_t n: range(3*3)) for(size_t i: range(3)) {
    ref<uint16> list(wireNeighbours[n] + offset + i * Grid::cellCapacity, Grid::cellCapacity);
    for(size_t j=0; list[j] && j<Grid::cellCapacity; j++) {
     assert_(grainWireCount < grainWireA.capacity, grainWireCount, grainWireA.capacity);
     size_t B = list[j]-1;
     size_t index = grainWireCount;
     grainWireA[index] = a; // Grain
     grainWireB[index] = B; // Wire
     for(size_t i: range(grainWire)) {
      size_t b = grainWireIndices[a*grainWire+i][0]; // wire index + 1
      if(!b) break;
      b--;
      if(b == B) { // Repack existing friction
       size_t j = grainWireIndices[a*grainWire+i][1];
       grainWireLocalAx[index] = this->grainWireLocalAx[j];
       grainWireLocalAy[index] = this->grainWireLocalAy[j];
       grainWireLocalAz[index] = this->grainWireLocalAz[j];
       grainWireLocalBx[index] = this->grainWireLocalBx[j];
       grainWireLocalBy[index] = this->grainWireLocalBy[j];
       grainWireLocalBz[index] = this->grainWireLocalBz[j];
       goto break__;
      }
     } /*else*/ {
      grainWireLocalAx[index] = 0; grainWireLocalAy[index] = 0; grainWireLocalAz[index] = 0;
      grainWireLocalBx[index] = 0; grainWireLocalBy[index] = 0; grainWireLocalBz[index] = 0;
     }
break__:;
     grainWireCount++;
    }
   }
  }

  this->grainWireLocalAx = move(grainWireLocalAx);
  this->grainWireLocalAy = move(grainWireLocalAy);
  this->grainWireLocalAz = move(grainWireLocalAz);
  this->grainWireLocalBx = move(grainWireLocalBx);
  this->grainWireLocalBy = move(grainWireLocalBy);
  this->grainWireLocalBz = move(grainWireLocalBz);
 }
 /*//assert(minD3 < 2*Grain::radius/sqrt(3.) - Grain::radius, minD3);
   grainSideGlobalMinD2 = minD3 - Grain::radius;
   assert(grainSideGlobalMinD2 > 0, grainSideGlobalMinD2);*/
 // Aligns
 size_t gWC = grainWireCount;
 while(gWC%8) { // Content does not matter as long as intersection evaluation does not trigger exceptions
  grainWireA[gWC] = 0;
  grainWireB[gWC] = 0;
  gWC++;
 }
 assert(gWC <= grainWireA.capacity);

 // Evaluates (packed) intersections from (packed) (TODO: verlet) lists
 size_t grainWireContactCount = 0;
 buffer<int> grainWireContact(grainWireCount);
 for(size_t index = 0; index < grainWireCount; index += 8) {
  v8si A = *(v8si*)(grainWireA.data+index), B = *(v8si*)(grainWireB.data+index);
  v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
  v8sf Bx = gather(wire.Px.data, B), By = gather(wire.Py.data, B), Bz = gather(wire.Pz.data, B);
  v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
  v8sf depth = float8(Grain::radius+Wire::radius) - length;
  for(size_t k: range(8)) {
   if(index+k == grainWireCount) break /*2*/;
   if(depth[k] > 0) {
    assert_(depth[k] < 0.02);
    grainWireContact[grainWireContactCount] = index+k;
    grainWireContactCount++;
   } else {
    // DEBUG:
    grainWireLocalAx[index+k] = 0; grainWireLocalAy[index+k] = 0; grainWireLocalAz[index+k] = 0;
    grainWireLocalBx[index+k] = 0; grainWireLocalBy[index+k] = 0; grainWireLocalBz[index+k] = 0;
   }
  }
 }

 // Aligns with invalid contacts
 size_t gWCC = grainWireContactCount;
 while(gWCC%8)
  grainWireContact[gWCC++] = grainWireCount; // Invalid entry

 // Evaluates forces from (packed) intersections
 buffer<float> Fx(gWCC), Fy(gWCC), Fz(gWCC);
 buffer<float> TAx(gWCC), TAy(gWCC), TAz(gWCC);
 for(size_t i = 0; i < grainWireContactCount; i += 8) {
  v8si contacts = *(v8si*)(grainWireContact.data+i);
  v8si A = gather(grainWireA, contacts), B = gather(grainWireB, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
  v8sf Bx = gather(wire.Px.data, B), By = gather(wire.Py.data, B), Bz = gather(wire.Pz.data, B);
  v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
  v8sf depth = float8(Grain::radius+Wire::radius) - length;
  v8sf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
  /*contact(grain, A, wire, B, depth, -Rx, -Ry, -Rz, Nx, Ny, Nz,
             *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i]);*/
  v8sf R = float8(Grain::radius);
  v8sf RAx = - R  * Nx, RAy = - R * Ny, RAz = - R * Nz;
  // Gather static frictions
  v8sf localAx = gather(grainWireLocalAx, contacts);
  v8sf localAy = gather(grainWireLocalAy, contacts);
  v8sf localAz = gather(grainWireLocalAz, contacts);
  v8sf localBx = gather(grainWireLocalBx, contacts);
  v8sf localBy = gather(grainWireLocalBy, contacts);
  v8sf localBz = gather(grainWireLocalBz, contacts);
  contact<Grain,Wire>(grain, A, wire, B, depth,
                      RAx, RAy, RAz,
                      Nx, Ny, Nz,
                      Ax, Ay, Az,
                      Bx, By, Bz,
                      localAx, localAy, localAz,
                      localBx, localBy, localBz,
                      *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i],
                      *(v8sf*)&TAx[i], *(v8sf*)&TAy[i], *(v8sf*)&TAz[i]
                      );
  // Scatter static frictions
  scatter(grainWireLocalAx, contacts, localAx);
  scatter(grainWireLocalAy, contacts, localAy);
  scatter(grainWireLocalAz, contacts, localAz);
  scatter(grainWireLocalBx, contacts, localBx);
  scatter(grainWireLocalBy, contacts, localBy);
  scatter(grainWireLocalBz, contacts, localBz);
 }

 for(size_t i = 0; i < grainWireContactCount; i++) { // Scalar scatter add
  size_t index = grainWireContact[i];
  size_t a = grainWireA[index];
  size_t b = grainWireB[index];
  grain.Fx[a] += Fx[i];
  wire.Fx[b] -= Fx[i];
  grain.Fy[a] += Fy[i];
  wire.Fy[b] -= Fy[i];
  grain.Fz[a] += Fz[i];
  wire.Fz[b] -= Fz[i];
  grain.Tx[a] += TAx[i];
  grain.Ty[a] += TAy[i];
  grain.Tz[a] += TAz[i];
 }

 return true;
}
