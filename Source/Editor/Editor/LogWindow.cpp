#include "LogWindow.h"

#include <HypImGui/IconsMaterialSymbols.h>
#include <HypImGui/ImGuiFrontend.h>

#include <algorithm>
#include <ctime>
#include <utility>

namespace hyp::ed
{
    namespace
    {
        const char* levelLabel(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Debug:
                return "Debug";
            case LogLevel::Info:
                return "Info";
            case LogLevel::Warn:
                return "Warn";
            case LogLevel::Error:
                return "Error";
            default:
                return "?";
            }
        }

        ImVec4 levelColor(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Debug:
                return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            case LogLevel::Warn:
                return ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
            case LogLevel::Error:
                return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            case LogLevel::Info:
            default:
                return ImGui::GetStyleColorVec4(ImGuiCol_Text);
            }
        }

        // 不用 std::format/std::chrono 的 %H:%M:%S 格式化——那条路径依赖时区数据库支持,
        // 在部分平台上不可靠; localtime_s/r + strftime 是 C 标准库里最朴素、最不会出岔子
        // 的做法, 跟 AssetBrowser.cpp 里已经在用的 #ifdef _WIN32 平台分支是同一个风格。
        std::string formatTime(std::chrono::system_clock::time_point time)
        {
            std::time_t t = std::chrono::system_clock::to_time_t(time);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char buffer[16];
            std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm);
            return buffer;
        }

        bool less(const LogSelectionPoint& a, const LogSelectionPoint& b)
        {
            return a.lineIndex != b.lineIndex ? a.lineIndex < b.lineIndex : a.byteOffset < b.byteOffset;
        }

        constexpr float kAutoScrollEdgeZone = 24.0f;
        constexpr float kAutoScrollMaxSpeed = 30.0f;

        // mouseCoord 落在 [minCoord, maxCoord] 边缘 kAutoScrollEdgeZone 像素以内(或者已经
        // 越界)时返回非 0, 越靠近/越过边缘越大, 超过一个 edge zone 的距离之后封顶在
        // kAutoScrollMaxSpeed, 不会因为鼠标远在天边而滚出离谱的速度。
        float axisAutoScrollSpeed(float mouseCoord, float minCoord, float maxCoord)
        {
            if (mouseCoord < minCoord + kAutoScrollEdgeZone)
            {
                float overshoot = std::min((minCoord + kAutoScrollEdgeZone) - mouseCoord, kAutoScrollEdgeZone);
                return -(overshoot / kAutoScrollEdgeZone) * kAutoScrollMaxSpeed;
            }
            if (mouseCoord > maxCoord - kAutoScrollEdgeZone)
            {
                float overshoot = std::min(mouseCoord - (maxCoord - kAutoScrollEdgeZone), kAutoScrollEdgeZone);
                return (overshoot / kAutoScrollEdgeZone) * kAutoScrollMaxSpeed;
            }
            return 0.0f;
        }

        // 逐字节按 ImFontBaked::GetCharAdvance() 累加宽度——把多字节 UTF-8 序列当独立字节
        // 处理, 对纯 ASCII 日志(绝大多数编译诊断/引擎日志)结果精确, 多字节字符命中位置会
        // 有些许偏差, 换来的是不用碰 imgui 内部的 UTF-8 解码接口。
        float measureTextWidth(const char* begin, const char* end)
        {
            return ImGui::CalcTextSize(begin, end).x;
        }

        // 首行的"完整可选文本"是时间戳+Level+location+消息拼成一整条, 跟控制台/日志文件里
        // spdlog 默认格式(`[time] [level] [file:line] message`)对齐, 方便复制出去之后拿去
        // 日志文件里搜——用同一份字符串既拿去测量/命中测试, 也拿去实际渲染(见 drawEntries()),
        // 保证"用来算选区高亮矩形的宽度"和"实际画出来的文字宽度"永远是同一次测量的结果。
        // 续行没有这段前缀, 就是消息本体。
        std::string rowSelectableText(const LogEntry& entry, const LogDisplayLine& line)
        {
            if (!line.isFirstLine)
                return std::string(line.text);

            std::string result = "[";
            result += formatTime(entry.time);
            result += "] [";
            result += levelLabel(entry.level);
            result += "] [";
            result += entry.location;
            result += "] ";
            result += line.text;
            return result;
        }
    }

    LogWindow::LogWindow() :
        EditorWindow("Log", "Log")
    {
        setIcon(ICON_MS_TERMINAL);
    }

    void LogWindow::onDraw()
    {
        bool clearRequested = false;
        LogSystem::get().withHistory([this, &clearRequested](const std::deque<LogEntry>& entries, const std::array<uint32_t, kLogLevelCount>& levelCounts, uint64_t version)
        {
            bool filterDirty = drawToolbar(levelCounts, clearRequested);
            ImGui::Separator();
            drawEntries(entries, version, filterDirty);
        });

        // withHistory() 的回调已经返回、mHistoryMutex 已经释放, 这里调 clearHistory() 才
        // 安全——原因见 drawToolbar() 声明处的注释。
        if (clearRequested)
            LogSystem::get().clearHistory();
    }

    bool LogWindow::drawToolbar(const std::array<uint32_t, kLogLevelCount>& levelCounts, bool& clearRequested)
    {
        bool changed = false;

        if (ImGui::Button("Clear"))
        {
            clearRequested = true;
            changed = true;
        }

        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &mAutoScroll);

        ImGui::SameLine();
        changed |= mFilter.Draw("Filter", 200.0f * ImGuiFrontend::getDpiScale());

        for (size_t i = 0; i < kLogLevelCount; ++i)
        {
            LogLevel level = static_cast<LogLevel>(i);
            std::string label = std::format("{} ({})", levelLabel(level), levelCounts[i]);

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, levelColor(level));
            changed |= ImGui::Checkbox(label.c_str(), &mLevelVisible[i]);
            ImGui::PopStyleColor();
        }

        return changed;
    }

    void LogWindow::drawEntries(const std::deque<LogEntry>& entries, uint64_t version, bool filterDirty)
    {
        if (ImGui::BeginChild("##LogScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
            // 按 level 勾选 + 文本搜索过滤出可见的"视觉行"(见 LogWindow.h 里 LogDisplayLine
            // 的注释——一条日志消息按 '\n' 拆成几行就产出几个 LogDisplayLine, 不是一条日志
            // 一个下标), 存进跨帧复用的 mVisibleLines——只有"LogSystem 那边有新日志/被 clear
            // 过"(version 变了)或者"这一帧过滤条件本身变了"(filterDirty)才重新扫一遍重建,
            // 其它帧直接复用上一次的结果。
            if (!mHasComputedOnce || version != mLastVersion || filterDirty)
            {
                mVisibleLines.clear();
                for (int i = 0; i < static_cast<int>(entries.size()); ++i)
                {
                    const LogEntry& entry = entries[i];
                    if (!mLevelVisible[static_cast<size_t>(entry.level)])
                        continue;
                    if (!mFilter.PassFilter(entry.message.c_str()))
                        continue;

                    std::string_view message = entry.message;
                    bool isFirstLine = true;
                    size_t lineStart = 0;
                    while (true)
                    {
                        size_t newlinePos = message.find('\n', lineStart);
                        std::string_view line = newlinePos == std::string_view::npos
                            ? message.substr(lineStart)
                            : message.substr(lineStart, newlinePos - lineStart);
                        LogDisplayLine displayLine{ i, isFirstLine, line, {} };
                        displayLine.selectableText = rowSelectableText(entry, displayLine);
                        mVisibleLines.push_back(std::move(displayLine));
                        isFirstLine = false;
                        if (newlinePos == std::string_view::npos)
                            break;
                        lineStart = newlinePos + 1;
                    }
                }
                mLastVersion = version;
                mHasComputedOnce = true;
                mHasSelection = false;
            }

            // 必须在这一帧新增内容之前记录"是否已经在底部"——新增日志会推高 GetScrollMaxY(),
            // 之后再判断只会永远判定成"没到底"。跟 imgui_demo.cpp 里 ExampleAppLog 的做法一致。
            bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;

            float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            ImVec2 originScreenPos = ImGui::GetCursorScreenPos();

            handleSelectionInput(originScreenPos, lineHeight);

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(mVisibleLines.size()), lineHeight);
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const LogDisplayLine& displayLine = mVisibleLines[row];
                    const LogEntry& entry = entries[displayLine.entryIndex];
                    const std::string& rowText = displayLine.selectableText;

                    drawSelectionHighlight(row, rowText, originScreenPos, lineHeight);

                    // rowText 已经是拼好的完整字符串(可能不是以 '\0' 结尾的地方截断——这里
                    // 用 c_str() 没问题, 因为 rowText 是我们自己拼的 std::string, 天然带
                    // 终止符), 但仍然用 TextUnformatted 的 (begin, end) 重载而不是 c_str()
                    // 单参数版本——message/location 本身可能含 '%', 绝不能让 ImGui::Text()
                    // 把它当格式串解析。
                    ImGui::PushStyleColor(ImGuiCol_Text, levelColor(entry.level));
                    ImGui::TextUnformatted(rowText.c_str(), rowText.c_str() + rowText.size());
                    ImGui::PopStyleColor();
                }
            }
            clipper.End();

            if (mAutoScroll && wasAtBottom)
                ImGui::SetScrollHereY(1.0f);

            if (ImGui::BeginPopupContextWindow("LogContextMenu"))
            {
                if (ImGui::MenuItem("Copy Selection", nullptr, false, mHasSelection))
                    copySelection();
                if (ImGui::MenuItem("Copy All Visible"))
                    copyAllVisible();
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
    }

    void LogWindow::handleSelectionInput(ImVec2 originScreenPos, float lineHeight)
    {
        if (mVisibleLines.empty())
            return;

        bool hovered = ImGui::IsWindowHovered();
        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            ImGui::SetWindowFocus();
            mSelectionAnchor = mSelectionActive = hitTest(ImGui::GetMousePos(), originScreenPos, lineHeight);
            mHasSelection = true;
        }
        else if (mHasSelection && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            autoScrollForDrag(ImGui::GetMousePos());
            mSelectionActive = hitTest(ImGui::GetMousePos(), originScreenPos, lineHeight);
        }

        bool focused = ImGui::IsWindowFocused();
        if (mHasSelection && focused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
            copySelection();

        if (mHasSelection && focused && ImGui::GetIO().KeyShift)
            handleKeyboardSelection(lineHeight);
    }

    void LogWindow::autoScrollForDrag(ImVec2 mousePos) const
    {
        ImVec2 windowMin = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();

        float scrollDeltaX = axisAutoScrollSpeed(mousePos.x, windowMin.x, windowMin.x + windowSize.x);
        if (scrollDeltaX != 0.0f)
            ImGui::SetScrollX(ImGui::GetScrollX() + scrollDeltaX);

        float scrollDeltaY = axisAutoScrollSpeed(mousePos.y, windowMin.y, windowMin.y + windowSize.y);
        if (scrollDeltaY != 0.0f)
            ImGui::SetScrollY(ImGui::GetScrollY() + scrollDeltaY);
    }

    void LogWindow::handleKeyboardSelection(float lineHeight)
    {
        LogSelectionPoint& active = mSelectionActive;
        int lastLine = static_cast<int>(mVisibleLines.size()) - 1;

        auto rowTextSize = [&](int lineIndex) -> size_t
        {
            return mVisibleLines[lineIndex].selectableText.size();
        };

        bool moved = false;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
        {
            if (active.byteOffset > 0)
            {
                --active.byteOffset;
            }
            else if (active.lineIndex > 0)
            {
                --active.lineIndex;
                active.byteOffset = rowTextSize(active.lineIndex);
            }
            moved = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
        {
            if (active.byteOffset < rowTextSize(active.lineIndex))
            {
                ++active.byteOffset;
            }
            else if (active.lineIndex < lastLine)
            {
                ++active.lineIndex;
                active.byteOffset = 0;
            }
            moved = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) && active.lineIndex > 0)
        {
            --active.lineIndex;
            active.byteOffset = std::min(active.byteOffset, rowTextSize(active.lineIndex));
            moved = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) && active.lineIndex < lastLine)
        {
            ++active.lineIndex;
            active.byteOffset = std::min(active.byteOffset, rowTextSize(active.lineIndex));
            moved = true;
        }

        if (!moved)
            return;

        // 键盘移动到的目标行如果不在当前可见范围内, 把滚动条拉过去让它可见——跟鼠标操作
        // 不一样, 键盘移动没有一个"鼠标当前指着哪"的天然可见性保证。
        float rowTop = active.lineIndex * lineHeight;
        float rowBottom = rowTop + lineHeight;
        float scrollY = ImGui::GetScrollY();
        float visibleHeight = ImGui::GetWindowHeight();
        if (rowTop < scrollY)
            ImGui::SetScrollY(rowTop);
        else if (rowBottom > scrollY + visibleHeight)
            ImGui::SetScrollY(rowBottom - visibleHeight);
    }

    LogSelectionPoint LogWindow::hitTest(ImVec2 mousePos, ImVec2 originScreenPos, float lineHeight) const
    {
        int lineIndex = static_cast<int>((mousePos.y - originScreenPos.y) / lineHeight);
        lineIndex = std::clamp(lineIndex, 0, static_cast<int>(mVisibleLines.size()) - 1);

        const LogDisplayLine& displayLine = mVisibleLines[lineIndex];
        const std::string& text = displayLine.selectableText;
        float targetX = mousePos.x - originScreenPos.x;

        size_t byteOffset = text.size();
        if (targetX <= 0.0f)
        {
            byteOffset = 0;
        }
        else
        {
            ImFontBaked* baked = ImGui::GetFontBaked();
            float accumulated = 0.0f;
            for (size_t i = 0; i < text.size(); ++i)
            {
                float advance = baked->GetCharAdvance(static_cast<ImWchar>(static_cast<unsigned char>(text[i])));
                if (accumulated + advance * 0.5f > targetX)
                {
                    byteOffset = i;
                    break;
                }
                accumulated += advance;
            }
        }
        return { lineIndex, byteOffset };
    }

    void LogWindow::drawSelectionHighlight(int row, std::string_view rowText, ImVec2 originScreenPos, float lineHeight) const
    {
        if (!mHasSelection)
            return;

        LogSelectionPoint lo = mSelectionAnchor;
        LogSelectionPoint hi = mSelectionActive;
        if (less(hi, lo))
            std::swap(lo, hi);
        if (row < lo.lineIndex || row > hi.lineIndex)
            return;

        bool isInteriorRow = row > lo.lineIndex && row < hi.lineIndex;
        size_t startByte = (row == lo.lineIndex) ? lo.byteOffset : 0;
        size_t endByte = (row == hi.lineIndex) ? hi.byteOffset : rowText.size();
        if (startByte >= endByte && !isInteriorRow)
            return;

        const char* base = rowText.data();
        float startX = measureTextWidth(base, base + startByte);
        float endX = isInteriorRow ? measureTextWidth(base, base + rowText.size()) : measureTextWidth(base, base + endByte);

        float rowTopY = originScreenPos.y + row * lineHeight;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(originScreenPos.x + startX, rowTopY),
            ImVec2(originScreenPos.x + endX, rowTopY + lineHeight),
            ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
    }

    void LogWindow::copySelection() const
    {
        if (!mHasSelection)
            return;

        LogSelectionPoint lo = mSelectionAnchor;
        LogSelectionPoint hi = mSelectionActive;
        if (less(hi, lo))
            std::swap(lo, hi);

        std::string result;
        for (int row = lo.lineIndex; row <= hi.lineIndex && row < static_cast<int>(mVisibleLines.size()); ++row)
        {
            const std::string& text = mVisibleLines[row].selectableText;
            size_t startByte = (row == lo.lineIndex) ? lo.byteOffset : 0;
            size_t endByte = (row == hi.lineIndex) ? hi.byteOffset : text.size();
            if (startByte < endByte)
                result.append(text.substr(startByte, endByte - startByte));
            if (row != hi.lineIndex)
                result += '\n';
        }
        ImGui::SetClipboardText(result.c_str());
    }

    void LogWindow::copyAllVisible() const
    {
        std::string result;
        for (const LogDisplayLine& line : mVisibleLines)
        {
            result += line.selectableText;
            result += '\n';
        }
        ImGui::SetClipboardText(result.c_str());
    }
}
