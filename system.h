#pragma once
#include "simd.h"
#include "parallel.h"
#include "variant.h"
#include "matrix.h"
typedef v4sf vec4f;
//typedef v8sf vec8f;
inline float length2(vec4f v) { return sqrt(sq2(v))[0]; }
inline float length(vec4f v) { return sqrt(sq3(v))[0]; }

#define sconst static constexpr

struct NoOperation {
  float operator [](size_t) const { return 0; }
};
string str(NoOperation) { return "NoOperation"_; }
inline void operator +=(NoOperation, vec4f) {}
inline void operator -=(NoOperation, vec4f) {}
inline void atomic_add(NoOperation, vec4f) {}
inline void atomic_sub(NoOperation, vec4f) {}

struct RadialSum {
 float radialSum;
 RadialSum& operator[](size_t){ return *this; }
 void operator -=(vec4f f) { radialSum += length2(f); }
};
inline void atomic_sub(RadialSum& t, vec4f f) { t.radialSum += length2(f); }

sconst bool validation = true;

struct System {
 // Integration
 const float dt;
 const vec4f b[4] = {float3(dt), float3(dt*dt/2), float3(dt*dt*dt/6), float3(dt*dt*dt*dt/24)};
 const vec4f c[5] = {float3(19./90), float3(3/(4*dt)), float3(2/(dt*dt)), float3(6/(2*dt*dt*dt)),
                                float3(24/(12*dt*dt*dt*dt))};

 // SI units
 sconst float s = 1, m = 1, kg = 1, N = kg*m/s/s;
 sconst float mm = 1e-3*m, g = 1e-3*kg;
 const float gz;
 sconst float densityScale = 5e4; // 5-6
 vec4f G {0, 0, -gz/densityScale, 0}; // Scaled gravity

 // Penalty model
 sconst float normalDamping = 0.02; //0.1;
 // Friction model
 sconst bool staticFriction = true;
 sconst float staticFrictionSpeed = inf; //1./3 *m/s; //inf; //1./3 *m/s;
 sconst float staticFrictionFactor = 1e5;
 sconst float staticFrictionLength = 1e-4;
 sconst float staticFrictionDamping = 15 *g/s/s;
 const float frictionCoefficient;

 struct Contact {
  vec4f relativeA, relativeB; // Relative to center but world axices
  vec4f normal;
  float depth;
  float u, v; // FIXME: barycentric coordinates passed from contact to force distribution
 };

 /// Returns contact between two objects
 /// Defaults implementation for spheres
 template<Type tA, Type tB>
 Contact contact(const tA& A, size_t a, const tB& B, size_t b) {
  vec4f relativePosition = A.position[a] - B.position[b];
  vec4f length = sqrt(sq3(relativePosition));
  vec4f normal = relativePosition/length; // B -> A
  return {float3(tA::radius)*(-normal), float3(tB::radius)*(+normal),
     normal, length[0]-tA::radius-tB::radius,0,0};
 }

 struct Friction {
  size_t index; // Identifies contact to avoid duplicates
  vec4f localA, localB; // Last static friction contact location
  size_t lastStatic;// = 0;
  float energy;
  //Friction(size_t index, vec4f localA, vec4f localB, size_t lastStatic, float energy)
  bool operator==(const Friction& b) const { return index == b.index; }
 };

 struct Vertex {
  sconst bool friction = true;
  const size_t base, capacity;
  float mass = 0;
  vec4f _1_mass = float4(0);
  buffer<vec4f> position { capacity };
  buffer<vec4f> velocity { capacity };
  buffer<vec4f> positionDerivatives[3]; // { [0 ... 2] = buffer<vec4f>(capacity) };
  buffer<vec4f> force { capacity };
  buffer<array<Friction>> frictions { capacity };
  size_t count = 0;

  struct { vec4f operator[](size_t) const { return _0001f; }} rotation;
  struct { vec4f operator[](size_t) const { return _0f; }} angularVelocity;

