#include "ImGuiFrontend.h"
#include "ImGuiBackend.h"
#include "ImGuiDrawDataSnapshot.h"

#include <Engine/Input/InputSystem.h>
#include <RenderBase/RenderBackend.h>
#include <RenderBase/RenderBaseDefines.h>

#include <backend/imgui_impl_glfw.h>

#include <GLFW/glfw3.h>

#include <array>
#include <memory>

namespace hyp
{
    namespace
    {
        // 按 io.WantCaptureMouse/WantCaptureKeyboard 在源头过滤掉 ImGui 想要的原始输入
        // 事件。只应在逻辑线程被 InputSystem::tick() 调用(ImGui 的 io 状态本来就只应在
        // 逻辑线程读写)。
        class ImGuiInputEventHandler : public IInputEventHandler
        {
        public:
            bool handle(const InputEvent& event) override
            {
                const ImGuiIO& io = ImGui::GetIO();
                switch (event.getType())
                {
                case InputEventType::Mouse_Button_Press:
                case InputEventType::Mouse_Button_Release:
                case InputEventType::Mouse_Move:
                case InputEventType::Mouse_Scroll:
                    return io.WantCaptureMouse;
                case InputEventType::Key_Press:
                case InputEventType::Key_Release:
                case InputEventType::Key_Repeat:
                    return io.WantCaptureKeyboard;
                default:
                    // 手柄事件: ImGui 不消费, 原样放行给游戏逻辑
                    return false;
                }
            }
        };

        // 按 flightIndex 轮转的持久 snapshot —— 数量必须等于 kMaxFramesInFlight, 这不是
        // 随意选的常量: RenderFrontend::beginFrame()/endFrame()(RenderFrontend.cpp)用一个
        // 同样是 kMaxFramesInFlight 大小的信号量(mFrameSemaphore)保证逻辑线程不会比
        // 渲染线程实际消费的进度领先超过这么多帧——beginFrame() 里 acquire, 对应那一帧的
        // 渲染命令在渲染线程执行完 endFrame() 之后才 release。这意味着: 当逻辑线程第 N
        // 次调用 endFrameAndSnapshot()、准备再次写入 slot (N % kMaxFramesInFlight) 时,
        // 渲染线程一定已经消费完第 (N - kMaxFramesInFlight) 帧写入这同一个 slot 的数据——
        // 和 FrameContext::mFlightIndex 复用 mCommandLists[mFlightIndex] 依赖的是同一个
        // 不变量。只要这里的轮转次数和调用 endFrameAndSnapshot() 的次数(每逻辑帧恰好
        // 一次, 不管这一帧是否产出有效绘制数据都要推进)保持一致, 就不需要任何额外同步。
        std::array<ImGuiDrawDataSnapshot, kMaxFramesInFlight> gSnapshots;
        uint32_t gFlightIndex = 0;
    }

    void ImGuiFrontend::init(GLFWwindow* window, RenderBackend& renderBackend, InputSystem& inputSystem)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // install_callbacks=true: 链式调用 Window.cpp 已经装好的 GLFW 回调, 见头文件
        // ImGuiFrontend::init() 的注释。
        if (renderBackend.getRHIBackend() == rhi::Backend::Vulkan)
            ImGui_ImplGlfw_InitForVulkan(window, true);
        else
            ImGui_ImplGlfw_InitForOther(window, true);

        // registerEventHandler 接管这个对象的生命周期, 不需要在 shutdown() 里手动删除——
        // InputSystem 析构时(随 Engine 一起)会自然清理。
        inputSystem.registerEventHandler(std::make_unique<ImGuiInputEventHandler>());
    }

    void ImGuiFrontend::shutdown()
    {
        // ImGui 自 1.92.0 起会在 DestroyContext() 里断言"不存在还没释放的 ImDrawList"——
        // 常驻 snapshot 缓存(每个 flight slot 一份)必须先于 DestroyContext() 清空
        // (官方 imgui_threaded_rendering.h 用例注释里明确提到的坑)。
        for (ImGuiDrawDataSnapshot& snapshot : gSnapshots)
            snapshot.clear();

        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiFrontend::beginFrame()
    {
        // ImGuiBackend::initRenderer() 必须在第一次 beginFrame() 之前就成功完成——只有
        // "设备不支持所需特性"(比如 Vulkan 没有 dynamic rendering)这一种情况会让
        // isAvailable() 保持 false, 这时候索性完全不构建 ImGui 面板, 而不是走进
        // ImGui::NewFrame() 触发它内部"字体图集必须已被后端认领"的断言。
        if (!ImGuiBackend::isAvailable())
            return;

        // ImGui 要求"渲染器的 NewFrame()"必须先于 ImGui::NewFrame() 执行(否则字体
        // 纹理相关的内部记账会出错)。ImGuiBackend::newFrame() 虽然名字挂在 Backend 下,
        // 实际只做 CPU 侧记账, 不触碰命令录制/GPU 提交, 所以在这里(逻辑线程)调用是
        // 安全的, 具体原因见 ImGuiBackend::newFrame() 的注释。
        ImGuiBackend::newFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    ImDrawData* ImGuiFrontend::endFrameAndSnapshot()
    {
        if (!ImGuiBackend::isAvailable())
            return nullptr;

        ImGui::Render();
        ImDrawData* rawDrawData = ImGui::GetDrawData();

        ImGuiDrawDataSnapshot& snapshot = gSnapshots[gFlightIndex];
        // 无论这一帧是否有有效绘制数据, flightIndex 都要照常推进——它必须和"调用
        // endFrameAndSnapshot() 的次数"严格同步, gSnapshots 声明处注释里的安全论证才
        // 成立。
        gFlightIndex = (gFlightIndex + 1) % kMaxFramesInFlight;

        // 窗口最小化等场景下 ImGui::Render() 可能产出 Valid=false 的 ImDrawData——
        // snapUsingSwap() 对此只有 assert、不做容错, 所以由这里负责跳过。
        if (!rawDrawData || !rawDrawData->Valid)
            return nullptr;

        snapshot.snapUsingSwap(rawDrawData, ImGui::GetTime());
        return &snapshot.getDrawData();
    }
}
