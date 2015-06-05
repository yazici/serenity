#include "thread.h"
#include "window.h"
#include "time.h"
#include "matrix.h"
#include "render.h"
#include "algebra.h"
#include "png.h"
#include "time.h"
#include "parallel.h"
#include "gl.h"
#include "layout.h"
#include "plot.h"
#include "encoder.h"
FILE(shader_glsl)
constexpr float pow5(float x) { return x*x*x*x*x; }

struct System {
 // (m - δt²·∂xf - δt·∂vf) δv = δt·(f + δt·∂xf·v) else m δv = δt f
 static constexpr bool implicit = true;
 static constexpr bool rollTest = false;
 static constexpr bool useWire = true && !rollTest;
 static constexpr bool useRotation = true;
 //(2*PI)/5
 /*static constexpr*/ float loopAngle = PI*(3-sqrt(5.)); // 2· Golden angle
 static constexpr float staticFriction = 1./256;
 static constexpr float dynamicFriction = 1./16; //1./512; // 1./100-1./16
 static constexpr int subStepCount = 26; // 27
 static constexpr float dt = 1./60 /subStepCount /(rollTest?1:1); // ~10¯⁴
 #define DBG_FRICTION 1
 // Characteristic dimensions (SI)
 static constexpr float T = 1; // 1 s
 static constexpr float L = 1; // 1 m
 static constexpr float M = 1; // 1 kg

 HList<Plot> plots;

 struct Contact {
  vec3 relativeA, relativeB; // Relative to center but world axices
  vec3 normal;
  float depth;
 };

 /// Returns contact between two objects
 /// Defaults implementation for spheres
 template<Type tA, Type tB> Contact contact(const tA& A, size_t a,
                                                                      const tB& B, size_t b) {
  vec3 relativePosition = A.position[a] - B.position[b];
  float length = ::length(relativePosition);
  vec3 normal = relativePosition/length; // B -> A
  return {tA::radius*(-normal), tB::radius*(+normal),
     normal, length-tA::radius-tB::radius};
 }

 struct Friction {
  size_t index; // Identifies contact to avoid duplicates
  vec3 localA, localB; // Last static friction contact location
  size_t lastUpdate = 0; // Whether to update static friction locations
  vec3 normal = 0;
  float fN = 0;
  vec3 relativeVelocity = 0;
#if DBG_FRICTION
  rgb3f color = 0;
  bool disable = false;
  array<vec3> lines = array<vec3>();
#endif
  bool operator==(const Friction& b) const { return index == b.index; }
 };

 struct Grain {
  // Properties
  static constexpr float radius = 2e-2; // 40 mm diameter
  static constexpr float thickness = radius;
  static constexpr float curvature = 1./radius;
  static constexpr float mass = 3e-3;
  static constexpr float elasticModulus = 1e6 * M / (L*T*T);
  static constexpr float normalDamping = 1./4 * M / T; // 0.3
  static constexpr float frictionCoefficient = 1;
  static constexpr float staticFrictionThresholdSpeed = staticFriction * Grain::radius / T;
  static constexpr float angularMass
   = 2./5*mass*(pow5(radius)-pow5(radius-1e-4))
                       / (cb(radius)-cb(radius-1e-4));

  size_t base = 0;
  static constexpr size_t capacity = rollTest ? 2 : 256;
  static constexpr bool fixed = false;
  buffer<vec3> position { capacity };
  buffer<vec3> velocity { capacity };
  buffer<quat> rotation { capacity };
  buffer<vec3> angularVelocity { capacity };
  buffer<vec3> torque { capacity };
  buffer<array<Friction>> frictions { capacity };
  buffer<rgba4f> color { capacity };
  Grain() { frictions.clear(); color.clear(1); }
  size_t count = 0;
 } grain;

 struct Wire {
  static constexpr float radius = 1e-3; // 2mm diameter
  static constexpr float thickness = radius;
  static constexpr float curvature = 1./radius;
  static constexpr float internodeLength = 1./2 * Grain::radius;
  static constexpr float volume = PI * sq(radius) * internodeLength;
  static constexpr float density = 500 * M / cb(L);
  static constexpr float mass = density * volume;
  static constexpr float elasticModulus = 1e6 * M / (L*T*T); // 1e6
  static constexpr float normalDamping = 1 * M / T; // 0.3
  static constexpr float frictionCoefficient = 1;
  static constexpr float staticFrictionThresholdSpeed = staticFriction * Grain::radius / T;

  static constexpr float tensionStiffness = 3 * elasticModulus * PI * sq(radius);
  static constexpr float tensionDamping = 0 * mass/T;

  size_t base = 0;
  static constexpr size_t capacity = useWire ? 2048 : 0;
  static constexpr bool fixed = false;
  buffer<vec3> position { capacity };
  buffer<vec3> velocity { capacity };
  buffer<quat> rotation { capacity }; // FIXME
  buffer<vec3> angularVelocity { capacity }; // FIXME
  buffer<vec3> torque { capacity }; // FIXME
  buffer<array<Friction>> frictions { capacity };
  buffer<bgr3f> color { capacity }; // FIXME
  Wire() { frictions.clear(); color.clear(1); }
  size_t count = 0;
 } wire;

 struct Floor {
  static constexpr float mass = inf;
  static constexpr float radius = inf;
  static constexpr float height = 0; //8*L/256; //Wire::radius;
  static constexpr float thickness = L;
  static constexpr float curvature = 0;
  static constexpr float elasticModulus = 1e6 * M/(L*T*T);
  static constexpr float normalDamping = 1 * M / T;
  static constexpr float frictionCoefficient = 1;
  static constexpr float staticFrictionThresholdSpeed = staticFriction * Grain::radius / T;
  size_t base = 1; static constexpr size_t count = 1;
  static constexpr bool fixed = true;
  static constexpr vec3 position[1] {vec3(0,0,0)};
  static constexpr vec3 velocity[1] {vec3(0,0,0)};
  static constexpr vec3 angularVelocity[1] {vec3(0,0,0)};
  static constexpr quat rotation[1] {quat(1,vec3(0,0,0))};
  vec3 torque[0] {};
  vec3 surfaceVelocity(size_t, vec3) const { return 0; }
 } floor;
 /// Sphere - Floor
 template<Type tA> Contact contact(const tA& A, size_t a,
                                                           const Floor&, size_t) {
  vec3 normal (0, 0, 1);
  return {tA::radius*(-normal), vec3(A.position[a].xy(), Floor::height),
              normal, A.position[a].z-tA::radius-Floor::height};
 }

