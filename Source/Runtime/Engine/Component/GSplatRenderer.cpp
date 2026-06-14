#include "GSplatRenderer.h"
#include "../Framework/Scene.h"
#include "../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/SceneProxy.h>
#include <Renderer/GSplatRenderProxy.h>

namespace hyp
{
    REGISTER_CLASS(GSplatRenderer)

    static uint32_t packColorToUint32(std::array<uint8_t, 4> color)
    {
        return static_cast<uint32_t>(color[0]) |
            (static_cast<uint32_t>(color[1]) << 8) |
            (static_cast<uint32_t>(color[2]) << 16) |
            (static_cast<uint32_t>(color[3]) << 24);
    }

    static uint32_t floatToUint32(float f)
    {
        union
        {
            float f;
            uint32_t u;
        } v;
        v.f = f;
        return v.u;
    }

    static uint16_t floatToHalf(float f)
    {
        uint32_t bits = floatToUint32(f);

        // 提取符号位（位31）
        uint32_t sign = (bits >> 31) & 0x0001;
        // 提取指数部分（位23-30）
        uint32_t exp = (bits >> 23) & 0x00FF;
        // 提取尾数部分（位0-22）
        uint32_t frac = bits & 0x007FFFFF;

        uint32_t newExp;

        // 处理指数为0的情况（非规格化数或0）
        if (exp == 0) {
            newExp = 0;
        }
        // 处理指数 < 113 (即原始指数 < -14)
        else if (exp < 113) {
            newExp = 0;
            // 添加隐式的1（规格化数的隐藏位）
            frac |= 0x00800000;
            // 右移调整以补偿小指数
            frac = frac >> (113 - exp);

            // 检查进位（如果第24位被设置）
            if (frac & 0x01000000) {
                newExp = 1;
                frac = 0;
            }
        }
        // 处理指数在113-141范围内（即指数在-14到15之间）
        else if (exp < 142) {
            // 调整指数偏移（FP32偏移127 → FP16偏移15）
            newExp = exp - 112;
        }
        // 处理指数 >= 142 (上溢到无穷)
        else {
            newExp = 31; // FP16最大指数值
            frac = 0;    // 尾数清零（无穷大）
        }

        // 组合结果：符号位 | 指数 | 尾数
        return static_cast<uint16_t>(
            (sign << 15) | (newExp << 10) | (frac >> 13)
        );
    }

    static uint32_t pack2HalfToUint32(float x, float y)
    {
        return static_cast<uint32_t>(floatToHalf(x)) |
            (static_cast<uint32_t>(floatToHalf(y)) << 16);
    }

