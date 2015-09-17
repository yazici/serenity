#include "system.h"
#include "time.h"

struct element { float key=inf; uint value=0; };
String str(element e) { return "("+str(e.key)+", "+str(e.value)+")"; }
template<size_t N> struct list { // Small sorted list
 uint size = 0;
 element elements[N];
 bool insert(float key, uint value) {
  uint i = 0;
  for(;i<size && key<elements[i].key; i++) if(i == N) return false;
  //if(i<size) assert_(value != elements[i].value);
  if(size < N) size++;
  for(uint j=size-1; j>i; j--) elements[j]=elements[j-1]; // Shifts right (drop last)
  elements[i] = {key, value}; // Inserts new candidate
  return true;
 }
};

generic struct Lattice {
 const vec4f scale;
 const vec4f min, max;
 const int3 size = ::max(int3(5,5,1), int3(::floor(toVec3(scale*(max-min))))+int3(1));
 int YX = size.y * size.x;
 buffer<T> cells {size_t(size.z*size.y*size.x + 3*size.y*size.x+3*size.x+3)}; // -1 .. 2 OOB margins
 T* const base = cells.begin()+size.y*size.x+size.x+1;
 const vec4f XYZ0 {float(1), float(size.x), float(size.y*size.x), 0};
 Lattice(float scale, vec4f min, vec4f max) : scale{scale, scale, scale, 1}, min(min), max(max) {
  cells.clear(0);
 }
 inline size_t index(float x, float y, float z) {
  int index = dot3(XYZ0, cvtdq2ps(cvttps2dq(scale*((v4sf){x,y,z,0}-min))))[0];
  assert(index >= 0 && index < size.z*size.y*size.x, index, min, x,y,z, max);
  return index;
 }
 inline T& cell(float x, float y, float z) { return base[index(x, y, z)]; }
};

enum Pattern { None, Helix, Cross, Loop };
static string patterns[] {"none", "helix", "cross", "loop"};
enum ProcessState { Pour, Pack, Load, Done, Fail };
static string processStates[] {"pour","pack","load","done","failed"};

struct Simulation : System {
 // Process parameters
 sconst bool intersectWireWire = false;
 const float targetHeight;
 const float pourRadius = side.initialRadius - Grain::radius;
 const float winchRadius = side.initialRadius - Wire::radius;
 const Pattern pattern;
 const float loopAngle;
 const float initialWinchSpeed = 0.1;
 float winchSpeed = initialWinchSpeed;
 const float targetWireDensity;
 const float winchRate;
 const float pressure;
 const float plateSpeed;

 // Process variables
 float currentPourHeight = Grain::radius;
 float maxSpawnHeight = 0;
 float lastAngle = 0, winchAngle = 0, currentRadius = winchRadius;
 float topZ0, bottomZ0;
 float lastSnapshotStrain = 0;
 size_t lastCollapseReport = 0;
 Random random;
 ProcessState processState = Pour;
 bool skip = false;
 size_t packStart;
 size_t lastSnapshot = 0;

 float topForce = 0, bottomForce  = 0;
 float lastBalanceAverage = inf;
 float balanceAverage = 0;
 float balanceSum = 0;
 size_t balanceCount = 0;
 String debug;

 // Performance
 int64 lastReport = realTime(), lastReportStep = timeStep;
 Time totalTime {true}, partTime;
 tsc stepTime;
 tsc sideForceTime, grainSideLatticeTime, grainSideTime, grainSideForceTime;
 tsc grainTime, grainGrainLatticeTime, grainGrainTime, grainGrainForceTime;
 tsc integrationTime;

 Lock lock;
 Stream stream;
 Dict parameters;
 String id = str(parameters);
 String name = str(parameters.value("Pattern","none"))+
   (parameters.contains("Pattern")?"-"+str(parameters.value("Rate",0)):""__)+
   (parameters.contains("Pressure")?"-soft"_:"");

 map<rgb3f, array<vec3>> lines;

 template<Type... Args> void fail(string reason, Args... args) {
  log(reason, args...); processState = Fail; snapshot(reason); return;
 }

 v4sf min = _0f4, max = _0f4;

 // Side - Grain
 float grainSideGlobalMinD2 = 0;
 uint sideSkipped = 0;

 size_t grainSideCount = 0;
 // FIXME: uint16 -> unpack
 buffer<int> grainSideA {side.capacity * 2};
 buffer<int> grainSideB {side.capacity * 2};

 // Grain - Grain
 float grainGrainGlobalMinD6 = 0;
 uint grainSkipped = 0;

 size_t grainGrainCount = 0;
 // FIXME: uint16 -> unpack
 buffer<int> grainGrainA {grain.capacity * 6};
 buffer<int> grainGrainB {grain.capacity * 6};
 //  Friction
 buffer<float> grainGrainLocalAx;
 buffer<float> grainGrainLocalAy;
 buffer<float> grainGrainLocalAz;
 buffer<float> grainGrainLocalBx;
 buffer<float> grainGrainLocalBy;
 buffer<float> grainGrainLocalBz;

 Simulation(const Dict& parameters, Stream&& stream) : System(parameters),
   targetHeight(/*parameters.at("Height"_)*/side.height),
   pattern(parameters.contains("Pattern")?
            Pattern(ref<string>(patterns).indexOf(parameters.at("Pattern"))):None),
   loopAngle(parameters.value("Angle"_, PI*(3-sqrt(5.)))),
   //winchSpeed(parameters.value("Speed"_, 0.f)),
   targetWireDensity(parameters.value("Wire"_, 0.f)),
   winchRate(parameters.value("Rate"_, 0.f)),
   pressure(parameters.value("Pressure", 0.f)),
   plateSpeed(/*parameters.at("PlateSpeed")*//*parameters.value("PlateSpeed",*/(1e-4)),
   random((uint)parameters.at("Seed").integer()),
   stream(move(stream)), parameters(copy(parameters)) {
  assert_(pattern == None || targetWireDensity > 0, parameters);
  plate.Pz[1] = targetHeight+Grain::radius;
  if(pattern) { // Initial wire node
   size_t i = wire.count++;
   wire.Px[i] = winchRadius;
   wire.Py[i] = 0;
   wire.Pz[i] = currentPourHeight;
   wire.Vx[i] = 0;
   wire.Vy[i] = 0;
   wire.Vz[i] = 0;
#if GEAR
   for(size_t n: range(3)) wire.positionDerivatives[n][i] = _0f;
#endif
   //wire.frictions.set(i);
   // To avoid bound check
   min = ::min(min, vec3(wire.Px[i], wire.Py[i], wire.Pz[i]));
   max = ::max(max, vec3(wire.Px[i], wire.Py[i], wire.Pz[i]));
  }
  /*for(size_t i : range(side.count)) { // Only needed once membrane is soft
   min = ::min(min, side.position[i]);
   max = ::max(max, side.position[i]);
  }*/
 }

 generic vec4f tension(T& A, size_t a, size_t b) {
  vec4f relativePosition = A.position[a] - A.position[b];
  vec4f length = sqrt(sq3(relativePosition));
  vec4f x = length - A.internodeLength4;
  vec4f fS = - A.tensionStiffness * x;
  assert(length[0], a, b);
  vec4f direction = relativePosition/length;
  vec4f relativeVelocity = A.velocity[a] - A.velocity[b];
  vec4f fB = - A.tensionDamping * dot3(direction, relativeVelocity);
  return (fS + fB) * direction;
 }