  Vertex(size_t base, size_t capacity, float mass) : base(base), capacity(capacity),
    mass(mass), _1_mass(float3(1./mass)) {
   for(size_t i: range(3)) positionDerivatives[i] = buffer<vec4f>(capacity);
   frictions.clear();
  }
 };

 void step(Vertex& p, size_t i) {
  // Correction
  vec4f r = b[1] * (p.force[i] * p._1_mass - p.positionDerivatives[0][i]);
  p.position[i] += c[0]*r;
  p.velocity[i] *= float4(1-0.1*dt); // 10%/s viscosity
  //p.velocity[i] *= float4(1-0.05*dt); // 5%/s viscosity
  //p.velocity[i] *= float4(1-0.01*dt); // 1%/s viscosity
  p.velocity[i] += c[1]*r;
  p.positionDerivatives[0][i] += c[2]*r;
  p.positionDerivatives[1][i] += c[3]*r;
  p.positionDerivatives[2][i] += c[4]*r;

  // Prediction
  p.position[i] += b[0]*p.velocity[i];
  p.position[i] += b[1]*p.positionDerivatives[0][i];
  p.position[i] += b[2]*p.positionDerivatives[1][i];
  p.position[i] += b[3]*p.positionDerivatives[2][i];
  p.velocity[i] += b[0]*p.positionDerivatives[0][i];
  p.velocity[i] += b[1]*p.positionDerivatives[1][i];
  p.velocity[i] += b[2]*p.positionDerivatives[2][i];
  p.positionDerivatives[0][i] += b[0]*p.positionDerivatives[1][i];
  p.positionDerivatives[0][i] += b[1]*p.positionDerivatives[2][i];
  p.positionDerivatives[1][i] += b[0]*p.positionDerivatives[2][i];
 }

 struct Plate : Vertex {
  struct { NoOperation operator[](size_t) const { return {}; }} torque;

  sconst bool friction = false;
  sconst float curvature = 0;
  sconst float elasticModulus = 1e9 * kg/(m*s*s); // 1 GPa

