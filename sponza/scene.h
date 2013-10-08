#pragma once
#include "gl.h"

struct Vertex {
    Vertex(vec3 position, vec2 texCoords, vec3 normal):position(position),texCoords(texCoords),normal(normal){}
    vec3 position;
    vec2 texCoords;
    vec3 normal;
};

struct Material : shareable {
    Material(const string& name):name(name){}
    String name;
    String diffusePath;//, maskPath, normalPath;
    GLTexture diffuseTexture; // + transparency
    //GLTexture normal; // + displacement
};

struct ptni { int p,t,n,i; };
struct Surface {
    Surface(const string& name):name(name){}
    String name;
    shared<Material> material = 0;
    array<Vertex> vertices;
    array<uint> indices;
    GLVertexBuffer vertexBuffer;
    GLIndexBuffer indexBuffer;
};

struct Scene {
    Scene();

    array<Surface> surfaces;
    vec3 worldMin=0, worldMax=0; // Scene bounding box in world space
};