 // Formats vectors as flat lists without brackets nor commas
 String flat(const vec3& v) { return str(v[0], v[1], v[2]); }
 String flat(const v4sf& v) { return str(v[0], v[1], v[2]); }
 virtual void snapshot(string step) {
  log("Snapshot disabled"); return;
  static int snapshotCount = 0;
  assert_(snapshotCount < 64 /*8 pack + 12 strain + <10 misc (state change)*/);
  snapshotCount++;
  String name = copyRef(this->id)+"."+step;
  log("Snapshot", name);
  if(existsFile(name+".grain")) remove(name+".grain");
  if(existsFile(name+".wire")) remove(name+".wire");
  if(existsFile(name+".side")) remove(name+".side");
  if(wire.count) {
   float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
   log("Wire density (%):", wireDensity*100);
  }
  //Locker lock(this->lock);
  {array<char> s;
   s.append(str(grain.radius)+'\n');
   s.append(str("X, Y, Z, grain contact count, grain contact energy (µJ), "
                "wire contact count, wire contact energy (µJ)")+'\n');
   buffer<int> wireCount(grain.count); wireCount.clear();
   buffer<float> wireEnergy(grain.count); wireEnergy.clear();
#if 0
   for(size_t i : range(wire.count)) {
    for(auto& f: wire.frictions[i]) {
     if(f.energy && f.index >= grain.base && f.index < grain.base+grain.capacity) {
      wireCount[f.index-grain.base]++;
      wireEnergy[f.index-grain.base] += f.energy;
     }
    }
   }
#endif
   for(size_t i : range(grain.count)) {
    int grainCount=0; float grainEnergy=0;
#if 0
    for(auto& f: grain.frictions[i]) {
     if(f.energy && f.index >= grain.base && f.index < grain.base+grain.capacity) {
      grainCount++;
      grainEnergy += f.energy;
     }
    }
#endif
    s.append(str(grain.Px[i], grain.Py[i], grain.Pz[i], grainCount, grainEnergy*1e6, wireCount[i], wireEnergy[i]*1e6)+'\n');
   }
   writeFile(name+".grain.write", s, currentWorkingDirectory(), true);
   rename(name+".grain.write", name+".grain"); // Atomic
  }

  if(wire.count) {
   array<char> s;
   s.append(str(wire.radius)+'\n');
   s.append(str("P1, P2, grain contact count, grain contact energy (µJ)")+'\n');
   for(size_t i: range(0, wire.count-1)) {
    //float energy = 0;
    //for(auto& f: wire.frictions[i]) energy += f.energy;
    s.append(str(wire.Px[i], wire.Py[i], wire.Pz[i], wire.Px[i+1], wire.Py[i+1], wire.Pz[i+1]
      /*, wire.frictions[i].size, energy*1e6*/)+'\n');
   }
   writeFile(name+".wire.write", s, currentWorkingDirectory(), true);
   rename(name+".wire.write", name+".wire"); // Atomic
  }

  {
   array<char> s;
   size_t W = side.W;
   for(size_t i: range(side.H-1)) for(size_t j: range(W-2)) {
    vec3 a (toVec3(side.position[i*W+j]));
    vec3 b (toVec3(side.position[i*W+(j+1)%W]));
    vec3 c (toVec3(side.position[(i+1)*W+(j+i%2)%W]));
    s.append(str(flat(a), flat(c))+'\n');
    s.append(str(flat(b), flat(c))+'\n');
    if(i) s.append(str(flat(a), flat(b))+'\n');
   }
   writeFile(name+".side.write", s, currentWorkingDirectory(), true);
   rename(name+".side.write", name+".side"); // Atomic
  }
 }

 const float pourPackThreshold = 2e-3;
 const float packLoadThreshold = 500e-6;
 const float transitionTime = 1*s;