 struct Side {
  static constexpr float mass = inf;
  static constexpr float radius = inf;
  static constexpr float thickness = L;
  static constexpr float curvature = 0; // -1/radius?
  static constexpr float elasticModulus = 1e6 * M/(L*T*T);
  static constexpr float normalDamping = 1 * M / T;
  static constexpr float frictionCoefficient = 1;
  static constexpr float staticFrictionThresholdSpeed = staticFriction * Grain::radius / T;
  static constexpr bool fixed = true;
  static constexpr vec3 position[1] {vec3(0,0,0)};
  static constexpr vec3 velocity[1] {vec3(0,0,0)};
  static constexpr vec3 angularVelocity[1] {vec3(0,0,0)};
  static constexpr quat rotation[1] {quat{1,vec3(0,0,0)}};
  vec3 torque[0] {};
  static constexpr float initialRadius = Grain::radius*8;
  float currentRadius = initialRadius;
  size_t base = 2; static constexpr size_t count = 1;
  vec3 surfaceVelocity(size_t, vec3) const { return 0; }
 } side;
 /// Sphere - Side
 template<Type tA> Contact contact(const tA& A, size_t a,
                                                           const Side& side, size_t) {
  vec2 r = A.position[a].xy();
  float length = ::length(r);
  vec3 normal = vec3(-r/length, 0); // Side -> Sphere
  return {tA::radius*(-normal),
     vec3((side.currentRadius)*-normal.xy(), A.position[a].z), normal,
     side.currentRadius-tA::radius-length};
 }

 // Update
 Matrix matrix;
 buffer<vec3d> F { Grain::capacity+Wire::capacity };
 System() { F.clear(); }

 size_t timeStep = 0;
 virtual void error(string message) abstract;
 template<Type... Args> void error(Args... args) { return error(str(args...)); }

 template<Type tA> vec3 toGlobal(tA& A, size_t a, vec3 localA) {
  return A.position[a] + A.rotation[a] * localA;
 }

 /// Evaluates contact penalty between two objects
 template<Type tA, Type tB> void penalty(const tA& A, size_t a,
                                                                 const tB& B, size_t b) {
  Contact c = contact(A, a, B, b);
  if(c.depth >= 0) return;
  // Stiffness
  constexpr float E = 1/(1/tA::elasticModulus+1/tB::elasticModulus);
  constexpr float R = 1/(tA::curvature+tB::curvature);
  const float Ks = E*sqrt(R)*sqrt(-c.depth);
  // Damping
  constexpr float Kb = 1/(1/tA::normalDamping+1/tB::normalDamping);
  vec3 relativeVelocity =
    A.velocity[a] + cross(A.angularVelocity[a], c.relativeA) -
    B.velocity[b] + cross(B.angularVelocity[b], c.relativeB);
  float fN = spring<tA::fixed, tB::fixed>(A.base+a, B.base+b, Ks, c.depth,
                                          0, Kb, c.normal, relativeVelocity);
  vec3 localA = A.rotation[a].conjugate()*c.relativeA;
  vec3 localB = B.rotation[b].conjugate()*c.relativeB;
  //assert_(length(toGlobal(A, a, localA)-toGlobal(B, b, localB))<Grain::radius);
  Friction& friction = A.frictions[a].add(Friction{B.base+b, localA, localB});
  if(friction.lastUpdate < timeStep-1) { // Resets contact
   friction.localA = localA;
   friction.localB = localB;
  }
  friction.lastUpdate = timeStep;
  friction.normal = c.normal;
  friction.fN = fN;
  friction.relativeVelocity = relativeVelocity;
 }
 template<bool fixedA=false, bool fixedB=false>
 float spring(size_t a, size_t b, float Ks, float length, float restLength,
                    float Kb, vec3 normal, vec3 relativeVelocity) {
  float fS = - Ks * (length - restLength);
  float normalSpeed = dot(normal, relativeVelocity);
  vec3 normalVelocity = normalSpeed*normal;
  float fB = - Kb * normalSpeed ; // Damping
  float fSB = fS + fB;
  if(implicit) {
   for(int i: range(3)) {
    float f = fSB * normal[i];
    for(int j: range(i)) {
     float Nij = normal[i]*normal[j];
     float dvf = - Kb * Nij;
     float dxf = - Ks * (((i==j?1:0) - restLength/length)*(1 - Nij) + Nij);
     if(!fixedA) matrix(a*3+i, a*3+j) += - dt*dt*dxf - dt*dvf;
     if(!fixedB) matrix(b*3+i, b*3+j) -= - dt*dt*dxf - dt*dvf;
    }
    float Nii = normal[i]*normal[i];
    float dvf = - Kb * Nii;
    float dxf = - Ks * ((1 - restLength/length)*(1 - Nii) + Nii);
    if(!fixedA) matrix(a*3+i, a*3+i) += - dt*dt*dxf - dt*dvf;
    if(!fixedB) matrix(b*3+i, b*3+i) -= - dt*dt*dxf - dt*dvf;
    if(!fixedA) F[a][i] += dt*f + dt*dt*dxf * normalVelocity[i]; //relativeVelocity
    if(!fixedB) F[b][i] -= dt*f + dt*dt*dxf * normalVelocity[i]; //relativeVelocity
   }
   //assert_(::length(F[a]) < Grain::mass/2 && ::length(F[b]) < Grain::mass/2);
  } else {
   vec3 f = fSB * normal;
   if(!fixedA) F[a] += vec3d(dt*f);
   if(!fixedB) F[b] -= vec3d(dt*f);
  }
  return fS + max(0.f, fB); // ?
 }
 template<Type tA, Type tB> void friction(Friction& f, tA& A, size_t a,
                                                                                 tB& B, size_t b) {
#if DBG_FRICTION
  f.color = 0;
  //return;
#endif
  vec3 tangentRelativeVelocity
    = f.relativeVelocity - dot(f.normal, f.relativeVelocity) * f.normal;
  float tangentRelativeSpeed = ::length(tangentRelativeVelocity);
  constexpr float staticFrictionThresholdSpeed
  = ::max(tA::staticFrictionThresholdSpeed, tB::staticFrictionThresholdSpeed);
  constexpr float frictionCoefficient
   = 2/(1/tA::frictionCoefficient+1/tB::frictionCoefficient);
  vec3 relativeA = A.rotation[a] * f.localA;
  vec3 relativeB = B.rotation[b] * f.localB;
  vec3 globalA = A.position[a] + relativeA;
  vec3 globalB = B.position[b] +  relativeB;
  bool staticFriction = tangentRelativeSpeed < staticFrictionThresholdSpeed;
  vec3 fT = 0;
  if(staticFriction) {
   vec3 x = globalA - globalB;
   vec3 tangentOffset = x - dot(f.normal, x) * f.normal;
   float tangentLength = ::length(tangentOffset);
   if(tangentLength) {
    constexpr float E = 1/(1/tA::elasticModulus+1/tB::elasticModulus);
    constexpr float R = 1/(tA::curvature+tB::curvature);
    constexpr float staticFrictionStiffness = frictionCoefficient * E * R;
    float fS = staticFrictionStiffness * f.fN * tangentLength;
    vec3 springDirection = tangentOffset / tangentLength;
    constexpr float Kb = 0; //0x1p-16;
    float fB = - Kb * dot(springDirection, f.relativeVelocity); // Damping
    fT = - (fS+fB) * springDirection;
#if DBG_FRICTION
    f.color = rgb3f(0,1,0);
#endif
   } // else static equilibrium
  }
  // Dynamic friction
  if(/*tangentRelativeSpeed &&*/ (!staticFriction || length(fT) > frictionCoefficient*f.fN)) {
   f.lastUpdate = 0;
   constexpr float dynamicFrictionCoefficient = dynamicFriction; //frictionCoefficient;
   float fS = dynamicFrictionCoefficient * f.fN;
   vec3 tangentVelocityDirection = tangentRelativeVelocity / tangentRelativeSpeed;
   constexpr float Kb = 0/*0x1p-30*/ * M / T/T;
   float fB = - Kb * dot(tangentVelocityDirection, f.relativeVelocity); // Damping
   fT = - (fS+fB) * tangentVelocityDirection;
#if DBG_FRICTION
    f.color = rgb3f(1,0,0);
#endif
  }
  //if(rollTest) log(grain.velocity[1], fT/Grain::mass);
  if(!tA::fixed) F[A.base+a] += vec3d(dt*fT);
  if(!tB::fixed) F[B.base+b] -= vec3d(dt*fT);
  if(!tA::fixed) A.torque[a] += cross(relativeA, fT);
  if(!tB::fixed) B.torque[b] -= cross(relativeB, fT);
 }
};
constexpr vec3 System::Floor::position[1];
constexpr quat System::Floor::rotation[1];
constexpr vec3 System::Floor::velocity[1];
constexpr vec3 System::Floor::angularVelocity[1];
constexpr vec3 System::Side::position[1];
constexpr quat System::Side::rotation[1];
constexpr vec3 System::Side::velocity[1];
constexpr vec3 System::Side::angularVelocity[1];

