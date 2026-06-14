#pragma once
#include "RenderableProxy.h"

#include <RenderBase/TextureProxy.h>

namespace hyp
{
    class SkyLightProxy : public RenderableProxy
    {
    public:
        SkyLightProxy();

        ~SkyLightProxy();

    private:
        TextureProxy* mSpecularPrefilter;
    };
}
