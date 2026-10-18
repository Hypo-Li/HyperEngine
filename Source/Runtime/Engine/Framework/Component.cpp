#include "Component.h"
#include "Entity.h"
#include "Scene.h"

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

    void Component::doRegister()
    {
        assert(getState() != State::Registered);
        onRegister();
        setState(State::Registered);
    }

    void Component::doUnregister()
    {
        assert(getState() != State::Unregistered);
        onUnregister();
        setState(State::Unregistered);
    }

    void Component::onRegister()
    {
    }

    void Component::onUnregister()
    {
    }

    void Component::onVisibilityChanged(bool visible)
    {
    }

    void Component::onBeginPlay()
    {
    }

    void Component::onTick(float deltaTime)
    {
    }

    void Component::onEndPlay()
    {
    }

    void Component::onMobilityChanged(Mobility mobility)
    {
    }

    Scene* Component::getScene() const noexcept
    {
        return getEntity()->getScene();
    }

    void Component::setTickGroup(TickGroup tickGroup) noexcept
    {
        if (tickGroup == mTickGroup)
            return;

        // Registered/Pending_Unregister 这两个状态下, 组件当前确实待在 mTickGroup 对应的
        // Scene::mComponentTickGroups 数组里, 得先把它搬到新数组; 其余状态还没被塞进任何
        // 数组, 改完字段之后自然会被后续的注册流程放到正确的位置, 不需要搬。
        State state = getState();
        if (state == State::Registered || state == State::Pending_Unregister)
            getScene()->moveComponentTickGroup(this, mTickGroup, tickGroup);

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
        c->addProperty("Tick Group", &Component::mTickGroup);
        return c;
    }
}
