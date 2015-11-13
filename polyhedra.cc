#include "window.h"
#include "matrix.h"

struct Face {
 size_t a, b, c;
};
struct Edge {
 size_t a, b;
};

// Convex polyhedra
struct Polyhedra {
 vec3 position;
 v4sf rotation;
 buffer<vec3> local; // Vertex positions in local frame
 buffer<vec4> planes; // normal, distance
 buffer<Edge> edges;
 buffer<vec3> global; // Vertex positions in global frame
 float radius;
 static constexpr float R = 1./16;
};
constexpr float Polyhedra::R;

struct Contact {
 size_t a, b;
 vec3 A, B;
 vec3 N;
 float depth;
 explicit operator bool() { return depth < inf; }
};

// Point - Plane
Contact vertexFace(const Polyhedra& A, const Polyhedra& B) {
 for(size_t vertexIndex: range(A.global.size)) {
  vec3 a = toVec3(qapply(conjugate(B.rotation), A.global[vertexIndex] - B.position)); // Transforms vertex to B local frame
  // Clips against all planes (convex polyhedra)
  for(vec4 plane: B.planes) {
   if(dot(plane.xyz(), a) <= plane.w + A.R + B.R) continue; // Inside
   goto break_;
  } /*else*/ { // P inside polyhedra B
   Contact contact {0, 0, {}, {}, {}, inf};
   for(vec4 plane: B.planes) {
    // Projects vertex on plane (B frame)
    float depth = plane.w + A.R + B.R - dot(plane.xyz(), a);
    if(depth < contact.depth) {
     contact.N = plane.xyz();
     assert_(depth > 0);
     contact.depth = depth;
     contact.A = A.local[vertexIndex];
     contact.B = a + depth * plane.xyz(); //B.position + toVec3(qapply(B.rotation, a + depth * plane.xyz()));
    }
   }
   return contact;
  }
  break_:;
 }
 return {0, 0, {}, {}, {}, inf};
}

// Cylinder - Cylinder
Contact edgeEdge(const Polyhedra& A, const Polyhedra& B) {
 for(Edge eA: A.edges) {
  vec3 A1 = A.global[eA.a], A2 = A.global[eA.b];
  for(Edge eB: B.edges) {
   vec3 B1 = B.global[eB.a], B2 = B.global[eB.b];
   v4sf gA, gB;
   closest(A1, A2, B1, B2, gA, gB);
   assert_(isNumber(A1) && isNumber(A2) && isNumber(B1) && isNumber(B2) && isNumber(gA) && isNumber(gB), A1, A2, B1, B2);
#if 0
   vec3 a = toVec3(qapply(conjugate(B.rotation), gA - B.position)); // Transforms global position gA to B local frame
   for(vec4 plane: B.planes) {
    if(dot(plane.xyz(), toVec3(a)) <= plane.w + A.R + B.R) continue; // Inside
    goto break_;
   } /*else*/ { // a inside polyhedra B
    vec3 b = toVec3(qapply(conjugate(A.rotation), gB - A.position)); // Transforms global position gB to A local frame
    for(vec4 plane: A.planes) {
     if(dot(plane.xyz(), toVec3(b)) <= plane.w + A.R + B.R) continue; // Inside
     //error(gA, gB, dot(plane.xyz(), toVec3(b)), plane.w);
    } /*else*/ { // b inside polyhedra A
     vec3 r = toVec3(gB-gA);
     float L = ::length(r) + A.R + B.R;
     return {0,0, toVec3(b), toVec3(a), r/L, L};
    }
   }
   break_:;
#else
   vec3 r = toVec3(gB-gA);
   float L = ::length(r);
   if(A.R + B.R - L > 0) {
    assert_(L > 0, L, gA, gB);
    vec3 a = toVec3(qapply(conjugate(B.rotation), gA - B.position)); // Transforms global position gA to B local frame
    vec3 b = toVec3(qapply(conjugate(A.rotation), gB - A.position)); // Transforms global position gB to A local frame
    assert_(isNumber(r/L));
    return {0,0, toVec3(b), toVec3(a), r/L,  A.R + B.R - L};
   }
#endif
  }
 }
 return {0, 0, {}, {}, {}, inf};
}

Contact contact(const Polyhedra& A, const Polyhedra& B) {
 return edgeEdge(A, B) ?: vertexFace(A, B) ?: vertexFace(B, A);
}

