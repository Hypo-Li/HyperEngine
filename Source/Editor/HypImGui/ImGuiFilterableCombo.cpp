#include "ImGuiFilterableCombo.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>

namespace hyp
{
    namespace
    {
        // 按 ImGuiID 存, 生命周期跟 UndoStack 的手势状态表是同一个道理: 只增不减,
        // 数量上限就是"曾经出现过的不同下拉框个数", 可以忽略不计。
        std::unordered_map<ImGuiID, std::string> gFilterTexts;

        // 当前处于展开状态的那个 FilterableCombo 的搜索串——同一时刻只可能有一个 Combo 的
        // popup 是打开的(ImGui 本身不支持 Combo 嵌套 Combo), 用一个全局指针足够,
        // 不需要栈。Begin 时指向 gFilterTexts 里对应的条目, End 时清空。
        std::string* gActiveFilter = nullptr;

        bool containsCaseInsensitive(std::string_view haystack, std::string_view needle)
        {
            if (needle.empty())
                return true;
            auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
            return it != haystack.end();
        }
    }

    bool BeginFilterableCombo(const char* strId, const char* previewValue, ImGuiComboFlags flags)
    {
        if (!ImGui::BeginCombo(strId, previewValue, flags))
            return false;

        ImGuiID id = ImGui::GetID(strId);
        std::string& filterText = gFilterTexts[id];
        gActiveFilter = &filterText;

        // Popup 刚展开的第一帧就抢焦点, 不用每次都先点一下搜索框才能打字
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##FilterableComboSearch", "Search...", &filterText);
        ImGui::Separator();

        return true;
    }

    bool PassesFilterableComboFilter(const char* label)
    {
        if (!gActiveFilter || gActiveFilter->empty())
            return true;
        return containsCaseInsensitive(label, *gActiveFilter);
    }

    bool FilterableComboItem(const char* label, bool selected, ImGuiSelectableFlags flags)
    {
        if (!PassesFilterableComboFilter(label))
            return false;
        return ImGui::Selectable(label, selected, flags);
    }

    void EndFilterableCombo()
    {
        gActiveFilter = nullptr;
        ImGui::EndCombo();
    }
}
