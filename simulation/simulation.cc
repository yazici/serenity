// TODO: Factorize force evaluation, contact management
#include "simulation.h"
//#include "process.h"
//#include "grain-bottom.h"
//#include "grain-side.h"
//#include "grain-grain.h"
//#include "grain-wire.h"
//#include "wire-bottom.h"

constexpr float System::Grain::radius;
constexpr float System::Wire::radius;
constexpr float System::Wire::mass;
constexpr float System::Wire::internodeLength;
constexpr float System::Wire::tensionStiffness;
constexpr float System::Wire::tensionDamping;

Simulation::Simulation(const Dict& p) : System(p.at("TimeStep")), radius(p.at("Radius")),
  pattern(p.contains("Pattern")?Pattern(ref<string>(patterns).indexOf(p.at("Pattern"))):None) {
 if(pattern) { // Initial wire node
  size_t i = wire.count++;
  wire.Px[i] = patternRadius; wire.Py[i] = 0; wire.Pz[i] = currentHeight;
  wire.Vx[i] = 0; wire.Vy[i] = 0; wire.Vz[i] = 0;
 }
}

bool Simulation::domain(vec3& min, vec3& max) {
 min = inf, max = -inf;
 // Grain
 // Avoids bound checks on grain-wire
 for(size_t i: range(grain.count)) {
  min.x = ::min(min.x, grain.Px[i]);
  max.x = ::max(max.x, grain.Px[i]);
  min.y = ::min(min.y, grain.Py[i]);
  max.y = ::max(max.y, grain.Py[i]);
  min.z = ::min(min.z, grain.Pz[i]);
  max.z = ::max(max.z, grain.Pz[i]);
 }
 if(!(min > vec3(-1) && max < vec3(1))) {
  log("Domain grain", min, max);
  processState = ProcessState::Error;
  return false;
 }
 // Wire
 // Avoids bound checks on grain-wire
 for(size_t i: range(wire.count)) {
  min.x = ::min(min.x, wire.Px[i]);
  max.x = ::max(max.x, wire.Px[i]);
  min.y = ::min(min.y, wire.Py[i]);
  max.y = ::max(max.y, wire.Py[i]);
  min.z = ::min(min.z, wire.Pz[i]);
  max.z = ::max(max.z, wire.Pz[i]);
 }
 if(!(min > vec3(-1) && max < vec3(1))) {
  log("Domain wire", min, max);
  processState = ProcessState::Error;
  return false;
 }
 return true;
}

unique<Lattice<uint16> > Simulation::generateLattice(const Mass& vertices, float radius) {
 vec3 min, max;
 if(!domain(min, max)) return nullptr;
 unique<Lattice<uint16>> lattice(sqrt(3.)/(2*radius), min, max);
 for(size_t i: range(vertices.count))
  lattice->cell(vertices.Px[i], vertices.Py[i], vertices.Pz[i]) = 1+i;
 return move(lattice);
}

unique<Grid> Simulation::generateGrid(const Mass& vertices, float length) {
 vec3 min, max;
 if(!domain(min, max)) return nullptr;
 unique<Grid> grid(1/length, min, max);
 for(size_t i: range(vertices.count))
  grid->cell(vertices.Px[i], vertices.Py[i], vertices.Pz[i]).append(1+i);
 return move(grid);
}

bool Simulation::step() {

 stepProcess();

 stepGrain();
 stepGrainBottom();
 if(processState == ProcessState::Running) stepGrainSide();
 if(!stepGrainGrain()) return false;
 if(!stepGrainWire()) return false;

 stepWire();
 stepWireTension();
 stepWireBottom();

 // Grain
 float maxGrainV = 0;
 for(size_t i: range(grain.count)) { // TODO: SIMD
  System::step(grain, i);
  maxGrainV = ::max(maxGrainV, length(grain.velocity(i)));
 }
 float maxGrainGrainV = maxGrainV + maxGrainV;
 grainGrainGlobalMinD12 -= maxGrainGrainV * dt;

 // Wire
 for(size_t i: range(wire.count)) { // TODO: SIMD
  System::step(wire, i);
 }

 timeStep++;
 return true;
}

void Simulation::stepGrain() {
 for(size_t a: range(grain.count)) {
  grain.Fx[a] = 0; grain.Fy[a] = 0; grain.Fz[a] = Grain::mass * Gz;
  grain.Tx[a] = 0; grain.Ty[a] = 0; grain.Tz[a] = 0;
 }
}

void Simulation::stepWire() {
 for(size_t i: range(wire.count)) { // TODO: SIMD
  wire.Fx[i] = 0; wire.Fy[i] = 0; wire.Fz[i] = wire.mass * Gz;
 }
}

void Simulation::stepWireTension() {
 // Tension
 if(wire.count) for(size_t i: range(1, wire.count)) { // TODO: SIMD
  size_t a = i-1, b = i;
  vec3 relativePosition = wire.position(a) - wire.position(b);
  vec3 length = ::length(relativePosition);
  vec3 x = length - vec3(wire.internodeLength);
  vec3 fS = - wire.tensionStiffness * x;
  vec3 direction = relativePosition/length;
  vec3 relativeVelocity = wire.velocity(a) - wire.velocity(b);
  vec3 fB = - wire.tensionDamping * dot(direction, relativeVelocity);
  vec3 fT = (fS + fB) * direction;
  wire.Fx[a] += fT.x;
  wire.Fy[a] += fT.y;
  wire.Fz[a] += fT.z;
  wire.Fx[b] -= fT.x;
  wire.Fy[b] -= fT.y;
  wire.Fz[b] -= fT.z;
 }
}

