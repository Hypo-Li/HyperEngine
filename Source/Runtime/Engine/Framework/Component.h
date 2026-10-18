#pragma once
#include <Core/Object/Object.h>
#include <RenderBase/ProxyHandle.h>

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
        virtual ~Component() = 0;

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

        // 分别把 onRegister()/onUnregister() 和紧跟着的 setState(Registered)/setState(Unregistered)
        // 绑成一次调用——mState 必须如实反映 onRegister()/onUnregister() 有没有被调过, 拆开
        // 调用容易漏掉后半句, 让 mState 跟真实生命周期脱节。
        void doRegister();

        void doUnregister();

        virtual void onVisibilityChanged(bool visible);

        // 只在 Scene 从 Stoped 进入 Playing/Paused 这一局游戏会话开始时触发一次——区别于
        // onRegister(): 编辑模式往场景里加组件也会触发 onRegister, 但不会触发这个。
        virtual void onBeginPlay();

        virtual void onTick(float deltaTime);

        // 触发时机与 onBeginPlay() 对称: 会话结束(回到 Stoped)或组件在会话中途被销毁时
        // 各触发一次, 且都发生在 onUnregister() 之前。此时不保证还能通过 Entity 查到自己或兄弟组件。
        virtual void onEndPlay();

        // 默认空实现(见 Component.cpp): 大多数组件不关心 Entity 的 Mobility, 只有需要按
        // Static/Dynamic 分组维护渲染线程状态的组件(比如 StaticMeshRenderer)才需要重写
        virtual void onMobilityChanged(Mobility mobility);

        TickGroup getTickGroup() const noexcept
        {
            return mTickGroup;
        }

        Scene* getScene() const noexcept;

    protected:
        virtual void onRegister();

        virtual void onUnregister();

        void setTickGroup(TickGroup tickGroup) noexcept;

    private:
        void setState(State state) noexcept;

    private:
        State mState{ State::Initial };
        TickGroup mTickGroup;
    };

    template <> Type* Reflection::createType<TickGroup>();

    template <> Type* Reflection::createType<Component>();
}
