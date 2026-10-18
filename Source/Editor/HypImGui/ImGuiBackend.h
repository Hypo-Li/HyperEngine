#pragma once
#include <RenderBase/RenderBackend.h>
#include <RHI/CommandList.h>
#include <RHI/Context.h>

#include <imgui.h>

namespace hyp::rhi
{
    class TextureView;
}

namespace hyp
{
    // ImGuiShadedImage.h 里那族 NormalImage()/GrayImage()/Image() 控件用来选择具体套哪个
    // 自定义 pipeline——每加一种可视化模式在这里加一个值, 对应的 pipeline 由 ImGuiBackend
    // 自己创建/持有, 见 initRenderer() 的实现。
    enum class ShadedImageMode : uint32_t
    {
        Normal,
        Gray,
        Plain,
        SRGB,
        Cube,
    };

    // AddCallback() 的 userdata: 只用来选 pipeline。channelMask(R/G/B/A 通道遮罩, 具体含义
    // 见 ImGuiShadedImage.h 顶部注释)不走这里——它编进了紧跟着的那条 AddImage() 顶点颜色
    // 的 R 通道字节里, 不是 push constant/descriptor set, 理由见 bindShadedImagePipeline()
    // 的实现注释(多加一段 push constant range 会让 imgui 后续的 descriptor set 绑定被判定
    // 跟当前 pipeline layout 不兼容)。
    struct ShadedImageParams
    {
        ShadedImageMode mode;
        // 只有 WGPU 后端下的 Cube 模式会用到: imgui_impl_wgpu.cpp 给紧跟着那条 AddImage()
        // 画图命令自动建 bind group 的路径写死了 2D viewDimension, CubeImage() 在 WGPU 下
        // 故意把 AddImage() 自己的 textureId 传成 0 绕开那条自动绑定, 真实的纹理句柄改由
        // 这个字段带给 bindShadedImagePipeline() 自己去绑维度正确的 bind group。
        ImTextureID textureId = 0;
    };

    // 渲染线程侧的 ImGui 接入: Vulkan/WebGPU 渲染后端资源的初始化/销毁, 以及把
    // ImGuiFrontend 产出的 ImDrawData 录制进当前 pass。和 ImGuiFrontend 的划分方式
    // 对应引擎里 RenderFrontend/RenderBackend 的逻辑线程/渲染线程划分——但注意
    // newFrame() 是个例外, 见它自己的注释。
    class ImGuiBackend
    {
    public:
        // 只应调用一次, 在 ImGuiFrontend::init() 之后、engine.run() 开始跑渲染循环
        // 之前(即渲染线程还没有提交过任何一帧 GPU 工作, 此时用同一个 Device 句柄同步
        // 创建 ImGui 的渲染后端资源不会和渲染线程产生并发访问)。不能像懒加载资源那样
        // 推迟到渲染线程第一次用到时才创建: ImGui::NewFrame() 内部会断言字体图集必须
        // 已经被某个渲染后端"认领"并标记为已构建, 也就是说渲染后端必须在第一次
        // beginFrame() 调用之前就初始化完毕。
        static void initRenderer(RenderBackend& renderBackend, rhi::TextureFormat colorFormat);

        // 只应调用一次, 渲染线程已经完全停下之后(和 GPU 资源析构的时机要求一致,
        // 应该在 ImGuiFrontend::shutdown() 之前调用)。
        static void shutdown();

        // 渲染线程, 必须在某个已经处于 render pass 内部的位置调用(比如 present pass
        // 的 execute 回调里、endRenderPass() 之前): 把 ImGui 的绘制命令通过
        // cmd.invokeNativeCallback() 记录进指令流。drawData 为 nullptr 时直接跳过
        // (没有面板内容, 或者 ImGuiFrontend::endFrameAndSnapshot() 这一帧没产出有效
        // 数据); initRenderer() 未成功时也直接跳过。
        static void renderDrawData(rhi::CommandList& cmd, ImDrawData* drawData);

        static bool isAvailable() noexcept;

        static rhi::Backend getBackend() noexcept;

        // 渲染线程, 在离屏渲染目标的 TextureView (重新)创建之后调用一次: 把它转换成
        // ImGui::Image() 能用的 ImTextureID。返回值本身是个不透明的整数/指针值, 可以安全地
        // 缓存到一个跨线程可读的槽位(比如 std::atomic<ImTextureID>)里供逻辑线程读取——
        // 逻辑线程读到的必然是上一次渲染线程写入的值, 允许有一帧延迟, 和 ImDrawData
        // 本身跨线程传递的既有约定(见 ImGuiFrontend 注释)一致, 不需要额外同步。
        // isAvailable() 为 false 时返回 0。
        static ImTextureID registerTexture(rhi::TextureView& view);

        // 渲染线程, 用来使一个 registerTexture() 返回的 ImTextureID 失效(对应纹理被 resize
        // 替换、或者不再需要展示时调用)。因为逻辑线程领先渲染线程一帧, 调用这个函数时可能还有
        // 尚未被渲染线程消费的 ImDrawData 快照引用着这个旧 id, 所以底层资源的实际释放
        // (Vulkan 下是 VkDescriptorSet)会通过 RenderBackend::getDeferredTaskQueue() 延后
        // kMaxFramesInFlight 帧, 调用方不需要、也不应该自己再做一遍延迟。
        static void unregisterTexture(ImTextureID id);

        // 供 ImGuiShadedImage.h 里那族控件通过 ImDrawList::AddCallback() 注册——签名匹配
        // ImDrawCallback, 在 ImGui_ImplVulkan_RenderDrawData()/ImGui_ImplWGPU_RenderDrawData()
        // 真正回放这个 drawlist 时被调用, 从 ImGui::GetPlatformIO().Renderer_RenderState 拿到
        // 当前原生命令缓冲, 把 cmd->UserCallbackData(指向一份 ShadedImageParams)里选中的
        // pipeline 绑上去, 再推送 channelMask。紧跟着的正常 ImGui::Image() 绘制命令会用这个
        // 刚绑上去的 pipeline 实际画出来——原理见 ImGuiShadedImage.h 顶部注释。
        // Vulkan/WebGPU 都支持。
        static void bindShadedImagePipeline(const ImDrawList* parentList, const ImDrawCmd* cmd);

        // 供 ImGuiShadedImage.h 里那族控件判断: initRenderer() 有没有成功建出这套自定义
        // pipeline。为 false 时调用方应该退化成普通 ImGui::Image(), 不要调用
        // bindShadedImagePipeline()。
        static bool isShadedImageAvailable() noexcept;

        // 供 ImGuiFrontend::beginFrame() 调用: ImGui 要求"渲染器的 NewFrame()"必须先于
        // ImGui::NewFrame() 执行(否则字体纹理相关的内部记账会出错), 而
        // ImGui::NewFrame() 只能发生在逻辑线程——所以这个函数虽然挂在 ImGuiBackend
        // 下面, 实际是从逻辑线程调用的。它只做 CPU 侧记账(检查字体纹理更新请求等),
        // 不触碰命令录制/GPU 提交, 和 renderDrawData() 的线程要求不冲突。
        static void newFrame();
    };
}