// Grid

struct Grid {
    float scale;
    int3 size;
    static constexpr size_t cellCapacity = 8; // Wire -> 64
    buffer<uint16> indices;
    Grid(float scale, vec3 size) : scale(scale), size(scale*size),
        indices(this->size.z*this->size.y*this->size.x*cellCapacity)
     { indices.clear(); }
    struct List : mref<uint16> {
        List(mref<uint16> o) : mref(o) {}
        size_t size() { size_t i = 0; while(at(i)!=0 && i<cellCapacity) i++; return i; }
        void remove(uint16 index) {
            size_t i = 0;
            while(at(i)!=index) i++;
            assert(i<cellCapacity);
            while(i+1<cellCapacity) { at(i) = at(i+1); i++; }
            at(i) = 0;
        }
        void append(uint16 index) { // Sorted by decreasing index
            size_t i = 0;
            while(at(i) > index) i++;
            assert(i<cellCapacity);
            while(index) { swap(index, at(i)); i++; }
            assert(i<=cellCapacity);
            if(i<cellCapacity) at(i) = 0;
        }
    };
    inline List operator[](size_t i) {
     return indices.slice(i*cellCapacity, cellCapacity);
    }
    size_t index(int x, int y, int z) {
        return (z*size[1]+y)*size[0]+x;
    }
    int3 index3(vec3 p) { // [-size/2·R..size/2·R, 0..size·R] -> [0..size-1]
        return int3(vec3(size.x/2,size.y/2,0)+scale*p);
    }
    size_t index(vec3 p) {
        int3 i = index3(p);
        assert(i.x == clamp(0, i.x, size.x-1), p, i, size, scale);
        assert(i.y == clamp(0, i.y, size.y-1), p, i, size, scale);
        assert(i.z == clamp(0, i.z, size.z-1), p, i, size, scale);
        return index(i.x, i.y, i.z);
    }
};

struct Simulation : System {
 // Space partition
 Grid grainGrid {1./(2*Grain::radius), 32*Grain::radius};
 // wireGrid {2./Grain::radius, 16*Grain::radius};

 template<Type T> void update(ref<vec3d> dv, Grid& grid, T& t, size_t i) {
  t.velocity[i] += vec3(dv[t.base+i]);
  size_t oldCell = grid.index(t.position[i]);
  /*if(length(t.velocity[i]) > Grain::radius/dt) error(i, t.velocity[i], dv[t.base+i],
    matrix(t.base+i,t.base+i)/Grain::mass, length(F[t.base+i])/double(Grain::mass));*/
  t.position[i] += dt*t.velocity[i];
  vec3 size (vec3(grainGrid.size)/grainGrid.scale);
  if(!(t.position[i].x >= -size.x/2.f && t.position[i].x < size.x/2) ||
     !(t.position[i].y >= -size.x/2.f && t.position[i].y < size.y/2) ||
     !(t.position[i].z >= 0 && t.position[i].z < size.z)) {
   error(i, "p", t.position[i], "v", t.velocity[i], "dv", dv[t.base+i], "F", F[t.base+i]);
   t.position[i].x = clamp(-1.f, t.position[i].x, 1.f-0x1p-12f);
   t.position[i].y = clamp(-1.f, t.position[i].y, 1.f-0x1p-12f);
   t.position[i].z = clamp(0.f, t.position[i].z, 2.f-0x1p-12f);
   return;
  }
  size_t newCell = grid.index(t.position[i]);
  if(oldCell != newCell) {
   grid[oldCell].remove(1+i);
   grid[newCell].append(1+i);
  }
 }

 // Process
 static constexpr float pourRadius = Side::initialRadius - Grain::radius;
 float pourHeight = Floor::height+Grain::radius;

 //static constexpr float loopRadius = 6 * Grain::radius;
 static constexpr float winchRadius = Side::initialRadius /*- loopRadius*/ - Wire::radius;
 static constexpr float winchRate = 16 / T;
 static constexpr float winchSpeed = 2 * Grain::radius / T;
 float winchRestLength = 0;
 float winchAngle = 0;

 /*static constexpr float loopRate = (5+1) * winchRate;
 float loopAngle = 0;*/

 map<rgb3f, array<vec3>> lines;
 struct State {
  buffer<vec3> grainPositions;
  buffer<vec3> wirePositions;
 };
 array<State> states;
 int stateIndex = 0;

 vec3 winchPosition(float winchAngle = -1) {
  if(winchAngle<0) winchAngle = this->winchAngle;
#if 0
  float t = mod(winchAngle / loopAngle, 1);
  const float dt = 1./16;
  /**/  if(t < dt) t = t/dt;
  else if(t < 1./2-dt) t = 1;
  else if(t < 1./2+dt) t = 1-(t-(1./2-dt))/dt;
  else if(t < 1-dt) t = -1;
  else t = t = -1+(t-(1-dt))/dt;
  //t = abs(2*t-1); // Symmetry
  //t = t*t*(-2*t+3);
  //float r = abs(t*2-1)*2-1;
  //float r = sin(2*PI*t);
  float r = t;
#elif 0
  float t = mod(winchAngle / loopAngle, 4);
  float r;
  /**/  if(/*0 <*/ t < 1) r = 1;
  else if(/*1 <*/ t < 2) r = 1-2*(t-1);
  else if(/*2 <*/ t < 3) r = -1;
  else    /*3 <    t < 4*/r = -1+2*(t-3);
#else
  float r = 1;
#endif
  return vec3(winchRadius*r*cos(winchAngle),
                      winchRadius*r*sin(winchAngle),
                      pourHeight+Grain::radius);
 }

