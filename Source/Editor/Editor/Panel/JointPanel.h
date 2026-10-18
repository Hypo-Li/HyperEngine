#pragma once

#include "ComponentPanel.h"
#include "../UndoStack.h"

#include <Engine/Component/Joint/Joint.h>

namespace hyp::ed
{
    // Joint 是抽象基类, 用法跟 ColliderPanel 完全一样(见那边的注释)——具体子类的 onDraw()
    // 显式调 JointPanel::onDraw() 画完 Collider/Connected Collider 这两个共有属性, 再紧接着
    // 画自己的约束参数, 两者要落在同一张 BeginPropertyTable()/EndPropertyTable() 之间。
    //
    // 这里的 Collider/Connected Collider 选择器是按文档"具体 UI 形态还没设计"临时写的最简
    // 版本(下拉框, 按 Entity 名字列出场景里所有 Collider), 只够验证 Joint 能不能跑起来——
    // 正式的场景对象选择器(拖拽/搜索之类)以后要另外设计, 不是这里定的样子。
    class JointPanel : public IComponentPanel
    {
    public:
        explicit JointPanel(Joint& joint) noexcept;

    protected:
        void onDraw() override;

        // 跟 ColliderPanel::pushReset 完全一样, 见那边的注释——Joint 系跟 Collider 系在
        // IComponentPanel 下面没有共同的中间基类, 两边各自留一份。
        template <typename T, typename Setter>
        void pushReset(HString name, Setter setter, const T& before, const T& after)
        {
            getUndoStack().push(name,
                [setter, before]() mutable { setter(before); },
                [setter, after]() mutable { setter(after); });
        }

        Joint& mJoint;
    };
}
