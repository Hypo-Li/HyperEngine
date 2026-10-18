#include "ImGuiFrontend.h"
#include "ImGuiBackend.h"
#include "ImGuiDrawDataSnapshot.h"
#include "IconsMaterialSymbols.h"

#include <Engine/Input/InputSystem.h>
#include <Platform/Platform.h>
#include <RenderBase/RenderBackend.h>
#include <RenderBase/RenderBaseDefines.h>

#include <imgui_freetype.h>
#include <backend/imgui_impl_glfw.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <span>

namespace hyp
{
    namespace
    {
        bool gGameMouseActive = false;
        bool gGameKeyboardActive = false;

        // 只应在逻辑线程被 InputSystem::tick() 调用——ImGui 的 io 状态只应在逻辑线程读写。
        class ImGuiInputEventHandler : public IInputEventHandler
        {
        public:
            bool handle(const InputEvent& event) override
            {
                const ImGuiIO& io = ImGui::GetIO();
                switch (event.getType())
                {
                case InputEventType::Mouse_Button_Press:
                case InputEventType::Mouse_Move:
                case InputEventType::Mouse_Scroll:
                    return io.WantCaptureMouse && !gGameMouseActive;
                case InputEventType::Key_Press:
                case InputEventType::Key_Repeat:
                    return io.WantCaptureKeyboard && !gGameKeyboardActive;
                case InputEventType::Mouse_Button_Release:
                case InputEventType::Key_Release:
                    // 松开事件始终放行: 按下时游戏拿到了, 松开时若被 ImGui 吞掉, PlayerInput 里的按住状态会卡死
                    return false;
                default:
                    // 手柄事件: ImGui 不消费, 原样放行给游戏逻辑
                    return false;
                }
            }
        };

        // 大小必须等于 kMaxFramesInFlight——RenderFrontend 用同样大小的信号量保证渲染线程
        // 消费进度不会落后逻辑线程超过这么多帧, 轮转下标复用同一个 slot 时对应帧必然已被
        // 渲染线程消费完。
        std::array<ImGuiDrawDataSnapshot, kMaxFramesInFlight> gSnapshots;
        uint32_t gFlightIndex = 0;

        float gDpiScale = 1.0f;
        ImFont* gMonospaceFont = nullptr;

        // ImFontConfig::GlyphRanges keeps a raw pointer (not a copy) for the atlas's lifetime, so
        // the range arrays built at init time (unlike kLatinGlyphRanges, which is a compile-time
        // constant) need to live somewhere with static storage duration.
        std::vector<ImWchar> gIconGlyphRanges;
        std::vector<ImWchar> gChineseGlyphRanges;

        std::vector<std::uint32_t> decodeUtf8Codepoints(const std::string& text)
        {
            std::vector<std::uint32_t> codepoints;
            codepoints.reserve(text.size());
            const char* cursor = text.c_str();
            const char* end = cursor + text.size();
            while (cursor < end)
            {
                unsigned int codepoint = 0;
                const int consumed = ImTextCharFromUtf8(&codepoint, cursor, end);
                cursor += consumed > 0 ? consumed : 1;
                if (codepoint != 0)
                    codepoints.push_back(codepoint);
            }
            return codepoints;
        }

        // Turns a flat list of codepoints into the sorted, deduplicated, range-merged
        // [start,end,...,0] array ImFontConfig::GlyphRanges expects.
        std::vector<ImWchar> buildGlyphRanges(std::vector<std::uint32_t> codepoints)
        {
            std::ranges::sort(codepoints);
            codepoints.erase(std::unique(codepoints.begin(), codepoints.end()), codepoints.end());

            std::vector<ImWchar> ranges;
            for (std::size_t i = 0; i < codepoints.size();)
            {
                std::size_t j = i;
                while (j + 1 < codepoints.size() && codepoints[j + 1] == codepoints[j] + 1)
                    ++j;
                ranges.push_back(static_cast<ImWchar>(codepoints[i]));
                ranges.push_back(static_cast<ImWchar>(codepoints[j]));
                i = j + 1;
            }
            ranges.push_back(0);
            return ranges;
        }

