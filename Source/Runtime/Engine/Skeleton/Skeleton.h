#pragma once
#include <Core/Object/Object.h>
#include <Core/Common/HString.h>
#include <Core/Math/Vector.h>
#include <Core/Math/Quaternion.h>
#include <Core/Math/Matrix.h>

namespace hyp
{
    // 骨骼层级——独立于任何具体 SkeletalMesh 的资产, 多个 SkeletalMesh(不同 LOD/换装/角色)
    // 可以共用同一份 Skeleton, 同一份动画数据就能在它们之间直接复用
    class Skeleton : public Object
    {
        REFLECT_CLASS(Skeleton)
    public:
        struct Bone
        {
            HString name;
            int32_t parentIndex = -1; // -1 表示根骨骼; 数组要求 parentIndex 小于自身下标(拓扑序)
            float3 localPosition{ 0, 0, 0 };
            Quaternion localRotation{ 1, 0, 0, 0 };
            float3 localScale{ 1, 1, 1 };
        };

        // 挂点——name 是外部(Transform::mSocketName)引用的名字, 独立于骨骼名字, 允许同一根
        // 骨骼上挂多个用途不同、偏移不同的点。localPosition/Rotation/Scale 是骨骼空间到挂点的
        // 固定偏移, 跟被挂物体自己的 local TRS 是两层独立的偏移(前者是资产级别、所有挂载共用
        // 的设计值, 后者是每次挂载时的个体微调)
        struct Socket
        {
            HString name;
            HString boneName;
            float3 localPosition{ 0, 0, 0 };
            Quaternion localRotation{ 1, 0, 0, 0 };
            float3 localScale{ 1, 1, 1 };

            // 派生缓存, 不参与反射——boneIndex 是 boneName 在 mBones 里的下标, localMatrix 是
            // localPosition/Rotation/Scale 直接 TRS 组装的结果, 在 addSocket()/postLoad() 里
            // 解析一次, 避免 SkeletalMeshRenderer::updateSocketAttachments() 每帧每个挂载都
            // 重新做一次线性查找 + quat_cast + 缩放
            int32_t boneIndex = -1;
            float4x4 localMatrix{ 1.0f };
        };

        Skeleton() = default;

        ~Skeleton() noexcept = default;

        void postLoad() override;

        uint32_t addBone(HString name, int32_t parentIndex, float3 localPosition, Quaternion localRotation, float3 localScale);

        uint32_t getBoneCount() const noexcept
        {
            return static_cast<uint32_t>(mBones.size());
        }

        const Bone& getBone(uint32_t index) const noexcept
        {
            assert(index < mBones.size());
            return mBones[index];
        }

        // 没有同名骨骼时返回 -1
        int32_t findBoneIndex(HString name) const noexcept;

        void calculateInverseBindMatrices();

        const float4x4& getInverseBindMatrix(uint32_t index) const noexcept
        {
            assert(index < mInverseBindMatrices.size());
            return mInverseBindMatrices[index];
        }

        uint32_t addSocket(HString name, HString boneName, float3 localPosition, Quaternion localRotation, float3 localScale);

        uint32_t getSocketCount() const noexcept
        {
            return static_cast<uint32_t>(mSockets.size());
        }

        const Socket& getSocket(uint32_t index) const noexcept
        {
            assert(index < mSockets.size());
            return mSockets[index];
        }

        // 没有同名挂点时返回 -1
        int32_t findSocketIndex(HString name) const noexcept;

    private:
        void resolveSocket(Socket& socket) const noexcept;

    private:
        std::vector<Bone> mBones;
        std::vector<Socket> mSockets;

        std::vector<float4x4> mInverseBindMatrices;
    };

    template <> Type* Reflection::createType<Skeleton::Bone>();

    template <> Type* Reflection::createType<Skeleton::Socket>();

    template <> Type* Reflection::createType<Skeleton>();
}
