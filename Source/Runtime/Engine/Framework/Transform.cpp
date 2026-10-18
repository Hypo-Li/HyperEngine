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
        markChildrenWorldDirty();
        mVersion++;
    }

    void Transform::setLocalRotation(Quaternion localRotation) noexcept
    {
        mLocalRotation = localRotation;
        mWorldDirty = true;
        mWorldMatrixDirty = true;
        mVersion++;
        markChildrenWorldDirty();
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
        markChildrenWorldDirty();
    }

    void Transform::setLocalTransform(const double3& localPosition, Quaternion localRotation, float3 localScale) noexcept
    {
        mLocalPosition = localPosition;
        mLocalRotation = localRotation;
        mLocalScale = localScale;
        mWorldDirty = true;
        mWorldMatrixDirty = true;
        mVersion++;
        markChildrenWorldDirty();
    }

    void Transform::setWorldPosition(const double3& worldPosition) noexcept
    {
        mWorldPosition = worldPosition;
        if (mParent)
        {
            double3 parentWorldPosition;
            Quaternion parentWorldRotation;
            float3 parentWorldScale;
            getEffectiveParentTransform(parentWorldPosition, parentWorldRotation, parentWorldScale);
            mLocalPosition = (glm::dquat(glm::inverse(parentWorldRotation)) * (mWorldPosition - parentWorldPosition)) / double3(parentWorldScale);
        }
        else
        {
            mLocalPosition = mWorldPosition;
        }
        mWorldMatrixDirty = true;
        mVersion++;
        markChildrenWorldDirty();
    }

    void Transform::setWorldRotation(Quaternion worldRotation) noexcept
    {
        mWorldRotation = worldRotation;
        if (mParent)
        {
            double3 parentWorldPosition;
            Quaternion parentWorldRotation;
            float3 parentWorldScale;
            getEffectiveParentTransform(parentWorldPosition, parentWorldRotation, parentWorldScale);
            mLocalRotation = glm::inverse(parentWorldRotation) * mWorldRotation;
        }
        else
        {
            mLocalRotation = mWorldRotation;
        }
        mWorldMatrixDirty = true;
        mVersion++;
        markChildrenWorldDirty();
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
            double3 parentWorldPosition;
            Quaternion parentWorldRotation;
            float3 parentWorldScale;
            getEffectiveParentTransform(parentWorldPosition, parentWorldRotation, parentWorldScale);
            mLocalScale = mWorldScale / parentWorldScale;
        }
        else
        {
            mLocalScale = mWorldScale;
        }
        mWorldMatrixDirty = true;
        mVersion++;
        markChildrenWorldDirty();
    }

    void Transform::setWorldTransform(const double3& worldPosition, Quaternion worldRotation, float3 worldScale) noexcept
    {
        mWorldPosition = worldPosition;
        mWorldRotation = worldRotation;
        mWorldScale = worldScale;

        if (mParent)
        {
            double3 parentWorldPosition;
            Quaternion parentWorldRotation;
            float3 parentWorldScale;
            getEffectiveParentTransform(parentWorldPosition, parentWorldRotation, parentWorldScale);
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
        markChildrenWorldDirty();
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

    size_t Transform::getSiblingIndex() const noexcept
    {
        if (!mParent)
            return 0;

        auto it = std::find(mParent->mChildren.begin(), mParent->mChildren.end(), this);
        return static_cast<size_t>(it - mParent->mChildren.begin());
    }

    bool Transform::setSiblingIndex(size_t index)
    {
        if (!mParent)
            return false;

        std::vector<Transform*>& siblings = mParent->mChildren;
        auto it = std::find(siblings.begin(), siblings.end(), this);
        assert(it != siblings.end());
        siblings.erase(it);
        siblings.insert(siblings.begin() + std::min(index, siblings.size()), this);
        return true;
    }

    void Transform::setSocketName(HString socketName) noexcept
    {
        mSocketName = socketName;
        mWorldDirty = true;
        mWorldMatrixDirty = true;
        mVersion++;
        markChildrenWorldDirty();
    }

    void Transform::setSocketProvider(ISocketProvider* provider) noexcept
    {
        mSocketProvider = provider;
        markChildrenWorldDirty();
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

    void Transform::markChildrenWorldDirty() noexcept
    {
        for (Transform* child : mChildren)
        {
            child->mWorldDirty = true;
            child->mWorldMatrixDirty = true;
            child->mVersion++;
            child->markChildrenWorldDirty();
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
            double3 parentWorldPosition;
            Quaternion parentWorldRotation;
            float3 parentWorldScale;
            getEffectiveParentTransform(parentWorldPosition, parentWorldRotation, parentWorldScale);
            mWorldPosition = parentWorldPosition + glm::dquat(parentWorldRotation) * (double3(parentWorldScale) * mLocalPosition);
            mWorldRotation = parentWorldRotation * mLocalRotation;
            mWorldScale = parentWorldScale * mLocalScale;
        }
        mWorldDirty = false;
    }

    void Transform::getEffectiveParentTransform(double3& outPosition, Quaternion& outRotation, float3& outScale) const noexcept
    {
        assert(mParent);
        if (!mSocketName.empty() && mParent->mSocketProvider &&
            mParent->mSocketProvider->getSocketWorldTransform(mSocketName, outPosition, outRotation, outScale))
        {
            return;
        }
        outPosition = mParent->getWorldPosition();
        outRotation = mParent->getWorldRotation();
        outScale = mParent->getWorldScale();
    }

    void Transform::updateWorldMatrix() const noexcept
    {
        assert(mWorldMatrixDirty);
        if (mWorldDirty)
            updateWorldFromLocal();
        double3x3 rotationBasis = glm::mat3_cast(glm::dquat(mWorldRotation));
        double4x4 worldMatrix(rotationBasis);
        worldMatrix[0] *= mWorldScale.x;
        worldMatrix[1] *= mWorldScale.y;
        worldMatrix[2] *= mWorldScale.z;
        worldMatrix[3] = double4(mWorldPosition, 1.0);
        mWorldMatrix = worldMatrix;
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
            markChildrenWorldDirty();
        }
        else if (reparentMode == ReparentMode::Reset_Local)
        {
            setLocalTransform({ 0, 0, 0 }, { 1, 0, 0, 0 }, { 1, 1, 1 });
        }
    }

    void Transform::postCopy()
    {
        // mWorldPosition/mWorldRotation/mWorldScale/mWorldMatrix 都没有参与反射(它们是由
        // Local Position/Rotation/Scale + Parent 链推导出来的缓存, 不是需要独立保存/复制的
        // 数据), 复制完成后这几个字段还停在默认构造值上, mWorldDirty/mWorldMatrixDirty
        // 也还是默认的 false——不强制标脏的话, getWorldPosition()/getWorldMatrix() 会因为
        // "看起来不脏"直接返回这份错误的缓存, 不会真正从已经复制好的 Local Transform 和
        // Parent 链重新算出来。
        mWorldDirty = true;
        mWorldMatrixDirty = true;
        mVersion++;
    }

    void Transform::finalizeCopy()
    {
        // Children 是 CopyPolicy::Ignore, 复制过程中完全没被处理, 此刻仍是默认构造的空
        // vector; 而 Parent 在 populate 阶段已经按 CopyPolicy 解析完毕、对本次操作涉及的
        // 所有副本都已经落定, 反过来把自己塞回 mParent 的孩子列表即可复原这一半关系。
        // 这里直接用 addChildInternal 而不是公开的 setParent(): 这不是一次真正的"改父子
        // 关系"操作(mParent 本身不会再变), 不需要 setParent() 里的循环检测和世界变换保持
        // 逻辑, 单纯把复制时被跳过的另一半数据补上。
        if (mParent)
            mParent->addChildInternal(this);
    }

    void Transform::postLoad()
    {
        // 跟 postCopy() 同样的道理: World* 系列字段和 mWorldDirty/mWorldMatrixDirty 都没有
        // 参与反射, load 完成后这几个字段还停在默认构造值上——不强制标脏,
        // getWorldPosition()/getWorldMatrix() 会因为"看起来不脏"直接返回这份错误的缓存,
        // 不会真正从已经读回来的 Local Transform 和 Parent 链重新算出来。
        mWorldDirty = true;
        mWorldMatrixDirty = true;
        mVersion++;
    }

    void Transform::finalizeLoad()
    {
        // Parent 是普通反射属性, load 阶段已经解析完毕、指向的对象也已经落定。如果 mParent
        // 这时候在这次反序列化范围之外(比如撤销删除单个 Entity, 父节点是外部现查现给的活
        // 对象), 它的 mChildren 从没被这次 load 碰过, 这里要补上这一半; 但如果 mParent 也在
        // 这次范围之内(整棵子树一起读回来), 它的 mChildren 早就在自己的属性反序列化阶段
        // 正常填过了, addChildInternal 会重复 push——查重后再决定要不要补。
        if (mParent)
            mParent->addChildInternal(this);

        // Children 同样是普通反射属性, load 阶段已经把它填成了"当时"那批子节点的活指针——
        // 但每个子节点自己的 mParent 是它们各自的数据, 不属于这次反序列化的对象图, 不会被
        // 这次 load 碰到, 这期间完全可能已经被别的操作重新挂到别处(比如这个 Transform 的
        // owner 被删除时, Scene::destroyEntity() 会先把子节点挪到别的 parent 下面, 这个
        // Transform 才轮到被序列化/反序列化)。这里反过来把每个子节点的 mParent 纠正回来,
        // 顺带从它当前(可能是别处)的 parent 手里摘掉——不能调用公开的 setParent(), 它会
        // 往 mChildren 里重复 push 一次(load 那份已经填过了, 这里只用得上 mParent 那一半)。
        // mParent 已经正确指向 this 时(整棵子树一起读回来的正常情况)跳过, 不然
        // Keep_World 那趟 local/world 往返换算会给刚读出来的精确值引入没必要的浮点误差。
        for (Transform* child : mChildren)
        {
            Transform* staleParent = child->mParent;
            if (staleParent == this)
                continue;
            if (staleParent)
                staleParent->removeChildInternal(child);
            child->setParentInternal(this, ReparentMode::Keep_World);
        }
    }

    void Transform::addChildInternal(Transform* newChild)
    {
        // mParent 也在同一次反序列化范围内时, mChildren 早就被属性系统正常填过一次了
        // (见 finalizeLoad() 的注释)——查重, 不会把同一个子节点塞进来两次。
        if (std::find(mChildren.begin(), mChildren.end(), newChild) != mChildren.end())
            return;
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
        // Intrinsic 顺序
        result = qz * qx * qy;
        return result;
    }

    template <> Type* Reflection::createType<Transform>()
    {
        auto c = new TClass<Transform>("Transform");
        c->addProperty("Local Position", &Transform::mLocalPosition);
        c->addProperty("Local Rotation", &Transform::mLocalRotation);
        c->addProperty("Local Scale", &Transform::mLocalScale);
        c->addProperty("Parent", &Transform::mParent);
        c->addProperty("Children", &Transform::mChildren, CopyPolicy::Ignore);
        c->addProperty("Socket Name", &Transform::mSocketName);
        return c;
    }
}
