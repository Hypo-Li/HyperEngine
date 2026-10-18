#pragma once

#include "InspectorPanel.h"
#include "../UndoStack.h"

#include <Engine/Framework/Transform.h>

namespace hyp::ed
{
    class Inspector;

    enum class TransformSpace
    {
        Local,
        World,
    };

    class TransformPanel final : public IInspectorPanel
    {
    public:
        TransformPanel(Transform& transform, TransformSpace& space, bool& scaleLocked, UndoStack& undoStack, Inspector& inspector) noexcept;

        Transform& getTransform() const noexcept
        {
            return mTransform;
        }

        // undo/redo 命令用: 把历史欧拉角原样写回缓存, 不经过四元数, 不会因为往返换算发生
        // 数值归一化跳变。sourceWasWorldSpace 跟当前 mSpace 对不上(两次操作之间手动切换过
        // Local/World)时才退化成从 Transform 反解。
        void syncRotationCache(bool sourceWasWorldSpace, const float3& euler) noexcept;

        // undo/redo 命令用: 改动的是这个 Transform 的祖先, World 旋转被连带影响, 没有历史
        // 原始值可用, 只能从 Transform 现在的值反解。
        void refreshRotationCache() noexcept;

    protected:
        void onDraw() override;

    private:
        Transform& mTransform;
        TransformSpace& mSpace;
        bool& mScaleLocked;
        UndoStack& mUndoStack;
        Inspector& mInspector;

        // 欧拉角只在这里缓存, 不跟着 Transform 内部的四元数每帧来回换算——四元数转欧拉角
        // 会把角度折回规范区间, 拖拽中途每帧这样换一次会卡在区间边界拖不动。只有 mSpace
        // 变化时才用 Transform 的当前值重新推导, 其余时候(包括拖拽结束后)保留原样;
        // undo/redo 走 syncRotationCache() 直接写回, 不受这条限制。
        TransformSpace mRotationCacheSpace;
        float3 mRotationCache;
    };
}
