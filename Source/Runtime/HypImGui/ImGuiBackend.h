#pragma once
#include <RenderBase/RenderBackend.h>
#include <RHI/CommandList.h>
#include <RHI/Context.h>

#include <imgui.h>

namespace hyp
{
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

        // 供 ImGuiFrontend::beginFrame() 调用: ImGui 要求"渲染器的 NewFrame()"必须先于
        // ImGui::NewFrame() 执行(否则字体纹理相关的内部记账会出错), 而
        // ImGui::NewFrame() 只能发生在逻辑线程——所以这个函数虽然挂在 ImGuiBackend
        // 下面, 实际是从逻辑线程调用的。它只做 CPU 侧记账(检查字体纹理更新请求等),
        // 不触碰命令录制/GPU 提交, 和 renderDrawData() 的线程要求不冲突。
        static void newFrame();
    };
}
