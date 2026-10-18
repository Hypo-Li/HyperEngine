#include "ImGuiColorWidgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hyp
{
    float LinearToSRGB(float linear) noexcept
    {
        return (linear <= 0.0031308f) ? (linear * 12.92f) : (1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f);
    }

    float SRGBToLinear(float srgb) noexcept
    {
        return (srgb <= 0.04045f) ? (srgb / 12.92f) : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    }

    namespace
    {
        // components 只能是 3 或 4(带不带 alpha), 复用同一份实现, 和 ImGui 自己
        // ColorEdit3/4 内部共享 ColorEdit4 逻辑的做法一致。
        bool linearColorEditImpl(const char* label, float* col, int components, ImGuiColorEditFlags flags)
        {
            // 弹出拾色器右侧"Current/Original"对比用: 打开弹窗那一刻的颜色, 只在
            // OpenPopup 时更新——和 ImGui 自己 ColorEdit4 用 g.ColorPickerRef 存这个
            // 引用值是同一个做法(imgui_widgets.cpp:5957)。同一时刻只会有一个这样的
            // 弹窗处于打开状态, 所以不需要按 ID 区分, 单份 static 就够。
            static ImVec4 sPickerRefColor;

            ImGuiContext& g = *GImGui;
            const bool alpha = (components == 4) && !(flags & ImGuiColorEditFlags_NoAlpha);

            // BeginGroup()/EndGroup() 把内部这几个子控件(拖拽框、预览色块、拾色器)包成一个
            // 逻辑上的单一 item——这跟 ImGui 自己 ColorEdit4() 内部用的是同一套手法
            // (imgui_widgets.cpp:5801, ColorEdit4), 让这个控件在外部调用方眼里表现得和内置的
            // ColorEdit3/4 完全一样: 函数返回之后直接调 ImGui::IsItemActivated()/
            // IsItemDeactivatedAfterEdit() 就能拿到"内部任意子控件正在被编辑/刚编辑完"的
            // 正确结果, 调用方不需要关心内部到底有几个子控件、也不需要额外的输出参数。
            ImGui::BeginGroup();
            ImGui::PushID(label);
            bool changed = false;

            const float previewSize = ImGui::GetFrameHeight();
            const float itemSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
            const float dragWidth = std::max(ImGui::CalcItemWidth() - previewSize - itemSpacing, 1.0f);

            // 拖拽框直接编辑线性数值, 不做 clamp(min==max==0 是 ImGui 的"无上下限"约定)——
            // 允许自发光这类属性的强度超过 1.0。
            ImGui::SetNextItemWidth(dragWidth);
            changed |= (components == 4)
                ? ImGui::DragFloat4("##value", col, 1.0f / 255.0f, 0.0f, 0.0f, "%.3f")
                : ImGui::DragFloat3("##value", col, 1.0f / 255.0f, 0.0f, 0.0f, "%.3f");

            // 颜色分量本身不应该是负数; alpha 额外 clamp 到 [0,1](alpha 是合成用的
            // 权重, 不是要 gamma 编码显示的颜色分量, 语义上和 RGB 不一样)。
            for (int i = 0; i < 3; i++)
                col[i] = std::max(col[i], 0.0f);
            if (alpha)
                col[3] = std::clamp(col[3], 0.0f, 1.0f);

            ImGui::SameLine(0.0f, itemSpacing);

            const ImVec4 previewColor(
                LinearToSRGB(std::clamp(col[0], 0.0f, 1.0f)),
                LinearToSRGB(std::clamp(col[1], 0.0f, 1.0f)),
                LinearToSRGB(std::clamp(col[2], 0.0f, 1.0f)),
                alpha ? col[3] : 1.0f
            );
            // ColorButton 自带的 hover tooltip(ColorTooltip, imgui_widgets.cpp 内私有
            // 函数)不管传 Uint8 还是 Float flag, 都会同时显示 0..255 整数和浮点数两种
            // 格式——而且浮点数那栏读的就是传给 ColorButton 的这份 sRGB 数据, 会违反
            // "浮点数=线性"的约定, 所以这里关掉它自带的 tooltip, 换成下面手写的一份,
            // 分别标清楚哪行是 sRGB、哪行是线性。
            const ImGuiColorEditFlags previewFlags = (flags & (ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_AlphaPreviewHalf
                | ImGuiColorEditFlags_NoBorder | ImGuiColorEditFlags_NoDragDrop)) | ImGuiColorEditFlags_NoTooltip;
            const bool previewPressed = ImGui::ColorButton("##preview", previewColor, previewFlags, ImVec2(previewSize, previewSize));
            if (!(flags & ImGuiColorEditFlags_NoTooltip) && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                const auto toByte = [](float c) { return static_cast<int>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
                int sr = toByte(previewColor.x), sg = toByte(previewColor.y), sb = toByte(previewColor.z), sa = toByte(previewColor.w);
                ImGui::BeginTooltip();
                // 照抄 ColorTooltip() 原来的排版(imgui_widgets.cpp:6567-6571): 左边一个大色块,
                // 右边跟一份文字说明, 只是文字内容换成我们自己的 sRGB/linear 两行。
                const float pad = ImGui::GetStyle().FramePadding.y;
                const ImVec2 swatchSize(ImGui::GetFontSize() * 3 + pad * 2, ImGui::GetFontSize() * 3 + pad * 2);
                ImGui::ColorButton("##tooltipPreview", previewColor,
                    (alpha ? ImGuiColorEditFlags_None : ImGuiColorEditFlags_NoAlpha) | ImGuiColorEditFlags_NoTooltip, swatchSize);
                ImGui::SameLine();
                if (alpha)
                    ImGui::Text("#%02X%02X%02X%02X\nR:%d, G:%d, B:%d, A:%d\n(%.3f, %.3f, %.3f, %.3f)",
                        sr, sg, sb, sa, sr, sg, sb, sa, col[0], col[1], col[2], col[3]);
                else
                    ImGui::Text("#%02X%02X%02X\nR:%d, G:%d, B:%d\n(%.3f, %.3f, %.3f)",
                        sr, sg, sb, sr, sg, sb, col[0], col[1], col[2]);
                ImGui::EndTooltip();
            }
            if (previewPressed)
                if (!(flags & ImGuiColorEditFlags_NoPicker))
                {
                    sPickerRefColor = previewColor; // 记住打开这一刻的颜色, 供 Current/Original 对比
                    ImGui::OpenPopup("picker");
                }

            if (!(flags & ImGuiColorEditFlags_NoLabel))
            {
                // "##" 之后的部分只作为 ID 盐值、不应该显示, 和 ImGui 自己控件的
                // label 约定一致(ColorEdit4 内部用的是私有的 FindRenderedTextEnd(),
                // 这里手写等价逻辑, 因为它没有导出到 imgui.h)。
                const char* labelEnd = strstr(label, "##");
                if (!labelEnd)
                    labelEnd = label + strlen(label);
                if (label != labelEnd)
                {
                    ImGui::SameLine(0.0f, itemSpacing);
                    ImGui::TextUnformatted(label, labelEnd);
                }
            }

            // 跟 ColorEdit4 一样记录拾色器弹窗是否正被操作(imgui_widgets.cpp:5965-5981),
            // 收尾时要用它判断要不要把 LastItemData.ID 换成拾色器内部控件的 ActiveId。
            ImGuiWindow* pickerActiveWindow = nullptr;
            if (ImGui::BeginPopup("picker"))
            {
                pickerActiveWindow = g.CurrentWindow;

                // 拾色器工作在 clamp 之后的 sRGB 空间(见头文件注释): 色轮/SV方块以及
                // ColorPicker4 自带的 R/G/B/H/S/V/Hex 数值行都是这份 sRGB 数据。约定:
                // 0..255 整数格式的数值 = sRGB, 浮点数格式的数值 = 线性——所以这里强制
                // Uint8, 不用 Float, 让这行数值一眼就能和下面的线性拖拽框区分开,
                // 不会被误当成同一份数字。
                float pickerCol[4] = { previewColor.x, previewColor.y, previewColor.z, previewColor.w };
                const ImGuiColorEditFlags pickerFlagsToForward = ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_AlphaBar;
                const ImGuiColorEditFlags pickerFlags = (flags & pickerFlagsToForward) | ImGuiColorEditFlags_NoLabel
                    | ImGuiColorEditFlags_Uint8
                    | (alpha ? ImGuiColorEditFlags_None : ImGuiColorEditFlags_NoAlpha);
                // ColorPicker3 不支持 ref_col(它本身就是内部转调 ColorPicker4 | NoAlpha
                // 实现的, imgui_widgets.cpp:6054-6061), 所以这里统一走 ColorPicker4,
                // 3 分量的情况靠 NoAlpha flag 隐藏透明度条, 效果和 ColorPicker3 等价,
                // 但能带上 Current/Original 对比。
                const bool pickerChanged = ImGui::ColorPicker4("##picker", pickerCol, pickerFlags, &sPickerRefColor.x);
                if (pickerChanged)
                {
                    col[0] = SRGBToLinear(pickerCol[0]);
                    col[1] = SRGBToLinear(pickerCol[1]);
                    col[2] = SRGBToLinear(pickerCol[2]);
                    if (alpha)
                        col[3] = std::clamp(pickerCol[3], 0.0f, 1.0f);
                    changed = true;
                }

                // 上面那行 0..255 整数是 sRGB 数值, 这里再补一行浮点数格式的线性
                // 拖拽框, 直接读写 col 本体——按"整数=sRGB, 浮点数=线性"的约定,
                // 两行数值格式不同, 一看格式就知道各自是哪个空间, 不会混淆。
                ImGui::Spacing();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                bool linearRowChanged = (components == 4)
                    ? ImGui::DragFloat4("##linearValue", col, 1.0f / 255.0f, 0.0f, 0.0f, "%.3f")
                    : ImGui::DragFloat3("##linearValue", col, 1.0f / 255.0f, 0.0f, 0.0f, "%.3f");
                if (linearRowChanged)
                {
                    for (int i = 0; i < 3; i++)
                        col[i] = std::max(col[i], 0.0f);
                    if (alpha)
                        col[3] = std::clamp(col[3], 0.0f, 1.0f);
                    changed = true;
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
            ImGui::EndGroup();

            // 跟 ColorEdit4 完全一样的两行收尾(imgui_widgets.cpp:6044-6049): 拾色器弹窗正在
            // 被操作时, 把 LastItemData.ID 换成拾色器内部控件真正的 ActiveId, 这样
            // IsItemActive() 才对; 只要这一帧有改动就手动 MarkItemEdited(), 这样后续
            // IsItemDeactivatedAfterEdit() 才能在手势结束时正确触发——BeginGroup/EndGroup
            // 本身不会自动做这两件事, 缺了这两行的话, 这个控件对 IsItemActivated()/
            // IsItemDeactivatedAfterEdit() 这两个查询来说仍然是"看得到但查不对"的状态。
            if (pickerActiveWindow && g.ActiveId != 0 && g.ActiveIdWindow == pickerActiveWindow)
                g.LastItemData.ID = g.ActiveId;
            if (changed && g.LastItemData.ID != 0)
                ImGui::MarkItemEdited(g.LastItemData.ID);

            return changed;
        }
    }

    bool LinearColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags)
    {
        return linearColorEditImpl(label, col, 3, flags | ImGuiColorEditFlags_NoAlpha);
    }

    bool LinearColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags)
    {
        return linearColorEditImpl(label, col, 4, flags);
    }
}
