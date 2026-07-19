#pragma once
#include <imgui.h>
#include <imgui_internal.h> // ImPool<>, ImHashData

#include <cassert>

namespace hyp
{
    // 移植自 imgui_club 官方的 imgui_threaded_rendering.h(ImDrawDataSnapshot), 按本项目
    // 的命名风格重写, 核心 swap 算法与官方实现保持一致。
    //
    // 和早期 GSplat2 里那版 CloneOutput() 深拷贝的思路完全不同: 不是每帧新分配一份
    // ImDrawList 再拷贝内容, 而是维护一份跨帧常驻的副本(按源 ImDrawList 指针身份缓存),
    // 每帧把源 ImDrawList 的 CmdBuffer/IdxBuffer/VtxBuffer 和这份常驻副本互相 swap——
    // 稳态下(同一个 ImGui 窗口每帧复用同一个 ImDrawList 指针)只是交换几个指针/size/
    // capacity, 不产生任何堆分配或内存拷贝, 比逐帧深拷贝更省。
    //
    // 只应在逻辑线程使用, 紧跟在 ImGui::Render() 之后调用 snapUsingSwap()。
    class ImGuiDrawDataSnapshot
    {
    public:
        ~ImGuiDrawDataSnapshot()
        {
            clear();
        }

        // 释放所有常驻副本。逻辑线程: 进程退出、ImGui::DestroyContext() 之前必须对
        // 每一个用到过的 snapshot 调用一次——否则 ImGui 自 1.92.0 起会在 DestroyContext()
        // 里断言"还有没释放的 ImDrawList"(官方 imgui_threaded_rendering.h 用例注释里
        // 明确提到的坑)。
        void clear() noexcept
        {
            for (int i = 0; i < mCache.GetMapSize(); ++i)
            {
                if (Entry* entry = mCache.TryGetMapData(i))
                    IM_DELETE(entry->ourCopy);
            }
            mCache.Clear();
            mDrawData.Clear();
        }

        // 逻辑线程, 必须紧跟在 ImGui::Render() 之后调用: src 必须是 Valid 的 ImDrawData
        // (调用方应先自己判断 src->Valid, 这里不做容错, 和官方实现一致)。调用之后 src
        // 内部的 buffer 已经被换成了这份 snapshot 上一次持有的旧内容, 调用方不应该再读它。
        void snapUsingSwap(ImDrawData* src, double currentTime) noexcept
        {
            assert(src != &mDrawData && src->Valid);

            // 除 CmdLists 外的字段(DisplayPos/DisplaySize/Textures/CmdListsCount/
            // TotalVtxCount/TotalIdxCount 等)整体拷贝; CmdLists 这个 ImVector 单独
            // 备份/恢复, 避免被下面这次结构体赋值顺带做一次没必要的深拷贝——下面会用
            // push_back 重新填充它, 元素个数天然和这里拷贝过来的 CmdListsCount 对得上。
            ImVector<ImDrawList*> backupCmdLists;
            backupCmdLists.swap(src->CmdLists);
            mDrawData = *src;
            backupCmdLists.swap(src->CmdLists);

            for (ImDrawList* srcList : src->CmdLists)
            {
                ImGuiID key = hashDrawListPtr(srcList);
                Entry* entry = mCache.GetOrAddByKey(key);
                if (!entry->ourCopy)
                {
                    entry->srcCopy = srcList;
                    entry->ourCopy = IM_NEW(ImDrawList)(srcList->_Data);
                }

                // 真正省下拷贝的地方: 只交换三个 ImVector 的内部指针/size/capacity,
                // 不搬动实际的顶点/索引/命令数据。
                entry->srcCopy->CmdBuffer.swap(entry->ourCopy->CmdBuffer);
                entry->srcCopy->IdxBuffer.swap(entry->ourCopy->IdxBuffer);
                entry->srcCopy->VtxBuffer.swap(entry->ourCopy->VtxBuffer);
                // 保留住上一次的 capacity, 避免下一帧因为容量不够又触发 realloc。
                entry->srcCopy->CmdBuffer.reserve(entry->ourCopy->CmdBuffer.Capacity);
                entry->srcCopy->IdxBuffer.reserve(entry->ourCopy->IdxBuffer.Capacity);
                entry->srcCopy->VtxBuffer.reserve(entry->ourCopy->VtxBuffer.Capacity);

                entry->lastUsedTime = currentTime;
                mDrawData.CmdLists.push_back(entry->ourCopy);
            }

            // 回收长期没被用到的常驻副本(比如某个窗口被关掉了, 它的 ImDrawList 指针
            // 身份不会再出现)。
            const double gcThreshold = currentTime - kGcSeconds;
            for (int i = 0; i < mCache.GetMapSize(); ++i)
            {
                Entry* entry = mCache.TryGetMapData(i);
                if (!entry || entry->lastUsedTime > gcThreshold)
                    continue;
                ImGuiID key = hashDrawListPtr(entry->srcCopy);
                IM_DELETE(entry->ourCopy);
                mCache.Remove(key, entry);
            }
        }

        ImDrawData& getDrawData() noexcept
        {
            return mDrawData;
        }

    private:
        struct Entry
        {
            ImDrawList* srcCopy = nullptr;
            ImDrawList* ourCopy = nullptr;
            double lastUsedTime = 0.0;
        };

        static ImGuiID hashDrawListPtr(ImDrawList* ptr) noexcept
        {
            return ImHashData(&ptr, sizeof(ptr));
        }

    private:
        ImDrawData mDrawData;
        ImPool<Entry> mCache;
        static constexpr float kGcSeconds = 20.0f; // 超过这么久没被用到的副本会被回收
    };
}
