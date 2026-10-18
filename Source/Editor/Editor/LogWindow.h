#pragma once
#include "EditorWindow.h"

#include <Core/Common/LogSystem.h>

#include <imgui.h>

#include <array>
#include <deque>
#include <string_view>
#include <vector>

namespace hyp::ed
{
    // ImGuiListClipper 要求列表里每一项占同一个行高——含换行符的日志消息(典型来源:
    // SlangShaderCompiler.cpp 里整段编译器诊断信息直接 LOG_ERROR 进来)如果整条当一个 clipper
    // 条目、用 TextUnformatted 画出来会变成好几个视觉行, clipper 按"单行高度 * 条目数"算出来
    // 的内容高度就比实际画出来的矮, 滚动条到不了真正的底部。DisplayLine 把每一条日志按 '\n'
    // 拆成若干"视觉行", 每个 DisplayLine 严格对应恰好一行文本——这样 clipper 处理的条目数
    // 就是真实的视觉行数, 假设永远成立。
    // selectableText 是这一行实际参与测量/命中测试/绘制/复制的完整文本(见 LogWindow.cpp
    // 里的 rowSelectableText())——mVisibleLines 重建时算一次存下来, 不是每帧现拼: 时间戳/
    // Level/location/消息对一条已经落盘的日志来说是常量, 没必要每帧重新格式化一次时间戳、
    // 重新分配一次字符串。
    struct LogDisplayLine
    {
        int entryIndex;
        bool isFirstLine;
        std::string_view text;
        std::string selectableText;
    };

    // 选区的一端: 落在第几条视觉行(mVisibleLines 下标)、该行 LogDisplayLine::selectableText
    // 里第几个字节。
    struct LogSelectionPoint
    {
        int lineIndex = 0;
        size_t byteOffset = 0;
    };

    // 类似 Chrome DevTools Console 的日志面板: 按 level 过滤(带每个 level 当前条数的
    // 复选框)、文本搜索、自动滚动到底部(用户往上翻看历史时暂停自动滚动, 不会看到一半被
    // 拽到最新一条)、按 level 上色、鼠标拖拽选中(含时间戳/Level)+ 复制。数据完全来自
    // LogSystem 自己维护的环形缓冲区, 这个窗口只管画, 不重复存一份。
    class LogWindow : public EditorWindow
    {
    public:
        LogWindow();

    protected:
        void onDraw() override;

    private:
        // 返回 true 表示这一帧过滤条件(level 勾选/搜索文本/点了 Clear)发生了变化——
        // drawEntries() 靠这个决定要不要重新扫一遍 entries 重建 mVisibleLines, 不是
        // 每帧都无条件重扫。
        //
        // Clear 按钮不能在这里直接调 LogSystem::clearHistory()——drawToolbar() 是在
        // LogSystem::withHistory() 的回调里被调用的, 那时候 mHistoryMutex 已经被
        // withHistory() 自己锁住了, clearHistory() 内部还要再锁一次同一把 mutex, 同一线程
        // 重入非递归锁是未定义行为(实测直接卡死/崩在锁构造那一步)。改成只在这里记一下
        // "这一帧点了 Clear"(clearRequested 出参), 真正调用挪到 onDraw() 里 withHistory()
        // 返回、锁已经释放之后, 跟 Inspector.cpp 的 componentToRemove/AssetBrowser.cpp 的
        // mPendingDelete 是同一个"画完再做"的道理。
        bool drawToolbar(const std::array<uint32_t, kLogLevelCount>& levelCounts, bool& clearRequested);
        void drawEntries(const std::deque<LogEntry>& entries, uint64_t version, bool filterDirty);

        // 鼠标左键在日志区域按下/拖拽时更新 mSelectionAnchor/mSelectionActive, 同时用
        // SetWindowFocus() 把焦点抢到这个 child 上; Ctrl+C/Shift+方向键都改成认"这个 child
        // 是否是当前 focus 窗口"(IsWindowFocused()), 不是"鼠标还停在上面"(hovered)——点一下
        // 选中之后鼠标移开也不影响继续用键盘操作选区, 直到焦点被其它面板抢走。originScreenPos
        // 是本地坐标 (0,0) 对应的屏幕位置(不是 BeginChild 后立刻 GetCursorScreenPos() 拿到的
        // 初始光标位置——那个已经加上了 WindowPadding, 跟 SameLine()/SetCursorPosX() 用的
        // "本地坐标"不是一回事, 见 drawEntries() 里的计算)。
        void handleSelectionInput(ImVec2 originScreenPos, float lineHeight);
        LogSelectionPoint hitTest(ImVec2 mousePos, ImVec2 originScreenPos, float lineHeight) const;

        // 拖拽选中时鼠标一旦顶到 child 的可视边缘(甚至物理屏幕边缘, 光标没法再往外移),
        // 单靠 hitTest() 永远够不到视口以外的文字——每帧按鼠标越过边缘的距离把对应方向
        // 滚动条推一点, 只要鼠标一直停在边缘不放开, 内容会持续滚过来, 直到选区覆盖到它。
        void autoScrollForDrag(ImVec2 mousePos) const;

        // Shift+方向键微调 mSelectionActive(锚点 mSelectionAnchor 不动), 按字节移动
        // 左右、按行移动上下(byteOffset 按新行长度 clamp, 不做"记住列位置"那种垂直对齐)。
        // 移动后如果目标行滚出了可见范围, 顺带把滚动条拉过去让它可见。
        void handleKeyboardSelection(float lineHeight);
        void drawSelectionHighlight(int row, std::string_view rowText, ImVec2 originScreenPos, float lineHeight) const;
        void copySelection() const;
        void copyAllVisible() const;

    private:
        ImGuiTextFilter mFilter;
        std::array<bool, kLogLevelCount> mLevelVisible{ true, true, true, true };
        bool mAutoScroll = true;

        // 按 level/搜索过滤出的可见"视觉行"(见 LogDisplayLine 的注释), 存成成员而不是
        // drawEntries() 里的局部变量——跨帧复用同一块内存(clear() 不释放容量), 避免每帧都
        // 重新申请; 只有 mLastVersion 落后于 LogSystem 当前 version(有新日志/被 clear 过),
        // 或者这一帧过滤条件本身变了, 才会真的重新扫一遍重建, 见 drawEntries() 的实现。
        // text 是指向 entries 里对应 LogEntry::message 的 string_view——deque 的 push/pop
        // 不会让其它未被弹出元素的引用失效, 只要 mVisibleLines 在下一次弹出发生前(必然伴随
        // version 变化)就已经整体重建过, 这些 view 就不会悬空。
        std::vector<LogDisplayLine> mVisibleLines;
        uint64_t mLastVersion = 0;
        bool mHasComputedOnce = false;

        // mVisibleLines 一旦重建, 行号跟旧选区就对不上了——直接清空选区, 不追踪"尽量保留"
        // 的映射(过滤条件一变选区跟着消失是合理预期)。
        bool mHasSelection = false;
        LogSelectionPoint mSelectionAnchor;
        LogSelectionPoint mSelectionActive;
    };
}
