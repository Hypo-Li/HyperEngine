#pragma once
#include <Core/Object/Object.h>

namespace hyp
{
    enum class TickGroup : uint8_t
    {
        Pre_Physics,
        Physics,
        Post_Physics,

        Count,
    };

    class Entity;
    class Scene;
    class Component : public Object
    {
        friend class Scene;
        REFLECT_CLASS(Component)
    public:
        Component();
        virtual ~Component();

        Entity* getEntity() const;

        enum class State : uint8_t
        {
            Initial,
            Pending_Register,
            Registered,
            Pending_Unregister,
            Unregistered,
        };

        State getState() const noexcept
        {
            return mState;
        }

        virtual void onRegister();

        virtual void onUnregister();

        virtual void onVisible();

        virtual void onInvisible();

        void setVisible(bool visible);

        bool isVisible() const noexcept
        {
            return mVisible;
        }

        virtual void onTick(float deltaTime);

        TickGroup getTickGroup() const noexcept
        {
            return mTickGroup;
        }

        Scene* getScene() const noexcept;

    protected:
        void setTickGroup(TickGroup tickGroup) noexcept;

    private:
        void setState(State state) noexcept;

    private:
        State mState{ State::Initial };
        bool mVisible{ true };
        TickGroup mTickGroup;
    };

    template <> Type* Reflection::createType<TickGroup>();

    template <> Type* Reflection::createType<Component>();
}