        std::vector<ImWchar> loadChineseGlyphRanges(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return buildGlyphRanges(decodeUtf8Codepoints(text));
        }

        // Decodes each ICON_MS_XXX macro's UTF-8 bytes back into its codepoint, so the glyph
        // range list stays derived from the actual macros instead of hand-copied hex.
        std::vector<ImWchar> loadIconGlyphRanges(std::span<const char* const> iconLiterals)
        {
            std::vector<std::uint32_t> codepoints;
            codepoints.reserve(iconLiterals.size());
            for (const char* icon : iconLiterals)
            {
                unsigned int codepoint = 0;
                ImTextCharFromUtf8(&codepoint, icon, nullptr);
                codepoints.push_back(codepoint);
            }
            return buildGlyphRanges(std::move(codepoints));
        }

        // io.IniFilename 存的是裸指针, 生命周期必须撑到 DestroyContext() 落盘完成, 不能用
        // 临时 std::string::c_str()。
        std::string gIniFilenamePath;
    }

    void ImGuiFrontend::init(Window& window, RenderBackend& renderBackend, InputSystem& inputSystem, const std::filesystem::path& resourceDir)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        // 基于可执行文件所在目录算成绝对路径, 不依赖进程启动时的当前工作目录。
        gIniFilenamePath = (Platform::getExecutableDir() / "HyperEditor.ini").string();
        // 文件名不用 ImGui 默认的 "imgui.ini"——Nsight 注入进同一进程的拦截层自己也起了一个
        // 用默认文件名的 Dear ImGui 实例, 两边会在退出时互相覆盖对方的存盘内容。
        io.IniFilename = gIniFilenamePath.c_str();

        // 挂到单个 ImFontConfig::FontLoader 上会在这版 new atlas 架构里直接命中
        // "FIXME-NEWATLAS: Unsupported yet." 断言, FreeType 的 LoaderInit/Shutdown 回调只认
        // atlas 级别的 SetFontLoader()。
        io.Fonts->SetFontLoader(ImGuiFreeType::GetFontLoader());

        float dpiScale = window.getContentScale().x;
        gDpiScale = dpiScale;
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(dpiScale);
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;

        ImFontConfig config;
        config.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
        std::string fontPath = (resourceDir / "Roboto-Regular.ttf").string();

