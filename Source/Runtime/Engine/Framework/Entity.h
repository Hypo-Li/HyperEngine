#pragma once
#include "Component.h"
#include "Transform.h"
namespace hyp
{
    class Scene;
    class Entity : public Object
    {
        friend class Scene;
        REFLECT_CLASS(Entity)
    public:
        Entity() noexcept;

        virtual ~Entity() noexcept;

        Scene* getScene() const noexcept;

        void setName(HString newName);

        HString getName() const noexcept
        {
            return mName;
        }

        Transform& getTransform() const noexcept
        {
            return *mTransform;
        }

        bool setParent(Entity* newParent, ReparentMode reparentMode);

        Entity* getParent() const noexcept;

        void addChild(Entity* newChild, ReparentMode reparentMode);

        size_t getChildCount() const noexcept;

        Entity* getChild(size_t index) const noexcept;

        bool removeChild(Entity* child, ReparentMode reparentMode);

        void removeChildren(size_t startIndex, size_t count, ReparentMode reparentMode);

        // 在同级里的位置: 有父节点时是父节点子列表里的下标, 否则是 Scene 里同级根的下标
        size_t getSiblingIndex() const;

        // index 是移动之后的最终位置
        bool setSiblingIndex(size_t index);

        Component* addComponent(Class* clazz);

        Component* addComponent(HString className);

        // 跟 addComponent() 不一样: addComponent() 走 createObject(), 每次都铸一个新 Guid,
        // 是给"用户在编辑器里新建一个组件"这种场景用的。这里接的是一个已经完整构造好、
        // 自带 Guid 的 Component(比如撤销"删除组件"时反序列化恢复出来的那个, 或者
        // Copier 复制出来的现成副本), 只补"塞进 mComponents + 注册进 Scene"这两步,
        // 不改它的身份。调用方要保证传入的 component 在构造时(反序列化用的 owner 参数、
        // Copier::copy() 的 newOwner 参数)已经以这个 Entity 作为 owner。
        void attachComponent(Component* component);

        template <typename T> requires std::is_base_of_v<Component, T>
        T* addComponent()
        {
            return static_cast<T*>(addComponent(T::staticClass()));
        }

        // guid 无效(默认)时找第一个匹配类型(含子类)的组件; guid 有效时额外要求 Guid 也
        // 匹配——undo 这类"组件可能已经被销毁重建、地址变了, 但 Guid 不变"的场景用这个
        // 精确定位, 不能只靠类型(同类型可能不止一个)。
        Component* getComponent(Class* clazz, Guid guid = Guid()) const;

        template <typename T> requires std::is_base_of_v<Component, T>
        T* getComponent(Guid guid = Guid()) const
        {
            return static_cast<T*>(getComponent(T::staticClass(), guid));
        }

        template <typename T> requires std::is_base_of_v<Component, T>
        std::vector<T*> getComponents() const
        {
            std::vector<T*> result;
            Class* targetClass = T::staticClass();
            for (Component* component : mComponents)
            {
                if (component->getClass()->isDerivedFrom(targetClass))
                    result.emplace_back(static_cast<T*>(component));
            }
            return result;
        }

        std::span<const TRef<Component>> getAllComponents() const noexcept
        {
            return mComponents;
        }

        bool removeComponent(Component* component);

        // 在组件列表里的位置, 不在列表里返回 Scene::kInvalidIndex
        size_t getComponentIndex(const Component* component) const;

        // index 是移动之后的最终位置, 超出范围按追加处理; 不在列表里返回 false
        bool moveComponent(Component* component, size_t index);

        void removeAllComponents();

        Mobility getMobility() const noexcept
        {
            return mMobility;
        }

        // 真的变了才通知——遍历 mComponents 挨个调 onMobilityChanged(), 和 finalizeLoad()
        // 遍历 mComponents 注册组件是同一种直接遍历的写法, 不需要单独的监听者注册机制
        // (Transform 那套 registerTransformListener 是因为 Transform 是独立对象、需要跨
        // Entity 的层级关系分发, Mobility 只是 Entity 自己的字段, 不需要那一层)
        void setMobility(Mobility mobility);

        bool isVisible() const noexcept
        {
            return mVisible;
        }

        void setVisible(bool visible);

        virtual void finalizeCopy() override;

        virtual void finalizeLoad() override;

    private:
        HString mName;
        std::vector<TRef<Component>> mComponents;
        TRef<Transform> mTransform;
        Mobility mMobility = Mobility::Static;
        bool mVisible = true;
    };

    // Mobility 这个枚举定义在 RenderBase/ProxyHandle.h(Renderer 也要用, 不能定义在 Engine 里
    // 造成依赖倒置), 但反射类型只有 Engine/Editor 用得到, 不该让 RenderBase 反过来依赖
    // Reflection——声明放在这里, 而不是跟着枚举定义放在 ProxyHandle.h
    template <> Type* Reflection::createType<Mobility>();

    template <> Type* Reflection::createType<Entity>();
}