 void step() {
  staticFrictionCount2 = staticFrictionCount;
  dynamicFrictionCount2 = dynamicFrictionCount;
  staticFrictionCount = 0; dynamicFrictionCount = 0;

  partTime.start();
  stepTime.start();
  // Process
  float height = plate.position[1][2] - plate.position[0][2];
  float voidRatio = grain.count ? PI*sq(side.radius)*height / (grain.count*Grain::volume) - 1 : 1;
  if(processState == Pour) {
   if(grain.count == grain.capacity ||
      skip || (voidRatio < 0.93 &&
               (!parameters.contains("Pressure") || grainKineticEnergy / grain.count < pourPackThreshold))
      || currentPourHeight >= targetHeight) {
    skip = false;
    processState = Pack;
    log("Pour->Pack", grain.count, wire.count,  grainKineticEnergy / grain.count*1e6, voidRatio);
    packStart = timeStep;
    snapshot("pour");
    // Only needed once membrane is soft
    for(size_t i: range(1, side.H-1)) {
     for(size_t j: range(0, side.W-2)) {
      size_t index = i*side.W+j;
      min = ::min(min, side.position[index]);
      max = ::max(max, side.position[index]);
     }
    }
   } else {
    // Generates falling grain (pour)
    const bool useGrain = 1;
    if(useGrain && currentPourHeight >= Grain::radius) {
     for(uint unused t: range(1/*::max(1.,dt/1e-5)*/)) for(;;) {
      vec4f p {random()*2-1,random()*2-1, currentPourHeight, 0};
      if(length2(p)>1) continue;
      float r = ::min(pourRadius, /*side.minRadius*/side.radius);
      vec3 newPosition (r*p[0], r*p[1], p[2]); // Within cylinder
      // Finds lowest Z (reduce fall/impact energy)
      for(size_t index: range(grain.count)) {
       vec3 p = grain.position[index];
       float x = length(toVec3(p - newPosition).xy());
       if(x < 2*Grain::radius) {
        float dz = sqrt(sq(2*Grain::radius+0x1p-24) - sq(x));
        newPosition[2] = ::max(newPosition[2], p[2]+dz);
       }
      }
      float wireDensity = grain.count ? (wire.count-1)*Wire::volume / (grain.count*Grain::volume) : 1;
      if(newPosition[2] > currentPourHeight
         // ?
         && (
          wireDensity < targetWireDensity
          || newPosition[2] > currentPourHeight+2*Grain::radius
          || newPosition[2] > targetHeight)
         ) break;
      for(size_t index: range(wire.count)) {
       vec3 p = wire.position[index];
       if(length(p - newPosition) < Grain::radius+Wire::radius)
        goto break2_;
      }
      maxSpawnHeight = ::max(maxSpawnHeight, newPosition[2]);
      Locker lock(this->lock);
      assert_(grain.count < grain.capacity);
      size_t i = grain.count++;
      grainGrainGlobalMinD6 = 0; grainSideGlobalMinD2 = 0; // Forces lattice reevaluation
      grain.Px[i] = newPosition.x;
      grain.Py[i] = newPosition.y;
      grain.Pz[i] = newPosition.z;
      min = ::min(min, grain.position[i]);
      max = ::max(max, grain.position[i]);
      grain.Vx[i] = 0; grain.Vy[i] = 0; grain.Vz[i] = 0;
#if GEAR
      for(size_t n: range(3)) grain.positionDerivatives[n][i] = _0f;
#endif
      grain.AVx[i] = 0;
      grain.AVy[i] = 0;
      grain.AVz[i] = 0;
      //for(size_t n: range(3)) grain.angularDerivatives[n][i] = _0f;
      //grain.frictions.set(i);
      float t0 = 2*PI*random();
      float t1 = acos(1-2*random());
      float t2 = (PI*random()+acos(random()))/2;
      grain.rotation[i] = (v4sf){sin(t0)*sin(t1)*sin(t2),
        cos(t0)*sin(t1)*sin(t2),
        cos(t1)*sin(t2),
        cos(t2)};
      // Speeds up pouring
      grain.Vz[i] = - ::min(grain.Pz[i],  0.01f);
      break;
     }
    }
break2_:;
    // Weights winch vertical speed by wire density
    float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
    if(targetWireDensity) { // Adapts winch vertical speed to achieve target wire density
     assert_( wireDensity-targetWireDensity > -1 && wireDensity-targetWireDensity < 1,
              wireDensity, targetWireDensity);
     winchSpeed += dt*/*initialWinchSpeed**/(wireDensity-targetWireDensity);
     if(winchSpeed < 0) {
      static bool unused once = ({ log("Initial winch speed too high", initialWinchSpeed,
                                   "for wire density:", str(targetWireDensity)+
                                   ", current wire density:", wireDensity); true; });
      winchSpeed = 0;
     }
    }
    if(currentPourHeight<targetHeight)
     currentPourHeight += winchSpeed * dt;
   }
  }

  float topZ = 0;
  for(float z: grain.Pz.slice(0, grain.count))
   topZ = ::max(topZ, z+Grain::radius);
  float alpha = processState < Pack ? 0 :
                                      (processState == Pack ? ::min(1.f, (timeStep-packStart)*dt / transitionTime) : 1);
  if(processState == Pack) {
   if(round(timeStep*dt) > round(lastSnapshot*dt)) {
    snapshot(str(uint(round(timeStep*dt)))+"s"_);
    lastSnapshot = timeStep;
   }

   plate.position[1][2] = ::min(plate.position[1][2], topZ); // Fits plate (Prevents initial decompression)
   G[2] = (1-alpha)* -gz/densityScale;
   grain.g = grain.mass * G;
   side.thickness = (1-alpha) * side.pourThickness + alpha*side.loadThickness;
   side.mass = side.massCoefficient * side.thickness;
   side._1_mass = float3(1./side.mass);
   side.tensionStiffness = side.tensionCoefficient * side.thickness;
   side.tensionDamping = side.mass / s;

   float speed = plateSpeed;
   float dz = dt * speed;
   plate.position[1][2] -= dz;
   plate.position[0][2] += dz;

   balanceSum += abs(topForce+bottomForce);
   balanceCount++;
   if(balanceCount > 1./(16*dt)) {
    float average = balanceSum / balanceCount;
    lastBalanceAverage = balanceAverage;
    balanceAverage = average;
    balanceSum = 0;
    balanceCount = 0;
   }

   if(parameters.contains("Pressure"_)) {
    //float plateForce = topForce - bottomForce;
    if((timeStep-packStart)*dt > transitionTime /*&& stress > pressure*/ &&
       (skip ||
        (timeStep-packStart)*dt > 8*s || (
         //plateForce / (PI * sq(side.radius)) > pressure &&
         voidRatio < 0.8 &&
         grainKineticEnergy / grain.count < packLoadThreshold &&
         // Not monotonously decreasing anymore
         (balanceAverage / (PI * sq(side.radius)) < pressure/80 ||
          (lastBalanceAverage < balanceAverage && balanceAverage / (PI * sq(side.radius)) < pressure/80))))) {
     skip = false;
     processState = ProcessState::Load;
     bottomZ0 = plate.position[0][2];
     topZ0 = plate.position[1][2];
     log("Pack->Load", grainKineticEnergy*1e6 / grain.count, "µJ / grain", voidRatio);

     // Strain independent results
     float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
     float height = plate.position[1][2] - plate.position[0][2];
     float grainDensity = grain.count*Grain::volume / (height * PI*sq(side.radius));
     Dict results;
     results.insert("Grain count"__, grain.count);
     results.insert("Weight (N)"__, (grain.count*grain.mass + wire.count*wire.mass) * (-G[2]));
     results.insert("Initial Height (mm)"__, (topZ0-bottomZ0)*1e3);
     results.insert("Grain density (%)"__, grainDensity*100);
     results.insert("Void Ratio (%)"__, voidRatio*100);
     if(wire.count) {
      results.insert("Wire count"__, wire.count);
      results.insert("Wire density (%)"__, wireDensity*100);
     }
     stream.write(str(results)+"\n");
     stream.write("Radius (m), Height (m), Plate Force (N), Radial Force (N)"
                  );//, Kinetic Energy (J), Contacts\n");
     snapshot("pack");
    }
   } else {
    if(grainKineticEnergy / grain.count < 1e-6 /*1µJ/grain*/) {
     log("Pack->Done", grainKineticEnergy*1e6 / grain.count, "µJ / grain");
     processState = ProcessState::Done;
     snapshot("done");
    }
   }
  }
  if(processState == ProcessState::Load) {
   float height = plate.position[1][2]-plate.position[0][2];
   if(height >= 4*Grain::radius && height/(topZ0-bottomZ0) > 1-1./8) {
    float dz = dt * plateSpeed;
    plate.position[1][2] -= dz;
    plate.position[0][2] += dz;
   }
   else {
    log(height >= 4*Grain::radius, height/(topZ0-bottomZ0) > 1-1./8);
    processState = Done;
    snapshot("done");
   }
  }

  // Energies
  normalEnergy=0, staticEnergy=0;

  // Plate
  plate.Fx[0] = 0; plate.Fy[0] = 0; plate.Fz[0] = 0;
  plate.Fx[1] = 0; plate.Fy[1] = 0; plate.Fz[1] = plate.mass * G.z;

  // Grain lattice
  grainTime.start();
  float scale = sqrt(3.)/(2*Grain::radius);
  const int3 size = int3(::floor(toVec3(float4(scale)*(max-min))))+int3(1);
  if(size[0] <= 2) max[0]+=1/scale, min[0]-=1/scale;
  if(size[1] <= 2) max[1]+=1/scale, min[1]-=1/scale;
  if(!(isNumber(min) && isNumber(max)) || size.x*size.y*size.z > 128*128*128) {
   return fail("Domain", min, max, size);
  }

  // Grain
  size_t grainCount8 = (grain.count+7)/8*8;
  buffer<int> grainBottom(grainCount8), grainTop(grainCount8), grainSide(grainCount8);
  size_t grainBottomCount = 0, grainTopCount =0, grainSideCount = 0;
  for(size_t a: range(grain.count)) {
   grain.Fz[a] = grain.g.z;
   grain.Tx[a] = 0;
   grain.Ty[a] = 0;
   grain.Tz[a] = 0;
   if((grain.Pz[a]-Grain::radius) - plate.Pz[0] < 0) grainBottom[grainBottomCount++] = a;
   if(plate.Pz[1] - (grain.Pz[a]+Grain::radius) < 0) grainTop[grainTopCount++] = a;
   if(processState <= ProcessState::Pour)
    if(sq(grain.Px[a]) + sq(grain.Py[a]) > sq(side.radius - Grain::radius))
     grainSide[grainSideCount++] = a;
  }
  {// Bottom
   while(grainBottomCount/8*8 < grainBottomCount) grainBottom[grainBottomCount++] = grain.count;
   assert(grainBottomCount <= grainCount8, grainBottomCount);
   v8sf Bz_R = float8(plate.Pz[0]+Grain::radius);
   buffer<float> Fx(grainBottomCount), Fy(grainBottomCount), Fz(grainBottomCount);
   for(size_t i = 0; i < grainBottomCount; i += 8) {
    v8si A = *(v8si*)&grainBottom[i];
    v8sf depth = gather(grain.Pz, A) - Bz_R;
    contact(grain, A, plate, _0i, depth, _0f, _0f, -Grain::radius8, _0f, _0f, _1f,
            *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i]);
   }
   for(size_t i = 0; i < grainBottomCount; i++) { // Scalar scatter add
    size_t a = grainBottom[i];
    grain.Fx[a] += Fx[i];
    grain.Fy[a] += Fy[i];
    grain.Fz[a] += Fz[i];
    plate.Fx[0] -= Fx[i];
    plate.Fy[0] -= Fy[i];
    plate.Fz[0] -= Fz[i];
   }
  }
  {// Top
   while(grainTopCount/8*8 < grainTopCount) grainTop[grainTopCount++] = grain.count;
   v8sf Bz_R = float8(plate.Pz[1]-Grain::radius);
   buffer<float> Fx(grainTopCount), Fy(grainTopCount), Fz(grainTopCount);
   for(size_t i = 0; i < grainTopCount; i += 8) {
    v8si A = *(v8si*)&grainTop[i];
    v8sf depth = Bz_R - gather(grain.Pz, A);
    contact(grain, A, plate, _1i, depth, _0f, _0f, Grain::radius8, _0f, _0f, -_1f,
            *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i]);
   }
   for(size_t i = 0; i < grainTopCount; i++) { // Scalar scatter add
    size_t a = grainTop[i];
    grain.Fx[a] += Fx[i];
    grain.Fy[a] += Fy[i];
    grain.Fz[a] += Fz[i];
    plate.Fx[1] -= Fx[i];
    plate.Fy[1] -= Fy[i];
    plate.Fz[1] -= Fz[i];
   }
  }
  {// Rigid Side
   while(grainSideCount/8*8 < grainSideCount) grainSide[grainSideCount++] = grain.count;
   v8sf R = float8(side.radius-Grain::radius);
   buffer<float> Fx(grainSideCount), Fy(grainSideCount), Fz(grainSideCount);
   for(size_t i = 0; i < grainSideCount; i += 8) {
    v8si A = *(v8si*)&grainSide[i];
    v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A);
    v8sf length = sqrt(Ax*Ax + Ay*Ay);
    v8sf Nx = -Ax/length, Ny = -Ay/length;
    v8sf depth = R - length;
    contact<Grain, RigidSide>(grain, A, depth, Grain::radius8*(-Nx), Grain::radius8*(-Ny), _0f,
                              Nx, Ny, _0f,
                              *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i]);
   }
   for(size_t i = 0; i < grainSideCount; i++) { // Scalar scatter add
    size_t a = grainSide[i];
    grain.Fx[a] += Fx[i];
    grain.Fy[a] += Fy[i];
    grain.Fz[a] += Fz[i];
   }
  }
  grainTime.stop();
