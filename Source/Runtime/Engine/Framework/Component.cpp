#include "Component.h"
#include "Entity.h"

namespace hyp
{
    REGISTER_CLASS(Component)

    Component::Component()
    {
    }

    Component::~Component()
    {
    }

    Entity* Component::getEntity() const
    {
        return static_cast<Entity*>(getOwner());
    }

    void Component::onRegister()
    {
    }

    void Component::onUnregister()
    {
    }

    void Component::onVisible()
    {
    }

    void Component::onInvisible()
    {
    }

    void Component::setVisible(bool visible)
    {
        if (mVisible == visible)
            return;

        mVisible = visible;
        if (mVisible)
            onVisible();
        else
            onInvisible();
    }

    void Component::onTick(float deltaTime)
    {
    }

    Scene* Component::getScene() const noexcept
    {
        return getEntity()->getScene();
    }

    void Component::setTickGroup(TickGroup tickGroup) noexcept
    {
        mTickGroup = tickGroup;
    }

    void Component::setState(State state) noexcept
    {
        mState = state;
    }

    template <> Type* Reflection::createType<TickGroup>()
    {
        auto e = new TEnumeration<TickGroup>("TickGroup");
        e->addValue("Pre Physics", TickGroup::Pre_Physics);
        e->addValue("Physics", TickGroup::Physics);
        e->addValue("Post Physics", TickGroup::Post_Physics);
        return e;
    }

    template <> Type* Reflection::createType<Component>()
    {
        auto c = new TClass<Component>("Component");
        return c;
    }
}
