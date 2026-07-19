#pragma once
#include "CameraProxy.h"
#include "RenderableProxy.h"

namespace hyp
{
    class DirectionalLightProxy : public RenderableProxy
    {
    public:
        DirectionalLightProxy(
            float3 direction,
            float3 intensity,
            bool castShadows,
            uint32_t cascadeCount,
            uint32_t cascadeResolution,
            float shadowDistance,
            float distributionExponent,
            float depthBias,
            float slopeBias,
            bool affectAtmosphere,
            uint32_t atmosphereSunLightIndex
        );

        ~DirectionalLightProxy() noexcept = default;

        void setLightIndex(uint32_t lightIndex) noexcept
        {
            mLightIndex = lightIndex;
        }

        uint32_t getLightIndex() const noexcept
        {
            return mLightIndex;
        }

        void setDirection(float3 direction) noexcept;

        float3 getDirection() const noexcept
        {
            return mDirection;
        }

        void setIntensity(float3 intensity) noexcept;

        float3 getIntensity() const noexcept
        {
            return mIntensity;
        }

        void setCastShadows(bool castShadows) noexcept;

        bool isCastShadows() const noexcept
        {
            return mCastShadows;
        }

        void setShadowParameters(
            uint32_t cascadeCount,
            uint32_t cascadeResolution,
            float shadowDistance,
            float distributionExponent,
            float depthBias,
            float slopeBias
        ) noexcept;

        float getDepthBias() const noexcept
        {
            return mDepthBias;
        }

        float getSlopeBias() const noexcept
        {
            return mSlopeBias;
        }

        std::vector<SceneViewGroup> getShadowSceneViewGroups(
            std::span<const CameraProxy* const> cameraProxies,
            ViewDataManager& viewDataManager
        );

        void clearCachedViewIds(ViewDataManager& viewDataManager) noexcept;

        HString getShadowDepthName(const CameraProxy* cameraProxy) const noexcept;

        HString getShadowMaskName(const CameraProxy* cameraProxy) const noexcept;

        void setAffectAtmosphere(bool affectAtmosphere) noexcept;

        bool isAffectAtmosphere() const noexcept
        {
            return mAffectAtmosphere;
        }

        void setAtmosphereSunLightIndex(uint32_t atmosphereSunLightIndex) noexcept;

        uint32_t getAtmosphereSunLightIndex() const noexcept
        {
            return mAtmosphereSunLightIndex;
        }

    private:
        uint32_t mLightIndex;
        float3 mDirection;
        float3 mIntensity;
        bool mCastShadows;
        uint32_t mCascadeCount;
        uint32_t mCascadeResolution;
        float mShadowDistance;
        float mDistributionExponent;
        float mDepthBias;
        float mSlopeBias;

        struct CameraShadowCache
        {
            HString shadowDepthName;
            HString shadowMaskName;
            std::vector<uint32_t> viewIds;
        };
        std::unordered_map<uint32_t, CameraShadowCache> mCameraShadowCaches;

        bool mAffectAtmosphere;
        uint32_t mAtmosphereSunLightIndex;
    };

    template <>
    struct TProxyTraits<ProxyType::Directional_Light>
    {
        using Proxy = DirectionalLightProxy;
    };
}
