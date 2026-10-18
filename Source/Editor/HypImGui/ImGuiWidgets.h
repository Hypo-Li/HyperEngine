#pragma once
#include <imgui.h>

namespace hyp
{
    // 兜底的小控件集合: 体量小、逻辑量不足以单独开一对 .h/.cpp、又跟其它 HypImGui 文件的
    // 主题(AssetCombo/ClassCombo/PropertyWidgets 这些)对不上号的一次性工具控件放这里,
    // 不要为了一个几行代码的控件专门新建文件。

    // 没有背景高亮的纯文字/图标按钮: 平时是稍暗一点的白, hover 时变亮白。用
    // InvisibleButton 占位取交互(什么都不画), 拿到 hover 结果之后自己用 AddText() 把
    // 文字画上去, 不走 ImGuiCol_Button/Hovered/Active 这套背景色机制, 也就没有任何底色。
    // label 直接当 InvisibleButton 的 str_id 用, 调用方要保证在当前 ID 作用域内唯一
    // (跟 ImGui::Button(label) 对 ID 的要求一致)。
    bool TextOnlyButton(const char* label);
}
