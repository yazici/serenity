#pragma once
#include "Camera.h"

struct Scene;

class PinholeCamera : public Camera
{
    float _fovDeg;
    float _fovRad;
    float _planeDist;
    float _invPlaneArea;

    void precompute();

public:
    PinholeCamera();
    PinholeCamera(const Mat4f &transform, const Vec2u &res, float fov);

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;

    virtual bool samplePosition(PathSampleGenerator &sampler, PositionSample &sample, Vec2u pixel) const override final;
    virtual float directionPdf(const PositionSample &point, const DirectionSample &direction) const override final;

    virtual bool isDirac() const override;

    virtual float approximateFov() const override
    {
        return _fovRad;
    }

    float fovDeg() const
    {
        return _fovDeg;
    }
};
