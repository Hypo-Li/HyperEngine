#pragma once
#include "Component.h"
#include "Transform.h"
namespace hyp
{
    enum class Mobility
    {
        Static,
        Dynamic,
    };

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

        Component* addComponent(Class* clazz);

        Component* addComponent(HString className);

        template <typename T> requires std::is_base_of_v<Component, T>
        T* addComponent()
        {
            return static_cast<T*>(addComponent(T::staticClass()));
        }

        template <typename T> requires std::is_base_of_v<Component, T>
        T* getComponent() const
        {
            Class* targetClass = Reflection::getClass<T>();
            for (Component* component : mComponents)
            {
                if (component->getClass() == targetClass)
                {
                    return component;
                }
            }
            return nullptr;
        }

        template <typename T> requires std::is_base_of_v<Component, T>
        std::vector<T*> getComponents() const
        {
            std::vector<T*> result;
            Class* targetClass = Reflection::getClass<T>();
            for (Component* component : mComponents)
            {
                if (component->getClass() == targetClass)
                    result.emplace_back(component);
            }
            return result;
        }

        std::span<const TRef<Component>> getAllComponentsForDebug() const noexcept
        {
            return mComponents;
        }

        bool removeComponent(Component* component);

        void removeAllComponents();

    private:
        HString mName;
        std::vector<TRef<Component>> mComponents;
        TRef<Transform> mTransform;
        Mobility mMobility = Mobility::Static;
    };

    template <> Type* Reflection::createType<Entity>();
}
