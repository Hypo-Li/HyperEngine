#pragma once
#include "LightProxy.h"
#include "CameraProxy.h"
#include <Core/Math/Matrix.h>

namespace hyp
{
    // 运行期按这个 enum 值在 ShadowMask.slang 里走 switch, 不是 shader permutation, 换档不重编 pipeline。
    enum class ShadowFilterQuality
    {
        None,
        PCF_1x1,
        PCF_3x3,
        PCF_5x5,
    };

    // Per-cascade receiver projection and depth-domain data consumed by the depth and mask passes.
    // logicalSplitFar is the nominal cascade boundary; renderSplitFar includes the overlap extension
    // used to keep both adjacent cascades valid throughout [fadeStart, fadeEnd].
    struct CascadeShadowData
    {
        float4x4 viewToLightClip;
        float logicalSplitFar;
        float renderSplitFar;
        // Normalized by the exact tight receiver projection depth range and ready to add to [0,1] NDC.
        // As in UE's traditional CSM path, the constant bias also defines the soft comparison width.
        float depthBias;
        float slopeDepthBias;
        float fadeStart;
        float fadeEnd;
    };

    class DirectionalLightProxy : public LightProxy
    {
    public:
        static constexpr uint32_t MaxCascadeCount = 8;
        static constexpr uint32_t CascadeAtlasColumnCount = 4;

        struct Desc
        {
            LightProxy::Desc light;
            float3 direction;
            uint32_t cascadeCount;
            uint32_t cascadeResolution;
            float shadowDistance;
            float distributionExponent;
            float depthBias;
            float slopeBias;
            float cascadeBiasDistribution;
            bool affectAtmosphere;
            uint32_t atmosphereSunLightIndex;
            float transitionFraction;
            float distanceFadeoutFraction;
            ShadowFilterQuality shadowFilterQuality;
        };

        DirectionalLightProxy(SceneProxy& sceneProxy, const Desc& desc);

        ~DirectionalLightProxy() noexcept override;

        void setDirection(float3 direction) noexcept;

        float3 getDirection() const noexcept
        {
            return mDirection;
        }

        void setShadowParameters(
            uint32_t cascadeCount,
            uint32_t cascadeResolution,
            float shadowDistance,
            float distributionExponent,
            float depthBias,
            float slopeBias,
            float cascadeBiasDistribution
        ) noexcept;

        float getDepthBias() const noexcept
        {
            return mDepthBias;
        }

        float getSlopeBias() const noexcept
        {
            return mSlopeBias;
        }

        float getCascadeBiasDistribution() const noexcept
        {
            return mCascadeBiasDistribution;
        }

        float getShadowDistance() const noexcept
        {
            return mShadowDistance;
        }

        void setTransitionFraction(float transitionFraction) noexcept
        {
            mTransitionFraction = transitionFraction;
        }

        float getTransitionFraction() const noexcept
        {
            return mTransitionFraction;
        }

        void setDistanceFadeoutFraction(float distanceFadeoutFraction) noexcept
        {
            mDistanceFadeoutFraction = distanceFadeoutFraction;
        }

        float getDistanceFadeoutFraction() const noexcept
        {
            return mDistanceFadeoutFraction;
        }

        void setShadowFilterQuality(ShadowFilterQuality shadowFilterQuality) noexcept
        {
            mShadowFilterQuality = shadowFilterQuality;
        }

        ShadowFilterQuality getShadowFilterQuality() const noexcept
        {
            return mShadowFilterQuality;
        }

        std::vector<SceneViewGroup> getShadowSceneViewGroups(
            std::span<const CameraProxy* const> cameraProxies,
            ViewDataManager& viewDataManager
        );

        void clearCachedViewIds(ViewDataManager& viewDataManager) noexcept;

        HString getShadowDepthName(const CameraProxy* cameraProxy) const noexcept;

        HString getShadowMaskName(const CameraProxy* cameraProxy) const noexcept;

        // 这个相机这一帧算过阴影的话, 长度等于 getCascadeCount(), 按近到远排列;否则是空 span
        // (比如这盏光没投影, 或者这个相机压根没被 getShadowSceneViewGroups 处理过)
        std::span<const CascadeShadowData> getCascadeShadowData(const CameraProxy* cameraProxy) const noexcept;

        uint32_t getCascadeCount() const noexcept
        {
            return mCascadeCount;
        }

        uint32_t getCascadeResolution() const noexcept
        {
            return mCascadeResolution;
        }

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

        // Depends only on direction; independent of any camera or cascade state.
        static void computeLightSpaceBasis(float3 direction, double3& outForward, double3& outRight, double3& outUp) noexcept;

    private:
        float3 mDirection;
        uint32_t mCascadeCount;
        uint32_t mCascadeResolution;
        float mShadowDistance;
        float mDistributionExponent;
        float mDepthBias;
        float mSlopeBias;
        float mCascadeBiasDistribution;
        float mTransitionFraction;
        float mDistanceFadeoutFraction;
        ShadowFilterQuality mShadowFilterQuality;

        struct CameraShadowCache
        {
            HString shadowDepthName;
            HString shadowMaskName;
            std::vector<uint32_t> viewIds;
            std::vector<CascadeShadowData> cascades;
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
