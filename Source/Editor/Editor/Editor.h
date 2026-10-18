#pragma once
#include <Engine/Engine.h>
#include "EditorMenuBar.h"
#include "EditorTabBar.h"
#include "EditorTabManager.h"
#include "GlobalShortcuts.h"
#include "EditorSelection.h"
#include "UndoStack.h"
#include "ThumbnailProvider.h"

#include <LanguageIntelligence/Providers/SlangProvider.h>

#include <memory>

struct ImDrawData;

namespace hyp
{
    class Asset;
}

namespace hyp::ed
{
    class ModuleServiceRegistration
    {
    public:
        ModuleServiceRegistration();
    };

    class Editor
    {
    public:
        Editor();

        ~Editor();

        void run();

        // play() 把 mCurrentScene 序列化成快照后整份销毁, 再从快照反序列化出一份新的接管
        // tick/渲染——同一时间只存在一份 Scene 实例。stop() 反过来: 销毁 Play 期间这份,
        // 用 play() 留下的快照反序列化出编辑场景。只能从 Stoped 调用。
        void play();

        // 只能在 Playing 或 Paused 状态下调用
        void pause();

        void resume();

        // 只能在 Playing 或 Paused 状态下调用
        void stop();

        // 只能在 Paused 时调用: 强制跑一次 onTick(), 跑完自动回到 Paused。
        void stepFrame();

        // mEngine 当前挂的场景的播放状态, 没有场景时视为 Stoped。EditorToolbar 用它决定
        // 播放控制按钮的图标/可用性。
        PlayState getPlayState() const;

        // 只能在 Stoped 时调用。把 mCurrentScene 这个活对象临时接到 mCurrentSceneAsset 上
        // 落盘, 存完立即用 forceUnload() 解绑——mCurrentSceneAsset 平时不持有 mCurrentScene
        // 的引用, 见下面 mCurrentSceneAsset 的注释。
        void saveCurrentScene();

    private:
        struct SceneSnapshot;

        static SceneSnapshot captureSceneSnapshot(Scene* scene);
        static TRef<Scene> restoreSceneFromSnapshot(const SceneSnapshot& snapshot);

        // 读 Config 的 DefaultScene 字段; 指向的资产存在且是 Scene 就直接用, 否则在项目目录下
        // 新建一个空场景资产、落盘, 并把路径写回 config.json。
        static Asset& resolveDefaultSceneAsset(Engine& engine);

    private:
        ModuleServiceRegistration mModuleServiceRegistration;
        std::unique_ptr<lang::DiagnosticsService> mSlangdKeepAlive;

        Engine mEngine;
        ThumbnailProvider mThumbnailProvider;
        EditorSelection mSelection;
        UndoStack mUndoStack;
        // Play 期间借放 Edit 模式的历史——play() 把 mUndoStack 换成一份全新的空栈, 原来的
        // 挪到这里存着; stop() 换回来, Play 期间那份连同它积累的记录一起被移动赋值覆盖掉、
        // 直接析构, 不需要手动清。Edit 模式下这个成员始终是空的。
        UndoStack mPlaySavedUndoStack;
        // 当前场景——Edit 和 Play 期间共用同一个槽位, 是编辑场景还是 Play 场景看
        // getPlayState() 而不是看这是不是另一个指针; 同一时间只会有一份 Scene 实例存在。
        TRef<Scene> mCurrentScene;

        // mCurrentScene 对应的磁盘资产(同 guid); 除 saveCurrentScene() 内部短暂绑定外始终
        // 是 Unloaded 状态、不持有 mCurrentScene 的引用, 以维持 play()/stop() 依赖的
        // "同一 guid 同一时刻只有一个活对象"这条前提。
        Asset& mCurrentSceneAsset;

        // play() 把 mCurrentScene 序列化进这里再销毁, stop() 用它反序列化出新的编辑场景。
        std::unique_ptr<SceneSnapshot> mEditSceneSnapshot;

        // "tab 从哪来、谁是激活的"这一整套状态都在这里面, 见 EditorTabManager。
        EditorTabManager mTabManager;
        EditorMenuBar mMenuBar;
        EditorTabBar mTabBar;
        GlobalShortcuts mGlobalShortcuts;

        // onPreTick 里 ImGui 画完这一帧存下来的快照, onPreRender 里把它录进 EditorPresent pass。
        // 拆成两个回调纯粹是为了减少输入延迟: 让视口拖拽触发的 EditorCamera::setViewMatrix()
        // 能排在 Scene::tick() 的 gather 之前生效, 而不是等到下一帧才体现——不是正确性要求,
        // CameraProxy::getSceneViewGroup() 什么时候被 setter 抢跑都不会出错。
        ImDrawData* mPendingDrawData = nullptr;
    };
}