#if 0
  unique<Lattice<uint16>> grainLattice = nullptr;

  // Soft membrane side
  //parallel_chunk(1, side.H-1, [this, alpha, &grainLattice](uint, size_t start, size_t size) {
  //assert_((side.W-2)%8 == 0);
  if(processState > ProcessState::Pour) {
   sideForceTime.start();
   // FIXME: single pass
   side.Fx.clear(); side.Fy.clear(); side.Fz.clear();
   /*::side(side.W, side.Px.data, side.Py.data, side.Pz.data, side.Fx.begin(), side.Fy.begin(), side.Fz.begin(),
          pressure, side.internodeLength, side.tensionStiffness, //side.radius - alpha*Grain::radius,
          0, side.H-1);*/
   int W = side.W;
   const float* Px = side.Px.data, *Py = side.Py.data, *Pz = side.Pz.data;
   float* Fx = side.Fx.begin(), *Fy =side.Fy.begin(), *Fz = side.Fz.begin();
   int dy[6] {-W,            0,       W,         W,     0, -W};
   int dx[2][6] {{-1, -1, -1, 0, 1, 0},{0, -1, 0, 1, 1, 1}};
   int D[2][6];
   for(int i=0; i<2; i++) for(int e=0; e<6; e++) D[i][e] = dy[e]+dx[i][e];
   v8sf P = float8(pressure/(2*3)); // area = length(cross)/2 / 3 vertices
   v8sf internodeLength8 = float8(side.internodeLength);
  #if EXP
   v8sf tensionStiffness_internodeLength8 = float8(tensionStiffness*internodeLength);
  #else
   v8sf tensionStiffness8 = float8(side.tensionStiffness);
  #endif
   //float sqRadius = radius*radius;

   if(0) for(int i=0; i<int(side.H-1); i++) {
    int base = i*W;
    //assert_((W-2)%8 == 0)
    for(int j=1; j<=W-2; j+=8) {
     // Load
     int index = base+j;
     v8sf Ox = *(v8sf*)(Px+index);
     v8sf Oy = *(v8sf*)(Py+index);
     v8sf Oz = *(v8sf*)(Pz+index);

     v8sf X[6], Y[6], Z[6];
     for(int e=0; e<6; e++) { // TODO: assert unrolled
      int a = index+D[i%2][e]; // Gather (TODO: assert reduced i%2)
      X[e] = *(v8sf*)(Px+a) - Ox;
      Y[e] = *(v8sf*)(Py+a) - Oy;
      Z[e] = *(v8sf*)(Pz+a) - Oz;
     }

     // Tension
     for(int e=2; e<=4; e++) { // TODO: assert unrolled
      int a = index+D[i%2][e]; // Gather (TODO: assert reduced i%2)
      v8sf x = X[e], y = Y[e], z = Z[e];
      v8sf sqLength = x*x+y*y+z*z;
      v8sf length = sqrt(sqLength);
      v8sf delta = length - internodeLength8;
  #if EXP
      v8sf T = (tensionStiffness_internodeLength8 * (exp256_ps(delta/internodeLength8)-_11111111f))
        /  length;
  #else
      v8sf T = (tensionStiffness8 * delta) / length;
  #endif
      v8sf fx = T * x;
      v8sf fy = T * y;
      v8sf fz = T * z;
      *(v8sf*)(Fx+index) += fx;
      *(v8sf*)(Fy+index) += fy;
      *(v8sf*)(Fz+index) += fz;
      *(v8sf*)(Fx+a) -= fx;
      *(v8sf*)(Fy+a) -= fy;
      *(v8sf*)(Fz+a) -= fz;
     }

     // Pressure
     v8sf Px = _0f, Py = _0f, Pz = _0f;
     for(int a=0; a<6; a++) { // TODO: assert peeled
      int b = (a+1)%6;
      Px += (Y[a]*Z[b] - Y[b]*Z[a]);
      Py += (Z[a]*X[b] - Z[b]*X[a]);
      Pz += (X[a]*Y[b] - X[b]*Y[a]);
     }
     *(v8sf*)(Fx+index) += P * Px;
     *(v8sf*)(Fy+index) += P * Py;
     *(v8sf*)(Fz+index) += P * Pz;
    }
   }
   sideForceTime.stop();

   if(grainSideGlobalMinD2 <= 0) { // Re-evaluates verlet lists (using a lattice for grains)
    grainSideLatticeTime.start();
    if(!grainLattice) {
     grainLattice = unique<Lattice<uint16>>(sqrt(3.)/(2*Grain::radius), min, max);
     for(size_t i: range(grain.count))
      grainLattice->cell(grain.Px[i], grain.Py[i], grain.Pz[i]) = 1+i; // for grain-grain, and grain-side
    }
    // Side - Grain
    const uint16* grainBase = grainLattice->base;
    int gX = grainLattice->size.x, gY = grainLattice->size.y;
    v4sf scale = grainLattice->scale, min = grainLattice->min;
    const uint16* grainNeighbours[3*3] = {
     grainBase-gX*gY-gX-1, grainBase-gX*gY-1, grainBase-gX*gY+gX-1,
     grainBase-gX-1, grainBase-1, grainBase+gX-1,
     grainBase+gX*gY-gX-1, grainBase+gX*gY-1, grainBase+gX*gY+gX-1
    };
    const v4sf XYZ0 = grainLattice->XYZ0;
    float minD3 = 2*Grain::radius/sqrt(3.) - Grain::radius;
    for(size_t i: range(1, side.H-1)) {
     for(size_t j: range(0, side.W-2)) {
      size_t s = i*side.W+j;
      v4sf O = side.position[s];
      int offset = dot3(XYZ0, floor(scale*(O-min)))[0];
      list<2> D;
      for(size_t n=0; n<3*3; n++) { // FIXME: assert unrolled
       v4hi line = *(v4hi*)(offset + grainNeighbours[n]); // Gather
       for(size_t i: range(3)) if(line[i]) { // FIXME: assert unrolled
        size_t g = line[i]-1;
        float d = sqrt(sq(side.Px[s]-grain.Px[g]) + sq(side.Py[s]-grain.Py[g]) + sq(side.Pz[s]-grain.Pz[g]));
        if(!D.insert(d, g)) minD3 = ::min(minD3, d);
       }
      }
      for(size_t i: range(D.size)) {
       grainSideA[grainSideCount] = D.elements[i].value;
       grainSideB[grainSideCount] = s;
       grainSideCount++;
      }
     }
    }
    grainSideGlobalMinD2 = minD3;
    // Aligns with invalid pairs
    // Needs at least one invalid pair for packed
    grainSideA[grainGrainCount] = grain.count;
    grainSideB[grainGrainCount] = 0;
    grainSideCount++;
    while(grainSideCount/8*8 < grainSideCount) {
     grainSideA[grainGrainCount] = grain.count;
     grainSideB[grainGrainCount] = 0;
     grainSideCount++;
    }
    grainSideLatticeTime.stop();
    //log("side", sideSkipped);capacity
    sideSkipped=0;
   } else sideSkipped++;

   grainSideTime.start();
   // Evaluates (packed) intersections from (packed) verlet lists
   size_t grainSideContactCount = 0;
   int grainSideContact[side.count * 2];
   for(size_t index = 0; index < grainSideCount; index += 8) {
    v8si A = *(v8si*)(grainSideA.data+index), B = *(v8si*)(grainSideB.data+index);
    v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
    v8sf Bx = gather(side.Px, B), By = gather(side.Py, B), Bz = gather(side.Pz, B);
    v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
    v8sf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
    v8sf depth = length - Grain::radius8;
    for(size_t subIndex: range(8)) {
     if(depth[subIndex] < 0) {
      grainSideContact[grainSideContactCount] = index+subIndex;
      grainSideContactCount++;
     }
    }
   }

   // Aligns with invalid contacts
   while(grainSideContactCount/8*8 < grainSideContactCount)
    grainSideContact[grainSideContactCount++] = grainSideCount-1;

   // Evaluates forces from (packed) intersections
   float F[grainSideContactCount*3];
   for(size_t index = 0; index < grainSideContactCount; index += 8) {
    v8si contacts = *(v8si*)(grainSideContact+index);
    v8si A = gather(grainSideA, contacts), B = gather(grainSideB, contacts);
    // FIXME: Recomputing from intersection (more efficient than storing?)
    v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
    v8sf Bx = gather(side.Px, B), By = gather(side.Py, B), Bz = gather(side.Pz, B);
    v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
    v8sf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
    v8sf depth = length - Grain::radius8;
    v8sf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
    contact((v8sf*)(F+index*3), grain, A, side, B, depth, -Rx, -Ry, -Rz, Nx, Ny, Nz);
   }
   for(size_t index = 0; index < grainSideContactCount; index++) { // Scalar scatter add
    size_t i = grainSideContact[index];
    size_t a = grainSideA[i];
    size_t b = grainSideB[i];
    size_t I = i/8*3+i%8;
    grain.Fx[a] += F[I+0*8];
    side.Fx[b] -= F[I+1*8];
    grain.Fy[a] += F[I+2*8];
    side.Fy[b] -= F[I+0*8];
    grain.Fz[a] += F[I+1*8];
    side.Fz[b] -= F[I+2*8];
   }
   grainSideTime.stop();
  }

  if(grainGrainGlobalMinD6 <= 0) { // Re-evaluates verlet lists (using a lattice for grains)
   buffer<int2> grainGrainIndices {grainCount8 * 6};
   grainGrainIndices.clear();
   // Index to packed frictions
   for(size_t index: range(grainGrainCount)) {
    size_t base = grainGrainA[index]*6;
    size_t i = 0;
    while(grainGrainIndices[base+i]) i++;
    assert_(i<6, grainGrainIndices.slice(base, 6));
    grainGrainIndices[base+i] = int2{grainGrainB[index]+1, int(index)};
   }

   grainGrainLatticeTime.start();
   if(!grainLattice) {
    grainLattice = unique<Lattice<uint16>>(sqrt(3.)/(2*Grain::radius), min, max);
    for(size_t i: range(grain.count)) // FIXME: SIMD
     grainLattice->cell(grain.Px[i], grain.Py[i], grain.Pz[i]) = 1+i; // for grain-grain, grain-side, grain-wire
   }
   float minD6 = 2*(2*Grain::radius/sqrt(3.)-Grain::radius);
   const int Z = grainLattice->size.z, Y = grainLattice->size.y, X = grainLattice->size.x;
   int di[62];
   size_t i = 0;
   for(int z: range(0, 2 +1)) for(int y: range((z?-2:0), 2 +1)) for(int x: range(((z||y)?-2:1), 2 +1)) {
    log(X,Y, x,y,z, z*Y*X + y*X + x);
    di[i++] = z*Y*X + y*X + x;
   }
   assert_(i==62);
   grainGrainCount = 0;
   {buffer<float> grainGrainLocalAx {grainCount8 * 6};
    buffer<float> grainGrainLocalAy {grainCount8 * 6};
    buffer<float> grainGrainLocalAz {grainCount8 * 6};
    buffer<float> grainGrainLocalBx {grainCount8 * 6};
    buffer<float> grainGrainLocalBy {grainCount8 * 6};
    buffer<float> grainGrainLocalBz {grainCount8 * 6};
    for(size_t latticeIndex: range(Z*Y*X)) {
     const uint16* current = grainLattice->base + latticeIndex;
     uint16 a = *current;
     if(!a) continue;
     a--;
     // Neighbours
     list<6> D;
     for(size_t n: range(62)) {
      uint16 b = current[di[n]];
      if(!b) continue;
      b--;
      float d = sqrt(sq(grain.Px[a]-grain.Px[b]) + sq(grain.Py[a]-grain.Py[b]) + sq(grain.Pz[a]-grain.Pz[b]));
      if(!D.insert(d, b)) minD6 = ::min(minD6, d);
     }
     log(D.size, ref<element>(D.elements,D.size));
     for(size_t i: range(D.size)) {
      assert(grainGrainCount < grainCount8 * 6);
      size_t index = grainGrainCount;
      grainGrainA[index] = a;
      size_t B = D.elements[i].value;
      grainGrainB[index] = B;
      log(latticeIndex, index, a, B);
      for(size_t i: range(6)) {
       size_t b = grainGrainIndices[index*6+i][0];
       if(!b) break;
       b--;
       if(b == B) { // Repack existing friction
        size_t j = grainGrainIndices[index*6+i][1];
        grainGrainLocalAx[index] = this->grainGrainLocalAx[j];
        grainGrainLocalAy[index] = this->grainGrainLocalAy[j];
        grainGrainLocalAz[index] = this->grainGrainLocalAz[j];
        grainGrainLocalBx[index] = this->grainGrainLocalBx[j];
        grainGrainLocalBy[index] = this->grainGrainLocalBy[j];
        grainGrainLocalBz[index] = this->grainGrainLocalBz[j];
        goto break_;
       }
      } /*else*/ {
       grainGrainLocalAx[index] = 0;
       grainGrainLocalAy[index] = 0;
       grainGrainLocalAz[index] = 0;
       grainGrainLocalBx[index] = 0;
       grainGrainLocalBy[index] = 0;
       grainGrainLocalBz[index] = 0;
      }
      break_:;
      grainGrainCount++;
     }
    }
    this->grainGrainLocalAx = move(grainGrainLocalAx);
    this->grainGrainLocalAy = move(grainGrainLocalAy);
    this->grainGrainLocalAz = move(grainGrainLocalAz);
    this->grainGrainLocalBx = move(grainGrainLocalBx);
    this->grainGrainLocalBy = move(grainGrainLocalBy);
    this->grainGrainLocalBz = move(grainGrainLocalBz);
   }
   assert_(grainGrainCount < grainCount8 * 6);
   {// Aligns with invalid pairs
    size_t grainGrainCount = this->grainGrainCount;
    assert_(grain.count < grain.capacity);
    // Need at least one invalid for packed
    grainGrainA[grainGrainCount] = grain.count;
    grainGrainB[grainGrainCount] = grain.count;
    grainGrainCount++;
    while(grainGrainCount/8*8 < grainGrainCount) {
     grainGrainA[grainGrainCount] = grain.count;
     grainGrainB[grainGrainCount] = grain.count;
     grainGrainCount++;
    }
   }

   grainGrainGlobalMinD6 = minD6;

   grainGrainLatticeTime.stop();
   //log("grain", grainSkipped);
   grainSkipped=0;
  } else grainSkipped++;


  grainGrainTime.start();

  // Evaluates (packed) intersections from (packed) verlet lists
  size_t grainGrainContactCount = 0;
  uint grainGrainContact[grainCount8*6];
  for(size_t index = 0; index < grainGrainCount; index += 8) {
   v8si A = *(v8si*)(grainGrainA.data+index), B = *(v8si*)(grainGrainB.data+index);
   v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
   v8sf Bx = gather(grain.Px, B), By = gather(grain.Py, B), Bz = gather(grain.Pz, B);
   v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
   v8sf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
   v8sf depth = length - (Grain::radius8+Grain::radius8);
   for(size_t subIndex: range(8)) {
    if(depth[subIndex] < 0) {
     grainGrainContact[grainGrainContactCount] = index+subIndex; // Indirect instead of pack for friction
     grainGrainContactCount++;
    }
   }
  }

  // Aligns with invalid contacts
  while(grainGrainContactCount/8*8 < grainGrainContactCount)
   grainGrainContact[grainGrainContactCount++] = grainGrainCount-1;

  // Evaluates forces from (packed) intersections
  float FTT[grainGrainContactCount*9]; // Force, Torque A, Torque B
  for(size_t index = 0; index < grainGrainContactCount; index += 8) {
   v8si contacts = *(v8si*)(grainGrainContact+index);
   v8si A = gather(grainGrainA, contacts), B = gather(grainGrainB, contacts);
   // FIXME: Recomputing from intersection (more efficient than storing?)
   v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
   v8sf Bx = gather(grain.Px, B), By = gather(grain.Py, B), Bz = gather(grain.Pz, B);
   v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
   v8sf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
   v8sf depth = length - (Grain::radius8+Grain::radius8);
   //log(grain.count, grainGrainCount, contacts, A, B, length);
   v8sf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
   v8sf RBx = Grain::radius8 * Nx, RBy = Grain::radius8 * Ny, RBz = Grain::radius8 * Nz;
   v8sf RAx = -RBx, RAy = -RBy, RAz = -RBz;
   // Gather static frictions
   v8sf localAx = gather(grainGrainLocalAx, contacts);
   v8sf localAy = gather(grainGrainLocalAy, contacts);
   v8sf localAz = gather(grainGrainLocalAz, contacts);
   v8sf localBx = gather(grainGrainLocalBx, contacts);
   v8sf localBy = gather(grainGrainLocalBy, contacts);
   v8sf localBz = gather(grainGrainLocalBz, contacts);
   /*for(size_t k: range(8)) if(A[k] < (int)grain.count) {
    assert_(B[k] < (int)grain.count, B[k]);
    assert_(A[k]!=B[k], A[k], B[k], grain.count);
    assert_(isNumber(grain.Fx[A[k]]) && isNumber(grain.Fy[A[k]]) && isNumber(grain.Fz[A[k]]),
      A[k], grain.Fx[A[k]], grain.Fy[A[k]], grain.Fz[A[k]], B[k],
      Ax[k], Ay[k], Az[k],
      Bx[k], By[k], Bz[k]
      );
    assert_(isNumber(grain.Fx[B[k]]) && isNumber(grain.Fy[B[k]]) && isNumber(grain.Fz[B[k]]),
      B[k], grain.Fx[B[k]], grain.Fy[B[k]], grain.Fz[B[k]], A[k]);
   }*/
   contact((v8sf*)(FTT+index*9), grain, A, grain, B, depth, RAx, RAy, RAz, RBx, RBy, RBz, Nx, Ny, Nz,
           localAx, localAy, localAz, localBx, localBy, localBz,
           Ax, Ay, Az, Bx, By, Bz,
           grainGrainLocalAx, contacts);
   /*for(size_t k: range(8)) if(A[k] < (int)grain.count) {
    assert_(B[k] < (int)grain.count, B[k]);
    assert_(A[k]!=B[k], A[k], B[k], grain.count);
    assert_(isNumber(grain.Fx[A[k]]) && isNumber(grain.Fy[A[k]]) && isNumber(grain.Fz[A[k]]),
      A[k], grain.Fx[A[k]], grain.Fy[A[k]], grain.Fz[A[k]], B[k],
      Ax[k], Ay[k], Az[k],
      Bx[k], By[k], Bz[k]
      );
    assert_(isNumber(grain.Fx[B[k]]) && isNumber(grain.Fy[B[k]]) && isNumber(grain.Fz[B[k]]),
      B[k], grain.Fx[B[k]], grain.Fy[B[k]], grain.Fz[B[k]], A[k]);
   }*/
  }
  for(size_t index = 0; index < grainGrainContactCount; index++) { // Scalar scatter add
   size_t i = grainGrainContact[index];
   size_t a = grainGrainA[i];
   size_t b = grainGrainB[i];
   size_t I = i/8*9+i%8;
   grain.Fx[a] += FTT[I+0*8];
   grain.Fx[b] -= FTT[I+1*8];
   grain.Fy[a] += FTT[I+2*8];
   grain.Fy[b] -= FTT[I+0*8];
   grain.Fz[a] += FTT[I+1*8];
   grain.Fz[b] -= FTT[I+2*8];

   grain.Tx[a] += FTT[I+3*8];
   grain.Ty[a] += FTT[I+4*8];
   grain.Tz[a] += FTT[I+5*8];
   grain.Tx[b] += FTT[I+6*8];
   grain.Ty[b] += FTT[I+7*8];
   grain.Tz[b] += FTT[I+8*8];
  }
  grainGrainTime.stop();
