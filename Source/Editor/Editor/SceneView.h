#pragma once
#include "EditorWindow.h"
#include "EditorSelection.h"
#include "Viewport/SceneViewport.h"

#include <Engine/Engine.h>

namespace hyp::ed
{
    class UndoStack;

    // 展示场景内容的编辑器窗口。编辑模式下视图由编辑器自己的相机提供(不受任何场景内
    // Camera 组件影响); 运行模式下应改由场景自身默认相机的画面接管(这一步依赖渲染器
    // 补完"多个空 RenderTarget 相机按 priority 合成"这条尚未实现的路径, 目前只处理了
    // 编辑模式这一半, 运行模式切换先留空)。
    class SceneView : public EditorWindow
    {
    public:
        explicit SceneView(Engine& engine, EditorSelection& selection, UndoStack& undoStack);

        ~SceneView();

        void onDraw() override;

        // SceneView 自己没有独立的编辑历史——它是同一个场景的另一个入口, 跟 Inspector
        // 编辑的是同一份 Entity/Component 数据, 焦点停在这里时 Ctrl+Z/Ctrl+Y 应该操作
        // 跟 Inspector 同一份共享 UndoStack, 不能因为用户鼠标恰好停在 SceneView 上(而不是
        // 专门点回 Inspector), 全局快捷键路由就查不到任何栈、什么都不做。
        UndoStack* getUndoStack() noexcept override
        {
            return &mUndoStack;
        }

        SceneViewport& getViewport() noexcept
        {
            return mViewport;
        }

    private:
        Engine& mEngine;
        EditorSelection& mSelection;
        UndoStack& mUndoStack;
        // 目前只放一个视口; 以后要支持分屏, 往这里再 push 几个 SceneViewport 即可,
        // SceneView 本身仍然是单例窗口。
        SceneViewport mViewport;
    };
}
