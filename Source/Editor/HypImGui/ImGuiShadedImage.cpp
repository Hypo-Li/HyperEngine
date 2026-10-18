#include "ImGuiShadedImage.h"
#include "ImGuiBackend.h"

namespace hyp
{
    namespace
    {
        // channelMask 通过顶点颜色的 R 通道字节传给 fragment shader(见 ImGuiBackend.cpp 里
        // 各模式 shader 的实现)——用 ImDrawList::AddImage() 而不是 ImGui::Image() 就是因为
        // 只有前者还留着可以指定顶点颜色的 col 参数(新版 ImGui::Image() 已经把 tint 参数拿掉
        // 了)。G/B 通道固定 0、A 固定 255(不透明), 不携带其它信息。
        void shadedImage(ShadedImageMode mode, ImTextureID textureId, const ImVec2& size,
            uint32_t channelMask, const ImVec2& uv0, const ImVec2& uv1)
        {
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1(p0.x + size.x, p0.y + size.y);
            ImU32 col = IM_COL32(channelMask & 0xFF, 0, 0, 255);

            ShadedImageParams params{ mode };
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddCallback(&ImGuiBackend::bindShadedImagePipeline, &params, sizeof(params));
            drawList->AddImage(textureId, p0, p1, uv0, uv1, col);
            drawList->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);

            ImGui::Dummy(size);
        }
    }

    void NormalImage(ImTextureID textureId, const ImVec2& size, uint32_t channelMask, const ImVec2& uv0, const ImVec2& uv1)
    {
        if (!ImGuiBackend::isShadedImageAvailable())
        {
            ImGui::Image(textureId, size, uv0, uv1);
            return;
        }

        shadedImage(ShadedImageMode::Normal, textureId, size, channelMask, uv0, uv1);
    }

    void Image(ImTextureID textureId, const ImVec2& size, uint32_t channelMask, const ImVec2& uv0, const ImVec2& uv1)
    {
        if (!ImGuiBackend::isShadedImageAvailable() || channelMask == 0xF)
        {
            // 全选(默认值)等价于不遮罩——直接用普通 ImGui::Image(), 不用为了不存在的遮罩
            // 需求去换 pipeline
            ImGui::Image(textureId, size, uv0, uv1);
            return;
        }

        shadedImage(ShadedImageMode::Plain, textureId, size, channelMask, uv0, uv1);
    }

    void GrayImage(ImTextureID textureId, const ImVec2& size, uint32_t channelMask, const ImVec2& uv0, const ImVec2& uv1)
    {
        if (!ImGuiBackend::isShadedImageAvailable())
        {
            ImGui::Image(textureId, size, uv0, uv1);
            return;
        }

        shadedImage(ShadedImageMode::Gray, textureId, size, channelMask, uv0, uv1);
    }

    void SRGBImage(ImTextureID textureId, const ImVec2& size, uint32_t channelMask, const ImVec2& uv0, const ImVec2& uv1)
    {
        if (!ImGuiBackend::isShadedImageAvailable())
        {
            ImGui::Image(textureId, size, uv0, uv1);
            return;
        }

        shadedImage(ShadedImageMode::SRGB, textureId, size, channelMask, uv0, uv1);
    }

    void CubeImage(ImTextureID textureId, const ImVec2& size, uint32_t channelMask, const ImVec2& uv0, const ImVec2& uv1)
    {
        if (!ImGuiBackend::isShadedImageAvailable())
        {
            ImGui::Image(textureId, size, uv0, uv1);
            return;
        }

        if (ImGuiBackend::getBackend() == rhi::Backend::Vulkan)
        {
            shadedImage(ShadedImageMode::Cube, textureId, size, channelMask, uv0, uv1);
            return;
        }

        // WGPU: imgui_impl_wgpu.cpp 给紧跟着这条 AddImage() 自动建 bind group 的路径写死了
        // 2D viewDimension, 直接传真实的 cube textureId 会在建 bind group 那一步就跟实际的
        // Cube view 对不上——这里改传 0 绕开那条自动绑定(0 时它整个跳过, 见该文件的
        // ImGui_ImplWGPU_RenderDrawData()), 真实句柄通过 ShadedImageParams::textureId 带给
        // bindShadedImagePipeline(), 由它自己绑一份维度正确的 group 1。
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + size.x, p0.y + size.y);
        ImU32 col = IM_COL32(channelMask & 0xFF, 0, 0, 255);

        ShadedImageParams params{ ShadedImageMode::Cube, textureId };
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddCallback(&ImGuiBackend::bindShadedImagePipeline, &params, sizeof(params));
        drawList->AddImage(ImTextureID(0), p0, p1, uv0, uv1, col);
        drawList->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);

        ImGui::Dummy(size);
    }
}
