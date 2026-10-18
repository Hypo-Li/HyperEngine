#pragma once
#include <imgui.h>

#include <cstdint>

namespace hyp
{
    // 一族基于自定义 pipeline 的 Image 变体, 用来在展示纹理时套一层 GPU 端可视化处理
    // (法线重建、灰度、通道遮罩), 而不是原样采样显示。调用方式和 ImGui::Image() 对齐——
    // textureId 必须是已经通过 ImGuiBackend::registerTexture() 注册过的句柄。
    //
    // 这几个函数本身只是薄薄的包装(AddCallback + ImDrawList::AddImage() + ResetRenderState
    // 三行), 真正的 pipeline 创建/销毁/绑定都在 ImGuiBackend 里(和 registerTexture() 属于
    // 同一类"跟图形 API 强绑定的后端细节", 见 ImGuiBackend.cpp 顶部注释), Vulkan/WebGPU
    // 都支持。ImGuiBackend 没有成功建出这套 pipeline 时, 这几个函数会退化成普通的
    // ImGui::Image(), 不做任何可视化处理, 也不会崩溃或报错。
    //
    // channelMask 是 R/G/B/A 四个 bit 的组合(bit0=R, bit1=G, bit2=B, bit3=A)。A 的规则三个
    // 函数统一: 没选中时整体不透明; 选中且 R/G/B 至少选了一个时, 用纹理自己的 alpha 驱动这块
    // 预览的透明度; 选中但 R/G/B 都没选时, 把 alpha 当灰度图显示(不透明)。R/G/B 的规则
    // 因函数而异, 见各自的注释。

    // 把 .rg 两个通道按切线空间法线贴图的方式重建出完整 XYZ(Z = sqrt(1 - x^2 - y^2)),
    // 映射到 [0,1] 显示范围, 再按 channelMask 遮罩——R/G/B 没选中的清零、留在原来的位置
    // (不合并成灰度), 比如只选 R 就是偏红, 不是灰度; R/G/B 都没选时是纯黑。
    void NormalImage(ImTextureID textureId, const ImVec2& size, uint32_t channelMask = 0xF,
        const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1));

    // 原样采样显示, 只按 channelMask 遮罩——R/G/B 没选中的清零、留在原来的位置, 跟
    // NormalImage() 同一套规则, 区别只是不做法线重建, 直接用采样到的原始颜色。用来替代
    // 需要通道遮罩功能的 ImGui::Image() 调用。
    void Image(ImTextureID textureId, const ImVec2& size, uint32_t channelMask = 0xF,
        const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1));

    // 把 R 通道广播给 G/B 当作 RGB 都有的灰度纹理, 再按 Image() 的规则遮罩。
    void GrayImage(ImTextureID textureId, const ImVec2& size, uint32_t channelMask = 0xF,
        const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1));

    // sRGB 格式纹理采样出来是硬件已解码的线性值, 这里在写出前重新编码回 sRGB, 显示效果
    // 对齐纹理原始存储字节。通道遮罩规则跟 Image() 一致。
    void SRGBImage(ImTextureID textureId, const ImVec2& size, uint32_t channelMask = 0xF,
        const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1));

    // textureId 必须是 TextureCube 的 cube view(不是 2D)注册出来的句柄。uv 是等距投影
    // (equirectangular)展开图上的 [0,1]x[0,1] 坐标, 按方向实时采样 cubemap, 不需要预先
    // 烘焙成一张 2D 纹理。采样出来是线性值, 写出前编码成 sRGB, 跟 SRGBImage() 显示口径一致。
    // 通道遮罩规则跟 Image() 一致。
    void CubeImage(ImTextureID textureId, const ImVec2& size, uint32_t channelMask = 0xF,
        const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1));
}
