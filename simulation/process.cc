#include "simulation.h"

void Simulation::stepProcess() {
 // Process
 if(/*currentHeight >= topZ-Grain::radius ||*/ grain.count == grain.capacity-simd) {
  const float targetPressure = 10000 * Pa;
  if(pressure < targetPressure) {
   processState = Pressure;
   pressure += dt * targetPressure * Pa/s;
  } else {
   if(processState  < Load) { // Fits plate (Prevents initial decompression)
    float topZ = 0;
    for(float z: grain.Pz.slice(simd, grain.count)) topZ = ::max(topZ, z+Grain::radius);
    this->topZ = ::min(this->topZ, topZ);
    topZ0 = this->topZ;
    processState = Load;
   }
   topZ -= dt * plateSpeed;
   bottomZ += dt * plateSpeed;
  }
 } else {
  // Increases current height
  if(currentHeight < topZ-Grain::radius) currentHeight += verticalSpeed * dt;

  // Generates wire
  if(pattern) {
   vec2 end;
   if(pattern == Helix) { // Simple helix
    float a = winchAngle;
    float r = patternRadius;
    end = vec2(r*cos(a), r*sin(a));
    winchAngle += linearSpeed/patternRadius * dt;
   }
   else if(pattern == Cross) { // Cross reinforced helix
    if(currentWinchRadius < -patternRadius) { // Radial -> Tangential (Phase reset)
     currentWinchRadius = patternRadius;
     lastAngle = winchAngle+PI;
     winchAngle = lastAngle;
    }
    if(winchAngle < lastAngle+loopAngle) { // Tangential phase
     float a = winchAngle;
     float r = patternRadius;
     end = vec2(r*cos(a), r*sin(a));
     winchAngle += linearSpeed/patternRadius * dt;
    } else { // Radial phase
     float a = winchAngle;
     float r = currentWinchRadius;
     end = vec2(r*cos(a), r*sin(a));
     currentWinchRadius -= linearSpeed * dt; // Constant velocity
    }
   }
   else if (pattern == Loop) { // Loops
    float A = winchAngle, a = winchAngle * (2*PI) / loopAngle;
    float R = patternRadius, r = R * loopAngle / (2*PI);
    end = vec2((R-r)*cos(A)+r*cos(a),(R-r)*sin(A)+r*sin(a));
    winchAngle += linearSpeed/r * dt;
   } else error("Unknown pattern:", int(pattern));
   float z = currentHeight+Grain::radius+Wire::radius; // Over pour
   //float z = currentHeight-Grain::radius-Wire::radius; // Under pour
   vec3 relativePosition = vec3(end.x, end.y, z) - wire.position(wire.count-1);
   float length = ::length(relativePosition);
   if(length >= Wire::internodeLength) {
    assert_(wire.count < wire.capacity);
    vec3 p = wire.position(wire.count-1) + Wire::internodeLength * relativePosition/length;
    size_t i = wire.count++;
    wire.Px[i] = p[0]; wire.Py[i] = p[1]; wire.Pz[i] = p[2];
    wire.Vx[i] = 0; wire.Vy[i] = 0; wire.Vz[i] = 0;
    // Forces verlet lists reevaluation
    grainWireGlobalMinD = 0;
   }
  }

  // Generates grain
  if(currentHeight >= Grain::radius) {
   vec2 p(random()*2-1,random()*2-1);
   if(length(p)<1) { // Within cylinder
    vec3 newPosition (patternRadius*p.x, patternRadius*p.y, Grain::radius);
    processTime.start();
    // Deposits grain without grain overlap
    for(size_t index: range(grain.count)) {
     vec3 other = grain.position(index);
     float Dxy = length((other - newPosition).xy());
     if(Dxy < 2*Grain::radius) {
      float dz = sqrt(sq(2*Grain::radius) - sq(Dxy));
      newPosition.z = ::max(newPosition.z, other.z+dz);
     }
    }
    processTime.stop();
    // Under current wire drop height
    if(newPosition.z < currentHeight) {
     // Without wire overlap
     processTime.start();
     for(size_t index: range(wire.count))
      if(length(wire.position(index) - newPosition) < Grain::radius+Wire::radius)
      { processTime.stop(); return; }
     processTime.stop();
     size_t i = grain.count;
     grain.Px[simd+i] = newPosition.x; grain.Py[i+simd] = newPosition.y; grain.Pz[simd+i] = newPosition.z;
     grain.Vx[i] = 0; grain.Vy[i] = 0; grain.Vz[i] = - 1 * m/s;
     grain.AVx[i] = 0; grain.AVy[i] = 0; grain.AVz[i] = 0;
     float t0 = 2*PI*random();
     float t1 = acos(1-2*random());
     float t2 = (PI*random()+acos(random()))/2;
     grain.Rx[i] = sin(t0)*sin(t1)*sin(t2);
     grain.Ry[i] = cos(t0)*sin(t1)*sin(t2);
     grain.Rz[i] = cos(t1)*sin(t2);
     grain.Rw[i] = cos(t2);
     grain.count++;
     // Forces verlet lists reevaluation
     grainGrainGlobalMinD = 0;
     grainWireGlobalMinD = 0;
     grainMembraneGlobalMinD = 0;
    }
   }
  }
 }
}
