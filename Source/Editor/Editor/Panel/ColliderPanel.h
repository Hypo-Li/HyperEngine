#pragma once

#include "ComponentPanel.h"
#include "../UndoStack.h"

#include <Engine/Component/Collider/Collider.h>

namespace hyp::ed
{
    // Collider 是抽象基类, 不会被直接注册进 ComponentPanelRegistry(它按 component.getClass()
    // 精确匹配, 不会退化到基类查找)——这个类不会被单独构造出来当某个组件的实际面板, 只是给
    // BoxColliderPanel 这类具体形状的面板提供一个可以复用的 onDraw() 实现: 具体子类的
    // onDraw() 里显式调 ColliderPanel::onDraw() 画完 Is Trigger/Motion Type/Mass 这几个共有
    // 属性, 再紧接着画自己的形状参数——两者要落在同一张 BeginPropertyTable()/
    // EndPropertyTable() 之间, 所以这里只管画行, 表的开关交给最外层的具体子类。
    // getTitle() 留给具体子类, 这里没有实现, ColliderPanel 因此仍是抽象类, 不能被直接构造。
    class ColliderPanel : public IComponentPanel
    {
    public:
        explicit ColliderPanel(Collider& collider) noexcept;

    protected:
        void onDraw() override;

        // getUndoStack().push() 包一层的小工具: undo 恢复到 before, redo 重新应用 after——
        // "点 Reset 按钮"这个场景每个属性都要用一遍, 提到这里给所有 Collider 系的面板共用。
        template <typename T, typename Setter>
        void pushReset(HString name, Setter setter, const T& before, const T& after)
        {
            getUndoStack().push(name,
                [setter, before]() mutable { setter(before); },
                [setter, after]() mutable { setter(after); });
        }

        Collider& mCollider;
    };
}