struct PolyhedraSimulation {
 Lock lock;
 array<Polyhedra> polyhedras;
 buffer<vec3> velocity, angularVelocity;
 const float dt = 1./50000;
 const float damping = 1;
 const float density = 1;
 const float E = 100; //1000
 const float R = 1;
 const float volume = 1; // FIXME
 const float angularMass = 1; // FIXME
 const vec3 g {0,0,-10};
 const float d = 4, D = 5;
 const float frictionCoefficient = 1;//0.1;

 size_t t = 0;
 array<Contact> contacts, contacts2;
 float maxF = 0;

 PolyhedraSimulation() {
  Random random;
  while(polyhedras.size < 8) {
   vec3 position(d*random(), d*random(), 1+d*random());
   const float r = 1./sqrt(3.);
   ref<vec3> vertices{vec3(r,r,r), vec3(r,-r,-r), vec3(-r,r,-r), vec3(-r,-r,r)};
   float radius = 1;
   for(auto& p: polyhedras) {
    if(length(p.position - position) <= p.radius + radius)
     goto break_;
   } /*else*/ {
    float t0 = 2*PI*random();
    float t1 = acos(1-2*random());
    float t2 = (PI*random()+acos(random()))/2;
    ref<Face> faces{{0,1,2}, {0,2,3}, {0,3,1}, {1,3,2}};
    buffer<vec4> planes (faces.size);
    for(size_t i: range(planes.size)) {
     Face f = faces[i];
     vec3 N = (vertices[f.a]+vertices[f.b]+vertices[f.c])/3.f;
     float l = length(N);
     planes[i] = vec4(N/l, l);
    }
    Polyhedra p{
     position,
     {sin(t0)*sin(t1)*sin(t2),  cos(t0)*sin(t1)*sin(t2), cos(t1)*sin(t2), cos(t2)},
       copyRef(vertices),
       move(planes),
       copyRef(ref<Edge>{{0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}}),
     buffer<vec3>(4), radius
    };
    polyhedras.append(move(p));
   }
   break_:;
  }
  velocity = buffer<vec3>(polyhedras.size);
  velocity.clear(0);
  angularVelocity = buffer<vec3>(polyhedras.size);
  angularVelocity.clear(0);
 }
 bool step() {
  //Locker lock(this->lock);
  buffer<vec3> force (polyhedras.size), torque (polyhedras.size);
  for(size_t a: range(polyhedras.size)) {
   Polyhedra& p = polyhedras[a];
   force[a] = g * density * volume;
   torque[a] = 0;
   vec3 N(0,0,1);
   for(size_t vertexIndex: range(polyhedras[a].local.size)) {
    vec3 A = p.local[vertexIndex];
    vec3 rA = toVec3(qapply(p.rotation, A));
    vec3 gA = p.global[vertexIndex] = toVec3(p.position + rA);
    float depth = 0 - gA.z;
    if(depth > 0) {
     vec3 v = velocity[a] + cross(angularVelocity[a], rA);
     float fN = 4.f/3 * E * sqrt(R) * sqrt(depth) * (depth - damping * dot(N, v));
     vec3 tV = v - dot(N, v) * N;
     vec3 fT = - frictionCoefficient * fN * tV;
     vec3 f = fN * N + fT;
     if(!isNumber(f)) {
      log("FAIL Floor", E, R, depth, damping, N, velocity[a], f);
      return false;
     }
     assert_(isNumber(f), "Floor", E, R, depth, damping, N, velocity[a], f);
     force[a] += f;
     torque[a] += cross(rA, f);
    }
   }
  }
  if(1) for(size_t a: range(polyhedras.size)) {
   Polyhedra& A = polyhedras[a];
   for(size_t b: range(polyhedras.size)) {
    if(a==b) continue;
    Polyhedra& B = polyhedras[b];
    Contact contact = ::contact(A, B);
    if(contact) {
     assert_(isNumber(contact.N));
     vec3 N = contact.N; float depth = contact.depth;
     assert_(depth > 0, depth);
     vec3 f = 4.f/3 * E * sqrt(R) * sqrt(depth) * (depth - damping * dot(N, velocity[a])) * N;
     assert_(isNumber(f), "contact", E, R, depth, damping, N, velocity[a]);
     //if(length(f) > maxF) { maxF=length(f); log(t, f, length(f)); }
     if(length(f) > 338) { log("STOP", length(f), f); return false; }
     if(contact.depth > (A.R+B.R)/2) { log("Overlap", contact.depth); return false; }
     force[a] += f;
     torque[a] += cross(toVec3(qapply(conjugate(A.rotation), contact.A)), f);
     contact.a = a, contact.b = b;
     contacts.append(contact);
    }
   }
  }
  for(size_t a: range(polyhedras.size)) {
   assert_(isNumber(force[a]));
   velocity[a] += dt * force[a];
   vec3 dx = dt * velocity[a];
   if(length(dx) > 1) { log("STOP1", dx, length(dx), velocity[a], force[a]); return false; }
   polyhedras[a].position += dx;
   //if(length(polyhedras[a].position) > D) { log("STOP2", polyhedras[a].position, length(polyhedras[a].position)); return false; }
   assert_(isNumber(polyhedras[a].position));

   polyhedras[a].rotation += float4(dt/2.f) * qmul(angularVelocity[a], polyhedras[a].rotation);
   angularVelocity[a] += dt / angularMass * torque[a];
   polyhedras[a].rotation *= rsqrt(sq4(polyhedras[a].rotation));

  }
  {Locker lock(this->lock);
   contacts2 = ::move(contacts);
  }
  t++;
  return true;
 }
};


