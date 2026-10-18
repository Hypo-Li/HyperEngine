#include "SkyBackground.h"
#include "../../Context.h"
#include "../../Framework/Scene.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/SceneProxy.h>
#include <Renderer/SkyBackgroundProxy.h>

namespace hyp
{
    REGISTER_CLASS(SkyBackground)

    SkyBackground::SkyBackground()
    {
    }

    SkyBackground::~SkyBackground() noexcept
    {
    }

    void SkyBackground::onRegister()
    {
        mIntensity = std::max(mIntensity, 0.0f);

        MaterialProxyHandle materialHandle;
        if (mMaterial)
            materialHandle = mMaterial->getOrCreateMaterialProxy();

        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();

        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Sky_Background>();
        renderFrontend.enqueueLambda(
            [sceneProxyHandle,
            proxyHandle = mProxyHandle,
            materialHandle,
            intensity = mIntensity,
            visible = getEntity()->isVisible()](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                proxyManager.createProxy(proxyHandle, *sceneProxy, SkyBackgroundProxy::Desc{
                    .visible = visible,
                    .material = materialHandle.isValid() ? proxyManager.getProxy(materialHandle) : nullptr,
                    .intensity = intensity
                });
            }
        );
    }

    void SkyBackground::onUnregister()
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle](FrameContext& context)
            {
                context.getProxyManager().destroyProxy(proxyHandle);
            }
        );

        renderFrontend.freeProxyHandle(mProxyHandle);
        mProxyHandle.reset();
    }

    void SkyBackground::onVisibilityChanged(bool visible)
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, visible](FrameContext& context)
            {
                context.getProxyManager().getProxy(proxyHandle)->setVisible(visible);
            }
        );
    }

    void SkyBackground::setMaterial(Material* material)
    {
        if (mMaterial == material)
            return;
        mMaterial = material;

        if (!mProxyHandle.isValid())
            return;

        MaterialProxyHandle materialHandle;
        if (material)
            materialHandle = material->getOrCreateMaterialProxy();

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, materialHandle](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                proxyManager.getProxy(proxyHandle)->setMaterial(
                    materialHandle.isValid() ? proxyManager.getProxy(materialHandle) : nullptr
                );
            }
        );
    }

    void SkyBackground::setIntensity(float intensity)
    {
        intensity = std::max(intensity, 0.0f);
        if (mIntensity == intensity)
            return;
        mIntensity = intensity;

        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, intensity](FrameContext& context)
            {
                context.getProxyManager().getProxy(proxyHandle)->setIntensity(intensity);
            }
        );
    }

    template <> Type* Reflection::createType<SkyBackground>()
    {
        auto c = new TClass<SkyBackground, Component>("SkyBackground");
        c->setDisplayName("Sky Background");
        c->addProperty("Material", &SkyBackground::mMaterial);
        c->addProperty("Intensity", &SkyBackground::mIntensity);
        return c;
    }
}
