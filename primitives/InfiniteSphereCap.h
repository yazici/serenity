#pragma once
#include "Primitive.h"

class InfiniteSphereCap : public Primitive
{
    const Scene *_scene;
    std::string _domeName;

    bool _doSample;
    float _capAngleDeg;

    Vec3f _capDir;
    TangentFrame _capFrame;
    float _capAngleRad;
    float _cosCapAngle;

    std::shared_ptr<TriangleMesh> _proxy;

    Box3f _sceneBounds;

    void buildProxy();

protected:
    virtual float powerToRadianceFactor() const override;

public:
    InfiniteSphereCap();

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;

    virtual bool intersect(Ray &ray, IntersectionTemporary &data) const override;
    virtual bool occluded(const Ray &ray) const override;
    virtual bool hitBackside(const IntersectionTemporary &data) const override;
    virtual void intersectionInfo(const IntersectionTemporary &data, IntersectionInfo &info) const override;
    virtual bool tangentSpace(const IntersectionTemporary &data, const IntersectionInfo &info,
            Vec3f &T, Vec3f &B) const override;

    virtual bool isSamplable() const override;
    virtual void makeSamplable(const TraceableScene &scene, uint32 threadIndex) override;

    virtual bool samplePosition(PathSampleGenerator &sampler, PositionSample &sample) const override;
    virtual bool sampleDirection(PathSampleGenerator &sampler, const PositionSample &point, DirectionSample &sample) const override;
    virtual bool sampleDirect(uint32 threadIndex, const Vec3f &p, PathSampleGenerator &sampler, LightSample &sample) const override;
    virtual float positionalPdf(const PositionSample &point) const override;
    virtual float directionalPdf(const PositionSample &point, const DirectionSample &sample) const override;
    virtual float directPdf(uint32 threadIndex, const IntersectionTemporary &data,
            const IntersectionInfo &info, const Vec3f &p) const override;
    virtual Vec3f evalPositionalEmission(const PositionSample &sample) const override;
    virtual Vec3f evalDirectionalEmission(const PositionSample &point, const DirectionSample &sample) const override;
    virtual Vec3f evalDirect(const IntersectionTemporary &data, const IntersectionInfo &info) const override;

    virtual bool invertParametrization(Vec2f uv, Vec3f &pos) const override;

    virtual bool isDirac() const override;
    virtual bool isInfinite() const override;

    virtual float approximateRadiance(uint32 threadIndex, const Vec3f &p) const override;
    virtual Box3f bounds() const override;

    virtual const TriangleMesh &asTriangleMesh() override;

    virtual void prepareForRender() override;

    virtual int numBsdfs() const override;
    virtual std::shared_ptr<Bsdf> &bsdf(int index) override;
    virtual void setBsdf(int index, std::shared_ptr<Bsdf> &bsdf) override;
        
    Vec3f lightDirection() const
    {
        return _transform.transformVector(Vec3f(0.0f, 1.0f, 0.0f)).normalized();
    }
    
    float capAngleDeg() const
    {
        return _capAngleDeg;
    }

    virtual Primitive *clone() override;
};