    static GPUSplatData splatDataToGPUSplatData(const SplatData& splat)
    {
        Quaternion quat;
        quat.w = (splat.rotation[0] / 255.0f) * 2.0f - 1.0f;
        quat.x = (splat.rotation[1] / 255.0f) * 2.0f - 1.0f;
        quat.y = (splat.rotation[2] / 255.0f) * 2.0f - 1.0f;
        quat.z = (splat.rotation[3] / 255.0f) * 2.0f - 1.0f;

        float3x3 m = glm::mat3_cast(quat);
        m[0] *= splat.scale[0];
        m[1] *= splat.scale[1];
        m[2] *= splat.scale[2];
        float sigma[] = {
            m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0],   // σ₀₀
            m[0][0] * m[0][1] + m[1][0] * m[1][1] + m[2][0] * m[2][1],   // σ₀₁
            m[0][0] * m[0][2] + m[1][0] * m[1][2] + m[2][0] * m[2][2],   // σ₀₂
            m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1],   // σ₁₁
            m[0][1] * m[0][2] + m[1][1] * m[1][2] + m[2][1] * m[2][2],   // σ₁₂
            m[0][2] * m[0][2] + m[1][2] * m[1][2] + m[2][2] * m[2][2]    // σ₂₂
        };

        return {
            splat.position,
            0u,
            uint3{
                pack2HalfToUint32(sigma[0], sigma[1]),
                pack2HalfToUint32(sigma[2], sigma[3]),
                pack2HalfToUint32(sigma[4], sigma[5])
            },
            packColorToUint32(splat.color),
        };
    }

    void GSplatRenderer::onRegister()
    {
        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();

        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::GSplat_Render>();

        std::vector<GPUSplatData> gpuSplatData;
        if (mGSplat)
        {
            std::span<const SplatData> splatData = mGSplat->getSplatData();
            gpuSplatData.reserve(splatData.size());
            for (const auto& splat : splatData)
            {
                gpuSplatData.emplace_back(splatDataToGPUSplatData(splat));
            }
        }

        auto& transform = getEntity()->getTransform();
        renderFrontend.enqueueLambda(
            [sceneProxyHandle,
            proxyHandle = mProxyHandle,
            gpuSplatData = std::move(gpuSplatData),
            worldMatrix = transform.getWorldMatrix()](RenderFrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                GSplatRenderProxy* proxy = proxyManager.createProxy(proxyHandle);
                sceneProxy->addGSplatRenderProxy(proxy);
                if (!gpuSplatData.empty())
                {
                    sceneProxy->setGSplatRenderProxySplatData(proxy, std::move(gpuSplatData));
                }

                uint32_t instanceIndex = proxy->getInstanceIndex();
                auto& instanceData = sceneProxy->getInstanceData(instanceIndex);
                instanceData.localToWorld.fromDouble4x4(worldMatrix);
                instanceData.localToWorldRot = float4x4(worldMatrix);
                sceneProxy->notifyInstanceDataUpdated(instanceIndex);
            }
        );

        scene->registerTransformListener(&transform, this);
    }

    void GSplatRenderer::onUnregister()
    {
        Scene* scene = getScene();
        scene->unregisterTransformListener(&getEntity()->getTransform(), this);

        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle,
            sceneProxyHandle](RenderFrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                GSplatRenderProxy* proxy = proxyManager.getProxy(proxyHandle);
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                sceneProxy->removeGSplatRenderProxy(proxy);
                proxyManager.destroyProxy(proxyHandle);
            }
        );

        renderFrontend.freeProxyHandle(mProxyHandle);
    }

    void GSplatRenderer::onVisible()
    {
    }

    void GSplatRenderer::onInvisible()
    {
    }

    void GSplatRenderer::onTick(float deltaTime)
    {
    }

    void GSplatRenderer::onTransformUpdate(const Transform& transform)
    {
        SceneProxyHandle sceneProxyHandle = getScene()->getOrCreateSceneProxy();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            sceneProxyHandle,
            worldMatrix = transform.getWorldMatrix()]
            (RenderFrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                GSplatRenderProxy* proxy = proxyManager.getProxy(proxyHandle);
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);

                uint32_t instanceIndex = proxy->getInstanceIndex();
                auto& instanceData = sceneProxy->getInstanceData(instanceIndex);
                instanceData.localToWorld.fromDouble4x4(worldMatrix);
                instanceData.localToWorldRot = float4x4(worldMatrix);
                sceneProxy->notifyInstanceDataUpdated(instanceIndex);
            }
        );
    }

    void GSplatRenderer::setGSplat(GSplat* gsplat)
    {
        if (mGSplat == gsplat)
            return;

        if (!mProxyHandle.isValid())
        {
            mGSplat = gsplat;
            return;
        }

        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();

        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::GSplat_Render>();

        std::vector<GPUSplatData> gpuSplatData;
        if (mGSplat)
        {
            std::span<const SplatData> splatData = mGSplat->getSplatData();
            gpuSplatData.reserve(splatData.size());
            for (const auto& splat : splatData)
            {
                gpuSplatData.emplace_back(splatDataToGPUSplatData(splat));
            }
        }

        auto& transform = getEntity()->getTransform();
        renderFrontend.enqueueLambda(
            [sceneProxyHandle,
            proxyHandle = mProxyHandle,
            gpuSplatData = std::move(gpuSplatData),
            worldMatrix = transform.getWorldMatrix()](RenderFrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                GSplatRenderProxy* proxy = proxyManager.getProxy(proxyHandle);
                sceneProxy->setGSplatRenderProxySplatData(proxy, std::move(gpuSplatData));

                uint32_t instanceIndex = proxy->getInstanceIndex();
                auto& instanceData = sceneProxy->getInstanceData(instanceIndex);
                instanceData.localToWorld.fromDouble4x4(worldMatrix);
                instanceData.localToWorldRot = float4x4(worldMatrix);
                sceneProxy->notifyInstanceDataUpdated(instanceIndex);
            }
        );
    }

    template <> Type* Reflection::createType<GSplatRenderer>()
    {
        auto c = new TClass<GSplatRenderer, Component>("GSplatRenderer");
        return c;
    }

}