 Random random;
 bool pour = true;

 // Performance
 int64 lastReport = realTime(), lastReportStep = timeStep;
 Time totalTime {true}, stepTime;
 Time miscTime, grainTime, wireTime, solveTime;
 Time grainContactTime, grainFrictionTime, grainIntegrationTime;
 Time wireContactTime, wireFrictionTime, wireIntegrationTime;
 Time wireBoundTime, wireTensionTime;

 // DBG_FRICTION
 Lock lock;
 array<uint16> fixed;

 bool winchObstacle = useWire && false;
 Simulation() {
  if(useWire) {
   {size_t i = addWire();
   vec3 pos = winchPosition();
   wire.position[i] = pos;
   wire.rotation[i] = quat();
   wire.velocity[i] = 0;
   wire.angularVelocity[i] = 0;
   wire.frictions.set(i);
   //wireGrid[wireGrid.index(wire.position[i])].append(1+i);
   }
   /*{size_t i = addWire();
   vec3 pos = winchPosition(winchRate*dt);
   wire.position[i] = pos;
   wire.rotation[i] = quat();
   wire.velocity[i] = 0;
   wire.angularVelocity[i] = 0;
   wire.frictions.set(i);
   //wireGrid[wireGrid.index(wire.position[i])].append(1+i);
   }*/

   if(winchObstacle) { // Winch obstacle
    size_t i = addGrain();
    grain.position[i] = vec3(winchPosition().xy(),
                             winchPosition().z+Grain::radius-Wire::radius);
    grain.velocity[i] = 0;
    grain.frictions.set(i);
    grainGrid[grainGrid.index(grain.position[i])].append(1+i);
    grain.rotation[i] = quat();
    grain.angularVelocity[i] = 0;
   }
  }
  if(rollTest) for(;;) {
   vec3 p(random()*2-1,random()*2-1, pourHeight);
   //vec3 p(0,0, grain.count ? 2-grain.radius : grain.radius);
   if(length(p.xy())>1) continue;
   vec3 newPosition (pourRadius*p.xy(), p.z); // Within cylinder
   for(vec3 p: wire.position.slice(0, wire.count))
    if(length(p - newPosition) < Grain::radius+Wire::radius) goto continue2_;
   for(vec3 p: grain.position.slice(0, grain.count))
    if(length(p - newPosition) < Grain::radius+Grain::radius) goto continue2_;
   {
    Locker lock(this->lock);
    size_t i = addGrain();
    grain.position[i] = newPosition;
    grain.velocity[i] = 0;
    grain.frictions.set(i);
    grainGrid[grainGrid.index(grain.position[i])].append(1+i);
    float t0 = 2*PI*random();
    float t1 = acos(1-2*random());
    float t2 = (PI*random()+acos(random()))/2;
    grain.rotation[i] = {cos(t2), vec3( sin(t0)*sin(t1)*sin(t2),
                         cos(t0)*sin(t1)*sin(t2),
                         cos(t1)*sin(t2))};
    grain.angularVelocity[i] = 0;
    if(rollTest) grain.rotation[i] = quat{1, 0};
    if(grain.capacity == 1 && i == 0) { // Roll test
     grain.velocity[0]  =  vec3((- grain.position[0] / T).xy(), 0);
     //log(grain.velocity[0]);
     //grain.angularVelocity[i] = vec3(0,0,1);
     break;
    }
    else if(grain.capacity == 2 && i == 1) { // Collision test
     grain.velocity[1] = (grain.position[0] - grain.position[1]) / T;
     grain.velocity[1] /= ::length(grain.velocity[1]);
     //log(grain.velocity[1]);
     break;
    }
   }
   continue2_:;
  }
 }

 size_t addGrain() {
  size_t index = grain.count;
  grain.count++;
  wire.base++;
  floor.base++;
  side.base++;
  for(auto& frictions: grain.frictions)
   for(auto& f: frictions) if(f.index > grain.base+index) f.index++;
  for(auto& frictions: wire.frictions)
   for(auto& f: frictions) if(f.index > grain.base+index) f.index++;
  return index;
 }
 size_t addWire() {
  size_t index = wire.count;
  wire.count++;
  floor.base++;
  side.base++;
  for(auto& frictions: grain.frictions)
   for(auto& f: frictions) if(f.index > wire.base+index) f.index++;
  for(auto& frictions: wire.frictions)
   for(auto& f: frictions) if(f.index > wire.base+index) f.index++;
  return index;
 }

