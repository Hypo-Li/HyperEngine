#pragma once
#include <imgui.h>

namespace hyp
{
    // 通用的"带搜索框过滤"下拉框构件——ClassCombo/AssetCombo/EnumCombo 候选项一多(继承树很深、
    // 资产/枚举值几十个)光靠滚动会很难用, 都基于这个来实现, 不用各自维护一份搜索缓冲区。
    // 搜索文本按 strId 对应的 ImGuiID 存, 调用方不需要自己声明成员变量。
    //
    // 用法(仿 ImGui::BeginCombo/EndCombo 的两段式):
    //   if (BeginFilterableCombo("##Foo", previewLabel))
    //   {
    //       for (...)
    //           if (FilterableComboItem(itemLabel, isSelected)) { ... }
    //       EndFilterableCombo();
    //   }
    bool BeginFilterableCombo(const char* strId, const char* previewValue, ImGuiComboFlags flags = 0);

    // label 是否匹配当前搜索框内容(大小写不敏感的子串匹配)——FilterableComboItem 内部就是
    // 用这个判断要不要画这一项。只有当某一项需要画得比"纯文字 Selectable"更复杂(比如 AssetCombo
    // 要在文字前面加一个图标)、没法直接调用 FilterableComboItem 时, 才需要自己调这个先判断
    // 要不要跳过。
    bool PassesFilterableComboFilter(const char* label);

    bool FilterableComboItem(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0);

    void EndFilterableCombo();
}
