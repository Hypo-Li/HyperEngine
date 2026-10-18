#pragma once
#include <Core/Common/HString.h>
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

    // Socket 挂载用——由骨骼所有者(比如 SkeletalMeshRenderer)实现, 通过 Transform::
    // setSocketProvider() 挂在自己的 Transform 上(见下面 mSocketProvider 的注释)。
    // 子节点的 mSocketName 非空时, Transform 会问自己的 mParent->mSocketProvider(如果有)
    // 按这个名字解出的世界变换, 代替 mParent 自己的刚性世界变换去做局部偏移的合成
    struct ISocketProvider
    {
        virtual bool getSocketWorldTransform(HString socketName, double3& outPosition, Quaternion& outRotation, float3& outScale) const = 0;
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

        // 没有父节点时没有同级可言: get 返回 0, set 返回 false
        size_t getSiblingIndex() const noexcept;

        // index 是移动之后的最终位置, 超出范围按末尾处理
        bool setSiblingIndex(size_t index);

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

        // 子节点侧: 非空时, 这个 Transform 在合成世界变换时会优先问 mParent->mSocketProvider
        // (如果有)按这个名字解出的世界变换当"有效父级", 解不出来(没设置 provider、或者
        // provider 没查到同名挂点)时退回普通刚性父节点——表现完全跟没设置一样, setParent()
        // 不会清空这个字段(挂载关系换了父级但挂点名字通常还想保留)
        HString getSocketName() const noexcept
        {
            return mSocketName;
        }

        void setSocketName(HString socketName) noexcept;

        // 父节点侧: 骨骼所有者(比如 SkeletalMeshRenderer)在 onRegister()/onUnregister() 里
        // 调用, 声明"我能替挂在我身上的子节点解析 socket 名字"。跟 mSocketName 是完全独立的
        // 两份数据, 分别装在关系的两端
        ISocketProvider* getSocketProvider() const noexcept
        {
            return mSocketProvider;
        }

        void setSocketProvider(ISocketProvider* provider) noexcept;

        // 递归标脏所有子孙节点的世界变换缓存——本来是内部维护自身不变量用的(setLocalPosition()
        // 这类调用), 没有任何前置条件, 公开出来同样给外部驱动方(比如骨骼动画)在结果变化后
        // 调用, 好让依赖 pull 的子树缓存重算
        void markChildrenWorldDirty() noexcept;

        virtual void postCopy() override;

        virtual void finalizeCopy() override;

        virtual void postLoad() override;

        virtual void finalizeLoad() override;

    private:
        // mParent 非空时, 算出用于合成局部偏移的"有效父级"世界变换——mSocketName 非空且
        // mParent->mSocketProvider 能解析出对应挂点时, 用挂点的世界变换代替 mParent 自己的
        // 刚性世界变换, 否则就是普通的 mParent->getWorld*()。updateWorldFromLocal() 和四个
        // world 系列 setter 共用这一处判断, 保证读(pull 世界变换)和写(world setter 反解
        // local)用的是同一个参照系, 不会出现两边不一致
        void getEffectiveParentTransform(double3& outPosition, Quaternion& outRotation, float3& outScale) const noexcept;

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

        HString mSocketName;

        // 不参与反射——运行时装配数据, 由骨骼所有者在自己注册/反注册时设置/清空, 复制/反
        // 序列化出来的新 Transform 始终从 nullptr 开始, 等对应组件 onRegister() 时自己补上
        ISocketProvider* mSocketProvider = nullptr;

        uint32_t mVersion = 0;
    };

    struct ITransformListener
    {
        virtual void onTransformUpdate(const Transform& transform) = 0;
    };

    template <> Type* Reflection::createType<Transform>();
}
