#include "Skeleton.h"

namespace hyp
{
    REGISTER_CLASS(Skeleton)

    void Skeleton::postLoad()
    {
        calculateInverseBindMatrices();
        for (Socket& socket : mSockets)
            resolveSocket(socket);
    }

    uint32_t Skeleton::addBone(HString name, int32_t parentIndex, float3 localPosition, Quaternion localRotation, float3 localScale)
    {
        assert(parentIndex < static_cast<int32_t>(mBones.size()));
        Bone bone;
        bone.name = name;
        bone.parentIndex = parentIndex;
        bone.localPosition = localPosition;
        bone.localRotation = localRotation;
        bone.localScale = localScale;
        mBones.push_back(bone);
        return static_cast<uint32_t>(mBones.size() - 1);
    }

    int32_t Skeleton::findBoneIndex(HString name) const noexcept
    {
        for (size_t i = 0; i < mBones.size(); ++i)
        {
            if (mBones[i].name == name)
                return static_cast<int32_t>(i);
        }
        return -1;
    }

    uint32_t Skeleton::addSocket(HString name, HString boneName, float3 localPosition, Quaternion localRotation, float3 localScale)
    {
        Socket socket;
        socket.name = name;
        socket.boneName = boneName;
        socket.localPosition = localPosition;
        socket.localRotation = localRotation;
        socket.localScale = localScale;
        resolveSocket(socket);
        mSockets.push_back(socket);
        return static_cast<uint32_t>(mSockets.size() - 1);
    }

    int32_t Skeleton::findSocketIndex(HString name) const noexcept
    {
        for (size_t i = 0; i < mSockets.size(); ++i)
        {
            if (mSockets[i].name == name)
                return static_cast<int32_t>(i);
        }
        return -1;
    }

    void Skeleton::resolveSocket(Socket& socket) const noexcept
    {
        socket.boneIndex = findBoneIndex(socket.boneName);

        float3x3 rotationBasis = glm::mat3_cast(socket.localRotation);
        float4x4 localMatrix(rotationBasis);
        localMatrix[0] *= socket.localScale.x;
        localMatrix[1] *= socket.localScale.y;
        localMatrix[2] *= socket.localScale.z;
        localMatrix[3] = float4(socket.localPosition, 1.0f);
        socket.localMatrix = localMatrix;
    }

    void Skeleton::calculateInverseBindMatrices()
    {
        // mBones 保证是拓扑序(parentIndex 恒小于自身下标, 见 Bone::parentIndex 的注释),
        // 一次线性遍历就能把每根骨骼的 bind pose 世界矩阵顺着父链累乘出来, 不需要递归。
        mInverseBindMatrices.resize(mBones.size());
        std::vector<float4x4> worldBindMatrices(mBones.size());
        for (size_t i = 0; i < mBones.size(); ++i)
        {
            const Bone& bone = mBones[i];
            // TRS 直接组装, 不走 translate()/scale() 各自构造单位矩阵再做两次完整 4x4×4x4
            // 矩阵乘法那条路——跟 SkeletalMeshRenderer::uploadSkinningMatricesAtTime() 保持
            // 同一套写法, 数学上完全等价
            float3x3 rotationBasis = glm::mat3_cast(bone.localRotation);
            float4x4 localMatrix(rotationBasis);
            localMatrix[0] *= bone.localScale.x;
            localMatrix[1] *= bone.localScale.y;
            localMatrix[2] *= bone.localScale.z;
            localMatrix[3] = float4(bone.localPosition, 1.0f);

            worldBindMatrices[i] = bone.parentIndex >= 0 ? worldBindMatrices[bone.parentIndex] * localMatrix : localMatrix;
            mInverseBindMatrices[i] = glm::inverse(worldBindMatrices[i]);
        }
    }

    template <> Type* Reflection::createType<Skeleton::Bone>()
    {
        auto s = new TStructure<Skeleton::Bone>("Skeleton.Bone");
        s->addProperty("Name", &Skeleton::Bone::name);
        s->addProperty("ParentIndex", &Skeleton::Bone::parentIndex);
        s->addProperty("LocalPosition", &Skeleton::Bone::localPosition);
        s->addProperty("LocalRotation", &Skeleton::Bone::localRotation);
        s->addProperty("LocalScale", &Skeleton::Bone::localScale);
        return s;
    }

    template <> Type* Reflection::createType<Skeleton::Socket>()
    {
        auto s = new TStructure<Skeleton::Socket>("Skeleton.Socket");
        s->addProperty("Name", &Skeleton::Socket::name);
        s->addProperty("BoneName", &Skeleton::Socket::boneName);
        s->addProperty("LocalPosition", &Skeleton::Socket::localPosition);
        s->addProperty("LocalRotation", &Skeleton::Socket::localRotation);
        s->addProperty("LocalScale", &Skeleton::Socket::localScale);
        return s;
    }

    template <> Type* Reflection::createType<Skeleton>()
    {
        auto c = new TClass<Skeleton>("Skeleton");
        c->addProperty("Bones", &Skeleton::mBones);
        c->addProperty("Sockets", &Skeleton::mSockets);
        return c;
    }
}
