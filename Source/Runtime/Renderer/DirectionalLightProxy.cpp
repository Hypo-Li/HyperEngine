#include "DirectionalLightProxy.h"
#include "SceneProxy.h"
#include <Core/Math/Matrix.h>

namespace hyp
{
    DirectionalLightProxy::DirectionalLightProxy(SceneProxy& sceneProxy, const Desc& desc) :
        LightProxy(sceneProxy, desc.light),
        mDirection(desc.direction),
        mTransitionFraction(desc.transitionFraction),
        mDistanceFadeoutFraction(desc.distanceFadeoutFraction),
        mShadowFilterQuality(desc.shadowFilterQuality),
        mAffectAtmosphere(desc.affectAtmosphere),
        mAtmosphereSunLightIndex(desc.atmosphereSunLightIndex)
    {
        sceneProxy.registerElement(this);
        initLightData(LightType::Directional);

        setShadowParameters(
            desc.cascadeCount,
            desc.cascadeResolution,
            desc.shadowDistance,
            desc.distributionExponent,
            desc.depthBias,
            desc.slopeBias,
            desc.cascadeBiasDistribution
        );
    }

    DirectionalLightProxy::~DirectionalLightProxy() noexcept
    {
        getSceneProxy().unregisterElement(this);
    }

    void DirectionalLightProxy::setDirection(float3 direction) noexcept
    {
        mDirection = direction;
    }

    void DirectionalLightProxy::setShadowParameters(
        uint32_t cascadeCount,
        uint32_t cascadeResolution,
        float shadowDistance,
        float distributionExponent,
        float depthBias,
        float slopeBias,
        float cascadeBiasDistribution
    ) noexcept
    {
        mCascadeCount = std::clamp(cascadeCount, 1u, MaxCascadeCount);
        mCascadeResolution = cascadeResolution;
        mShadowDistance = shadowDistance;
        mDistributionExponent = std::max(distributionExponent, 0.1f);
        mDepthBias = depthBias;
        mSlopeBias = slopeBias;
        mCascadeBiasDistribution = std::clamp(cascadeBiasDistribution, 0.0f, 1.0f);
    }

    // 照 UE CVarCSMShadowDepthBias/CVarCSMShadowSlopeScaleDepthBias 的量级取的经验常数,
    // 详见 getShadowSceneViewGroups() 里的用法和注释
    static constexpr float kShadowDepthBias = 10.0f;
    static constexpr float kShadowSlopeScaleDepthBias = 3.0f;
    // Unreal's traditional CSM constants and whole-unit radius quantization are expressed in
    // centimeters. HyperEngine uses meters, so one Unreal world unit is one centimeter here.
    static constexpr double kUnrealWorldUnitInMeters = 0.01;
    // PCF_5x5 uses radius 2, and the bilinear tent implementation reads one additional tap.
    static constexpr double kMaxPcfFootprintRadiusTexels = 3.0;

