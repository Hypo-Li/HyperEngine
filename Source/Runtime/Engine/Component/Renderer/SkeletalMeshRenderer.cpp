#include "SkeletalMeshRenderer.h"
#include "../../Framework/Entity.h"
#include "../../Framework/Scene.h"
#include "../../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/MeshRenderProxy.h>

#include <algorithm>
#include <cmath>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace hyp
{
    namespace
    {
        // 结果矩阵不是单纯的 TRS(骨骼链上非等比缩放可能带来切变), decompose 是唯一能安全
        // 拆出 pos/rot/scale 的路——getBoneWorldTransform()/getSocketWorldTransform() 共用。
        void decomposeWorldMatrix(const double4x4& worldMatrix, double3& outPosition, Quaternion& outRotation, float3& outScale)
        {
            double3 skew;
            double4 perspective;
            double3 scale;
            glm::dquat rotation;
            double3 translation;
            glm::decompose(worldMatrix, scale, rotation, translation, skew, perspective);

            outPosition = translation;
            outRotation = Quaternion(static_cast<float>(rotation.w), static_cast<float>(rotation.x), static_cast<float>(rotation.y), static_cast<float>(rotation.z));
            outScale = float3(scale);
        }
    }

    REGISTER_CLASS(SkeletalMeshRenderer)

    void SkeletalMeshRenderer::onRegister()
    {
        MeshRenderer::onRegister();
        getEntity()->getTransform().setSocketProvider(this);
        updateSkinningAllocation();
    }

    void SkeletalMeshRenderer::onUnregister()
    {
        assert(getEntity()->getTransform().getSocketProvider() == this);
        getEntity()->getTransform().setSocketProvider(nullptr);

        MeshRenderer::onUnregister();
    }

    void SkeletalMeshRenderer::updateSkinningAllocation()
    {
        if (!getProxyHandle().isValid())
            return;

        // 骨架换了(或者反序列化后第一次注册, mAnimation 早就是反射直接赋值好的), 骨骼下标
        // 顺序可能跟之前不一样, 缓存要跟着重建
        rebuildBoneTrackIndexCache();

        Skeleton* skeleton = mSkeletalMesh ? mSkeletalMesh->getSkeleton() : nullptr;
        uint32_t boneCount = skeleton ? skeleton->getBoneCount() : 0;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = getProxyHandle(), boneCount](FrameContext& context)
            {
                MeshRenderProxy* proxy = context.getProxyManager().getProxy(proxyHandle);
                proxy->freeSkinning();
                if (boneCount > 0)
                    proxy->allocateSkinning(boneCount);
            }
        );

        // mWorldMatricesScratch 立刻跟当前骨架同步——不然骨架数变了之后, 在下一次真正 tick
        // 之前, getSocketWorldTransform() 会拿旧骨架大小的缓存去按新骨架的 boneIndex 取值
        uploadSkinningMatricesAtTime(mCurrentTime);
    }

    void SkeletalMeshRenderer::onTick(float deltaTime)
    {
        // 没有动画或者暂停时姿势不会变, 不重复计算/上传——只有 mCurrentTime 真的推进了才值得
        // 重算一遍蒙皮矩阵
        if (!mSkeletalMesh || !mAnimation || !mPlaying)
            return;

        float previousTime = mCurrentTime;
        mCurrentTime += deltaTime * mPlaybackSpeed;
        float duration = mAnimation->getDuration();
        if (mAnimation->isLooping() && duration > 0.0f)
        {
            mCurrentTime = std::fmod(mCurrentTime, duration);
            if (mCurrentTime < 0.0f)
                mCurrentTime += duration;
        }
        else
        {
            mCurrentTime = std::clamp(mCurrentTime, 0.0f, duration);
        }

        // 非循环动画播完之后每帧都会钳制到同一个 duration 值——时间没变说明姿势也不会变
        if (mCurrentTime == previousTime)
            return;

        uploadSkinningMatricesAtTime(mCurrentTime);
    }

    void SkeletalMeshRenderer::uploadSkinningMatricesAtTime(float time)
    {
        if (!getProxyHandle().isValid())
            return;

        Skeleton* skeleton = mSkeletalMesh ? mSkeletalMesh->getSkeleton() : nullptr;
        if (!skeleton)
            return;

        // 逐骨骼求出最终蒙皮矩阵: 按 Skeleton 的拓扑序线性遍历一次就够(parentIndex 恒小于
        // 自身下标, 见 Skeleton::Bone::parentIndex 的注释)。Position/Rotation/Scale 先用
        // bind pose 打底, mAnimation->sampleBone() 找不到这根骨骼对应的 Track 时原样保留
        // bind pose 值, 这是 sampleBone() 自己的契约。
        uint32_t boneCount = skeleton->getBoneCount();
        mWorldMatricesScratch.resize(boneCount); // 骨骼数没变时是 no-op, 不重新分配
        mBoneOverrides.resize(boneCount); // 同上; 新增的骨骼默认无覆盖
        std::vector<float4x4>& worldMatrices = mWorldMatricesScratch;
        std::vector<float3x4> skinningMatrices(boneCount);

        for (uint32_t i = 0; i < boneCount; ++i)
        {
            if (mBoneOverrides[i].has_value())
            {
                // 覆盖优先于动画采样——存的已经是模型空间矩阵, 可以直接当这根骨骼的
                // worldMatrices[i] 用, 它的子骨骼(如果没有自己的覆盖)会按下面的拓扑序累乘
                // 自动继承这个结果, 不需要额外处理。
                worldMatrices[i] = *mBoneOverrides[i];
            }
            else
            {
                const Skeleton::Bone& bone = skeleton->getBone(i);

                float3 position = bone.localPosition;
                Quaternion rotation = bone.localRotation;
                float3 scale = bone.localScale;
                if (mAnimation)
                    mAnimation->sampleBone(mBoneTrackIndices[i], time, position, rotation, scale);

                // TRS 直接组装, 不走 translate()/scale() 各自构造单位矩阵再做两次完整 4x4×4x4
                // 矩阵乘法那条路——旋转矩阵按列乘缩放系数、平移直接放进第 4 列就是完整的局部矩阵,
                // 数学上完全等价, 但省掉了两次乘加次数是直接构造的好几倍的矩阵乘法
                float3x3 rotationBasis = glm::mat3_cast(rotation);
                float4x4 localMatrix(rotationBasis);
                localMatrix[0] *= scale.x;
                localMatrix[1] *= scale.y;
                localMatrix[2] *= scale.z;
                localMatrix[3] = float4(position, 1.0f);

                worldMatrices[i] = bone.parentIndex >= 0 ? worldMatrices[bone.parentIndex] * localMatrix : localMatrix;
            }

            // float3x4(float4x4) 截断构造只取前 3 列(旋转+缩放的基向量, 第 4 分量恒为 0),
            // 平移(第 4 列)要另外手动补进每一列的 .w——跟 WorldMatrix::fromDouble4x4() 同一个
            // 打包约定, shader 侧(VertexLayout.cpp 生成的 computeSkinningMatrix)按这个约定读
            float4x4 skin4x4 = worldMatrices[i] * skeleton->getInverseBindMatrix(i);
            float3x4 skin = float3x4(skin4x4);
            skin[0].w = skin4x4[3][0];
            skin[1].w = skin4x4[3][1];
            skin[2].w = skin4x4[3][2];
            skinningMatrices[i] = skin;
        }

        // 骨骼世界矩阵变了, 但不会经过任何 Transform 的 local setter——挂在自己身上的 socket
        // 子节点(以及它们自己的子孙节点)的 pull 缓存需要主动标脏, 不然下次查询会拿到上一次
        // 缓存住的旧值
        getEntity()->getTransform().markChildrenWorldDirty();

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = getProxyHandle(), skinningMatrices = std::move(skinningMatrices)](FrameContext& context) mutable
            {
                MeshRenderProxy* proxy = context.getProxyManager().getProxy(proxyHandle);
                proxy->updateSkinningMatrices(std::move(skinningMatrices));
            }
        );
    }

    bool SkeletalMeshRenderer::getSocketWorldTransform(HString socketName, double3& outPosition, Quaternion& outRotation, float3& outScale) const
    {
        Skeleton* skeleton = mSkeletalMesh ? mSkeletalMesh->getSkeleton() : nullptr;
        if (!skeleton)
            return false;

        int32_t socketIndex = skeleton->findSocketIndex(socketName);
        if (socketIndex < 0)
            return false;

        const Skeleton::Socket& socket = skeleton->getSocket(static_cast<uint32_t>(socketIndex));
        if (socket.boneIndex < 0 || static_cast<size_t>(socket.boneIndex) >= mWorldMatricesScratch.size())
            return false;

        float4x4 modelSpaceMatrix = mWorldMatricesScratch[socket.boneIndex] * socket.localMatrix;
        double4x4 worldMatrix = getEntity()->getTransform().getWorldMatrix() * double4x4(modelSpaceMatrix);
        decomposeWorldMatrix(worldMatrix, outPosition, outRotation, outScale);
        return true;
    }

    bool SkeletalMeshRenderer::getBoneWorldTransform(int32_t boneIndex, double3& outPosition, Quaternion& outRotation, float3& outScale) const
    {
        if (boneIndex < 0 || static_cast<size_t>(boneIndex) >= mWorldMatricesScratch.size())
            return false;

        double4x4 worldMatrix = getEntity()->getTransform().getWorldMatrix() * double4x4(mWorldMatricesScratch[boneIndex]);
        decomposeWorldMatrix(worldMatrix, outPosition, outRotation, outScale);
        return true;
    }

    void SkeletalMeshRenderer::setBoneOverride(int32_t boneIndex, double3 worldPosition, Quaternion worldRotation)
    {
        if (boneIndex < 0 || static_cast<size_t>(boneIndex) >= mBoneOverrides.size())
            return;

        // 存的是模型空间(相对这个 Entity), 跟 mWorldMatricesScratch 同一个参考系——世界空间
        // 转模型空间要用 Entity 世界矩阵的逆。旋转先按 float 精度算好(引擎里旋转向来是单精度,
        // 跟 Collider::getBodyRotation() 等一致), 平移单独用 double 精度设进去。
        float4x4 rotationMatrix(glm::mat3_cast(worldRotation));
        double4x4 worldMatrix = double4x4(rotationMatrix);
        worldMatrix[3] = double4(worldPosition, 1.0);

        double4x4 entityWorldMatrix = getEntity()->getTransform().getWorldMatrix();
        double4x4 modelSpaceMatrix = glm::inverse(entityWorldMatrix) * worldMatrix;
        mBoneOverrides[boneIndex] = float4x4(modelSpaceMatrix);
    }

    void SkeletalMeshRenderer::clearBoneOverride(int32_t boneIndex)
    {
        if (boneIndex < 0 || static_cast<size_t>(boneIndex) >= mBoneOverrides.size())
            return;

        mBoneOverrides[boneIndex].reset();
    }

    void SkeletalMeshRenderer::clearAllBoneOverrides()
    {
        for (auto& boneOverride : mBoneOverrides)
            boneOverride.reset();
    }

    void SkeletalMeshRenderer::refreshPose()
    {
        uploadSkinningMatricesAtTime(mCurrentTime);
    }

    void SkeletalMeshRenderer::setMesh(SkeletalMesh* newMesh)
    {
        if (mSkeletalMesh == newMesh)
            return;

        TRef<SkeletalMesh> oldMesh = mSkeletalMesh;
        mSkeletalMesh = newMesh;
        onMeshChanged(oldMesh.get());
        updateSkinningAllocation();
    }

    void SkeletalMeshRenderer::setAnimation(Animation* animation)
    {
        mAnimation = animation;
        mCurrentTime = 0.0f;
        rebuildBoneTrackIndexCache();

        // 换动画后的姿势跟上一次上传的姿势不连续, 不标记的话 prev 会拿到不相关的旧姿势
        if (getProxyHandle().isValid())
        {
            Context::get().getRenderFrontend().enqueueLambda(
                [proxyHandle = getProxyHandle()](FrameContext& context)
                {
                    MeshRenderProxy* proxy = context.getProxyManager().getProxy(proxyHandle);
                    if (proxy)
                        proxy->markSkinningDiscontinuous();
                }
            );
        }

        // 编辑模式下 onTick() 不会跑(见 Scene::tick() 的 isPlaying() 判断), 换完动画立刻
        // 补传一次第一帧的姿势, 不然网格会一直停在 bind pose 直到真正进入 Play 模式
        uploadSkinningMatricesAtTime(mCurrentTime);
    }

    void SkeletalMeshRenderer::rebuildBoneTrackIndexCache()
    {
        mBoneTrackIndices.clear();

        Skeleton* skeleton = mSkeletalMesh ? mSkeletalMesh->getSkeleton() : nullptr;
        if (!skeleton || !mAnimation)
            return;

        uint32_t boneCount = skeleton->getBoneCount();
        mBoneTrackIndices.resize(boneCount);
        for (uint32_t i = 0; i < boneCount; ++i)
            mBoneTrackIndices[i] = mAnimation->resolveBoneTrackIndex(skeleton->getBone(i).name);
    }

    template <> Type* Reflection::createType<SkeletalMeshRenderer>()
    {
        auto c = new TClass<SkeletalMeshRenderer, MeshRenderer>("SkeletalMeshRenderer");
        c->setDisplayName("Skeletal Mesh Renderer");
        c->addProperty("Skeletal Mesh", &SkeletalMeshRenderer::mSkeletalMesh);
        c->addProperty("Animation", &SkeletalMeshRenderer::mAnimation);
        c->addProperty("Playing", &SkeletalMeshRenderer::mPlaying);
        c->addProperty("Playback Speed", &SkeletalMeshRenderer::mPlaybackSpeed);
        return c;
    }
}