#endif
#if 0
  if(wire.count) {
   if(!grainLattice) { // FIXME: use verlet lists for wire-grain
    grainLattice = unique<Lattice<uint16>>(sqrt(3.)/(2*Grain::radius), min, max);
    for(size_t grainIndex: range(grain.count))
     grainLattice->cell(grain.position[grainIndex]) = 1+grainIndex; // for grain-grain, and grain-side
   }
   uint16* grainBase = grainLattice->base;
   const int gX = grainLattice->size.x, gY = grainLattice->size.y;
   const uint16* grainNeighbours[3*3] = {
    grainBase-gX*gY-gX-1, grainBase-gX*gY-1, grainBase-gX*gY+gX-1,
    grainBase-gX-1, grainBase-1, grainBase+gX-1,
    grainBase+gX*gY-gX-1, grainBase+gX*gY-1, grainBase+gX*gY+gX-1
   };

   for(size_t i: range(wire.count)) { // TODO: SIMD
    // Gravity
    wire.Fx[i] = 0; wire.Fy[i] = 0; wire.Fz[i] = wire.mass * G.z;
    // Wire - Plate
    contact(wire, i, plate, 0);
    contact(wire, i, plate, 1);
    // Wire - Grain (FIXME: Verlet list)
    {size_t offset = grainLattice->index(wire.position[i]);
     for(size_t n: range(3*3)) {
      v4hi line = loada(grainNeighbours[n] + offset);
      if(line[0]) contact(wire, i, grain, line[0]-1);
      if(line[1]) contact(wire, i, grain, line[1]-1);
      if(line[2]) contact(wire, i, grain, line[2]-1);
     }}
   }

   // Tension
   for(size_t i: range(1, wire.count)) { // TODO: SIMD
    vec4f fT = tension(wire, i-1, i);
    wire.Fx[i-1] += fT[0];
    wire.Fy[i-1] += fT[1];
    wire.Fz[i-1] += fT[2];
    wire.Fx[i] -= fT[0];
    wire.Fy[i] -= fT[1];
    wire.Fz[i] -= fT[2];
    }
  }