 // Single implicit Euler time step
 void step() {
  stepTime.start();
  // Process
  if(pour && (pourHeight>=2 || grain.count == grain.capacity
              || (wire.capacity && wire.count == wire.capacity))) {
   log("Release", "Grain", grain.count, "Wire", wire.count);
   pour = false;
  }
  if(!pour) side.currentRadius = 16*Grain::radius;
  //Locker lock(this->lock);
  if(pour) {
   // Generates falling grain (pour)
   const bool pourGrain = true;
   if(pourGrain) for(;;) {
    vec3 p(random()*2-1,random()*2-1, pourHeight);
    //vec3 p(0,0, grain.count ? 2-grain.radius : grain.radius);
    if(length(p.xy())>1) continue;
    vec3 newPosition (pourRadius*p.xy(), p.z); // Within cylinder
    for(vec3 p: wire.position.slice(0, wire.count))
     if(length(p - newPosition) < Grain::radius+Wire::radius) goto break2_;
    for(vec3 p: grain.position.slice(0, grain.count))
     if(length(p - newPosition) < Grain::radius+Grain::radius) goto break2_;
    Locker lock(this->lock);
    size_t i = addGrain();
    grain.position[i] = newPosition;
    grain.velocity[i] = 0;
    grain.frictions.set(i);
    grainGrid[grainGrid.index(grain.position[i])].append(1+i);
    float t0 = 2*PI*random();
    float t1 = acos(1-2*random());
    float t2 = (PI*random()+acos(random()))/2;
    grain.rotation[i] = {cos(t2), vec3( sin(t0)*sin(t1)*sin(t2),
                                                         cos(t0)*sin(t1)*sin(t2),
                                                                     cos(t1)*sin(t2))};
    grain.angularVelocity[i] = 0;
    if(rollTest) grain.rotation[i] = quat{1, 0};
    if(grain.capacity == 1 && i == 0) { // Roll test
     grain.velocity[0]  =  vec3((- grain.position[0] / T).xy(), 0);
     //log(grain.velocity[0]);
     //grain.angularVelocity[i] = vec3(0,0,1);
    }
    else if(grain.capacity == 2 && i == 1) { // Collision test
     grain.velocity[1] = (grain.position[0] - grain.position[1]) / T;
     grain.velocity[1] /= ::length(grain.velocity[1]);
     //log(grain.velocity[1]);
    }
    break;
   }
break2_:;
   pourHeight += winchSpeed * dt;
   if(useWire) { // Generates wire (winch)
    winchAngle += winchRate * dt;
    //winchRestLength += winchRadius * winchRate * dt;
    //loopAngle += loopRate * dt;

    if(winchObstacle) { // Moves winch obstacle keeping spawn clear
     size_t i = 0;
     size_t oldCell = grainGrid.index(grain.position[i]);
     grain.position[i] = vec3(winchPosition().xy(),
                              winchPosition().z+Grain::radius-Wire::radius);
     grain.velocity[i] = 0;
     grain.angularVelocity[0] = 0;
     size_t newCell = grainGrid.index(grain.position[i]);
     if(oldCell != newCell) {
      grainGrid[oldCell].remove(1+i);
      grainGrid[newCell].append(1+i);
     }
    }
    //wire.position[0] = 0;
    //vec3 r = wire.position[wire.count-1] - wire.position[wire.count-2];
    //float l = length(r);
    if(length(wire.position[wire.count-1]-winchPosition()) >= Wire::internodeLength) { // Stretch
     winchRestLength = 0;
     Locker lock(this->lock);
     size_t i = addWire();
     vec3 pos = winchPosition(); //lastPosition + Wire::internodeLength/l*r;
     wire.position[i] = pos;
     wire.rotation[i] = quat();
     wire.velocity[i] = 0;
     wire.angularVelocity[i] = 0;
     wire.frictions.set(i);
     //wireGrid[wireGrid.index(wire.position[i])].append(1+i);
    }
   }
  } else if(winchObstacle) {
   size_t i = 0;
   grain.position[i] = vec3(winchPosition().xy(),
                                         winchPosition().z+Grain::radius-Wire::radius);
   grain.velocity[i] = 0;
   grain.angularVelocity[i] = 0;
  }

  // Initialization
  F.size = grain.count+wire.count;
  miscTime.start();
  matrix.reset(F.size*3);
  miscTime.stop();
  constexpr vec3 g {0, 0, -1};

  // Grain
  {grainTime.start();
    //Locker lock(this->lock);
   // Initialization
#if 0
   // TODO: keep non-trailing slots to avoid moves
   //Locker lock(this->lock);
   for(size_t i: range(grain.count)) {
    // Kept from previous step for visualization
    size_t a = i;
    for(size_t i=0; i<grain.frictions[a].size;) {
     Friction& f = grain.frictions[a][i];
     if(f.disable) { grain.frictions[a].removeAt(i); continue; }
     else i++;
    }
   }
#endif
   for(size_t i: range(grain.count)) {
    grain.torque[i] = 0;
    if(implicit) for(int c: range(3))
     matrix((grain.base+i)*3+c, (grain.base+i)*3+c) = Grain::mass;
    F[grain.base+i] = vec3d(dt * Grain::mass * g);
    Locker lock(this->lock);
    penalty(grain, i, floor, 0);
    penalty(grain, i, side, 0);
   }
   // Grain - Grain repulsion
   grainContactTime.start();
   parallel_for(grain.count, [this](uint, int a) {
    if(1) {
     int3 index = grainGrid.index3(grain.position[a]);
     for(int z: range(max(0, index.z-1), min(index.z+2, grainGrid.size.z)))
      for(int y: range(max(0, index.y-1), min(index.y+2, grainGrid.size.y)))
       for(int x: range(max(0, index.x-1), min(index.x+2, grainGrid.size.x)))
       {
        Grid::List list = grainGrid[grainGrid.index(x, y, z)];
        for(size_t i=0; i<grainGrid.cellCapacity; i++) {
         int b = list[i]-1;
         if(b <= a) break; // Single penalty per pair, until 0
         Locker lock(this->lock);
         penalty(grain, a, grain, b);
        }
       }
    } else {
     for(int b: range(grain.count)) if(a<b) {
      Locker lock(this->lock);
      penalty(grain, a, grain, b);
     }
    }
   });
   grainContactTime.stop();

   // Grain - Grain friction
   grainFrictionTime.start();
   if(1) {
    Locker lock(this->lock);
   parallel_for(grain.count, [this](uint, size_t a) {
    for(size_t i=0; i<grain.frictions[a].size;) {
     Friction& f = grain.frictions[a][i];
     if(f.lastUpdate >= timeStep-1) {
      if(f.lastUpdate == timeStep)  {
       size_t b = f.index;
       /**/  if(b < grain.base+grain.count) friction(f, grain, a, grain, b-grain.base);
       else if(b < wire.base+wire.count) friction(f, grain, a, wire, b-wire.base);
       else if(b==floor.base) friction(f, grain, a, floor, 0);
       else if(b==side.base) friction(f, grain, a, side, 0);
       else ::error(b);
      } // else no more contact or dynamic friction
      i++;
      continue;
     } else
#if DBG_FRICTION
      i++;
#else
      { grain.frictions[a].removeAt(i); continue; } // Garbage collection
#endif
    }
   });}
   grainFrictionTime.stop();
   grainTime.stop();}

  // Wire
  // Initialization
  {wireTime.start();
    //Locker lock(this->lock);
   wireBoundTime.start();
   for(size_t i: range(wire.count)) {
    if(implicit) for(int c: range(3))
     matrix((wire.base+i)*3+c, (wire.base+i)*3+c) = Wire::mass;
    F[wire.base+i] = vec3d(dt * Wire::mass * g);
    Locker lock(this->lock);
    penalty(wire, i, floor, 0);
    //assert_(length(F[wire.base+i].xy()) < 0x1p-14, log2(length(F[wire.base+i].xy())));
   }
   // Winch tension
   /*F[wire.base+i] = vec3d(dt * Wire::mass * winchRadius / sq(winchRate)
                                         * vec3(-sin(winchAngle), cos(winchAngle). 0));*/
   if(1 && wire.count) {
    vec3 x = winchPosition()-wire.position[wire.count-1];
   spring<true, false>(wire.count-1, 0, Wire::tensionStiffness,
                       length(x), winchRestLength, Wire::tensionDamping, x/length(x), 0);
   }
   wireBoundTime.stop();

   // Tension
   wireTensionTime.start();
   for(size_t i: range(1, wire.count)) {
    size_t a = i-1, b = i;
    vec3 relativePosition = wire.position[a] - wire.position[b];
    float length = ::length(relativePosition);
    assert_(isNumber(length), a, b, wire.position[a], wire.position[b], wire.count);
    float restLength = Wire::internodeLength;
    vec3 normal = relativePosition/length;
    vec3 relativeVelocity = wire.velocity[a] - wire.velocity[b];
    //assert_(restLength/4 < length && length < 4*restLength, i, length/restLength);
    spring<>(wire.base+a, wire.base+b, Wire::tensionStiffness, length,
             restLength, Wire::tensionDamping, normal, relativeVelocity);
   }
   wireTensionTime.stop();

   wireContactTime.start();
   parallel_for(wire.count, [this](uint, int a) {
    /*{ // Wire - Wire
     int3 index = wireGrid.index3(position[a]);
     for(int z: range(max(0, index.z-1), min(index.z+2, wireGrid.size.z)))
      for(int y: range(max(0, index.y-1), min(index.y+2, wireGrid.size.y)))
       for(int x: range(max(0, index.x-1), min(index.x+2, wireGrid.size.x)))
       {
        Grid::List list = wireGrid[wireGrid.index(x, y, z)];
        for(size_t i=0; i<wireGrid.cellCapacity; i++) {
         int b = list[i]-1;
         if(!(a+1 < b)) break; // No adjacent, single contact per pair, until 0
         penalty(wire, a, wire, b);
        }
       }
    }*/
    {// Wire - Grain
     int3 index = grainGrid.index3(wire.position[a]);
     for(int z: range(max(0, index.z-1), min(index.z+2, grainGrid.size.z)))
      for(int y: range(max(0, index.y-1), min(index.y+2, grainGrid.size.y)))
       for(int x: range(max(0, index.x-1), min(index.x+2, grainGrid.size.x)))
       {
        Grid::List list = grainGrid[grainGrid.index(x, y, z)];
        for(size_t i=0; i<grainGrid.cellCapacity; i++) {
         int b = list[i]-1;
         if(b<=0) break; // until 0, except grain #0 (winch obstacle)
         Locker lock(this->lock);
         penalty(wire, a, grain, b);
        }
       }
    }
   });
   wireContactTime.stop();

   // Wire - Grain friction
   wireFrictionTime.start();
   if(1) {
    //Locker lock(this->lock);
   parallel_for(wire.count, [this](uint, size_t a) {
    for(size_t i=0; i<wire.frictions[a].size;) {
     Friction& f = wire.frictions[a][i];
     if(f.lastUpdate >= timeStep-1)  {
      if(f.lastUpdate == timeStep) {
       size_t b = f.index;
       /**/  if(b < grain.base+grain.count) friction(f, wire, a, grain, b-grain.base);
       else if(b < wire.base+wire.count) friction(f, wire, a, wire, b-wire.base);
       else if(b==floor.base) friction(f, wire, a, floor, 0);
       else if(b==side.base) friction(f, wire, a, side, 0);
       else ::error(b);
      } // else no more contact or dynamic friction
      i++;
     } else
#if DBG_FRICTION
      i++;
#else
     { wire.frictions[a].removeAt(i); continue; } // Garbage collection
#endif
    }
   });}
   wireFrictionTime.stop();
   wireTime.stop();
  }
  //Locker lock(this->lock);
  solveTime.start();
  buffer<vec3d> dv;
  if(implicit) {
   if(grain.count || wire.count) {
    matrix.factorize();
    //log(F[grain.base+1]/double(Grain::mass));
    dv = cast<vec3d>(matrix.solve(cast<real>(F)));
   }
  } else {
   dv = buffer<vec3d>(F.capacity);
   for(size_t i: range(grain.count))
    dv[grain.base+i] = F[grain.base+i] / real(Grain::mass);
   for(size_t i: range(wire.count))
    dv[wire.base+i] = F[wire.base+i] / real(Wire::mass);
  }
  solveTime.stop();

  grainTime.start();
  grainIntegrationTime.start();
  for(size_t i: range(grain.count)) update(dv, grainGrid, grain, i);

  if(useRotation) parallel_for(grain.count, [this](uint, size_t i) {
   // PCDM rotation integration
   quat& q = grain.rotation[i];
   vec3& ww = grain.angularVelocity[i];
   vec3 w = q.conjugate() * ww;
   vec3 t = q.conjugate() * grain.torque[i];
   vec3 dw = dt/Grain::angularMass * (t - cross(w, Grain::angularMass*w));
   vec3 w4 = w + 1.f/4*dw;
   vec3 w4w = q * w4; // -> Global
   // Prediction (multiplicative update)
   quat qp = angleVector(dt/2, w4w) * q;
   vec3 w2 = w + 1.f/2*dw;
   vec3 w2w = qp * w2;
   // Correction (multiplicative update)
   q = angleVector(dt, w2w) * q;
   q = normalize(q); // FIXME
   ww = q * (w + dw);
  });
  grainIntegrationTime.stop();
  grainTime.stop();

  wireTime.start();
  wireIntegrationTime.start();
  //for(size_t i: range(wire.count)) update(dv, wireGrid, wire, i);
  for(size_t i: range(wire.count)) {
   wire.velocity[i] += vec3(dv[wire.base+i]);
   wire.position[i] += dt*wire.velocity[i];
   if(0) assert_(wire.position[i] >= vec3(-1) && wire.position[i] <= vec3(1), i,
           wire.position[i], wire.velocity[i], dv[wire.base+i]);
  }
  wireIntegrationTime.stop();
  wireTime.stop();

  {State state;
   state.grainPositions = copyRef(grain.position.slice(0, grain.count));
   state.wirePositions = copyRef(wire.position.slice(0, wire.count));
   states.append(move(state));
   stateIndex = states.size-1;
  }

  timeStep++;

  if(0) {
   while(plots.size<2) plots.append();
   if(rollTest) {
    Locker lock(this->lock);
    plots[0].dataSets["Ecc"__][timeStep]
      = 1./2*Grain::mass*sq(grain.velocity[0]);
    plots[0].dataSets["Ecr"__][timeStep]
      = 1./2*Grain::angularMass*sq(grain.angularVelocity[0]);
    plots[1].dataSets["Vt"__][timeStep] = length(grain.velocity[0] +
      cross(grain.angularVelocity[0], vec3(0,0,-Grain::radius)));
   }
   if(wire.count) {
    //miscTime.start();
    float wireLength = 0;
    vec3 last = wire.position[0];
    for(vec3 p: wire.position.slice(1, wire.count-1)) {
     wireLength += ::length(p-last);
     last = p;
    }
    //assert_(isNumber(wireLength), wireLength);
    if(!isNumber(wireLength)) error(withName(wireLength));
    //if(!plots) plots.append();
    //plots[0].dataSets["length"__][timeStep] = wireLength;
    Locker lock(this->lock);
    plots[0].dataSets["stretch"__][timeStep]
      = (wireLength / wire.count) / Wire::internodeLength;
    float ssqV = 0;
    for(vec3 v: grain.velocity.slice(1, grain.count)) ssqV += sq(v);
    plots[1].dataSets["Kt"__][timeStep] = 1./2*Grain::mass*ssqV;
    float ssqR = 0;
    for(vec3 v: grain.angularVelocity.slice(0, grain.count)) ssqR += sq(v);
    plots[1].dataSets["Kr"__][timeStep] = 1./2*Grain::angularMass*ssqR;
    //miscTime.stop();
   }
  }
  stepTime.stop();
 }
};

