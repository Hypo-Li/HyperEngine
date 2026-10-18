#pragma once
#include <imgui.h>

namespace hyp
{
    // IEC 61966-2-1 sRGB 传递函数, 单通道, 输入输出都不做额外 clamp(调用方按需自己 clamp)。
    float LinearToSRGB(float linear) noexcept;
    float SRGBToLinear(float srgb) noexcept;

    // ColorEdit3/4 的线性颜色版本: col 数组存的是线性颜色值, 不做 clamp——允许超过 1.0,
    // 供自发光强度这类 HDR 属性使用, 拖拽数值框时也没有上限。控件本身的可视化
    // (预览色块、点开的色轮拾色器)在显示前会把颜色 clamp 到 [0,1] 再做
    // linear->sRGB 编码, 保证肉眼看到的是经过 gamma 校正、和 sRGB 显示器一致的颜色,
    // 和 col 里存的线性数值是两回事。
    //
    // 拾色器工作在 clamp 之后的 sRGB 空间——用色轮直接选一个 HDR 强度本身没有意义,
    // 通常流程是先在色轮里选好基础色调, 强度超过 1.0 的部分回到数值框里单独调。
    // alpha 分量(如果存在)不做 gamma 编码, 也会被 clamp 到 [0,1]。
    //
    // 虽然内部由拖拽框/预览色块/拾色器好几个子控件组成, 但对外表现得和 ImGui 内置的
    // ColorEdit3/4 完全一样(内部用了跟 ColorEdit4 同一套 BeginGroup/EndGroup +
    // MarkItemEdited 手法, 见 .cpp): 函数返回之后可以直接调用 ImGui::IsItemActivated()/
    // IsItemDeactivatedAfterEdit()/IsItemActive() 等查询, 不需要关心内部有几个子控件。
    bool LinearColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags = 0);
    bool LinearColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags = 0);
}
