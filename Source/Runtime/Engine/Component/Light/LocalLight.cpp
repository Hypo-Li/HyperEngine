#include "LocalLight.h"

namespace hyp
{
    REGISTER_CLASS(LocalLight)

    void LocalLight::setAttenuationRadius(float attenuationRadius)
    {
        mAttenuationRadius = attenuationRadius;
    }

    void LocalLight::setSourceRadius(float sourceRadius)
    {
        mSourceRadius = sourceRadius;
    }

    template <> Type* Reflection::createType<LocalLight>()
    {
        auto c = new TClass<LocalLight, Light>("LocalLight");
        c->setDisplayName("Local Light");
        c->addProperty("Attenuation Radius", &LocalLight::mAttenuationRadius);
        c->addProperty("Source Radius", &LocalLight::mSourceRadius);
        return c;
    }
}
