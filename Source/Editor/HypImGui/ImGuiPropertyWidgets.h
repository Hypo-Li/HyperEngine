#pragma once
#include <imgui.h>
#include "IconsMaterialSymbols.h"
#include "ImGuiWidgets.h"

namespace hyp
{
    // 当前生效的共享标签列宽比例(0~1)——所有属性表共用同一个值, 拖动任意一张表的分隔条
    // 都会改这一个值, 下一帧起所有属性表都会用新比例重新排版。默认 0.35f。
    float GetPropertyLabelWidthRatio() noexcept;

    // 开一张三列的属性表(Label 列 + Value 列 + Reset 列), 列宽比例/排版策略全局统一——
    // TransformPanel、Inspector 里 Entity 的属性、以后各个 ComponentPanel 都应该用这一套,
    // 不要各自再摆一份 TableSetupColumn 参数, 否则各个面板的标签列宽会不一致。
    //
    // 不开 ImGuiTableFlags_Resizable: 一个可拖拽的 Stretch 列一旦被手动拖过一次, 它自己的
    // StretchWeight 就会定住, 之后 TableSetupColumn 再传新比例进去也不会被采纳(见
    // imgui_tables.cpp::TableUpdateLayout() 里 column_is_resizable && StretchWeight>=0 时
    // 跳过重新赋值那段)——也就是说 ImGui 内置的列拖拽天生是"每张表各自记各自的", 拖一张表
    // 不会传导到另一张不同 ID 的表, 做不到"跨表格统一"。所以调节比例这件事改由
    // BeginPropertyRow() 里画的分隔条自己实现, 表格本身永远只读共享比例, 不使用 Resizable。
    // Reset 列是固定宽度(不参与比例分配), 只用来放 DrawPropertyReset() 画的按钮。
    //
    // extraFlags 用来叠加调用方自己需要的其它 flag(比如 BordersInnerH), 不影响列宽策略。
    // strId 跟 ImGui::BeginTable 一样, 同一个 window 里同时存在的表格要求各自唯一。
    bool BeginPropertyTable(const char* strId, ImGuiTableFlags extraFlags = ImGuiTableFlags_None);

    void EndPropertyTable();

    // 把接下来要画的东西在高度为 rowHeight 的这一行里垂直居中, 只挪光标 Y、不画任何东西——
    // 给那些自己会把所在行撑高的控件(比如 AssetCombo 的两行缩略图布局)配套用: 撑高之后
    // 同一行里别的单行内容(label 文字、Reset/Remove 按钮)默认还是贴在行顶部, 需要手动居中。
    // contentHeight 传"接下来要画的东西自己的高度"(文字传 GetTextLineHeight(), 按钮传
    // GetFrameHeight())。rowHeight <= contentHeight(包括单行场景传 0)时什么都不做。
    void AlignVerticallyInRow(float rowHeight, float contentHeight) noexcept;

    // 当前这一行的实际像素高度, BeginPropertyRow() 按 rowCount 换算好之后记下——
    // DrawPropertyReset() 用它来把 Reset 按钮同样居中, 不需要调用方在 DrawPropertyReset()
    // 时再传一遍。
    float GetCurrentPropertyRowHeight() noexcept;

    // 只管"在当前列画 label 文字, 按 rowHeight 垂直居中"这一件事——不建表、不切列、不管
    // 共享列宽比例的分隔条。BeginPropertyRow() 内部用它画 Label 列; 也给那些不想跟全局
    // 共享比例挂钩、自己独立建表的调用方(比如 MaterialTemplateEditor 的参数列表, 用的是
    // 自带 Resizable 的独立列宽, 不能用 BeginPropertyRow() 那一套)直接复用, 不用各自重新
    // 抄一遍"rowHeight > 0 ? AlignVerticallyInRow() : AlignTextToFramePadding()"这段判断。
    // rowHeight 传 0 就是普通单行(老规矩), 传大于单行高度的值就在这个高度内垂直居中。
    void DrawPropertyLabel(const char* label, float rowHeight) noexcept;

    // 起一行属性: 左列画 label(基线对齐控件), 中间列切到光标位置、控件宽度撑满整列——
    // 调用方紧跟着画自己的控件即可, 不用再手动摆 TableNextRow/TableSetColumnIndex。
    //
    // 顺带在列分界线上叠一个很窄的拖拽热区(悬浮时鼠标变左右箭头), 拖动直接调整共享比例——
    // 每一行都有一段, 相当于沿整张表的高度都能抓, 不需要单独预留一整根竖线。
    //
    // rowMultiplier 默认 1(普通单行控件, label 走 AlignTextToFramePadding() 老规矩); 传大于 1
    // 的值表示这一行的 Value 列会画一个跨 N 行高的控件(比如 AssetCombo 的两行缩略图布局
    // 传 2), label 改成在这个高度内垂直居中, 不再贴顶——具体像素高度按
    // "FrameHeight * rowMultiplier + ItemSpacing.y * (rowMultiplier - 1)" 换算(N 行框线控件叠起来,
    // 中间 N-1 条行间距), 调用方不需要自己拼像素值。
    void BeginPropertyRow(const char* label, float rowMultiplier = 1.0f);

    // 在这一行的 Reset 列画重置按钮: value 等于 defaultValue 时什么都不画(不是画一个
    // disabled 的按钮), 不等于时画一个可点的 ↩, 点击后把 value 设为 defaultValue 并返回
    // true。调用方自己决定要不要因此触发一次"应用"逻辑(updateParameters()/undo push 等)——
    // 这里只管比较+赋值+摆放, 不管语义, 也不管 defaultValue 具体是什么("默认值"不一定是
    // 空/零值, 比如 StaticMeshRenderer 某个材质槽没被覆盖时的"默认值"是网格自己在那个槽位上原本
    // 指定的材质, 调用方按各自组件的语义传对应的 defaultValue 即可)。
    //
    // label 需要跟 BeginPropertyRow() 传的是同一个字符串, 用来给按钮的 ID 加盐(同一张表里
    // 不同行都叫"Reset"会撞 ID, 用 label 撑开作用域, 和 BeginPropertyRow() 内部分隔条的
    // 做法一致)。T 需要支持 operator==(判断是否已经是默认值)和拷贝赋值。
    template <typename T>
    bool DrawPropertyReset(const char* label, T& value, const T& defaultValue)
    {
        ImGui::TableSetColumnIndex(2);
        if (value == defaultValue)
            return false;

        ImGui::PushID(label);
        AlignVerticallyInRow(GetCurrentPropertyRowHeight(), ImGui::GetFrameHeight());
        bool clicked = TextOnlyButton(ICON_MS_REPLY);
        ImGui::PopID();
        if (clicked)
            value = defaultValue;
        return clicked;
    }
}