        // 这里同步预烤的字形范围之后会被锁死(见下面 ImFontFlags_NoLoadGlyphs), 不会再被
        // ImGui 1.92 的动态图集机制在渲染时按需加载——避免逻辑线程光栅化新字形和渲染线程
        // 同时读取同一份 ImTextureData 产生数据竞争, 这是偶发字形残缺的根因。
        static constexpr ImWchar kLatinGlyphRanges[] = {
            0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
            0x0100, 0x017F, // Latin Extended-A
            0x0180, 0x024F, // Latin Extended-B
            0,
        };
        config.GlyphRanges = kLatinGlyphRanges;

        ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.0f * dpiScale, &config);

        // MergeMode 合并进同一个 ImFont*——面板画图标跟画普通文字一样直接用 ICON_MS_XXX,
        // 不需要来回 PushFont/PopFont。
        //
        // 图标是方形字形, 不设 GlyphMinAdvanceX 的话每个图标步进宽度按自己的 bounding box
        // 走, 摆在一起宽窄不一。
        //
        // MergeMode 字体源不会重算 baked->Ascent, 图标字形借用 Latin 字体(Roboto)自己的
        // ascent 当基线, 但 Material Symbols 是按自身 ascent/descent 居中设计的、比例跟
        // Roboto 差得远, 不加 GlyphOffset 图标会明显偏上。
        //
        // 图标单独放大到 24px(Latin 基准是 16px), 图标线条细, 跟正文同尺寸摆在按钮里显得
        // 偏小。
        //
        // GlyphOffset.y = 6px 是用 freetype-py 复刻 FreeType 的字形定位逻辑、对
        // refresh/home/delete/close/settings/link 这 6 个代表性图标实测算出来的, 不是试出来的。
        ImFontConfig iconConfig;
        iconConfig.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
        iconConfig.MergeMode = true;
        iconConfig.GlyphOffset = ImVec2(0.0f, 6.0f * dpiScale);
        iconConfig.GlyphMinAdvanceX = 24.0f * dpiScale;
        std::string iconFontPath = (resourceDir / "MaterialSymbolsOutlined.ttf").string();

        // ICON_MIN_MS..ICON_MAX_MS 覆盖近一百万个码位, 这里只列出实际用到的十几个图标。
        static constexpr const char* kUsedIconNames[] = {
            ICON_MS_ADD, ICON_MS_ARROW_BACK, ICON_MS_ARROW_DOWNWARD, ICON_MS_ARROW_FORWARD,
            ICON_MS_ARROW_UPWARD, ICON_MS_CHEVRON_RIGHT, ICON_MS_CLOSE, ICON_MS_EMERGENCY,
            ICON_MS_ENABLE, ICON_MS_MORE_HORIZ, ICON_MS_PACKAGE_2, ICON_MS_PAUSE, ICON_MS_PLAY_ARROW,
            ICON_MS_REFRESH, ICON_MS_REPLY, ICON_MS_RESUME, ICON_MS_SAVE, ICON_MS_SKIP_NEXT, ICON_MS_STOP,
            ICON_MS_TAB_SEARCH, ICON_MS_VISIBILITY, ICON_MS_VISIBILITY_OFF,
            ICON_MS_CATEGORY, ICON_MS_DEPLOYED_CODE, ICON_MS_FLASHLIGHT_ON,
            ICON_MS_LIGHTBULB, ICON_MS_LINK, ICON_MS_SCATTER_PLOT, ICON_MS_SKULL, ICON_MS_SUNNY, ICON_MS_SUNNY_SNOWING,
            ICON_MS_CIRCLE, ICON_MS_FOLDER, ICON_MS_VIDEOCAM, ICON_MS_VRPANO, ICON_MS_WB_TWILIGHT,
            ICON_MS_ACCOUNT_TREE, ICON_MS_FOLDER_OPEN, ICON_MS_TERMINAL, ICON_MS_TUNE, ICON_MS_VIEW_IN_AR,
            ICON_MS_LOCK, ICON_MS_LOCK_OPEN, ICON_MS_IMAGE, ICON_MS_TEXT_FIELDS,
        };
        gIconGlyphRanges = loadIconGlyphRanges(kUsedIconNames);
        iconConfig.GlyphRanges = gIconGlyphRanges.data();
        io.Fonts->AddFontFromFileTTF(iconFontPath.c_str(), 24.0f * dpiScale, &iconConfig);

        // 不需要 GlyphOffset: 中文字体的竖直度量是按跟拉丁文混排对齐设计的, 不像 Material
        // Symbols 那样按自身 ascent/descent 居中。
        //
        // 字号比 Latin 基准大一圈(20px vs 16px)——同样像素高度下汉字笔画密度更高, 视觉上
        // 显得更小。
        ImFontConfig cjkConfig;
        cjkConfig.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
        cjkConfig.MergeMode = true;
        std::string cjkFontPath = (resourceDir / "NotoSansSC-Regular.ttf").string();
        gChineseGlyphRanges = loadChineseGlyphRanges(resourceDir / "cns_common_char.txt");
        cjkConfig.GlyphRanges = gChineseGlyphRanges.data();
        io.Fonts->AddFontFromFileTTF(cjkFontPath.c_str(), 20.0f * dpiScale, &cjkConfig);

        // 独立的 ImFont*(不是 MergeMode 合进 font), 用的地方(TextEditor)自己
        // PushFont/PopFont 切过去。
        ImFontConfig monospaceConfig;
        monospaceConfig.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
        monospaceConfig.SizePixels = 13.0f * dpiScale; // ImGui 默认字体历史上的基准尺寸

        ImFont* monospaceFont = nullptr;
        float monospaceSizePixels = monospaceConfig.SizePixels;