struct SimulationView : Simulation, Widget, Poll {
 Time renderTime;

 bool stop = false;
 void error(string message) override {
  stop = true;
  log(message);
  window->setTitle(message);
 }

 void step() {
  Simulation::step();
  if(!rollTest) viewYawPitch.x += 2*PI*dt / 16;
#define THREAD 0
#if !THREAD
  if(timeStep%subStepCount == 0)
#endif
   window->render();
  int64 elapsed = realTime() - lastReport;
  if(elapsed > 2e9) {
   if(0) log((timeStep-lastReportStep) / (elapsed*1e-9),
       "step",str(stepTime, totalTime),
       "misc",str(miscTime, stepTime),
       "grain",str(grainTime, stepTime),
       "wire",str(wireTime, stepTime),
       "solve",str(solveTime, stepTime),
       //"grainContact",str(grainContactTime, grainTime),
       //"grainFriction",str(grainFrictionTime, grainTime),
       //"grainIntegration",str(grainIntegrationTime, grainTime),
       "wireContact",str(wireContactTime, wireTime),
       "wireBound",str(wireBoundTime, wireTime),
       "wireTensionTime",str(wireTensionTime, wireTime),
       "wireFriction",str(wireFrictionTime, wireTime),
       "wireIntegration",str(wireIntegrationTime, wireTime)
       );
   lastReport = realTime();
   lastReportStep = timeStep;
#if PROFILE
   requestTermination();
#endif
  }
  if(!stop) queue();
 }
 void event() {
#if PROFILE
  static unused bool once =
    ({ extern void profile_reset(); profile_reset(); true; });
#endif
  step();
 }