  Plate() : Vertex(0, 2, 1e-3 * densityScale) {
   count = 2;
   position.clear(); velocity.clear(); force.clear();
   for(size_t i: range(3)) positionDerivatives[i].clear();
  }
 } plate;
 /// Sphere - Plate
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Plate& B, size_t b) {
  if(b == 0) { // Bottom
   vec4f normal {0, 0, 1, 0};
   return { float3(tA::radius)*(-normal),
      vec4f{A.position[a][0], A.position[a][1], B.position[b][2], 0},
      normal, (A.position[a][2]-tA::radius) - B.position[b][2],0,0 };
  } else { // Top
   vec4f normal {0, 0, -1, 0};
   return { float3(tA::radius)*(-normal),
      vec4f{A.position[a][0], A.position[a][1], B.position[b][2], 0},
      normal, B.position[b][2] - (A.position[a][2]+tA::radius),0,0 };
  }
 }

 struct Grain : Vertex {
  // Properties
  sconst float radius =  2.47*mm; // 40 mm diameter
  sconst float volume = 4./3 * PI * cb(radius);
  sconst float curvature = 1./radius;
  sconst float shearModulus = 79e9 * kg / (m*s*s);
  sconst float poissonRatio = 0.28;
  sconst float elasticModulus = 0 ? 2*shearModulus*(1+poissonRatio) : 1e10; // ~2e11

  //sconst float mass = 3*g;
  sconst float density = 7.8e3 * densityScale;
  sconst float mass = density * volume;
  sconst float angularMass = 2./5*mass*sq(radius);
  /*sconst float angularMass = 2./5*mass*(pow5(radius)-pow5(radius-1e-4))
                                         / (cb(radius)-cb(radius-1e-4));*/

  const vec4f dt_angularMass;
  buffer<vec4f> rotation { capacity };
  buffer<vec4f> angularVelocity { capacity };
  buffer<vec4f> angularDerivatives[3]; // { [0 ... 2] = buffer<vec4f>(capacity) };

  v4sf g;

  Grain(const System& system) : Vertex(2, 3852*4/*=15408*/, Grain::mass), // 5 MB
   dt_angularMass(float3(system.dt/angularMass)),
    g(float4(mass) * system.G) {
   for(size_t i: range(3)) angularDerivatives[i] = buffer<vec4f>(capacity);
  }
  buffer<vec4f> torque { capacity };
 } grain {*this};

 const vec4f dt_2 = float4(dt/2);
 void step(Grain& p, size_t i) {
  step((Vertex&)p, i);
  p.angularVelocity[i] *= float4(1-0.5*dt); // 50%/s viscosity
  p.rotation[i] += dt_2 * qmul(p.angularVelocity[i], p.rotation[i]);
  p.angularVelocity[i] += p.dt_angularMass * p.torque[i];
  p.rotation[i] *= rsqrt(sq4(p.rotation[i]));
 }

 struct Wire : Vertex {
  using Vertex::Vertex;
  struct { NoOperation operator[](size_t) const { return {}; }} torque;

  sconst float radius = 0.5 * mm; // 2mm diameter
  sconst float curvature = 1./radius;
  sconst float internodeLength = Grain::radius/2;
  sconst vec4f internodeLength4 = float3(internodeLength);
  sconst float section = PI * sq(radius);
  sconst float volume = section * internodeLength;
  sconst float density = 1e3 * kg / cb(m) * densityScale;
  sconst float angularMass = 0;
  const float elasticModulus; // ~ 1e7
  const vec4f tensionStiffness = float3(100*/*FIXME*/ elasticModulus * PI * sq(radius));
  sconst vec4f tensionDamping = _0f; //float3(mass / s);
  sconst float areaMomentOfInertia = PI/4*pow4(radius);
  const float bendStiffness = 10*/*FIXME*/ elasticModulus * areaMomentOfInertia / internodeLength;
  sconst float mass = Wire::density * Wire::volume;
  sconst float bendDamping = 0; //mass / s;
  //float tensionEnergy=0;
  Wire(float elasticModulus, size_t base) :
    Vertex{base, 1<<17, Wire::mass}, elasticModulus(elasticModulus) {}
 } wire;

 struct Side : Vertex {
  sconst bool friction = false;

  sconst float curvature = 0; // -1/radius?
  const float elasticModulus = 1e8; //8; // for contact
  sconst float density = 1e3;
  const float resolution;
  const float initialRadius;
  const float height;
  const size_t W, H;
  size_t faceCount = (H-1)*W*2; // Always soft membrane
  float minRadius = initialRadius-Grain::radius, maxRadius;
  float minRadiusSq, maxRadiusSq;
  const float radius = initialRadius;

  const float internodeLength = 2*PI*initialRadius/W;

  const float pourThickness;
  const float loadThickness;
  float thickness = pourThickness;
  //const float thickness;

  const float tensionElasticModulus; // for contact
  const float massCoefficient = sqrt(3.)/2 * sq(internodeLength) * densityScale * density;
  const float tensionCoefficient = sqrt(3.)/2 * internodeLength * tensionElasticModulus;
  float tensionStiffness = tensionCoefficient  * thickness; // FIXME

  float tensionDamping = mass / s;
  //sconst float areaMomentOfInertia = pow4(1*mm); // FIXME
  //const float bendStiffness = 0;//elasticModulus * areaMomentOfInertia / internodeLength; // FIXME

  //float tensionEnergy = 0, tensionEnergy2 = 0;

  struct { v4sf operator[](size_t) const { return _0f; } } position;

  struct { NoOperation operator[](size_t) const { return {}; }} torque;

  Side(float resolution, float initialRadius, float height, size_t base, float thickness, float elasticModulus)
      : Vertex(base, /*W*H*/int(2*PI*initialRadius/resolution) * (int(height/resolution*2/sqrt(3.))+1), 0),
        resolution(resolution),
        initialRadius(initialRadius),
        height(height),
        W(int(2*PI*initialRadius/resolution)),
        H(int(height/resolution*2/sqrt(3.))+1),
        pourThickness(thickness*10),
        loadThickness(thickness),
        tensionElasticModulus(elasticModulus)
  {
      mass = massCoefficient*thickness;
      _1_mass = float3(1./mass);
      count = W*H;
      for(size_t i: range(H)) for(size_t j: range(W)) {
          float z = i*height/(H-1);
          float a = 2*PI*(j+(i%2)*1./2)/W;
          float x = initialRadius*cos(a), y = initialRadius*sin(a);
          Vertex::position[i*W+j] = (v4sf){x,y,z,0};
      }
      Vertex::velocity.clear(_0f);
      for(size_t i: range(3)) positionDerivatives[i].clear(_0f);
  }
 } side;

 /// Side - Sphere
 template<Type tB>
 Contact contact(const Side& side, size_t vertexIndex, const tB& B, size_t bIndex) {
  if(sq2(B.position[bIndex])[0]/*+Grain::radius*/ < side.minRadiusSq) return {_0f,_0f,_0f,0,0,0}; // Quick cull
  vec4f a = side.Vertex::position[vertexIndex];
  vec4f relativePosition = a - B.position[bIndex];
  vec4f length = sqrt(sq3(relativePosition));
  vec4f normal = relativePosition/length; // B -> A
  return {float3(tB::radius)*(-normal), _0f, normal, length[0]-tB::radius, 0, 0};
 }
 /// Sphere - Side
 template<Type tA>
 Contact contact(const tA& A, size_t aIndex, const Side& side, size_t vertexIndex) {
  vec4f b = side.Vertex::position[vertexIndex];
  vec4f relativePosition = A.position[aIndex] - b;
  vec4f length = sqrt(sq3(relativePosition));
  vec4f normal = relativePosition/length; // B -> A
  return {float3(tA::radius)*(-normal), _0f, normal, length[0]-tA::radius, 0, 0};
 }

 System(const Dict& p) :
   dt(p.at("TimeStep"_)),
   gz(10/*p.at("G")*/),
   frictionCoefficient(p.at("Friction"_)),
   wire(p.value("Elasticity"_, 0.f), grain.base+grain.capacity),
   side(Grain::radius/(float)p.at/*value*/("Resolution"/*,2*/), p.at("Radius"_),
        /*p.at("Height"_)*/ (float)p.at("Radius"_)*4.f,
          /*p.at("Thickness"_),*/ wire.base+wire.capacity, p.at("Thickness"_), p.at("Side")) {
  //log("System");
     if(p.at("Pattern")!="none"_) assert_(wire.elasticModulus);
 }

 // Update
 size_t timeStep = 0;
 float grainKineticEnergy=0, wireKineticEnergy=0, normalEnergy=0, staticEnergy=0;
 //bendEnergy=0;

 /*bool recordContacts = false;
 struct ContactForce { size_t a, b; vec3 relativeA, relativeB; vec3 force; };
 array<ContactForce> contacts, contacts2;*/

 size_t staticFrictionCount = 0, dynamicFrictionCount = 0;
 size_t staticFrictionCount2 = 0, dynamicFrictionCount2 = 0;

 /// Evaluates contact penalty between two objects
 template<Type tA, Type tB> bool penalty(const tA& A, size_t a, tB& B, size_t b) {
     vec4f normalForce;
     return penalty(A, a, B, b, normalForce);
 }
