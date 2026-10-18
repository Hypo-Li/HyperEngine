#pragma once
#include "MeshRenderer.h"
#include "../../Mesh/SkeletalMesh.h"
#include "../../Animation/Animation.h"
#include "../../Framework/Transform.h"

#include <optional>

namespace hyp
{
    class SkeletalMeshRenderer : public MeshRenderer, public ISocketProvider
    {
        REFLECT_CLASS(SkeletalMeshRenderer)
    public:
        SkeletalMeshRenderer() = default;

        ~SkeletalMeshRenderer() = default;

        void onRegister() override;

        void onUnregister() override;

        void onTick(float deltaTime) override;

        void setMesh(SkeletalMesh* newMesh);

        SkeletalMesh* getMesh() const noexcept override
        {
            return mSkeletalMesh.get();
        }

        // 换动画资产: 播放时间重置为 0, 播放/暂停状态不变——不强制换了动画就自动播放,
        // 尊重调用方已经设好的 isPlaying() 状态
        void setAnimation(Animation* animation);

        Animation* getAnimation() const noexcept
        {
            return mAnimation.get();
        }

        void setPlaying(bool playing) noexcept
        {
            mPlaying = playing;
        }

        bool isPlaying() const noexcept
        {
            return mPlaying;
        }

        void setCurrentTime(float time) noexcept
        {
            mCurrentTime = time;
        }

        float getCurrentTime() const noexcept
        {
            return mCurrentTime;
        }

        void setPlaybackSpeed(float speed) noexcept
        {
            mPlaybackSpeed = speed;
        }

        float getPlaybackSpeed() const noexcept
        {
            return mPlaybackSpeed;
        }

        // 非循环动画播到末尾的判定——没有 Notify 系统之前, gameplay 靠这个查询"一次性动作
        // (比如攻击/死亡)是不是播完了"
        bool hasFinished() const noexcept
        {
            return mAnimation && !mAnimation->isLooping() && mCurrentTime >= mAnimation->getDuration();
        }

        // ISocketProvider——按名字查 mSkeletalMesh 的骨架里同名 Socket, 用 mWorldMatricesScratch
        // 里对应骨骼的模型空间矩阵(uploadSkinningMatricesAtTime() 算好的)乘 Socket 自己的固定
        // 偏移、再乘自己 Entity 的世界矩阵, 得到这个挂点当前的世界变换。不掺被挂物体自己的
        // local 偏移——那部分交给 Transform::getEffectiveParentTransform() 用标准 TRS 公式叠加
        bool getSocketWorldTransform(HString socketName, double3& outPosition, Quaternion& outRotation, float3& outScale) const override;

        // 任意骨骼下标的世界变换, boneIndex 越界或没有骨架时返回 false——getSocketWorldTransform()
        // 内部现在也是转调这个(拿到骨骼世界矩阵后再叠加 Socket 自己的固定偏移)。
        bool getBoneWorldTransform(int32_t boneIndex, double3& outPosition, Quaternion& outRotation, float3& outScale) const;

        // 覆盖某根骨骼这一帧的姿势(世界空间), 参与 uploadSkinningMatricesAtTime() 的计算时
        // 优先于动画采样——RagdollInstance 用这个把物理模拟结果写回骨架。只覆盖位置/旋转,
        // 不覆盖缩放(物理模拟没有"缩放"这个概念, 缩放沿用 bind pose/动画给的值)。
        // 不会立即重算蒙皮矩阵, 一批设完之后要调 refreshPose()。
        void setBoneOverride(int32_t boneIndex, double3 worldPosition, Quaternion worldRotation);

        void clearBoneOverride(int32_t boneIndex);

        void clearAllBoneOverrides();

        // 用当前时间立刻重新计算一遍蒙皮矩阵并上传——setBoneOverride()/clearBoneOverride() 一批
        // 调完之后调一次这个, 不依赖 onTick() 本来的调度时机(onTick() 跑在 Pre_Physics, 比
        // RagdollInstance 读物理结果的 Post_Physics 更早, 等下一次 onTick() 会晚一帧生效)。
        void refreshPose();

    private:
        // 骨骼数变了(换 mesh、或者换成骨骼数不同的另一个骨架)就要重新分配 SkinningManager
        // 的矩阵表 range——onRegister()/setMesh() 各自调一次。顺带调一次
        // uploadSkinningMatricesAtTime(), 保证 mWorldMatricesScratch 立刻跟当前骨架同步
        // (大小、内容都对), 不然 getSocketWorldTransform() 在下一次真正 tick 之前可能读到
        // 空的或者跟新骨架对不上大小的旧数据
        void updateSkinningAllocation();

        // 按给定时间点算出蒙皮矩阵并上传——onTick() 每帧调(用推进后的 mCurrentTime),
        // setAnimation() 也会调一次(用 0.0f, 即动画第一帧), 让编辑模式下(onTick 不跑)换完
        // 动画立刻能看到第一帧的姿势, 而不是一直停在 bind pose。骨架不存在(mesh 没设置)或
        // 组件还没注册时是 no-op, 跟 updateSkinningAllocation() 同样的判断
        void uploadSkinningMatricesAtTime(float time);

        // 重建 mBoneTrackIndices——骨架的骨骼下标 -> mAnimation 里对应 Track 的下标, 按名字
        // 查一次的结果(Animation::resolveBoneTrackIndex())在动画和骨架都没换的情况下每帧不变,
        // 缓存下来避免 uploadSkinningMatricesAtTime() 每帧每根骨骼都付一次哈希查找的开销。
        // setAnimation()(动画换了)和 updateSkinningAllocation()(骨架可能换了, 骨骼下标
        // 顺序跟着变)各调一次。没有动画或没有骨架时清空
        void rebuildBoneTrackIndexCache();

    private:
        TRef<SkeletalMesh> mSkeletalMesh;

        TRef<Animation> mAnimation;
        bool mPlaying = true;
        float mCurrentTime = 0.0f;
        float mPlaybackSpeed = 1.0f;

        // uploadSkinningMatricesAtTime() 算世界矩阵用的临时缓冲区——纯函数内部中间结果,
        // 不会传到函数外面, 骨骼数没变时复用同一块内存, 不用每帧重新 malloc。不能用同样的
        // 思路复用 skinningMatrices(那份数据每帧都要 move 进渲染线程的 lambda, 本来就得
        // 是一份新分配)
        std::vector<float4x4> mWorldMatricesScratch;

        // 见 rebuildBoneTrackIndexCache() 的注释——下标是骨架的骨骼下标, 值是 mAnimation
        // 对应的 Track 下标(-1 表示这根骨骼没有对应 Track)
        std::vector<int32_t> mBoneTrackIndices;

        // 下标对齐骨架的骨骼下标, 跟 mWorldMatricesScratch 一起在 uploadSkinningMatricesAtTime()
        // 里按当前骨骼数 resize——存的是模型空间矩阵(相对这个 Entity), 跟 mWorldMatricesScratch
        // 同一个参考系, 这样参与拓扑序累乘时不需要对被覆盖的骨骼做任何特殊处理。不参与序列化,
        // 跟 mWorldMatricesScratch 一样是纯运行时中间数据。
        std::vector<std::optional<float4x4>> mBoneOverrides;
    };

    template <> Type* Reflection::createType<SkeletalMeshRenderer>();
}