 unique<Window> window = ::window(this, -1);
 // View
 vec2 lastPos; // for relative cursor movements
 vec2 viewYawPitch = vec2(0, rollTest?0:-PI/3); // Current view angles
 float2 scale = 2./(32*Grain::radius);
 float3 translation = 0;
 float3 rotationCenter = 0;
#if THREAD
 Thread simulationThread;
#endif
 unique<Encoder> encoder = nullptr;

 SimulationView()
#if THREAD
  : Poll(0, POLLIN, simulationThread)
#endif
 {
  /*window->actions[Space] = [this]{ writeFile(str(timeStep*dt)+".png",
                                          encodePNG(window->readback()), home()); };*/
  window->actions[RightArrow] = [this]{ if(stop) queue(); };
  window->actions[Space] = [this]{ stop=!stop; if(!stop) queue(); };
  window->actions[Return] = [this]{
   if(pour) {
    pour = false; window->setTitle("Released");
    log("Release", withName(grain.count,wire.count));
   }
  };
  /*window->actions[Key('E')] = [this] {
   if(!encoder) {
    encoder = unique<Encoder>("tas.mp4");
    encoder->setH264(window->Window::size, 60);
    encoder->open();
   }
  };*/
  if(arguments().contains("export")) {
   encoder = unique<Encoder>("tas.mp4"_);
   encoder->setH264(int2(1280,720), 60);
   encoder->open();
  }

#if THREAD
  simulationThread.spawn();
#endif
  queue();
 }
 ~SimulationView() {
  if(pour) log("~", "grain", grain.count, "wire", wire.count);
 }
 vec2 sizeHint(vec2) { return vec2(1050, 1050*720/1280); }
 shared<Graphics> graphics(vec2) {
  renderTime.start();
  size_t start = pour || rollTest ? 0 : 1;

  const State& state = states[stateIndex];

  const float dt = 1./60;
  {
   float3 min = 0, max = 0;
   Locker lock(this->lock);
   for(size_t i: range(start, state.grainPositions.size)) { // FIXME: proper BS
    min = ::min(min, state.grainPositions[i]);
    max = ::max(max, state.grainPositions[i]);
   }
   for(size_t i: range(start, state.wirePositions.size)) { // FIXME: proper BS
    min = ::min(min, state.wirePositions[i]);
    max = ::max(max, state.wirePositions[i]);
   }
   float3 rotationCenter = (min+max)/2.f;
   rotationCenter.xy() = 0;
   if(!rollTest)
    this->rotationCenter = this->rotationCenter*(1-dt) + dt*rotationCenter;
    //this->rotationCenter = rotationCenter;
  }

  quat viewRotation = angleVector(viewYawPitch.y, vec3(1,0,0)) *
                                 angleVector(viewYawPitch.x, vec3(0,0,1));

  float3 min = -1./32, max = 1./32;
  {Locker lock(this->lock);
   for(size_t i: range(start, state.grainPositions.size)) {
    float3 O = viewRotation * (state.grainPositions[i]-rotationCenter);
    min = ::min(min, O - float3(float2(Grain::radius), 0)); // Parallel
    max = ::max(max, O + float3(float2(Grain::radius), 0)); // Parallel
   }
   for(size_t i: range(start, state.wirePositions.size)) {
    float3 O = viewRotation * (state.wirePositions[i]-rotationCenter);
    min = ::min(min, O - float3(float2(Grain::radius), 0)); // Parallel
    max = ::max(max, O + float3(float2(Grain::radius), 0)); // Parallel
   }
  }

  static GLFrameBuffer target(int2(1280,720));
  vec2 size (target.size.x, target.size.y);
  vec2 viewSize = size;

  vec3 scale (2*::min(viewSize/(max-min).xy())/viewSize, -1/(2*(max-min).z));

  scale.xy() = this->scale = this->scale*(1-dt) + dt*scale.xy();

  if(rollTest) scale.xy() = vec2(viewSize.x/(16*Grain::radius))/viewSize;
  //scale.x *= size.y/size.x;
  //scale.y *= size.x/size.y;
  float3 fitTranslation = -scale*(min+max)/2.f;
  //vec2 aspectRatio (size.x/size.y, 1);
  float3 translation = this->translation = vec3((size-viewSize)/size, 0);
  //this->translation*(1-dt) + dt*fitTranslation;
  /*mat4 viewProjection;
  for(int e: range(3)) {
   vec3 axis = 0; axis[e] = 1;
   viewProjection[e] = vec4(scale*(viewRotation*axis),
                            (translation + scale*(viewRotation*(-rotationCenter)))[e]);
  }*/
  mat4 viewProjection = mat4()
    .translate(translation)
    .scale(scale)
    .rotateX(viewYawPitch.y) .rotateZ(viewYawPitch.x)
    .translate(-rotationCenter);

  //map<rgb3f, array<vec3>> lines;

  target.bind(ClearColor|ClearDepth);
  glDepthTest(true);


  if(state.grainPositions) {
   buffer<float3> positions {state.grainPositions.size*6};
   //Locker lock(this->lock);
   for(size_t i: range(state.grainPositions.size)) {
    // FIXME: GPU quad projection
    float3 O = viewProjection * state.grainPositions[start+i];
    float2 min = O.xy() - scale.xy()*float2(Grain::radius); // Parallel
    float2 max = O.xy() + scale.xy()*float2(Grain::radius); // Parallel
    positions[i*6+0] = float3(min, O.z);
    positions[i*6+1] = float3(max.x, min.y, O.z);
    positions[i*6+2] = float3(min.x, max.y, O.z);
    positions[i*6+3] = float3(min.x, max.y, O.z);
    positions[i*6+4] = float3(max.x, min.y, O.z);
    positions[i*6+5] = float3(max, O.z);

    for(int d: range(0)) {
     vec3 axis = 0; axis[d] = 1;
     lines[rgb3f(axis)].append(viewProjection*toGlobal(grain, i, 0));
     lines[rgb3f(axis)].append(viewProjection*toGlobal(grain, i,
                                                       Grain::radius/2*axis));
    }
#if DBG_FRICTION
    Locker lock(this->lock);
    for(const Friction& f : grain.frictions[i]) {
     if(f.lastUpdate < timeStep-1) continue;
     vec3 A = toGlobal(grain, i, f.localA);
     size_t b = f.index;
     vec3 B;
     /**/  if(b < grain.base+grain.count) B = toGlobal(grain, b-grain.base, f.localB);
     else if(b < wire.base+wire.count) B = toGlobal(wire, b-wire.base, f.localB);
     else if(b==floor.base) B = toGlobal(floor, b-floor.base, f.localB);
     else if(b==side.base) B = toGlobal(side, b-side.base, f.localB);
     else ::error("grain", b);
     vec3 vA = viewProjection*A, vB=viewProjection*B;
     if(length(vA-vB) < 16/size.y) vA.y -= 8/size.y, vB.y += 8/size.y;
     lines[f.color].append(vA);
     lines[f.color].append(vB);
    }
#endif
   }

   static GLShader shader {::shader_glsl(), {"sphere"}};
   shader.bind();
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   GLBuffer rotationBuffer (apply(grain.rotation.slice(start),
                [=](quat q) -> quat { return (viewRotation*q).conjugate(); }));
   shader.bind("rotationBuffer"_, rotationBuffer, 0);
   GLBuffer colorBuffer (grain.color.slice(start));
   shader.bind("colorBuffer"_, colorBuffer, 1);
   shader["radius"] = scale.z/2 * Grain::radius;
   //glBlendAlpha();
   vertexArray.draw(Triangles, positions.size);
  }

  if(state.wirePositions.size>1) {
   buffer<float3> positions {(state.wirePositions.size-1)*6};
   Locker lock(this->lock);
   for(size_t i: range(state.wirePositions.size-1)) {
    vec3 a = state.wirePositions[i], b = state.wirePositions[i+1];
    // FIXME: GPU quad projection
    float3 A = viewProjection * a, B= viewProjection * b;
    float2 r = B.xy()-A.xy();
    float l = length(r);
    float2 t = r/l;
    float3 n = scale*Wire::radius*vec3(t.y, -t.x, 0);
    float3 P[4] {A-n, A+n, B-n, B+n};
    positions[i*6+0] = P[0];
    positions[i*6+1] = P[1];
    positions[i*6+2] = P[2];
    positions[i*6+3] = P[2];
    positions[i*6+4] = P[1];
    positions[i*6+5] = P[3];

#if DBG_FRICTION
    for(const Friction& f : wire.frictions[i]) {
     if(f.lastUpdate < timeStep-1) continue;
     vec3 A = toGlobal(wire, i, f.localA);
     size_t b = f.index;
     vec3 B;
     /**/  if(b < grain.base+grain.count) B = toGlobal(grain, b-grain.base, f.localB);
     else if(b < wire.base+wire.count) B = toGlobal(wire, b-wire.base, f.localB);
     else if(b==floor.base) B = toGlobal(floor, b-floor.base, f.localB);
     else if(b==side.base) B = toGlobal(side, b-side.base, f.localB);
     else ::error("wire", b);
     vec3 vA = viewProjection*A, vB=viewProjection*B;
     if(length(vA-vB) < 2/size.y) vA.y -= 4/size.y, vB.y += 4/size.y;
     lines[f.color].append(vA);
     lines[f.color].append(vB);
    }
#endif
   }
   static GLShader shader {::shader_glsl(), {"cylinder"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["radius"] = scale.z/2 * Wire::radius;
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   vertexArray.draw(Triangles, positions.size);
  }

  glDepthTest(false);
  static GLShader shader {::shader_glsl(), {"flat"}};
  shader.bind();
  shader.bindFragments({"color"});
  static GLVertexArray vertexArray;
  for(auto entry: lines) {
   shader["uColor"] = entry.key;
   //GLBuffer positionBuffer (apply(entry.value, [=](vec3 p){ return viewProjection*p;}));
   GLBuffer positionBuffer (entry.value);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   vertexArray.draw(Lines, entry.value.size);
  }
  lines.clear();

  if(1 && plots) {
   Image target(int2(size.x,/*size.y/3*/size.y-viewSize.y), true);
   target.clear(byte4(byte3(0xFF), 0));
   this->lock.lock();
   auto graphics = plots.graphics(vec2(target.size),
                                            Rect(vec2(target.size)));
   this->lock.unlock();
   render(target, graphics);
   GLTexture image = flip(move(target));
   static GLShader shader {::shader_glsl(), {"blit"}};
   shader.bind();
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   shader["image"] = 0; image.bind(0);
   vec2 min (-1,-1), max (1, (float)target.size.y/size.y*2-1);
   GLBuffer positionBuffer (ref<vec2>{
                             vec2(min.x,min.y), vec2(max.x,min.y), vec2(min.x,max.y),
                             vec2(min.x,max.y), vec2(max.x,min.y), vec2(max.x,max.y)});
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             2, Float, positionBuffer);
   glBlendAlpha();
   vertexArray.draw(Triangles, 6);
  }

  if(encoder) {
   encoder->writeVideoFrame(target.readback());
   if(stop) encoder = nullptr;
  }
  //target.blit(0, ::max(int2(0), (target.size-window->size)/2), window->size);
  target.blit(0, window->size);
  renderTime.stop();
  if(stop && fitTranslation != this->translation) window->render();
  //window->setTitle(str(grain.count, wire.count));
  return shared<Graphics>();
 }

 // Orbital ("turntable") view control
 bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button,
                                Widget*&) override {
  vec2 delta = cursor-lastPos; lastPos=cursor;
  if(event==Press && button==WheelDown) {
   stateIndex = clamp(0, stateIndex-1, int(states.size-1));
   window->setTitle(str(stateIndex));
   return true;
  }
  if(event==Press && button==WheelUp) {
   stateIndex = clamp(0, stateIndex+1, int(states.size-1));
   window->setTitle(str(stateIndex));
   return true;
  }
  if(event==Motion && button==LeftButton) {
   viewYawPitch += float(2.f*PI) * delta / size; //TODO: warp
   viewYawPitch.y = clamp(float(-PI), viewYawPitch.y, 0.f);
   if(encoder && !pour) encoder = nullptr;
  }
  else return false;
  return true;
 }
} view;