#endif

  // Integration
  integrationTime.start();
  min = _0f4; max = _0f4;

  // Grain
  float maxGrainV = 0;
  for(size_t i: range(grain.count)) {
   assert_(isNumber(grain.Fx[i]) && isNumber(grain.Fy[i]) && isNumber(grain.Fz[i]),
           i, grain.Fx[i], grain.Fy[i], grain.Fz[i]);
   System::step(grain, i);
   assert_(isNumber(grain.position[i]), i, grain.position[i]);
   min = ::min(min, grain.position[i]);
   max = ::max(max, grain.position[i]);
   maxGrainV = ::max(maxGrainV, length(grain.velocity[i]));
  }
  float maxGrainGrainV = maxGrainV + maxGrainV;
  grainGrainGlobalMinD6 -= maxGrainGrainV * dt;

  // Side
  float maxSideV = 0;
  if(processState>=Pack) {
   float maxRadius = 0;
   for(size_t i: range(1, side.H-1)) { // First and last line are fixed
    // Adds force from repeated nodes
    side.Fx[i*side.W+0] += side.Fx[i*side.W+side.W-2+0];
    side.Fy[i*side.W+0] += side.Fy[i*side.W+side.W-2+0];
    side.Fz[i*side.W+0] += side.Fz[i*side.W+side.W-2+0];
    side.Fx[i*side.W+1] += side.Fx[i*side.W+side.W-2+1];
    side.Fy[i*side.W+1] += side.Fy[i*side.W+side.W-2+1];
    side.Fz[i*side.W+1] += side.Fz[i*side.W+side.W-2+1];
    for(size_t j: range(0, side.W-2)) {
     size_t index = i*side.W+j;
     System::step(side, index);
     // To avoid bound check
     min = ::min(min, side.position[index]);
     max = ::max(max, side.position[index]);
     //side.velocity[index] *= float4(1-10*dt); // Additionnal viscosity
     maxSideV = ::max(maxSideV, length(side.velocity[index]));
     maxRadius = ::max(maxRadius, length2(side.position[index]));
    }
    // Copies position back to repeated nodes
    side.position[i*side.W+side.W-2+0] = side.position[i*side.W+0];
    side.position[i*side.W+side.W-2+1] = side.position[i*side.W+1];
   }

   // Checks whether grains are all within the membrane
   for(size_t index: range(grain.count)) {
    vec3 p (grain.Px[index], grain.Py[index], grain.Pz[index]);
    if(length2(p) > maxRadius) {
     log("Grain slipped through membrane", 4*side.initialRadius, length2(p));
     snapshot("slip");
     processState = ProcessState::Fail;
    }
   }
  }

  // Wire
  for(size_t i: range(wire.count)) {
   System::step(wire, i);
   // To avoid bound check
   min = ::min(min, wire.position[i]);
   max = ::max(max, wire.position[i]);
  }

  float maxSideGrainV = maxGrainV + maxSideV;
  grainSideGlobalMinD2 -= maxSideGrainV * dt;
  integrationTime.stop();

  /// All around pressure
  if(processState == ProcessState::Pack) {
   plate.Fz[0] += pressure * PI * sq(side.radius);
   bottomForce = plate.Fz[0];
   System::step(plate, 0);
   plate.Vz[0] = ::max(0.f, plate.Vz[0]); // Only compression

   plate.Fz[1] -= pressure * PI * sq(side.radius);
   topForce = plate.Fz[1];
   System::step(plate, 1);
   plate.Vz[1] = ::min(plate.Vz[1], 0.f); // Only compression
  } else {
   bottomForce = plate.Fz[0];
   topForce = plate.Fz[1];
  }

  {
   v8sf ssqV8 = _0f;
   for(v8sf v: cast<v8sf>(grain.Vx.slice(0, grain.count/8*8))) ssqV8 += v*v;
   for(v8sf v: cast<v8sf>(grain.Vy.slice(0, grain.count/8*8))) ssqV8 += v*v;
   for(v8sf v: cast<v8sf>(grain.Vz.slice(0, grain.count/8*8))) ssqV8 += v*v;
   grainKineticEnergy = 1./2*grain.mass*reduce8(ssqV8);
  }
  {
   v8sf ssqV8 = _0f;
   for(v8sf v: cast<v8sf>(wire.Vx.slice(0, wire.count/8*8))) ssqV8 += v*v;
   for(v8sf v: cast<v8sf>(grain.Vy.slice(0, wire.count/8*8))) ssqV8 += v*v;
   for(v8sf v: cast<v8sf>(grain.Vz.slice(0, wire.count/8*8))) ssqV8 += v*v;
   wireKineticEnergy = 1./2*wire.mass*reduce8(ssqV8);
  }

  if(size_t((1./60)/dt)==0 || timeStep%size_t((1./60)/dt) == 0) {
   if(processState==ProcessState::Load) { // Records results
    /*assert_(grainSideCount);
    float radius = grainSideRadiusSum/grainSideCount + Grain::radius;
    assert_(isNumber(radius));*/
    float radius = side.radius;
    float radialForceSum = pressure * 2 * PI * radius * height;
    float plateForce = plate.Fz[1] - plate.Fz[0];
    stream.write(str(radius, height, plateForce, radialForceSum/*, grainKineticEnergy, grainSideCount*/)+'\n');

    // Snapshots every 1% strain
    float strain = (1-height/(topZ0-bottomZ0))*100;
    if(round(strain) > round(lastSnapshotStrain)) {
     lastSnapshotStrain = strain;
     snapshot(str(round(strain))+"%");
    }
   }
  }

  if(processState==Pour && pattern) { // Generates wire (winch)
   vec2 end;
   if(pattern == Helix) { // Simple helix
    float a = winchAngle;
    float r = winchRadius;
    end = vec2(r*cos(a), r*sin(a));
    winchAngle += winchRate * dt;
   }
   else if(pattern == Cross) { // Cross reinforced helix
    if(currentRadius < -winchRadius) { // Radial -> Tangential (Phase reset)
     currentRadius = winchRadius;
     lastAngle = winchAngle+PI;
     winchAngle = lastAngle;
    }
    if(winchAngle < lastAngle+loopAngle) { // Tangential phase
     float a = winchAngle;
     float r = winchRadius;
     end = vec2(r*cos(a), r*sin(a));
     winchAngle += winchRate * dt;
    } else { // Radial phase
     float a = winchAngle;
     float r = currentRadius;
     end = vec2(r*cos(a), r*sin(a));
     currentRadius -= winchRate * dt * winchRadius; // Constant velocity
    }
   }
   else if (pattern == Loop) { // Loops
    float A = winchAngle, a = winchAngle * (2*PI) / loopAngle;
    float R = winchRadius, r = R * loopAngle / (2*PI);
    end = vec2((R-r)*cos(A)+r*cos(a),(R-r)*sin(A)+r*sin(a));
    winchAngle += winchRate*R/r * dt;
   } else error("Unknown pattern:", int(pattern));
   float z = currentPourHeight+Grain::radius+Wire::radius;
   vec4f relativePosition = (v4sf){end[0], end[1], z, 0} - wire.position[wire.count-1];
  vec4f length = sqrt(sq3(relativePosition));
  if(length[0] >= Wire::internodeLength) {
   //Locker lock(this->lock);
   assert_(wire.count < wire.capacity, wire.capacity);
   size_t i = wire.count++;
   v4sf p = wire.position[wire.count-2]
     + float3(Wire::internodeLength) * relativePosition/length;
   wire.Px[i] = p[0];
   wire.Py[i] = p[1];
   wire.Pz[i] = p[2];
   min[0] = ::min(min[0], wire.Px[i]);
   min[1] = ::min(min[1], wire.Py[i]);
   min[2] = ::min(min[2], wire.Pz[i]);
   max[0] = ::max(max[0], wire.Px[i]);
   max[1] = ::max(max[1], wire.Py[i]);
   max[2] = ::max(max[2], wire.Pz[i]);
   wire.Vx[i] = 0; wire.Vy[i] = 0; wire.Vz[i] = 0;
#if GEAR
   for(size_t n: range(3)) wire.positionDerivatives[n][i] = _0f;
#endif
   //wire.frictions.set(i);
  }
 }
 stepTime.stop();
 partTime.stop();
 timeStep++;
}

