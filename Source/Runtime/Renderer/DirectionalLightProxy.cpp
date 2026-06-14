#include "DirectionalLightProxy.h"

#include <glm/gtc/matrix_transform.hpp>

namespace hyp
{
    static uint32_t gAtmosphereRequestCounter = 0;

    DirectionalLightProxy::DirectionalLightProxy(
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
    ) : mDirection(direction),
        mIntensity(intensity),
        mCastShadows(castShadows),
        mCascadeCount(cascadeCount),
        mCascadeResolution(cascadeResolution),
        mShadowDistance(shadowDistance),
        mDistributionExponent(distributionExponent),
        mDepthBias(depthBias),
        mSlopeBias(slopeBias),
        mAffectAtmosphere(affectAtmosphere),
        mAtmosphereSunLightIndex(atmosphereSunLightIndex)
    {
        if (mAffectAtmosphere)
        {
            mAtmosphereRequestOrder = gAtmosphereRequestCounter++;
        }
    }

    void DirectionalLightProxy::setDirection(float3 direction) noexcept
    {
        mDirection = direction;
    }

    void DirectionalLightProxy::setIntensity(float3 intensity) noexcept
    {
        mIntensity = intensity;
    }

    void DirectionalLightProxy::setCastShadows(bool castShadows) noexcept
    {
        mCastShadows = castShadows;
    }

    void DirectionalLightProxy::setShadowParameters(
        uint32_t cascadeCount,
        uint32_t cascadeResolution,
        float shadowDistance,
        float distributionExponent,
        float depthBias,
        float slopeBias
    ) noexcept
    {
        mCascadeCount = cascadeCount;
        mCascadeResolution = cascadeResolution;
        mShadowDistance = shadowDistance;
        mDistributionExponent = distributionExponent;
        mDepthBias = depthBias;
        mSlopeBias = slopeBias;
    }

