#include "ImGuiAssetCombo.h"
#include "ImGuiFilterableCombo.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Core/Common/Guid.h>

namespace hyp
{
    namespace
    {
        // 下拉框展开后每一项的绘制, AssetCombo 主体(带缩略图的两行版本)和
        // (None) 之外的正常资产项共用——两处都要按同一个 itemSize 摆图标+文字。
        //
        // 仿 UE 资产选择器: 列表里只显式资产名字(不是全路径, 同名资产分布在不同目录下时
        // 单看名字容易分不清是哪一个), 全路径改成挪到 hover tooltip 里——过滤搜索仍然按
        // 全路径匹配(PassesFilterableComboFilter), 这样搜文件夹名也能筛出里面的资产,
        // 比只按显示的名字搜更好用, 且这个放宽不影响"看到的是什么"这件事。
        void drawAssetComboItem(Asset* asset, float itemSize, Guid& value, bool& changed, const std::function<ImTextureID(Asset*)>& iconForAsset)
        {
            const char* path = asset->getPath().c_str();
            if (!PassesFilterableComboFilter(path))
                return;

            ImGui::PushID(asset);

            ImTextureID icon = iconForAsset ? iconForAsset(asset) : ImTextureID(0);

            // 先画一个铺满整行(图标+文字都算在内)的隐藏 Selectable 占住可点击/高亮包围盒,
            // 再把光标挪回原位置补画图标和文字——如果反过来先画图标再画 Selectable, 图标
            // 占用的那部分区域会落在 Selectable 包围盒之外, 悬停在图标上不会高亮。
            ImVec2 rowPos = ImGui::GetCursorScreenPos();
            float rowHeight = icon ? itemSize : 0.0f;
            bool isSelected = asset->getRootGuid() == value;
            bool selected = ImGui::Selectable("##row", isSelected, 0, ImVec2(0.0f, rowHeight));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", path);
            if (selected)
            {
                value = asset->getRootGuid();
                changed = true;
            }

            // Selectable() 画完已经把光标推进到下一行该在的位置——先记下来, 图标+文字画完后
            // 挪回这里, 不然后面手动摆位置的画图会把光标留在自己的高度上, 打乱下一行的布局。
            ImVec2 nextRowPos = ImGui::GetCursorScreenPos();

            // ImGui::Image() 不推进布局光标那一路是靠手动 SetCursorScreenPos 补的, 文字起始
            // 位置改成手动算(rowPos.x + itemSize + 一个 ItemSpacing)。
            if (icon)
            {
                ImGui::SetCursorScreenPos(rowPos);
                ImGui::Image(icon, ImVec2(itemSize, itemSize));
            }
            ImVec2 textPos(rowPos.x, rowPos.y);
            if (icon)
            {
                textPos.x += itemSize + ImGui::GetStyle().ItemSpacing.x;
                // 图标比文字行高——文字垂直居中, 不然会贴在加高之后的行顶部,
                // 跟纵向居中的图标看起来不对齐。
                textPos.y += (itemSize - ImGui::GetTextLineHeight()) * 0.5f;
            }
            ImGui::SetCursorScreenPos(textPos);
            ImGui::TextUnformatted(asset->getName().toCString());

            // ImGui 要求 SetCursorScreenPos() 推进边界之后必须紧跟一次真实的 item 提交
            // (哪怕是零尺寸 Dummy), 否则内容区域最大边界跟不上而在下拉框自动布局时触发
            // IM_ASSERT_USER_ERROR。
            ImGui::SetCursorScreenPos(nextRowPos);
            ImGui::Dummy(ImVec2(0.0f, 0.0f));

            ImGui::PopID();
        }
    }

    float GetAssetComboRowHeight() noexcept
    {
        return ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
    }