String info() {
 array<char> s {copyRef(processStates[processState])};
 s.append(" "_+str(pressure,0u,1u));
 s.append(" "_+str(dt,0u,1u));
 s.append(" "_+str(grain.count)/*+" grains"_*/);
 s.append(" "_+str(int(timeStep*this->dt/**1e3*/))+"s"_);
 if(grain.count) s.append(" "_+decimalPrefix(grainKineticEnergy/*/densityScale*//grain.count, "J"));
 if(processState>=ProcessState::Load) {
  float bottomZ = plate.position[0][2], topZ = plate.position[1][2];
  float displacement = (topZ0-topZ+bottomZ-bottomZ0);
  s.append(" "_+str(displacement/(topZ0-bottomZ0)*100));
 }
 if(grain.count) {
  float height = plate.position[1][2] - plate.position[0][2];
  float voidRatio = PI*sq(side.radius)*height / (grain.count*Grain::volume) - 1;
  s.append(" Ratio:"+str(int(voidRatio*100))+"%");
 }
 if(wire.count) {
  float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
  s.append(" Wire density:"+str(int(wireDensity*100))+"%");
 }
 s.append(" S/D: "+str(staticFrictionCount2, dynamicFrictionCount2));
 if(processState >= ProcessState::Pack) {
  s.append(" "+str(round(lastBalanceAverage), round(balanceAverage)));
  float plateForce = topForce - bottomForce;
  float stress = plateForce/(2*PI*sq(side.initialRadius));
  s.append(" "_+str(decimalPrefix(stress, "Pa"), decimalPrefix(pressure, "Pa")));
 }
 return move(s);
}
};

