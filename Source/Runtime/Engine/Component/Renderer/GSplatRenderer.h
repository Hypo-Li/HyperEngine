#pragma once
#include "../../Framework/Component.h"
#include "../../Framework/Transform.h"
#include "../../GSplat/GSplat.h"

#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    class GSplatRenderer : public Component, public ITransformListener
    {
        REFLECT_CLASS(GSplatRenderer)
    public:
        GSplatRenderer() = default;

        ~GSplatRenderer() noexcept = default;

        void onRegister() override;

        void onUnregister() override;

        void onVisibilityChanged(bool visible) override;

        void onTick(float deltaTime) override;

        void onTransformUpdate(const Transform& transform) override;

        void setGSplat(GSplat* gsplat);

        GSplat* getGSplat() const noexcept
        {
            return mGSplat;
        }

    private:
        TRef<GSplat> mGSplat;
        GSplatRenderProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<GSplatRenderer>();
}
