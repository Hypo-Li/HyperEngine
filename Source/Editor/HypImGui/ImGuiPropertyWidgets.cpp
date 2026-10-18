#include "ImGuiPropertyWidgets.h"
#include "ImGuiFrontend.h"

#include <algorithm>

namespace hyp
{
    namespace
    {
        float gPropertyLabelWidthRatio = 0.35f;

        // 当前这张属性表的总宽度(BeginPropertyTable() 记, BeginPropertyRow() 里的分隔条拖拽
        // 用它把像素位移换算成比例增量)。同一时刻只会有一张属性表在画(不会嵌套), 用一个
        // 模块级变量就够, 不需要额外传参串起来。
        float gCurrentTableWidth = 0.0f;

        // BeginPropertyRow() 记下当前行的 rowHeight, 供同一行里稍后调用的 DrawPropertyReset()
        // 读取——两者中间隔着调用方自己画的 Value 列控件, 没法直接传参, 用模块级变量跟
        // gCurrentTableWidth 是同一个道理(同一时刻只会有一张属性表在画, 不需要栈)。
        float gCurrentRowHeight = 0.0f;

        // 这几个都是设计基准值(96 DPI 下的像素数), 用到的地方要乘 ImGuiFrontend::getDpiScale()——
        // 不能直接 constexpr 用, ImGuiStyle::ScaleAllSizes() 只缩放 ImGui 内置的 style 值,
        // 这里是我们自己代码里写死的像素数, 不会被那次缩放覆盖到。
        constexpr float kSplitterHitWidthBase = 6.0f;
        constexpr float kMinRatio = 0.1f;
        constexpr float kMaxRatio = 0.9f;

        // 属性行以前完全交给内容自动撑高, 控件的实际高度正好等于内容本身的高度, 上下没有
        // 任何呼吸空间——试过放大 ImGuiStyleVar_CellPadding 的 Y 分量, 对 Table 的行高不起
        // 作用(行高由 TableNextRow() 的 min_row_height 决定, 不是 CellPadding), 改成显式
        // 传 min_row_height, 并在行高大于内容高度的差值里把内容居中(单行/多行统一处理)。
        //
        // 这里必须是固定像素的加法量, 不能是按 contentHeight 等比放大的乘法系数: AssetCombo
        // 那种两行布局的 contentHeight 本身就是单行的两倍多, 乘法系数会让多行的上下留白跟着
        // 成倍变大, 行数越多留白越夸张; 加法量是"每行顶/底各留一点固定呼吸空间", 不随内容
        // 本身的高度变化, 单行/多行看起来才是同一种呼吸感。
        constexpr float kRowVerticalPaddingBase = 4.0f;
    }

    float GetPropertyLabelWidthRatio() noexcept
    {
        return gPropertyLabelWidthRatio;
    }

    bool BeginPropertyTable(const char* strId, ImGuiTableFlags extraFlags)
    {
        gCurrentTableWidth = ImGui::GetContentRegionAvail().x;

        ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | extraFlags;
        if (!ImGui::BeginTable(strId, 3, flags))
            return false;

        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, gPropertyLabelWidthRatio);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f - gPropertyLabelWidthRatio);
        static const float kResetButtonWidth = ImGui::CalcTextSize(ICON_MS_REPLY).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::TableSetupColumn("##Reset", ImGuiTableColumnFlags_WidthFixed, kResetButtonWidth);
        return true;
    }

    void EndPropertyTable()
    {
        ImGui::EndTable();
    }

    void AlignVerticallyInRow(float rowHeight, float contentHeight) noexcept
    {
        float offset = (rowHeight - contentHeight) * 0.5f;
        if (offset > 0.0f)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offset);
    }

    float GetCurrentPropertyRowHeight() noexcept
    {
        return gCurrentRowHeight;
    }

    void DrawPropertyLabel(const char* label, float rowHeight) noexcept
    {
        if (rowHeight > 0.0f)
            AlignVerticallyInRow(rowHeight, ImGui::GetTextLineHeight());
        else
            ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
    }

    void BeginPropertyRow(const char* label, float rowMultiplier)
    {
        // contentHeight 是"这一行本来要画的东西自己的高度"——单行控件是 FrameHeight, 多行
        // 内容(比如 AssetCombo 的两行缩略图布局)是 rowCount 份 FrameHeight 加上 rowCount-1
        // 份行间距。rowHeight 在此基础上按 kRowHeightScale 放大, 多出来的差值统一在下面用
        // AlignVerticallyInRow() 居中——单行/多行是同一套逻辑, 不需要分支特殊处理。
        float contentHeight = rowMultiplier > 1.0f
            ? ImGui::GetFrameHeight() * rowMultiplier + ImGui::GetStyle().ItemSpacing.y * (rowMultiplier - 1.0f)
            : ImGui::GetFrameHeight();
        float rowHeight = contentHeight + kRowVerticalPaddingBase * ImGuiFrontend::getDpiScale() * 2.0f;
        gCurrentRowHeight = rowHeight;

        ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
        ImGui::TableSetColumnIndex(0);
        DrawPropertyLabel(label, rowHeight);

        ImGui::TableSetColumnIndex(1);

        // 分隔条: 横跨在列分界线两侧的一条窄热区, 拖拽直接改共享比例。用完之后把光标挪回
        // 分界线原位, 不影响接下来这一列正常绘制控件(手法跟 AssetBrowser 面包屑里的自绘控件
        // 一致——InvisibleButton 会推动 ImGui 的光标, 用完得自己复位)。热区高度跟这一行的
        // 实际行高一致(而不是固定 FrameHeight), 行变高之后拖拽手感跟着变宽, 不会缩成一条
        // 只占行内一小截的窄条。
        //
        // 必须用 label 撑一层 ID 作用域: 之前直接写死 "##PropertySplitter", 同一张表里
        // Position/Rotation/Scale 这几行算出来的 ID 全部一样, 会被 ImGui 判定为 ID 冲突
        // ("4 visible items with conflicting ID")——label 在同一张表内本来就唯一("Space"/
        // "Position"/"Rotation"/"Scale"), 用它撑开作用域就行。
        float splitterHitWidth = kSplitterHitWidthBase * ImGuiFrontend::getDpiScale();
        ImVec2 dividerPos = ImGui::GetCursorScreenPos();
        ImGui::PushID(label);
        ImGui::SetCursorScreenPos(ImVec2(dividerPos.x - splitterHitWidth * 0.5f, dividerPos.y));
        ImGui::InvisibleButton("##split", ImVec2(splitterHitWidth, rowHeight));

        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        if (ImGui::IsItemActive() && gCurrentTableWidth > 0.0f)
        {
            gPropertyLabelWidthRatio += ImGui::GetIO().MouseDelta.x / gCurrentTableWidth;
            gPropertyLabelWidthRatio = std::clamp(gPropertyLabelWidthRatio, kMinRatio, kMaxRatio);
        }

        ImGui::PopID();
        ImGui::SetCursorScreenPos(dividerPos);

        // Value 列接下来画的控件高度就是 contentHeight(单行是 FrameHeight, 多行是 AssetCombo
        // 那种两行缩略图布局自己的高度)——rowHeight 现在统一比它高了 kRowHeightScale 倍,
        // 提前把光标居中好, 调用方不用自己管、也不需要再按 rowCount 分支处理。
        AlignVerticallyInRow(rowHeight, contentHeight);

        ImGui::SetNextItemWidth(-1.0f);
    }
}
