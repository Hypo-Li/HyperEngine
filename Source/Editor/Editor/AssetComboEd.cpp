#include "AssetComboEd.h"
#include "AssetBrowser.h"
#include "ThumbnailProvider.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Core/Reflection/Class.h>

#include <HypImGui/ImGuiPropertyWidgets.h>
#include <HypImGui/ImGuiWidgets.h>
#include <HypImGui/IconsMaterialSymbols.h>

#include <imgui.h>

#include <optional>

namespace hyp::ed
{
    namespace
    {
        bool assetMatchesFilter(Asset& asset, Class* filterClass, bool includeDerived)
        {
            if (!filterClass)
                return true;
            Class* rootClass = asset.getRootClass();
            return rootClass == filterClass || (includeDerived && rootClass->isDerivedFrom(filterClass));
        }
    }

    bool AssetComboEd(
        const char* strId,
        AssetManager& assetManager,
        Class* filterClass,
        bool includeDerived,
        Guid& value,
        AssetBrowser& assetBrowser,
        ThumbnailProvider& thumbnailProvider,
        const std::function<bool(Asset*)>& extraFilter
    )
    {
        ImVec2 startPos = ImGui::GetCursorScreenPos();
        float thumbSize = GetAssetComboRowHeight();

        // 非空的贴图类型引用现查真实缩略图, 取不到(非贴图资产、格式不支持、生成配额用完)
        // 就回退默认占位图——跟 AssetBrowser 网格里的取用规则一致。
        auto iconForAsset = [&thumbnailProvider](Asset* asset) -> ImTextureID
        {
            if (ImTextureID thumbnail = thumbnailProvider.getThumbnail(asset))
                return thumbnail;
            return thumbnailProvider.getDefaultThumbnail();
        };

        bool changed = AssetCombo(strId, assetManager, filterClass, includeDerived, value, iconForAsset, extraFilter);

        // AssetCombo() 画完之后, 光标已经被顶到整个两行区域的底部(缩略图比下拉框高,
        // ImGui 按 SameLine 这一行里最高的那个决定行高)——手动挪回第二行该在的位置
        // (缩略图右边、下拉框正下方), 画完两个按钮后再挪回两行区域真正的底部, 让调用方
        // 后续的布局(下一个属性行/EndPropertyTable)跟没加这两个按钮时完全一样。
        ImVec2 secondRowPos(
            startPos.x + thumbSize + ImGui::GetStyle().ItemSpacing.x,
            startPos.y + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y
        );
        ImGui::SetCursorScreenPos(secondRowPos);

        ImGui::PushID(strId);

        std::optional<Guid> selectedGuid = assetBrowser.getSingleSelectedAssetGuid();
        Asset* selectedAsset = selectedGuid ? assetManager.getAsset(*selectedGuid) : nullptr;
        bool canUseSelected = selectedAsset
            && assetMatchesFilter(*selectedAsset, filterClass, includeDerived)
            && (!extraFilter || extraFilter(selectedAsset));

        ImGui::BeginDisabled(!canUseSelected);
        if (TextOnlyButton(ICON_MS_ENABLE))
        {
            value = *selectedGuid;
            changed = true;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        bool hasValue = value.isValid();
        ImGui::BeginDisabled(!hasValue);
        if (TextOnlyButton(ICON_MS_TAB_SEARCH))
            assetBrowser.revealAsset(value);
        ImGui::EndDisabled();

        ImGui::PopID();

        ImGui::SetCursorScreenPos(ImVec2(startPos.x, startPos.y + thumbSize));

        return changed;
    }
}