template<Type tA, Type tB> bool penalty(const tA& A, size_t a, tB& B, size_t b, vec4f& normalForce) {
  Contact c = contact(A, a, B, b);
  if(c.depth >= 0) return false;
  // Stiffness
  const float E = 1/(1/A.elasticModulus+1/B.elasticModulus);
  constexpr float R = 1/(tA::curvature+tB::curvature);
  const float K = 4./3*E*sqrt(R);
  const float Ks = K * sqrt(-c.depth);
  atomic_add(normalEnergy, 1./2 * Ks * sq(c.depth));
  float fK = - Ks * c.depth;
  // Damping
  const float Kb = K * normalDamping * sqrt(-c.depth);
  vec4f relativeVelocity =
      A.velocity[a] + cross(A.angularVelocity[a], c.relativeA)
   - (B.velocity[b] + cross(B.angularVelocity[b], c.relativeB));
  float normalSpeed = dot3(c.normal, relativeVelocity)[0];
  float fB = - Kb * normalSpeed ; // Damping
  float fN = fK + fB;
  //assert_(fN > 0, fK, fB);
  normalForce = float3(fN) * c.normal;
  vec4f force = normalForce;

  if(B.friction) {
   Friction& friction = A.frictions[a].add(Friction{B.base+b, _0f, _0f, 0, 0.f});
   if(friction.lastStatic < timeStep-1) { // Resets contact (TODO: GC)
    friction.localA = qapply(conjugate(A.rotation[a]), c.relativeA);
    friction.localB = qapply(conjugate(B.rotation[b]), c.relativeB);
   }

   vec4f relativeA = qapply(A.rotation[a], friction.localA);
   vec4f relativeB = qapply(B.rotation[b], friction.localB);
   vec4f globalA = A.position[a] + relativeA;
   vec4f globalB = B.position[b] + relativeB;
   vec4f x = globalA - globalB;
   vec4f tangentOffset = x - dot3(c.normal, x) * c.normal;
   vec4f tangentLength = sqrt(sq3(tangentOffset));
   float staticFrictionStiffness = staticFrictionFactor * frictionCoefficient;
   float kS = staticFrictionStiffness * fN;
   float fS = kS * tangentLength[0]; // 0.1~1 fN

   vec4f tangentRelativeVelocity
     = relativeVelocity - dot3(c.normal, relativeVelocity) * c.normal;
   vec4f tangentRelativeSpeed = sqrt(sq3(tangentRelativeVelocity));
   float fD = frictionCoefficient * fN;
   vec4f fT;
   friction.energy = 0;
   if(staticFriction && fS < fD // fS < fD ?
      && tangentLength[0] < staticFrictionLength
      && tangentRelativeSpeed[0] < staticFrictionSpeed
      ) {
    friction.lastStatic = timeStep; // Otherwise resets contact next step
    if(tangentLength[0]) {
     vec4f springDirection = tangentOffset / tangentLength;
     float fB = staticFrictionDamping * dot3(springDirection, relativeVelocity)[0];
     atomic_add(staticEnergy, 1./2 * kS * sq(tangentLength[0]));
     friction.energy = 1./2 * kS * sq(tangentLength[0]);
     fT = - float3(fS+fB) * springDirection;
     staticFrictionCount++;
    } else fT = _0f;
   } else {
    if(tangentRelativeSpeed[0]) {
     fT = - float3(fD) * tangentRelativeVelocity / tangentRelativeSpeed;
     dynamicFrictionCount++;
    } else fT = _0f;
   }
   force += fT;
   A.torque[a] += cross(relativeA, fT);
   atomic_sub(B.torque[b], cross(relativeB, fT));
  }
  A.force[a] += force;
  atomic_sub(B.force[b], force);
  /*if(recordContacts) {
   contacts.append(A.base+a, B.base+b, toVec3(c.relativeA), toVec3(c.relativeB), toVec3(force));
  }*/
  return true;
 }
};

constexpr float System::Grain::radius;
constexpr float System::Grain::mass;
constexpr float System::Wire::radius;
constexpr float System::Wire::mass;
constexpr vec4f System::Wire::internodeLength4;
constexpr float System::Plate::elasticModulus;
//constexpr float System::Side::elasticModulus;
constexpr float System::Side::density;
constexpr float System::Grain::elasticModulus;
constexpr vec4f System::Wire::tensionDamping;
//constexpr float System::RigidSide::elasticModulus;