    bool AssetCombo(
        const char* strId,
        AssetManager& assetManager,
        Class* filterClass,
        bool includeDerived,
        Guid& value,
        const std::function<ImTextureID(Asset*)>& iconForAsset,
        const std::function<bool(Asset*)>& extraFilter
    )
    {
        // 仿 UE: 关闭态的预览只显式资产名字, 不带路径——同名资产分布在不同目录时容易分不清
        // 是哪一个, 全路径挪到 hover tooltip 里(见下面两处 IsItemHovered), 不占预览这行的
        // 显示空间。HString::toCString() 是 pool 里的稳定内存, 不需要额外拷进一份 std::string
        // 再取 c_str()。
        Asset* currentAsset = value.isValid() ? assetManager.getAsset(value) : nullptr;
        const char* preview = currentAsset ? currentAsset->getName().toCString() : "(None)";

        // 没有缩略图可画的调用方(iconForAsset 为空)走单行旧布局, 不占两行的地方。
        if (!iconForAsset)
        {
            bool changedNoIcon = false;
            bool comboOpened = BeginFilterableCombo(strId, preview);
            if (currentAsset && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", currentAsset->getPath().c_str());
            if (comboOpened)
            {
                if (FilterableComboItem("(None)", !value.isValid()))
                {
                    value = Guid();
                    changedNoIcon = true;
                }
                assetManager.forEachAsset(
                    [&](Asset* asset)
                    {
                        if (!extraFilter || extraFilter(asset))
                            drawAssetComboItem(asset, 0.0f, value, changedNoIcon, iconForAsset);
                    },
                    filterClass,
                    includeDerived
                );
                EndFilterableCombo();
            }
            return changedNoIcon;
        }

        // 仿 UE 资产选择器的两行布局: 左边一个大缩略图, 右边上一行是可展开的下拉框触发器,
        // 下一行先留空(以后要加"在 AssetBrowser 里定位"/"清空引用"之类的按钮可以往这儿摆)。
        // 缩略图目前复用 iconForAsset 给的那张按资产类型区分的通用图标——引擎里还没有按
        // 资产内容渲染的真实预览图, 见 ThumbnailProvider.h 顶部的注释。
        float thumbSize = GetAssetComboRowHeight();

        ImTextureID previewIcon = iconForAsset(currentAsset);
        if (previewIcon)
            ImGui::Image(previewIcon, ImVec2(thumbSize, thumbSize));
        else
            ImGui::Dummy(ImVec2(thumbSize, thumbSize));
        if (currentAsset && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", currentAsset->getPath().c_str());

        bool changed = false;

        // 图标本身既是拖拽源(把当前引用的资产拖给别的 AssetCombo)也是拖拽目标(从
        // AssetBrowser 或另一个 AssetCombo 的图标拖资产过来赋值)——单行布局(iconForAsset
        // 为空)没有图标, 不参与拖拽。ImGuiDragDropFlags_SourceAllowNullID 必须传:
        // Image()/Dummy() 不像 Button() 那样自带一个交互用的 item ID, 不传这个 flag 的话
        // BeginDragDropSource() 在"没有 ID 又要禁止空 ID"的分支里直接 IM_ASSERT(0) 崩溃
        // (imgui.cpp:15624-15629 有说明)。SourceNoPreviewTooltip 也必须传: 悬浮提示统一在
        // Editor.cpp 里按 payload 数据画一份, 不依赖这个源头小部件本身每帧还在不在(切换
        // 标签页导致源头没被画到时, ImGui 会掉回退成"..."提示, 见那边的注释)。
        if (currentAsset && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID | ImGuiDragDropFlags_SourceNoPreviewTooltip))
        {
            AssetDragDropPayload payload{ value, currentAsset->getRootClass(), previewIcon };
            ImGui::SetDragDropPayload(kAssetDragDropType, &payload, sizeof(payload));
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            // 先偷看一眼 payload 判断类型是否匹配, 不消费它、也不让 ImGui 画默认高亮框
            // (AcceptPeekOnly = AcceptBeforeDelivery | AcceptNoDrawDefaultRect)——只有
            // 类型匹配才真正调一次不带 PeekOnly 的 AcceptDragDropPayload(), 由它自己画
            // 默认的黄框、并在鼠标松开那一帧真正返回非空完成投放; 类型不匹配就什么都不做,
            // 不出现任何高亮框, 用户能直观看出"这里不能放"。
            bool typeMatches = false;
            if (const ImGuiPayload* peek = ImGui::AcceptDragDropPayload(kAssetDragDropType, ImGuiDragDropFlags_AcceptPeekOnly))
            {
                const AssetDragDropPayload& dropped = *static_cast<const AssetDragDropPayload*>(peek->Data);
                typeMatches = !filterClass || dropped.rootClass == filterClass
                    || (includeDerived && dropped.rootClass && dropped.rootClass->isDerivedFrom(filterClass));
            }
            if (typeMatches)
            {
                if (const ImGuiPayload* dragPayload = ImGui::AcceptDragDropPayload(kAssetDragDropType))
                {
                    const AssetDragDropPayload& dropped = *static_cast<const AssetDragDropPayload*>(dragPayload->Data);
                    value = dropped.guid;
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(-1.0f);
        bool comboOpened = BeginFilterableCombo(strId, preview);
        if (currentAsset && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", currentAsset->getPath().c_str());
        if (comboOpened)
        {
            if (FilterableComboItem("(None)", !value.isValid()))
            {
                value = Guid();
                changed = true;
            }

            // forEachAsset 按 AssetManager 内部的路径表迭代, 天然已经按路径字符串排好序——
            // 不需要自己收集一份 vector 再 sort 一遍, 直接在回调里画。
            assetManager.forEachAsset(
                [&](Asset* asset)
                {
                    if (!extraFilter || extraFilter(asset))
                        drawAssetComboItem(asset, thumbSize, value, changed, iconForAsset);
                },
                filterClass,
                includeDerived
            );
            EndFilterableCombo();
        }
        ImGui::EndGroup();

        return changed;
    }
}