    std::vector<SceneViewGroup> DirectionalLightProxy::getShadowSceneViewGroups(
        std::span<const CameraProxy* const> cameraProxies,
        ViewDataManager& viewDataManager
    )
    {
        std::vector<SceneViewGroup> sceneViewGroups;
        clearCachedViewIds(viewDataManager);
        if (!isCastShadows())
        {
            mCameraShadowCaches.clear();
            return sceneViewGroups;
        }

        // 场景包围盒的 8 个角点, 提前算好(不依赖相机/cascade, 每次调用只用算一次)
        Boxd sceneBounds = getSceneProxy().getSceneBounds();
        bool hasSceneBounds = sceneBounds.getMin().x <= sceneBounds.getMax().x &&
            sceneBounds.getMin().y <= sceneBounds.getMax().y &&
            sceneBounds.getMin().z <= sceneBounds.getMax().z;
        std::array<double3, 8> sceneCorners;
        if (hasSceneBounds)
            sceneBounds.getVertices(sceneCorners);

        sceneViewGroups.reserve(cameraProxies.size() * mCascadeCount);
        for (const CameraProxy* cameraProxy : cameraProxies)
        {
            std::vector<SceneView> sceneViews;
            sceneViews.reserve(mCascadeCount);

            uint32_t key = cameraProxy->getIndex();
            auto& cache = mCameraShadowCaches[key];
            cache.viewIds.resize(mCascadeCount);
            cache.cascades.resize(mCascadeCount);
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
            double4x4 invProjectionMatrix = glm::inverse(double4x4(projMatrix));
            double4x4 invViewMatrix = glm::inverse(cameraProxy->getViewMatrix());

            float weightSum = 0.0f;
            for (uint32_t i = 0; i < mCascadeCount; ++i)
                weightSum += std::pow(mDistributionExponent, float(i));
            float zNear = cameraProxy->getNearPlane();
            float cameraFarPlane = cameraProxy->getFarPlane();
            float zFar = std::min(cameraFarPlane == 0.0f ? FLT_MAX : cameraFarPlane, mShadowDistance);
            float cascadeDistance = zFar - zNear;
            float cascadeNear = zNear;
            float currentWeight = 0.0f;
            for (uint32_t i = 0; i < mCascadeCount; ++i)
            {
                currentWeight += std::pow(mDistributionExponent, float(i));
                float logicalCascadeFar = zNear + (currentWeight / weightSum) * cascadeDistance;
                bool isLastCascade = (i == mCascadeCount - 1);
                float overlapLength = isLastCascade
                    ? 0.0f
                    : mTransitionFraction * (logicalCascadeFar - cascadeNear);
                float renderCascadeFar = std::min(logicalCascadeFar + overlapLength, zFar);
                float ndcNear = (-cascadeNear * projMatrix[2][2] + projMatrix[3][2]) / (-cascadeNear * projMatrix[2][3] + projMatrix[3][3]);
                float ndcFar = (-renderCascadeFar * projMatrix[2][2] + projMatrix[3][2]) / (-renderCascadeFar * projMatrix[2][3] + projMatrix[3][3]);

                double4 cameraViewSpace[8] = {
                    invProjectionMatrix * double4(-1.f, -1.f, ndcNear, 1.f),
                    invProjectionMatrix * double4(-1.f, -1.f, ndcFar, 1.f),
                    invProjectionMatrix * double4(-1.f,  1.f, ndcNear, 1.f),
                    invProjectionMatrix * double4(-1.f,  1.f, ndcFar, 1.f),
                    invProjectionMatrix * double4(1.f, -1.f, ndcNear, 1.f),
                    invProjectionMatrix * double4(1.f, -1.f, ndcFar, 1.f),
                    invProjectionMatrix * double4(1.f,  1.f, ndcNear, 1.f),
                    invProjectionMatrix * double4(1.f,  1.f, ndcFar, 1.f)
                };
                for (int j = 0; j < 8; ++j)
                    cameraViewSpace[j] *= 1.0 / cameraViewSpace[j].w;

                // UE's analytic sub-frustum sphere center. Keeping the calculation in camera space makes
                // its center depth and radius independent of camera rotation. Orthographic projections use
                // the corresponding midpoint solution.
                double centerDepth = 0.5 * (double(cascadeNear) + double(renderCascadeFar));
                bool isPerspectiveProjection = std::abs(projMatrix[2][3]) > 1e-6f;
                if (isPerspectiveProjection)
                {
                    double tanHalfFovX = 1.0 / std::abs(double(projMatrix[0][0]));
                    double tanHalfFovY = 1.0 / std::abs(double(projMatrix[1][1]));
                    double nearDiagonalSq = Math::square(tanHalfFovX * cascadeNear)
                        + Math::square(tanHalfFovY * cascadeNear);
                    double farDiagonalSq = Math::square(tanHalfFovX * renderCascadeFar)
                        + Math::square(tanHalfFovY * renderCascadeFar);
                    double frustumLength = double(renderCascadeFar) - double(cascadeNear);
                    double optimalOffset = (nearDiagonalSq - farDiagonalSq) / (2.0 * frustumLength)
                        + 0.5 * frustumLength;
                    centerDepth = std::clamp(double(renderCascadeFar) - optimalOffset,
                        double(cascadeNear), double(renderCascadeFar));
                }

                double3 centerViewSpace(0.0, 0.0, -centerDepth);
                double boundingSphereRadius = 0.0;
                for (int j = 0; j < 8; ++j)
                {
                    double distance = Math::length(double3(cameraViewSpace[j]) - centerViewSpace);
                    boundingSphereRadius = std::max(boundingSphereRadius, distance);
                }
                // UE rounds up to one Unreal world unit (one centimeter), not one HyperEngine meter.
                // This keeps the projection stable without needlessly inflating small-scene cascades.
                boundingSphereRadius = std::max(
                    std::ceil(boundingSphereRadius / kUnrealWorldUnitInMeters) * kUnrealWorldUnitInMeters,
                    kUnrealWorldUnitInMeters
                );

                double4 worldSpace[8];
                for (int j = 0; j < 8; ++j)
                    worldSpace[j] = invViewMatrix * cameraViewSpace[j];
                double3 center = double3(invViewMatrix * double4(centerViewSpace, 1.0));

                // 把场景包围盒的角点投影到光源方向轴上(相对 center), 取最靠近光源那一侧的
                // 投影距离——eye 至少要拉到这么远, 场景里所有潜在投影物才不会被近裁剪面切掉。
                // 下限不能是 0: 如果场景整体都在 center 的"下游"(比如整个场景都比这一级视锥低,
                // 没有比 center 更靠近光源的几何体), -minProjection 会是负数, offset 一旦被
                // 钳到 0 就会让 eye 和 center 重合, glm::lookAt 直接退化——下限用这一级 cascade
                // 自己的包围球半径, 保证 eye 总是有意义地偏离 center
                double lightFrustumNearOffset = boundingSphereRadius;
                if (hasSceneBounds)
                {
                    double minProjection = std::numeric_limits<double>::max();
                    for (const double3& corner : sceneCorners)
                        minProjection = std::min(minProjection, Math::dot(corner - center, double3(mDirection)));
                    lightFrustumNearOffset = std::max(boundingSphereRadius, -minProjection);
                }
                double3 upVector = std::abs(mDirection.z) == 1.0f ? double3(0.0, 1.0, 0.0) : double3(0.0, 0.0, 1.0);

                // 级联的世界空间尺寸用包围球半径(旋转不变), 不用视锥角点的紧贴 AABB——同一个
                // 球形包络里的视锥体朝不同方向摆放时, 投影到(固定不动的)光源坐标轴上的紧贴包围盒
                // 大小是会变的, 相机一转 halfSize 就跟着抖, 阴影贴图"每纹素代表多大距离"这个
                // 比例尺跟着一帧一帧变, 边缘锯齿看起来像在"呼吸"。
                // Reserve a guard band for the largest PCF footprint and for the half-texel maximum
                // displacement introduced by round-to-nearest snapping. Solving
                // radius + 0.5 * texelSize <= halfSize - footprint * texelSize gives this extent.
                double guardDiameterTexels = 2.0 * kMaxPcfFootprintRadiusTexels + 1.0;
                double usableResolution = std::max(double(mCascadeResolution) - guardDiameterTexels, 1.0);
                double projectionHalfSize = boundingSphereRadius * double(mCascadeResolution) / usableResolution;
                float halfSize = static_cast<float>(projectionHalfSize);
                double worldUnitsPerTexel = (2.0 * projectionHalfSize) / double(mCascadeResolution);

                // texel snapping: 把 center 投影到光源的固定 right/up 基向量上(只由 mDirection/
                // upVector 决定, 不依赖 center 自己——不能像第一版那样用 glm::lookAt(eye, center,
                // up) 建 view matrix 再拿 center 去变换, center 是这个 view matrix 的 lookAt
                // 目标, 变换出来的 X/Y 按定义恒为 0, 那样"取整"永远是没有意义的整了个 0), 在这两根
                // 轴上按纹素大小取整对齐, 再把 center 挪到对齐后的位置——这样级联只会以整数个纹素
                // 为步长移动, 阴影贴图对世界空间的采样网格帧间保持一致, 消除相机移动/旋转时阴影
                // 边缘的爬行抖动。lightFrustumNearOffset 沿用取整前算好的那个值就够了(取整只挪动
                // center 半个纹素量级, 相对场景尺度可忽略, 不影响"eye 是否已经退到所有潜在遮挡物
                // 上游"这个结论)。
                {
                    double3 lightForward, lightRight, lightUp;
                    computeLightSpaceBasis(mDirection, lightForward, lightRight, lightUp);

                    // The projected world origin defines a fixed grid for every frame. Round-to-nearest
                    // avoids floor's directional bias and limits the receiver displacement to half a texel.
                    double projRight = Math::dot(center, lightRight);
                    double projUp = Math::dot(center, lightUp);
                    double snappedProjRight = std::round(projRight / worldUnitsPerTexel) * worldUnitsPerTexel;
                    double snappedProjUp = std::round(projUp / worldUnitsPerTexel) * worldUnitsPerTexel;

                    center += lightRight * (snappedProjRight - projRight) + lightUp * (snappedProjUp - projUp);
                }

                double3 eye = center - double3(mDirection) * lightFrustumNearOffset;
                double4x4 lightViewMatrix = glm::lookAt(eye, center, upVector);

                // halfSize 不再依赖角点投影；XY 使用带 guard band 的稳定范围，Z 使用稳定包围球范围。
                // Use the cascade sphere for Z as UE does for directional CSM subject bounds. Fitting minZ/maxZ
                // to rotated frustum corners makes the depth range and upstream-clamp plane breathe as the
                // camera rotates. The analytic sphere keeps the receiver range fixed at 2 * radius.
                double receiverCenterDistance = -(
                    lightViewMatrix * double4(center, 1.0)
                ).z;
                float receiverNear = static_cast<float>(std::max(
                    receiverCenterDistance - boundingSphereRadius,
                    0.0
                ));
                float receiverFar = static_cast<float>(
                    receiverCenterDistance + boundingSphereRadius
                );

                float pcfSafeHalfSize = halfSize
                    - static_cast<float>(kMaxPcfFootprintRadiusTexels * worldUnitsPerTexel);
                float coverageTolerance = std::max(halfSize, 1.0f) * 1e-5f;
                for (int j = 0; j < 8; ++j)
                {
                    float3 viewSpace = float3(lightViewMatrix * worldSpace[j]);
                    assert(std::abs(viewSpace.x) <= pcfSafeHalfSize + coverageTolerance);
                    assert(std::abs(viewSpace.y) <= pcfSafeHalfSize + coverageTolerance);
                    assert(-viewSpace.z >= receiverNear - coverageTolerance);
                    assert(-viewSpace.z <= receiverFar + coverageTolerance);
                }

                float receiverDepthRange = receiverFar - receiverNear;
                assert(receiverDepthRange > 0.0f);
                float4x4 lightProjMatrix = glm::orthoRH_ZO(
                    -halfSize,
                    halfSize,
                    -halfSize,
                    halfSize,
                    receiverNear,
                    receiverFar
                );

                // CPU culling needs to retain casters between the light-view origin and the receiver near plane.
                // They are kept from being near-clipped by hardware depth clamp (CascadeShadowDepthPass.cpp).
                float4x4 casterCullingProjMatrix = glm::orthoRH_ZO(
                    -halfSize,
                    halfSize,
                    -halfSize,
                    halfSize,
                    0.0f,
                    receiverFar
                );
                Frustumd casterCullingFrustum(double4x4(casterCullingProjMatrix) * lightViewMatrix);

                // 提前把"主相机视空间坐标 -> 这一级光源裁剪空间"合并成一个矩阵, 在这里(仍是双精度)
                // 算完再收窄成 float——ShadowMaskPass 运行在另一个 pipeline 调用里, 拿不到这里的
                // 局部变量, 只能靠 CameraShadowCache 把结果带出去(见 DirectionalLightProxy.h 里
                // CascadeShadowData 的注释)
                double4x4 mainCameraViewToWorld = glm::inverse(cameraProxy->getViewMatrix());
                double4x4 worldToLightClip = double4x4(lightProjMatrix) * lightViewMatrix;
                cache.cascades[i].viewToLightClip = float4x4(worldToLightClip * mainCameraViewToWorld);
                cache.cascades[i].logicalSplitFar = logicalCascadeFar;
                cache.cascades[i].renderSplitFar = renderCascadeFar;
                if (isLastCascade)
                {
                    cache.cascades[i].fadeStart = logicalCascadeFar
                        - mDistanceFadeoutFraction * (logicalCascadeFar - cascadeNear);
                    cache.cascades[i].fadeEnd = logicalCascadeFar;
                }
                else
                {
                    cache.cascades[i].fadeStart = logicalCascadeFar;
                    cache.cascades[i].fadeEnd = renderCascadeFar;
                }
                cascadeNear = logicalCascadeFar;

                // Match UE's CSM bias distribution: 0 keeps a uniform world-space bias, while 1 scales
                // the bias by this cascade's world-space texel size.
                float worldSpaceTexelScale = halfSize / static_cast<float>(mCascadeResolution);
                float baseDepthBias = kShadowDepthBias / receiverDepthRange;
                // UE's uniform endpoint is one Unreal world unit (1 cm). Using 1.0 directly in this
                // meter-based engine would turn it into 1 m whenever CascadeBiasDistribution < 1.
                float cascadeDepthBiasScale = static_cast<float>(kUnrealWorldUnitInMeters)
                    * (1.0f - mCascadeBiasDistribution)
                    + worldSpaceTexelScale * mCascadeBiasDistribution;
                float depthBias = baseDepthBias * cascadeDepthBiasScale * mDepthBias;
                float slopeDepthBias = depthBias * (kShadowSlopeScaleDepthBias * mSlopeBias);
                cache.cascades[i].depthBias = depthBias;
                cache.cascades[i].slopeDepthBias = slopeDepthBias;

                uint32_t shadowDepthRenderMask = getRenderBackend().getRenderTagRegistry().getRenderMask(BuiltinRenderTags::ShadowDepth);

                sceneViews.emplace_back(
                    cache.viewIds[i],
                    ~0u,
                    shadowDepthRenderMask,
                    lightViewMatrix,
                    lightProjMatrix,
                    casterCullingFrustum,
                    float4(
                        float((i % CascadeAtlasColumnCount) * mCascadeResolution),
                        float((i / CascadeAtlasColumnCount) * mCascadeResolution),
                        float(mCascadeResolution),
                        float(mCascadeResolution)
                    ),
                    cache.shadowDepthName,
                    OutputRouting::Direct_Write
                );
            }

            sceneViewGroups.emplace_back(
                SceneViewGroup::Type::Cascade_Shadow,
                std::move(sceneViews),
                CascadeShadowViewSource{ this, cameraProxy }
            );
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
                if (viewId == UINT32_MAX)
                    continue;
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

    std::span<const CascadeShadowData> DirectionalLightProxy::getCascadeShadowData(const CameraProxy* cameraProxy) const noexcept
    {
        uint32_t key = cameraProxy->getIndex();
        auto it = mCameraShadowCaches.find(key);
        if (it != mCameraShadowCaches.end())
            return it->second.cascades;
        return {};
    }

    void DirectionalLightProxy::setAffectAtmosphere(bool affectAtmosphere) noexcept
    {
        mAffectAtmosphere = affectAtmosphere;
    }

    void DirectionalLightProxy::setAtmosphereSunLightIndex(uint32_t atmosphereSunLightIndex) noexcept
    {
        mAtmosphereSunLightIndex = atmosphereSunLightIndex;
    }

    void DirectionalLightProxy::computeLightSpaceBasis(float3 direction, double3& outForward, double3& outRight, double3& outUp) noexcept
    {
        double3 upHint = std::abs(direction.z) == 1.0f ? double3(0.0, 1.0, 0.0) : double3(0.0, 0.0, 1.0);
        outForward = glm::normalize(double3(direction));
        outRight = glm::normalize(glm::cross(outForward, upHint));
        outUp = glm::cross(outRight, outForward);
    }

}
