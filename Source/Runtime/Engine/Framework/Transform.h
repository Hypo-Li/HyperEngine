#pragma once
#include <Core/Math/Vector.h>
#include <Core/Math/Quaternion.h>
#include <Core/Math/Matrix.h>
#include <Core/Object/Object.h>

namespace hyp
{
    enum class ReparentMode
    {
        Keep_World,
        Keep_Local,
        Reset_Local,
    };

    class Transform : public Object
    {
        REFLECT_CLASS(Transform)
    public:
        Transform() = default;

        virtual ~Transform() = default;

        void setLocalPosition(const double3& localPosition) noexcept;

        void setLocalRotation(Quaternion localRotation) noexcept;

        void setLocalRotationEuler(float3 eulerAngles) noexcept;

        void setLocalScale(float3 localScale) noexcept;

        void setLocalTransform(const double3& localPosition, Quaternion localRotation, float3 localScale) noexcept;

        void setWorldPosition(const double3& worldPosition) noexcept;

        void setWorldRotation(Quaternion worldRotation) noexcept;

        void setWorldRotationEuler(float3 eulerAngles) noexcept;

        void setWorldScale(float3 worldScale) noexcept;

        void setWorldTransform(const double3& worldPosition, Quaternion worldRotation, float3 worldScale) noexcept;

        const double3& getLocalPosition() const noexcept;

        Quaternion getLocalRotation() const noexcept;

        float3 getLocalRotationEuler() const noexcept;

        float3 getLocalScale() const noexcept;

        const double3& getWorldPosition() const noexcept;

        Quaternion getWorldRotation() const noexcept;

        float3 getWorldRotationEuler() const noexcept;

        float3 getWorldScale() const noexcept;

        const double4x4& getWorldMatrix() const noexcept;

        bool setParent(Transform* newParent, ReparentMode reparentMode);

        Transform* getParent() const noexcept
        {
            return mParent;
        }

        void addChild(Transform* newChild, ReparentMode reparentMode);

        bool removeChild(Transform* child, ReparentMode reparentMode);

        void removeChildren(size_t startIndex, size_t count, ReparentMode reparentMode);

        size_t getChildCount() const noexcept
        {
            return mChildren.size();
        }

        Transform* getChild(size_t index) const noexcept
        {
            if (index < mChildren.size())
                return mChildren[index];
            return nullptr;
        }

        static constexpr float3 ForwardVector = { 0, 1, 0 };

        static constexpr float3 UpVector = { 0, 0, 1 };

        static constexpr float3 RightVector = { 1, 0, 0 };

        float3 getLocalForwardVector() const noexcept;

        float3 getLocalUpVector() const noexcept;

        float3 getLocalRightVector() const noexcept;

        float3 getWorldForwardVector() const noexcept;

        float3 getWorldUpVector() const noexcept;

        float3 getWorldRightVector() const noexcept;

        uint32_t getVersion() const noexcept
        {
            return mVersion;
        }

    private:
        void notifyChildrenDirtyWorld() noexcept;

        void updateWorldFromLocal() const noexcept;

        void updateWorldMatrix() const noexcept;

        void setParentInternal(Transform* newParent, ReparentMode reparentMode);

        void addChildInternal(Transform* newChild);

        void removeChildInternal(Transform* child);

        static float3 quaternionToEulerAnglesZXY(Quaternion quaternion) noexcept;

        static Quaternion eulerAnglesZXYToQuaternion(float3 eulerAngles) noexcept;

    private:
        mutable double3 mLocalPosition{ 0, 0, 0 };
        mutable Quaternion mLocalRotation{ 1, 0, 0, 0 };
        mutable float3 mLocalScale{ 1, 1, 1 };

        mutable double3 mWorldPosition{ 0, 0, 0 };
        mutable Quaternion mWorldRotation{ 1, 0, 0, 0 };
        mutable float3 mWorldScale{ 1, 1, 1 };
        mutable bool mWorldDirty = false;

        mutable double4x4 mWorldMatrix{ glm::identity<double4x4>() };
        mutable bool mWorldMatrixDirty = false;

        Transform* mParent = nullptr;
        std::vector<Transform*> mChildren;

        uint32_t mVersion = 0;
    };

    struct ITransformListener
    {
        virtual void onTransformUpdate(const Transform& transform) = 0;
    };

    template <> Type* Reflection::createType<Transform>();
}