    std::vector<SceneViewGroup> DirectionalLightProxy::getShadowSceneViewGroups(
        std::span<const CameraProxy* const> cameraProxies,
        ViewDataManager& viewDataManager
    )
    {
        std::vector<SceneViewGroup> sceneViewGroups;
        clearCachedViewIds(viewDataManager);
        if (!mCastShadows)
            return sceneViewGroups;

        sceneViewGroups.reserve(cameraProxies.size() * mCascadeCount);
        for (const CameraProxy* cameraProxy : cameraProxies)
        {
            std::vector<SceneView> sceneViews;
            sceneViews.reserve(mCascadeCount);

            uint32_t key = cameraProxy->getIndex();
            auto& cache = mCameraShadowCaches[key];
            cache.viewIds.resize(mCascadeCount);
            if (cache.shadowDepthName.empty())
            {
                std::string prefix = "DirectionalLight" + std::to_string(getIndex()) + ".Camera" + std::to_string(cameraProxy->getIndex());
                cache.shadowDepthName = HString(prefix + ".ShadowDepth");
                cache.shadowMaskName = HString(prefix + ".ShadowMask");
            }
            for (uint32_t i = 0; i < mCascadeCount; ++i)
            {
                cache.viewIds[i] = viewDataManager.acquireViewId();
            }

            auto& projMatrix = cameraProxy->getProjectionMatrix();
            double4x4 invViewProjMatrix = glm::inverse(double4x4(projMatrix) * cameraProxy->getViewMatrix());

            float weightSum = (1.0f - std::pow(mDistributionExponent, float(mCascadeCount))) / (1.0f - mDistributionExponent); // e.g. 1 + 3 + 9 + 27 = 40
            float zNear = cameraProxy->getNearPlane();
            float cameraFarPlane = cameraProxy->getFarPlane();
            float zFar = std::min(cameraFarPlane == 0.0f ? FLT_MAX : cameraFarPlane, mShadowDistance);
            float cascadeDistance = zFar - zNear;
            float cascadeNear = zNear;
            float currentWeight = 0.0f;
            for (uint32_t i = 0; i < mCascadeCount; ++i)
            {
                currentWeight += std::pow(mDistributionExponent, float(i));
                float cascadeFar = zNear + (currentWeight / weightSum) * cascadeDistance;
                float ndcNear = (-cascadeNear * projMatrix[2][2] + projMatrix[3][2]) / (-cascadeNear * projMatrix[2][3] + projMatrix[3][3]);
                float ndcFar = (-cascadeFar * projMatrix[2][2] + projMatrix[3][2]) / (-cascadeFar * projMatrix[2][3] + projMatrix[3][3]);

                double4 worldSpace[8] = {
                    invViewProjMatrix * double4(-1.f, -1.f, ndcNear, 1.f),
                    invViewProjMatrix * double4(-1.f, -1.f, ndcFar, 1.f),
                    invViewProjMatrix * double4(-1.f,  1.f, ndcNear, 1.f),
                    invViewProjMatrix * double4(-1.f,  1.f, ndcFar, 1.f),
                    invViewProjMatrix * double4(1.f, -1.f, ndcNear, 1.f),
                    invViewProjMatrix * double4(1.f, -1.f, ndcFar, 1.f),
                    invViewProjMatrix * double4(1.f,  1.f, ndcNear, 1.f),
                    invViewProjMatrix * double4(1.f,  1.f, ndcFar, 1.f)
                };
                double3 center = { 0.0, 0.0, 0.0 };
                for (int j = 0; j < 8; ++j)
                {
                    worldSpace[j] *= 1.0 / worldSpace[j].w;
                    center += worldSpace[j];
                }
                center *= 0.125;
                double boundingSphereRadius = 0.0;
                for (int j = 0; j < 8; ++j)
                {
                    double distance = Math::length(double3(worldSpace[j]) - center);
                    boundingSphereRadius = std::max(boundingSphereRadius, distance);
                }

                double lightFrustumNearOffset = 100.0f; // TODO: 应该由场景包围盒计算得到
                double3 eye = center - double3(mDirection) * lightFrustumNearOffset;
                double3 upVector = std::abs(mDirection.z) == 1.0f ? double3(0.0, 1.0, 0.0) : double3(0.0, 0.0, 1.0);
                double4x4 lightViewMatrix = glm::lookAt(eye, center, upVector);

                float minX = std::numeric_limits<float>::max();
                float maxX = std::numeric_limits<float>::lowest();
                float minY = std::numeric_limits<float>::max();
                float maxY = std::numeric_limits<float>::lowest();
                float minZ = std::numeric_limits<float>::max();
                float maxZ = std::numeric_limits<float>::lowest();
                for (int j = 0; j < 8; ++j)
                {
                    float3 viewSpace = float3(lightViewMatrix * worldSpace[j]);
                    minX = std::min(minX, viewSpace.x);
                    maxX = std::max(maxX, viewSpace.x);
                    minY = std::min(minY, viewSpace.y);
                    maxY = std::max(maxY, viewSpace.y);
                    minZ = std::min(minZ, viewSpace.z);
                    maxZ = std::max(maxZ, viewSpace.z);
                }

                float halfWidth = std::max(-minX, maxX);
                float halfHeight = std::max(-minY, maxY);
                float halfSize = std::max(halfWidth, halfHeight);
                
                float4x4 lightProjMatrix = glm::orthoRH_ZO(-halfSize, halfSize, -halfSize, halfSize, -maxZ, -minZ);

                uint32_t shadowDepthRenderMask = getRenderBackend().getRenderTagRegistry().getRenderMask(BuiltinRenderTags::ShadowDepth);

                sceneViews.emplace_back(
                    cache.viewIds[i],
                    ~0u,
                    shadowDepthRenderMask,
                    lightViewMatrix,
                    lightProjMatrix,
                    float4(float(i * mCascadeResolution), 0.0f, float(mCascadeResolution), float(mCascadeResolution)),
                    cache.shadowDepthName,
                    OutputRouting::Direct_Write
                );
            }

            sceneViewGroups.emplace_back(SceneViewGroup::Type::Cascade_Shadow, std::move(sceneViews), this);
        }

        for (auto it = mCameraShadowCaches.begin(); it != mCameraShadowCaches.end();)
        {
            if (it->second.viewIds[0] == UINT32_MAX)
                it = mCameraShadowCaches.erase(it);
            else
                ++it;
        }

        return sceneViewGroups;
    }

    void DirectionalLightProxy::clearCachedViewIds(ViewDataManager& viewDataManager) noexcept
    {
        for (auto& cache : mCameraShadowCaches)
        {
            for (uint32_t& viewId : cache.second.viewIds)
            {
                viewDataManager.releaseViewId(viewId);
                viewId = UINT32_MAX;
            }
        }
    }

    HString DirectionalLightProxy::getShadowDepthName(const CameraProxy* cameraProxy) const noexcept
    {
        uint32_t key = cameraProxy->getIndex();
        auto it = mCameraShadowCaches.find(key);
        if (it != mCameraShadowCaches.end())
            return it->second.shadowDepthName;
        return HString();
    }

    HString DirectionalLightProxy::getShadowMaskName(const CameraProxy* cameraProxy) const noexcept
    {
        uint32_t key = cameraProxy->getIndex();
        auto it = mCameraShadowCaches.find(key);
        if (it != mCameraShadowCaches.end())
            return it->second.shadowMaskName;
        return HString();
    }

    void DirectionalLightProxy::setAffectAtmosphere(bool affectAtmosphere) noexcept
    {
        if (mAffectAtmosphere == affectAtmosphere)
            return;

        mAffectAtmosphere = affectAtmosphere;

        if (mAffectAtmosphere)
        {
            mAtmosphereRequestOrder = gAtmosphereRequestCounter++;
        }
    }

    void DirectionalLightProxy::setAtmosphereSunLightIndex(uint32_t atmosphereSunLightIndex) noexcept
    {
        if (mAtmosphereSunLightIndex == atmosphereSunLightIndex)
            return;

        mAtmosphereSunLightIndex = atmosphereSunLightIndex;
        if (mAffectAtmosphere)
        {
            mAtmosphereRequestOrder = gAtmosphereRequestCounter++;
        }
    }

}