struct PolyhedraView : PolyhedraSimulation, Widget {
 struct State {
  buffer<vec3> position;
  buffer<v4sf> rotation;
  buffer<Contact> contacts;
 };
 array<State> states;
 size_t viewT = 0;
 bool running = true;

 vec2 lastPos; // for relative cursor movements
 vec2 viewYawPitch = vec2(0, PI/3); // Current view angles

 // Orbital ("turntable") view control
 bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button,
                                Widget*&) override {
  vec2 delta = cursor-lastPos; lastPos=cursor;
  if(event==Motion && button==LeftButton) {
   viewYawPitch += float(2*PI) * delta / size; //TODO: warp
   viewYawPitch.y = clamp<float>(0, viewYawPitch.y, PI);
  }
  else if(event==Motion && button==RightButton) {
   if(states) viewT = clamp<int>(0, int(viewT) + (states.size-1) * delta.x / (size.x-1), states.size-1); // Relative
   running = false;
  }
  else return false;
  return true;
 }

 vec2 sizeHint(vec2) override { return 768; }
 shared<Graphics> graphics(vec2 size) override {
  //Locker lock(this->lock);
  shared<Graphics> graphics;

  vec3 center = 0;
  v4sf viewRotation = qmul(angleVector(viewYawPitch.y, vec3(1,0,0)),
                                             angleVector(viewYawPitch.x, vec3(0,0,1)));

  // Transforms vertices and evaluates scene bounds
  vec3 min = -D, max = D;
  for(const Polyhedra& p: polyhedras) {
   for(vec3 a: p.global) {
    vec3 A = toVec3(qapply(viewRotation, a - center));
    min = ::min(min, A);
    max = ::max(max, A);
   }
  }

  vec2 scale = size/(max-min).xy();
  scale.x = scale.y = ::min(scale.x, scale.y);
  vec2 offset = - scale * min.xy();

  {vec2 P1 = offset + scale * toVec3(qapply(viewRotation, vec3(-d, -d, 0))).xy();
   vec2 P2 = offset + scale * toVec3(qapply(viewRotation, vec3(d, -d, 0))).xy();
   assert_(isNumber(P1) && isNumber(P2));
   graphics->lines.append(P1, P2);}

  {vec2 P1 = offset + scale * toVec3(qapply(viewRotation, vec3(d, -d, 0))).xy();
   vec2 P2 = offset + scale * toVec3(qapply(viewRotation, vec3(d, d, 0))).xy();
   assert_(isNumber(P1) && isNumber(P2));
   graphics->lines.append(P1, P2);}

  {vec2 P1 = offset + scale * toVec3(qapply(viewRotation, vec3(d, d, 0))).xy();
   vec2 P2 = offset + scale * toVec3(qapply(viewRotation, vec3(-d, d, 0))).xy();
   assert_(isNumber(P1) && isNumber(P2));
   graphics->lines.append(P1, P2);}

  {vec2 P1 = offset + scale * toVec3(qapply(viewRotation, vec3(-d, d, 0))).xy();
   vec2 P2 = offset + scale * toVec3(qapply(viewRotation, vec3(-d, -d, 0))).xy();
   assert_(isNumber(P1) && isNumber(P2));
   graphics->lines.append(P1, P2);}

  {vec2 P1 = offset + scale * toVec3(qapply(viewRotation, vec3(0, 0, 0))).xy();
   vec2 P2 = offset + scale * toVec3(qapply(viewRotation, vec3(0, 0, d))).xy();
   assert_(isNumber(P1) && isNumber(P2));
   graphics->lines.append(P1, P2);}

  {State state;
   state.position = buffer<vec3>(polyhedras.size);
   state.rotation = buffer<v4sf>(polyhedras.size);
   for(size_t p: range(polyhedras.size)) {
    state.position[p] = polyhedras[p].position;
    state.rotation[p] = polyhedras[p].rotation;
    //assert_(viewT < trajectories[p].size, viewT, trajectories[p].size);
   }
   state.contacts = ::move(contacts2);
   states.append(move(state));
  }
  /*for(const auto& t: trajectories) {
   for(size_t i: range(t.size-1)) {
    {vec2 P1 = offset + scale * toVec3(qapply(viewRotation, t[i])).xy();
     vec2 P2 = offset + scale * toVec3(qapply(viewRotation, t[i+1])).xy();
     assert_(isNumber(P1) && isNumber(P2));
     graphics->lines.append(P1, P2);}
   }
  }*/

  //for(const Polyhedra& A: polyhedras) {
  for(size_t p: range(polyhedras.size)) {
   const Polyhedra& A = polyhedras[p];
   vec3 position = states[viewT].position[p];
   v4sf rotation = states[viewT].rotation[p];
   for(Edge eA: A.edges) {
    vec3 A1 = position + toVec3(qapply(rotation, A.local[eA.a]));
    vec3 A2 = position + toVec3(qapply(rotation, A.local[eA.b]));
    {
     vec2 P1 = offset + scale * toVec3(qapply(viewRotation, A1 - center)).xy();
     vec2 P2 = offset + scale * toVec3(qapply(viewRotation, A2 - center)).xy();
     assert_(isNumber(P1) && isNumber(P2));
     graphics->lines.append(P1, P2);
    }
   }
   if(0) for(Edge eA: A.edges) {
    vec3 A1 = A.global[eA.a], A2 = A.global[eA.b];
    {
     vec2 P1 = offset + scale * toVec3(qapply(viewRotation, A1 - center)).xy();
     vec2 P2 = offset + scale * toVec3(qapply(viewRotation, A2 - center)).xy();
     assert_(isNumber(P1) && isNumber(P2));
     graphics->lines.append(P1, P2);
    }
    if(0) for(const Polyhedra& B: polyhedras) {
     if(&A == &B) continue;
     for(Edge eB: B.edges) {
      vec3 B1 = B.global[eB.a], B2 = B.global[eB.b];
      v4sf gA, gB;
      closest(A1, A2, B1, B2, gA, gB);
      vec2 P1 = offset + scale * toVec3(qapply(viewRotation, gA - center)).xy();
      vec2 P2 = offset + scale * toVec3(qapply(viewRotation, gB - center)).xy();
      assert_(isNumber(P1) && isNumber(P2));
      graphics->lines.append(P1, P2);
      break;
     }
    }
   }
  }
  {Locker lock(this->lock);
   for(const Contact& contact: states[viewT].contacts) {
    vec2 A = offset + scale * toVec3(qapply(viewRotation, polyhedras[contact.a].position + qapply(polyhedras[contact.a].rotation, contact.A) - center)).xy();
    vec2 B = offset + scale * toVec3(qapply(viewRotation, polyhedras[contact.b].position + qapply(polyhedras[contact.b].rotation, contact.B) - center)).xy();
    assert_(isNumber(A) && isNumber(B));
    graphics->lines.append(A, B);
   }
  }

  if(running) viewT++;
  return graphics;
 }
};

struct PolyhedraApp : PolyhedraView, Poll {
 unique<Window> window = ::window(this);
 Thread physicThread;
 PolyhedraApp() : Poll(0,0,physicThread) {
  step();
  window->presentComplete = [this]{ window->render(); };
  physicThread.spawn();
  queue();
 }
 void event() { if(running && step()) queue(); else running=false; }
} polyhedra;
