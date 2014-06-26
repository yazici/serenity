#pragma once
#include "matrix.h"
#include "image.h"

// Projection settings
struct Projection {
    // Resolution
    int3 volumeSize;
    int3 projectionSize;
    uint count = projectionSize.z;
    // Parameters
    float detectorHalfWidth = 1;
    float cameraLength = 64;
    float specimenDistance = 1./16;
    bool doubleHelix;
    uint numberOfRotations;
    float photonCount; // Photon count per pixel for a blank scan (without attenuation) of same duration (0: no noise)

    // Projection setup (coordinates in view space)
    const float volumeAspectRatio = float(volumeSize.z/*-1*/) / float(volumeSize.x/*-1*/);
    const float projectionAspectRatio = float(projectionSize.y/*-1*/) / float(projectionSize.x/*-1*/);
    const float detectorHalfHeight = projectionAspectRatio * detectorHalfWidth; //image_height * pixel_size; // [mm] ~ 397 mm
    const float volumeRadius = detectorHalfWidth / sqrt(sq(detectorHalfWidth)+sq(cameraLength)) * specimenDistance;
    const float zExtent = (specimenDistance - volumeRadius) / cameraLength * detectorHalfHeight; // Fits cap tangent intersection to detector top edge
    const float deltaZ = volumeAspectRatio - zExtent/volumeRadius;
    const float distance = specimenDistance/volumeRadius; // Distance in world space
    const float extent = 2/sqrt(1-1/sq(distance)); // Projection of the tangent intersection point on the origin plane (i.e projection of the detector extent on the origin plane)

    Projection(int3 volumeSize, int3 projectionSize, const bool doubleHelix, const uint numberOfRotations, const float photonCount = 0) : volumeSize(volumeSize), projectionSize(projectionSize), doubleHelix(doubleHelix), numberOfRotations(numberOfRotations), photonCount(photonCount) {}

    float angle(uint index) const { return doubleHelix ? 2*PI*numberOfRotations*float(index%(count/2))/((count)/2) + (index/(count/2)?PI:0) : 2*PI*numberOfRotations*float(index)/count; } // Rotation angle (in radians) around vertical axis

    // Transforms from world coordinates [±size/2] to view coordinates (only rotation and translation)
    mat4 worldToView(uint index) const;
    // Transforms from world coordinates to view coordinates (scaled to [±size/2]) (FIXME)
    mat4 worldToScaledView(uint index) const;
    // Transforms from world coordinates [±size] to device coordinates [±size/2]
    mat4 worldToDevice(uint index) const; };

inline mat4 Projection::worldToView(uint index) const {
    const float dz = count > 1 ? (doubleHelix ? float(index%(count/2))/float((count-1)/2) : float(index)/float(count-1)) : 0;
    const float z = -volumeAspectRatio + zExtent/volumeRadius + 2*dz*deltaZ; // Z position in world space
    return mat4().rotateZ(PI/2).rotateY(-PI/2).translate(vec3(distance,0,z)*((volumeSize.x-1)/2.f)).rotateZ(angle(index));
}

inline mat4 Projection::worldToScaledView(uint index) const {
    return mat4().scale(vec3(vec2(float(projectionSize.x-1)/extent),1/distance)).scale(vec3(1.f/((volumeSize.x-1)/2.f))) * worldToView(index);
}

inline mat4 Projection::worldToDevice(uint index) const {
    mat4 projectionMatrix; projectionMatrix(3,2) = 1;  projectionMatrix(3,3) = 0; // copies Z to W (FIXME: move scale(vec3(vec2(float(projectionSize.x-1)/extent),1/distance)) from worldToView to projectionMatrix
    return projectionMatrix * worldToScaledView(index);
}

inline String str(const Projection& A) { return strx(A.volumeSize)+"."_+(A.doubleHelix?"double"_:"simple"_)+"."_+str(A.numberOfRotations); }
