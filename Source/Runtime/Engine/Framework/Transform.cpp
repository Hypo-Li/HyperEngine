#include "Transform.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace hyp
{
    REGISTER_CLASS(Transform)

    void Transform::setLocalPosition(const double3& localPosition) noexcept
    {
        mLocalPosition = localPosition;
        mWorldDirty = true;
        mWorldMatrixDirty = true;
        notifyChildrenDirtyWorld();
        mVersion++;
    }

    void Transform::setLocalRotation(Quaternion localRotation) noexcept
    {
        mLocalRotation = localRotation;
        mWorldDirty = true;
        mWorldMatrixDirty = true;
        mVersion++;
        notifyChildrenDirtyWorld();
    }

    void Transform::setLocalRotationEuler(float3 eulerAngles) noexcept
    {
        setLocalRotation(eulerAnglesZXYToQuaternion(eulerAngles));
    }

    void Transform::setLocalScale(float3 localScale) noexcept
    {
        mLocalScale = localScale;
        mWorldDirty = true;
        mWorldMatrixDirty = true;
        mVersion++;
        notifyChildrenDirtyWorld();
    }

    void Transform::setLocalTransform(const double3& localPosition, Quaternion localRotation, float3 localScale) noexcept
    {
        mLocalPosition = localPosition;
        mLocalRotation = localRotation;
        mLocalScale = localScale;
        mWorldDirty = true;
        mWorldMatrixDirty = true;
        mVersion++;
        notifyChildrenDirtyWorld();
    }

    void Transform::setWorldPosition(const double3& worldPosition) noexcept
    {
        mWorldPosition = worldPosition;
        if (mParent)
        {
            auto& parentWorldPosition = mParent->getWorldPosition();
            Quaternion parentWorldRotation = mParent->getWorldRotation();
            float3 parentWorldScale = mParent->getWorldScale();
            mLocalPosition = (glm::dquat(glm::inverse(parentWorldRotation)) * (mWorldPosition - parentWorldPosition)) / double3(parentWorldScale);
        }
        else
        {
            mLocalPosition = mWorldPosition;
        }
        mWorldMatrixDirty = true;
        mVersion++;
        notifyChildrenDirtyWorld();
    }

    void Transform::setWorldRotation(Quaternion worldRotation) noexcept
    {
        mWorldRotation = worldRotation;
        if (mParent)
        {
            Quaternion parentWorldRotation = mParent->getWorldRotation();
            mLocalRotation = glm::inverse(parentWorldRotation) * mWorldRotation;
        }
        else
        {
            mLocalRotation = mWorldRotation;
        }
        mWorldMatrixDirty = true;
        mVersion++;
        notifyChildrenDirtyWorld();
    }

    void Transform::setWorldRotationEuler(float3 eulerAngles) noexcept
    {
        setWorldRotation(eulerAnglesZXYToQuaternion(eulerAngles));
    }

    void Transform::setWorldScale(float3 worldScale) noexcept
    {
        mWorldScale = worldScale;
        if (mParent)
        {
            float3 parentWorldScale = mParent->getWorldScale();
            mLocalScale = mWorldScale / parentWorldScale;
        }
        else
        {
            mLocalScale = mWorldScale;
        }
        mWorldMatrixDirty = true;
        mVersion++;
        notifyChildrenDirtyWorld();
    }

    void Transform::setWorldTransform(const double3& worldPosition, Quaternion worldRotation, float3 worldScale) noexcept
    {
        mWorldPosition = worldPosition;
        mWorldRotation = worldRotation;
        mWorldScale = worldScale;

        if (mParent)
        {
            auto& parentWorldPosition = mParent->getWorldPosition();
            Quaternion parentWorldRotation = mParent->getWorldRotation();
            float3 parentWorldScale = mParent->getWorldScale();
            mLocalPosition = (glm::dquat(glm::inverse(parentWorldRotation)) * (mWorldPosition - parentWorldPosition)) / double3(parentWorldScale);
            mLocalRotation = glm::inverse(parentWorldRotation) * mWorldRotation;
            mLocalScale = mWorldScale / parentWorldScale;
        }
        else
        {
            mLocalPosition = mWorldPosition;
            mLocalRotation = mWorldRotation;
            mLocalScale = mWorldScale;
        }

        mWorldMatrixDirty = true;
        mVersion++;
        notifyChildrenDirtyWorld();
    }

    const double3& Transform::getLocalPosition() const noexcept
    {
        return mLocalPosition;
    }

    Quaternion Transform::getLocalRotation() const noexcept
    {
        return mLocalRotation;
    }

    float3 Transform::getLocalRotationEuler() const noexcept
    {
        return quaternionToEulerAnglesZXY(mLocalRotation);
    }

    float3 Transform::getLocalScale() const noexcept
    {
        return mLocalScale;
    }

    const double3& Transform::getWorldPosition() const noexcept
    {
        if (mWorldDirty)
            updateWorldFromLocal();
        return mWorldPosition;
    }

    Quaternion Transform::getWorldRotation() const noexcept
    {
        if (mWorldDirty)
            updateWorldFromLocal();
        return mWorldRotation;
    }

    float3 Transform::getWorldRotationEuler() const noexcept
    {
        if (mWorldDirty)
            updateWorldFromLocal();
        return quaternionToEulerAnglesZXY(mWorldRotation);
    }

    float3 Transform::getWorldScale() const noexcept
    {
        if (mWorldDirty)
            updateWorldFromLocal();
        return mWorldScale;
    }

    const double4x4& Transform::getWorldMatrix() const noexcept
    {
        if (mWorldMatrixDirty)
            updateWorldMatrix();
        return mWorldMatrix;
    }

    bool Transform::setParent(Transform* newParent, ReparentMode reparentMode)
    {
        if (newParent == this || newParent == mParent)
            return false;

        // 循环检查, 防止 newParent 为自身后代
        if (newParent)
        {
            Transform* ancestor = newParent;
            while (ancestor)
            {
                if (ancestor == this)
                    return false;
                ancestor = ancestor->mParent;
            }
        }

        if (mParent)
            mParent->removeChildInternal(this);

        if (newParent)
            newParent->addChildInternal(this);

        setParentInternal(newParent, reparentMode);

        return true;
    }

    void Transform::addChild(Transform* newChild, ReparentMode reparentMode)
    {
        if (!newChild || newChild == this || newChild->getParent() == this)
            return;

        newChild->setParent(this, reparentMode);
    }

    bool Transform::removeChild(Transform* child, ReparentMode reparentMode)
    {
        if (!child || child == this || child->getParent() != this)
            return false;

        return child->setParent(nullptr, reparentMode);
    }

    void Transform::removeChildren(size_t startIndex, size_t count, ReparentMode reparentMode)
    {
        if (startIndex >= mChildren.size())
            return;

        size_t removedCount = std::min(count, mChildren.size() - startIndex);
        if (removedCount == 0)
            return;

        // 确保父节点数据是新的，避免子节点反算时重复触发递归
        if (mWorldDirty)
        {
            updateWorldFromLocal();
        }

        auto startIt = mChildren.begin() + startIndex;
        auto endIt = startIt + removedCount;

        // 拷贝指针，先清理容器，防止重入 Bug
        std::vector<Transform*> removedList(startIt, endIt);
        mChildren.erase(startIt, endIt);

        for (Transform* child : removedList)
        {
            // 此时 child->mParent 还是 this，符合 setParentInternal 的预期
            child->setParentInternal(nullptr, reparentMode);
        }
    }

    float3 Transform::getLocalForwardVector() const noexcept
    {
        return getLocalRotation() * ForwardVector;
    }

    float3 Transform::getLocalUpVector() const noexcept
    {
        return getLocalRotation() * UpVector;
    }

    float3 Transform::getLocalRightVector() const noexcept
    {
        return getLocalRotation() * RightVector;
    }

    float3 Transform::getWorldForwardVector() const noexcept
    {
        return getWorldRotation() * ForwardVector;
    }

    float3 Transform::getWorldUpVector() const noexcept
    {
        return getWorldRotation() * UpVector;
    }

    float3 Transform::getWorldRightVector() const noexcept
    {
        return getWorldRotation() * RightVector;
    }

    void Transform::notifyChildrenDirtyWorld() noexcept
    {
        for (Transform* child : mChildren)
        {
            child->mWorldDirty = true;
            child->mWorldMatrixDirty = true;
            child->mVersion++;
            child->notifyChildrenDirtyWorld();
        }
    }

    void Transform::updateWorldFromLocal() const noexcept
    {
        assert(mWorldDirty);
        if (!mParent)
        {
            mWorldPosition = mLocalPosition;
            mWorldRotation = mLocalRotation;
            mWorldScale = mLocalScale;
        }
        else
        {
            auto& parentWorldPosition = mParent->getWorldPosition();
            Quaternion parentWorldRotation = mParent->getWorldRotation();
            float3 parentWorldScale = mParent->getWorldScale();
            mWorldPosition = parentWorldPosition + glm::dquat(parentWorldRotation) * (double3(parentWorldScale) * mLocalPosition);
            mWorldRotation = parentWorldRotation * mLocalRotation;
            mWorldScale = parentWorldScale * mLocalScale;
        }
        mWorldDirty = false;
    }

    void Transform::updateWorldMatrix() const noexcept
    {
        assert(mWorldMatrixDirty);
        if (mWorldDirty)
            updateWorldFromLocal();
        auto T = glm::translate(glm::identity<double4x4>(), mWorldPosition);
        auto R = glm::mat4_cast(glm::dquat(mWorldRotation));
        auto S = glm::scale(glm::identity<double4x4>(), double3(mWorldScale));
        mWorldMatrix = T * R * S;
        mWorldMatrixDirty = false;
    }

    void Transform::setParentInternal(Transform* newParent, ReparentMode reparentMode)
    {
        // Parent 还未更换, 此时获取 Local Transform 和 World Transform 是安全的
        if (mWorldDirty)
            updateWorldFromLocal();

        double3 oldWorldPosition = getWorldPosition();
        Quaternion oldWorldRotation = getWorldRotation();
        float3 oldWorldScale = getWorldScale();

        mParent = newParent;

        if (reparentMode == ReparentMode::Keep_World)
        {
            setWorldTransform(oldWorldPosition, oldWorldRotation, oldWorldScale);
        }
        else if (reparentMode == ReparentMode::Keep_Local)
        {
            mWorldDirty = true;
            mWorldMatrixDirty = true;
            mVersion++;
            notifyChildrenDirtyWorld();
        }
        else if (reparentMode == ReparentMode::Reset_Local)
        {
            setLocalTransform({ 0, 0, 0 }, { 1, 0, 0, 0 }, { 1, 1, 1 });
        }
    }

    void Transform::addChildInternal(Transform* newChild)
    {
        mChildren.push_back(newChild);
    }

    void Transform::removeChildInternal(Transform* child)
    {
        auto it = std::find(mChildren.begin(), mChildren.end(), child);
        if (it != mChildren.end())
            mChildren.erase(it);
    }

    float3 Transform::quaternionToEulerAnglesZXY(Quaternion quaternion) noexcept
    {
        float3 result;
        glm::extractEulerAngleZXY(glm::mat4_cast(quaternion), result.z, result.x, result.y);
        result = glm::degrees(result);
        return result;
    }

    Quaternion Transform::eulerAnglesZXYToQuaternion(float3 eulerAngles) noexcept
    {
        Quaternion result;
        float3 eulerRadians = glm::radians(eulerAngles);
        Quaternion qz = glm::angleAxis(eulerRadians.z, float3{ 0, 0, 1 });
        Quaternion qx = glm::angleAxis(eulerRadians.x, float3{ 1, 0, 0 });
        Quaternion qy = glm::angleAxis(eulerRadians.y, float3{ 0, 1, 0 });
        result = qy * qx * qz;
        return result;
    }

    template <> Type* Reflection::createType<Transform>()
    {
        auto c = new TClass<Transform>("Transform");
        c->addProperty("Local Position", &Transform::mLocalPosition);
        c->addProperty("Local Rotation", &Transform::mLocalRotation);
        c->addProperty("Local Scale", &Transform::mLocalScale);
        c->addProperty("Parent", &Transform::mParent);
        c->addProperty("Children", &Transform::mChildren, CopyPolicy::Skip);
        return c;
    }
}