#ifdef _WIN32
        const char* windowsDir = std::getenv("WINDIR");
        std::filesystem::path consolasPath = std::filesystem::path(windowsDir ? windowsDir : R"(C:\Windows)") / "Fonts" / "consola.ttf";
        if (std::filesystem::exists(consolasPath))
        {
            monospaceSizePixels = 15.0f * dpiScale;
            monospaceFont = io.Fonts->AddFontFromFileTTF(consolasPath.string().c_str(), monospaceSizePixels, &monospaceConfig);
        }
#endif
        if (!monospaceFont)
            monospaceFont = io.Fonts->AddFontDefault(&monospaceConfig);
        gMonospaceFont = monospaceFont;

        // 放大比例不能太大: 排版按 CodePoint::isEastAsian() 把汉字算作 2 列宽, 字形明显
        // 宽过这 2 列预留空间就会盖到下一个字符上。
        ImFontConfig monospaceCjkConfig;
        monospaceCjkConfig.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
        monospaceCjkConfig.MergeMode = true;
        monospaceCjkConfig.GlyphRanges = gChineseGlyphRanges.data();
        io.Fonts->AddFontFromFileTTF(cjkFontPath.c_str(), monospaceSizePixels * 1.5f, &monospaceCjkConfig);

        // 不用 io.Fonts->Build() 预烤: 它的 legacy 全烤分支会标记 PreloadedAllGlyphsRanges,
        // 但这个标记跟随后 ImGuiBackend::initRenderer() 置位的 RendererHasTextures 同时为
        // true 时, 会在第一次 NewFrame() 命中断言——改成逐字形调 FindGlyph() 强制光栅化,
        // 效果等价但不碰这个标记。
        if (font)
        {
            ImFontBaked* baked = font->GetFontBaked(font->LegacySize);
            for (const ImWchar* ranges = kLatinGlyphRanges; ranges[0]; ranges += 2)
                for (unsigned int c = ranges[0]; c <= ranges[1]; ++c)
                    baked->FindGlyph(static_cast<ImWchar>(c));
            // 图标/汉字字体都合并进同一个 font, 强制预烤要覆盖它们的码点范围, 否则会触发
            // 跟 Latin 字体一样的动态加载竞争。
            for (const ImWchar* ranges = gIconGlyphRanges.data(); ranges[0]; ranges += 2)
                for (unsigned int c = ranges[0]; c <= ranges[1]; ++c)
                    baked->FindGlyph(static_cast<ImWchar>(c));
            for (const ImWchar* ranges = gChineseGlyphRanges.data(); ranges[0]; ranges += 2)
                for (unsigned int c = ranges[0]; c <= ranges[1]; ++c)
                    baked->FindGlyph(static_cast<ImWchar>(c));

            // 锁死: 之后任何时候都不会再有新字形被动态加进图集, 图集从此对渲染线程
            // 而言是只读且不再变化的。
            font->Flags |= ImFontFlags_NoLoadGlyphs | ImFontFlags_LockBakedSizes;
        }

        // 等宽字体是独立的 ImFont*, 要单独走一遍同样的强制预烤 + 锁。
        //
        // U+2026 不在 kLatinGlyphRanges 覆盖范围内, 但 TextEditor 折叠代码块时会用
        // font->EllipsisChar 画这个省略号, 不预烤的话锁死后会找不到字形。
        static constexpr ImWchar kMonospaceExtraGlyphRanges[] = {
            0x2026, 0x2026, // HORIZONTAL ELLIPSIS ("…"), used by TextEditor's fold indicator
            0,
        };
        if (monospaceFont)
        {
            ImFontBaked* baked = monospaceFont->GetFontBaked(monospaceFont->LegacySize);
            for (const ImWchar* ranges = kLatinGlyphRanges; ranges[0]; ranges += 2)
                for (unsigned int c = ranges[0]; c <= ranges[1]; ++c)
                    baked->FindGlyph(static_cast<ImWchar>(c));
            for (const ImWchar* ranges = kMonospaceExtraGlyphRanges; ranges[0]; ranges += 2)
                for (unsigned int c = ranges[0]; c <= ranges[1]; ++c)
                    baked->FindGlyph(static_cast<ImWchar>(c));
            for (const ImWchar* ranges = gChineseGlyphRanges.data(); ranges[0]; ranges += 2)
                for (unsigned int c = ranges[0]; c <= ranges[1]; ++c)
                    baked->FindGlyph(static_cast<ImWchar>(c));
            monospaceFont->Flags |= ImFontFlags_NoLoadGlyphs | ImFontFlags_LockBakedSizes;
        }

        // install_callbacks=true: 链式调用 Window.cpp 已经装好的 GLFW 回调, 见头文件
        // ImGuiFrontend::init() 的注释。
        GLFWwindow* glfwWindow = nullptr;
        window.getGlfwHandle(&glfwWindow);
        if (renderBackend.getRHIBackend() == rhi::Backend::Vulkan)
            ImGui_ImplGlfw_InitForVulkan(glfwWindow, true);
        else
            ImGui_ImplGlfw_InitForOther(glfwWindow, true);

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

    void ImGuiFrontend::setGameInputActive(bool mouse, bool keyboard)
    {
        gGameMouseActive |= mouse;
        gGameKeyboardActive |= keyboard;
    }

    void ImGuiFrontend::beginFrame()
    {
        gGameMouseActive = false;
        gGameKeyboardActive = false;

        // ImGuiBackend::initRenderer() 必须在第一次 beginFrame() 之前就成功完成——只有
        // "设备不支持所需特性"(比如 Vulkan 没有 dynamic rendering)这一种情况会让
        // isAvailable() 保持 false, 这时候索性完全不构建 ImGui 面板, 而不是走进
        // ImGui::NewFrame() 触发它内部"字体图集必须已被后端认领"的断言。
        if (!ImGuiBackend::isAvailable())
            return;

        // "渲染器的 NewFrame()"必须先于 ImGui::NewFrame() 执行, 否则字体纹理相关的内部
        // 记账会出错。
        ImGuiBackend::newFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // GLFW_CURSOR_DISABLED(见 Window::captureCursor)下 GLFW 不直接上报 OS 光标位置, 而是
        // 反复把 OS 光标复位到窗口中心、按"当前坐标 - 上次记录坐标"累加出一个虚拟位置。复位之前
        // 产生、之后才被处理的那条 WM_MOUSEMOVE, 算出来的增量是"滞留坐标 - 窗口中心", 不是位移。
        // DragFloat 按 io.MouseDelta 逐帧累加, 旋转控件 0.25°/px 撞上一次就跳两百多度。
        //
        // 滞留坐标落在窗口内, 所以这个伪影逐轴的幅度上界恰好是窗口边长的一半——阈值必须锚在窗口
        // 尺寸上, 写成像素常数会在换分辨率时失效(小窗口漏判、大窗口误判), 写成速度更错: 伪影是
        // 纯空间量, 乘帧时间只会让卡顿帧的阈值放大、反而放它过去。
        // 系数 0.5 取自实测: 抓到过 850px 的伪影, 对 1920 宽窗口是上界的 0.885, 取 0.5 留出余量,
        // 同时"人手一帧划过窗口 1/4"远超拖数值控件的实际手速, 不会误伤。
        // 整帧丢弃而不是钳位——伪影里没有任何真实位移分量值得保留。
        ImGuiIO& io = ImGui::GetIO();
        constexpr float kMaxPlausibleDisplayFraction = 0.25f;
        const ImVec2 limit(
            io.DisplaySize.x * kMaxPlausibleDisplayFraction,
            io.DisplaySize.y * kMaxPlausibleDisplayFraction);
        if (io.MouseDelta.x > limit.x || io.MouseDelta.x < -limit.x
            || io.MouseDelta.y > limit.y || io.MouseDelta.y < -limit.y)
        {
            io.MouseDelta = ImVec2(0.0f, 0.0f);
        }
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

    float ImGuiFrontend::getDpiScale() noexcept
    {
        return gDpiScale;
    }

    ImFont* ImGuiFrontend::getMonospaceFont() noexcept
    {
        return gMonospaceFont;
    }
}
