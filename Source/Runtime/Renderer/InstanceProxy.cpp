#include "InstanceProxy.h"
#include "SceneProxy.h"

namespace hyp
{
    namespace
    {
        void writeTransform(InstanceData& instanceData, const double4x4& worldMatrix) noexcept
        {
            instanceData.objectToWorld.fromDouble4x4(worldMatrix);

            // R*S(左上 3x3 子矩阵, 含 scale)——切线这类该跟着表面一起形变的向量用, 就是原始矩阵,
            // 不求逆不转置
            const double3x3 objectRS(worldMatrix);
            instanceData.objectToWorldRS = float3x4(float3x3(objectRS));

            // 纯 Rotation, 不含 scale——按列长度把 scale 除掉。引擎的 Transform 只支持独立设置
            // T/R/S, 不支持 shear, 所以每一列的长度就是那根轴的缩放系数, 除掉之后剩下的就是纯
            // 旋转基。光源方向这类不该被 scale 影响的方向向量用这个, 不能直接用 objectToWorldRS
            const double3 axisScale(glm::length(objectRS[0]), glm::length(objectRS[1]), glm::length(objectRS[2]));
            const double3x3 objectRotOnly(
                objectRS[0] / axisScale.x,
                objectRS[1] / axisScale.y,
                objectRS[2] / axisScale.z
            );
            instanceData.objectToWorldRot = float3x4(float3x3(objectRotOnly));

            // 只求左上 3x3(旋转+缩放)子矩阵的逆, 不是整个 4x4——这个字段只给"把方向/射线从世界
            // 空间转回物体空间"这类用途用, 平移不参与, 见 SceneProxyData.h 里的字段注释。缩放
            // 一般不是正交阵, 这里必须是真正的矩阵求逆, 不能用转置代替
            const double3x3 inverseObjectRS = glm::inverse(objectRS);
            instanceData.worldToObjectRS = float3x4(float3x3(inverseObjectRS));

            // inverse(transpose(RS))——专给法线用, 非均匀缩放下也能保持垂直关系。上面已经求过
            // inverse(objectRS) 了, 这里直接转置复用, 不再重新求一次逆
            instanceData.objectToWorldNormalRot = float3x4(float3x3(glm::transpose(inverseObjectRS)));
        }
    }

    InstanceProxy::InstanceProxy(SceneProxy& sceneProxy, const Desc& desc) :
        SceneElementProxy(sceneProxy, desc.visible),
        mInstanceIndex(sceneProxy.acquireInstanceIndex()),
        mMobility(desc.mobility)
    {
        syncVisibleToInstanceData();

        InstanceData& instanceData = sceneProxy.getInstanceData(mInstanceIndex);
        writeTransform(instanceData, desc.worldMatrix);
        instanceData.prevObjectToWorld = instanceData.objectToWorld;
        sceneProxy.markInstanceTransformUpdated(mInstanceIndex);
    }

    InstanceProxy::~InstanceProxy() noexcept
    {
        getSceneProxy().releaseInstanceIndex(mInstanceIndex);
    }

    void InstanceProxy::setVisible(bool visible) noexcept
    {
        SceneElementProxy::setVisible(visible);
        syncVisibleToInstanceData();
    }

    void InstanceProxy::syncVisibleToInstanceData() noexcept
    {
        SceneProxy& sceneProxy = getSceneProxy();
        InstanceData& instanceData = sceneProxy.getInstanceData(mInstanceIndex);
        if (isVisible())
            enumAddFlags(instanceData.flags, InstanceFlags::Visible);
        else
            enumRemoveFlags(instanceData.flags, InstanceFlags::Visible);
        sceneProxy.markInstanceDataUpdated(mInstanceIndex);
    }

    void InstanceProxy::updateInstanceTransform(const double4x4& worldMatrix) noexcept
    {
        SceneProxy& sceneProxy = getSceneProxy();
        auto& instanceData = sceneProxy.getInstanceData(mInstanceIndex);

        if (!sceneProxy.isInstanceTransformUpdated(mInstanceIndex))
        {
            // 同一渲染帧可能收到多次 Transform setter。只有第一次更新时 current 仍代表上一帧
            // 实际上传的值，才允许推进 prev；后续更新只覆盖 current，保留正确的帧间起点。
            instanceData.prevObjectToWorld = instanceData.objectToWorld;
        }

        writeTransform(instanceData, worldMatrix);
        sceneProxy.markInstanceTransformUpdated(mInstanceIndex);
    }
}
