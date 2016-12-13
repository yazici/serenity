#include "TransparencyBsdf.h"
#include "LambertBsdf.h"

#include "samplerecords/SurfaceScatterEvent.h"

#include "materials/ConstantTexture.h"

#include "io/JsonObject.h"
#include "io/Scene.h"

namespace Tungsten {

TransparencyBsdf::TransparencyBsdf()
: _opacity(std::make_shared<ConstantTexture>(1.0f)),
  _base(std::make_shared<LambertBsdf>())
{
}

TransparencyBsdf::TransparencyBsdf(std::shared_ptr<Texture> opacity, std::shared_ptr<Bsdf> base)
: _opacity(opacity),
  _base(base)
{
}

void TransparencyBsdf::fromJson(const rapidjson::Value &v, const Scene &scene)
{
    Bsdf::fromJson(v, scene);
    _base = scene.fetchBsdf(JsonUtils::fetchMember(v, "base"));

    scene.textureFromJsonMember(v, "alpha", TexelConversion::REQUEST_AUTO, _opacity);
}

rapidjson::Value TransparencyBsdf::toJson(Allocator &allocator) const
{
    return JsonObject{Bsdf::toJson(allocator), allocator,
        "type", "transparency",
        "base", *_base,
        "alpha", *_opacity
    };
}

bool TransparencyBsdf::sample(SurfaceScatterEvent &event) const
{
    return _base->sample(event);
}

Vec3f TransparencyBsdf::eval(const SurfaceScatterEvent &event) const
{
    if (event.requestedLobe.isForward())
        return (-event.wi == event.wo) ? Vec3f(1.0f - (*_opacity)[*event.info].x()) : Vec3f(0.0f);
    else
        return _base->eval(event);
}

float TransparencyBsdf::pdf(const SurfaceScatterEvent &event) const
{
    return _base->pdf(event);
}

void TransparencyBsdf::prepareForRender()
{
    _lobes = BsdfLobes(BsdfLobes::ForwardLobe, _base->lobes());
}

}
