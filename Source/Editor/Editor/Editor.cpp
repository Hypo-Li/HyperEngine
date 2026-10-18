#include "Editor.h"
#include "SceneView.h"
#include "MainEditorTab.h"

#include <HypImGui/ImGuiFrontend.h>
#include <HypImGui/ImGuiBackend.h>
#include <HypImGui/ImGuiAssetCombo.h>
#include <Engine/Context.h>
#include <Engine/Component/Collider/BoxCollider.h>
#include <Engine/Component/Collider/CapsuleCollider.h>
#include <Engine/Component/Collider/ConvexHullCollider.h>
#include <Engine/Component/Collider/CylinderCollider.h>
#include <Engine/Component/Collider/MeshCollider.h>
#include <Engine/Component/Light/PointLight.h>
#include <Engine/Component/Collider/SphereCollider.h>
#include <Engine/Component/Light/SpotLight.h>
#include <Engine/Visualization/VisualizationEmitter.h>
#include <RenderBase/ProxyManager.h>
#include <RenderBase/RenderFrontend.h>
#include <RenderBase/TextureProxy.h>
#include <Renderer/CameraProxy.h>
#include <Renderer/RendererContext.h>
#include <Renderer/RenderPass/VisualizationPass.h>
#include <RenderBase/RenderTagRegistry.h>
#include "RenderPass/PickPass.h"

#include <Core/Math/Math.h>
#include <Core/Serialization/Serializer.h>
#include <Core/Serialization/SerializeContext.h>
#include <Core/Serialization/BinarySaver.h>
#include <Core/Serialization/BinaryLoader.h>
#include <Core/Asset/AssetManager.h>
#include <Core/Common/ModuleServiceRegistry.h>
#include <FontUtility/FontUtility.h>
#include <MeshUtility/MeshUtility.h>
#include <ShaderCompiler/SlangShaderCompiler.h>
#include <TextureUtility/TextureUtility.h>

#include <imgui.h>

#include <fstream>
#include <memory>

namespace hyp::ed
{
    ModuleServiceRegistration::ModuleServiceRegistration()
    {
        ModuleServiceRegistry::get().registerService<IShaderCompiler>(std::make_unique<SlangShaderCompiler>());
        ModuleServiceRegistry::get().registerService<ITextureUtility>(std::make_unique<TextureUtility>());
        ModuleServiceRegistry::get().registerService<IMeshUtility>(std::make_unique<MeshUtility>());
        ModuleServiceRegistry::get().registerService<IFontUtility>(std::make_unique<FontUtility>());
    }

    struct EditorPresentData
    {
        ImDrawData* drawData;
        std::vector<TextureProxyHandle> prepareTextures;
    };

    Editor::Editor() :
        mSlangdKeepAlive(std::make_unique<lang::DiagnosticsService>()),
        mEngine(Context::Environment::Editor),
        mCurrentSceneAsset(resolveDefaultSceneAsset(mEngine)),
        mTabManager(*this, mEngine, mSelection, mUndoStack, mThumbnailProvider),
        mMenuBar(mTabManager),
        mTabBar(mTabManager),
        mGlobalShortcuts(mTabManager)
    {
        mCurrentScene = mCurrentSceneAsset.getOrLoadRootObject<Scene>();
        mCurrentSceneAsset.forceUnload();
        mEngine.setScene(mCurrentScene.get());

        auto& window = mEngine.get<Window>();
        auto& renderSystem = mEngine.get<RenderSystem>();
        auto& inputSystem = mEngine.get<InputSystem>();
        auto& renderBackend = renderSystem.getBackend();

        // EditorPick render tag 只在这里注册一次——渲染线程此刻还没被唤醒过(RenderBackend
        // 构造函数里起的线程在 mWakeUpSignal 上等着), registerTag() 内部是裸 unordered_map,
        // 不是线程安全容器, 必须在渲染线程开始并发读这张表之前完成。Engine 侧(RenderBackend/
        // RenderTagRegistry 自己)完全不知道 "EditorPick" 这个 tag 存在, 见 Document/
        // GPU_Pick_System_Design.md。
        {
            std::filesystem::path editorPickShaderPath = mEngine.get<Config>().getShaderDir() / "Editor/EditorPick.slang";
            std::ifstream editorPickShaderFile(editorPickShaderPath, std::ios::binary);
            std::string editorPickShaderTemplate((std::istreambuf_iterator<char>(editorPickShaderFile)), std::istreambuf_iterator<char>());
            renderBackend.getRenderTagRegistry().registerTag(
                EditorRenderTags::Pick,
                editorPickShaderPath.string(),
                editorPickShaderTemplate,
                RenderTagFlags::Geometry_Only
            );
        }

        ImGuiFrontend::init(window, renderBackend, inputSystem, mEngine.get<Config>().getResourceDir());
        rhi::TextureFormat surfaceFormat = renderBackend.getDevice().getSwapchain()->getSurfaceTextureFormat();
        ImGuiBackend::initRenderer(renderBackend, surfaceFormat);

        // ImGui 的绘制(含视口拖拽这类输入检测)全程只是 CPU 计算, 不依赖这一帧的场景是否已经
        // 渲染, 所以放在 onPreTick(先于 Scene::tick() 的 gather)里做, 让 EditorCamera 的相机
        // setter 能在 gather 之前生效, 不再需要额外的"gather 时快照"机制。真正要求排在
        // Scene::tick() 之后的, 只有 EditorPresent 这个 pass 的加入时机(RenderPipeline.cpp
        // 里每个 view 的 PresentPass 永远是这个 view 自己那组 pass 里最后加进渲染图的一个)——
        // 这一步单独放进 onPreRender, 用上面存好的 drawData 快照。
        mEngine.setPreTickCallback(
            [this](float)
            {
                mSelection.syncScene(mEngine.getScene());
                ImGuiFrontend::beginFrame();

                mGlobalShortcuts.update();

                // 菜单栏和标签条画在同一行(共享同一对 BeginMainMenuBar/EndMainMenuBar)——
                // 标签标题是纯文字, 不像 Toolbar 那样需要图标字体撑开的行高, 见
                // MainEditorTab 的注释。
                if (ImGui::BeginMainMenuBar())
                {
                    mMenuBar.drawMenus();
                    // 横向布局(菜单栏内部的布局类型)下 Separator() 画的是一条竖线, 不是
                    // 通常那种横线——用它把菜单和标签条分隔开, 不需要另外手画。
                    ImGui::Separator();
                    mTabBar.draw();
                    ImGui::EndMainMenuBar();
                }
                mTabManager.draw(ImGui::GetFrameHeight());

                // 拖拽资产时的悬浮预览统一在这里画, 不依赖具体哪个拖拽源(AssetBrowser 的
                // 格子、AssetCombo 的图标)这一帧是否还被画到——那些源头都传了
                // ImGuiDragDropFlags_SourceNoPreviewTooltip 关掉了各自的内置预览, 原因是
                // 一旦源头所在的 tab 不是当前激活的, 它就不会被画, ImGui 会把内置预览掉
                // 退化成占位的 "..."(imgui.cpp 里 AcceptDragDropPayload 附近的注释建议的
                // 正是这个写法)。图标直接用 payload.icon(拖拽发起那一刻源头自己已经解析好、
                // 正在显示的那张)画, 不是这里另外按 rootClass 现查一次——以后不同资产类型
                // 有各自的图标时, 各个源头解析图标的地方升级了, 这里不需要跟着改, 见
                // AssetDragDropPayload 的注释。尺寸不借用 GetAssetComboRowHeight()(那个名字
                // 说的是 combo 那一整行该多高, AssetCombo/AssetComboEd 内部只是恰好拿它的值
                // 当图标尺寸用, 语义对不上), 这里独立算一份。
                //
                // 定位必须自己用 SetNextWindowPos() 顶掉 BeginTooltip() 的默认逻辑: ImGui
                // 只有在 g.DragDropWithinSource/WithinTarget 为真(也就是这段代码还嵌在
                // BeginDragDropSource()/BeginDragDropTarget() 内部)时才会用"贴着鼠标"这套
                // 定位公式(imgui.cpp:12761-12785 的 TOOLTIP_DEFAULT_OFFSET_MOUSE), 我们
                // 从一个独立位置调 BeginTooltip(), 这两个标记都是 false, ImGui 会当成普通
                // hover 提示走另一套跟鼠标实时位置没有强绑定的定位算法, 看起来就是"悬浮框
                // 离鼠标一直隔着一段距离"。这里手动复刻同一条公式, 不依赖那两个内部标记。
                if (const ImGuiPayload* payload = ImGui::GetDragDropPayload())
                {
                    if (payload->IsDataType(kAssetDragDropType))
                    {
                        const AssetDragDropPayload& dropped = *static_cast<const AssetDragDropPayload*>(payload->Data);
                        if (dropped.icon)
                        {
                            float iconSize = ImGui::GetFrameHeight() * 2.0f;
                            // ImVec2 没有全局启用运算符重载(imconfig.h 里 IMGUI_DEFINE_MATH_OPERATORS
                            // 被注释掉了), 手动按分量算, 不能直接写 mousePos + offset * scale。
                            float cursorScale = ImGui::GetStyle().MouseCursorScale;
                            ImVec2 mousePos = ImGui::GetIO().MousePos;
                            ImVec2 tooltipPos(mousePos.x + 16.0f * cursorScale, mousePos.y + 10.0f * cursorScale);
                            ImGui::SetNextWindowPos(tooltipPos);
                            ImGui::BeginTooltip();
                            ImGui::Image(dropped.icon, ImVec2(iconSize, iconSize));
                            ImGui::EndTooltip();
                        }
                    }
                }

                mPendingDrawData = ImGuiFrontend::endFrameAndSnapshot();
            }
        );

        mEngine.setPreRenderCallback(
            [this]()
            {
                // 必须在主线程采集成一份快照随命令传下去, 不能让渲染线程执行时现场去遍历
                // mTabManager: 渲染线程可以落后若干帧, 会读到主线程刚构造、创建 proxy
                // 的命令还排在队列更后面的新窗口, getProxy() 拿到空指针。
                std::vector<TextureProxyHandle> prepareTextures;
                mThumbnailProvider.collectPrepareTextures(prepareTextures);
                mTabManager.collectPrepareTextures(prepareTextures);

                // Visualization: PointLight 画线框球(半径=衰减半径), SpotLight 画线框锥体(锥轴跟
                // 聚光灯锥轴同一个 -Z 约定, 直接拿实体的世界变换用, 不需要额外重映射), 都需要
                // 深度测试(会被真实几何体遮挡, 不是常驻置顶的 Handle), 同样的道理必须在主线程
                // 遍历 mEngine.getScene()——碰撞体可视化的公共变换: 实体世界矩阵(带旋转+缩放)+
                // 把平移换成 Collider::getWorldCenter()(mCenter 按世界旋转偏移之后的世界坐标,
                // 跟建物理体时用的是同一个公式)——CompoundCollider/PlaneCollider 暂不实现
                auto colliderTransform = [](Collider& collider, Entity& entity) -> double4x4
                {
                    double4x4 matrix = entity.getTransform().getWorldMatrix();
                    matrix[3] = double4(collider.getWorldCenter(), 1.0);
                    return matrix;
                };

                VisualizationEmitter visualizationEmitter;
                for (const TRef<Entity>& entity : mEngine.getScene()->getEntities())
                {
                    if (PointLight* pointLight = entity->getComponent<PointLight>())
                    {
                        visualizationEmitter.drawWireframeSphere(entity->getTransform().getWorldMatrix(), pointLight->getAttenuationRadius());
                    }
                    if (SpotLight* spotLight = entity->getComponent<SpotLight>())
                    {
                        float halfAngleRadians = spotLight->getOuterConeAngle() * (Math::PI / 180.0f);
                        visualizationEmitter.drawWireframeLightCone(entity->getTransform().getWorldMatrix(), spotLight->getAttenuationRadius(), halfAngleRadians);
                    }
                    if (MeshCollider* meshCollider = entity->getComponent<MeshCollider>())
                    {
                        if (StaticMesh* mesh = meshCollider->getMesh())
                        {
                            GeometryProxyHandle geometryHandle = mesh->getOrCreateGeometryProxy(meshCollider->getLODLevel());
                            visualizationEmitter.drawWireframeGeometry(geometryHandle, entity->getTransform().getWorldMatrix());
                        }
                    }
                    if (BoxCollider* boxCollider = entity->getComponent<BoxCollider>())
                    {
                        visualizationEmitter.drawWireframeBox(colliderTransform(*boxCollider, *entity), boxCollider->getHalfExtent());
                    }
                    if (SphereCollider* sphereCollider = entity->getComponent<SphereCollider>())
                    {
                        visualizationEmitter.drawWireframeSphere(colliderTransform(*sphereCollider, *entity), sphereCollider->getRadius());
                    }
                    if (CapsuleCollider* capsuleCollider = entity->getComponent<CapsuleCollider>())
                    {
                        visualizationEmitter.drawWireframeCapsule(colliderTransform(*capsuleCollider, *entity), capsuleCollider->getRadius(), capsuleCollider->getHalfHeight());
                    }
                    if (CylinderCollider* cylinderCollider = entity->getComponent<CylinderCollider>())
                    {
                        visualizationEmitter.drawWireframeCylinder(colliderTransform(*cylinderCollider, *entity), cylinderCollider->getRadius(), cylinderCollider->getHalfHeight());
                    }
                    if (ConvexHullCollider* convexHullCollider = entity->getComponent<ConvexHullCollider>())
                    {
                        if (ConvexHull* hull = convexHullCollider->getHull())
                        {
                            visualizationEmitter.drawWireframeGeometry(hull->getOrCreateGeometryProxy(), colliderTransform(*convexHullCollider, *entity));
                        }
                    }
                }
                VisualizationItems visualizationItems = visualizationEmitter.takeItems();

                // Visualization 只画进编辑器自己的 SceneView 视口, 不影响游戏内相机——SceneView 是
                // 单例窗口, 没开着就直接跳过(不采集也不提交)。CameraProxy 是编辑器相机自己创建、
                // 独占持有的, 这个 handle 天然就唯一对应它, 不需要从一堆相机里挑。
                SceneView* sceneViewWindow = mTabManager.getMainTab().getSceneView();
                CameraProxyHandle visualizationCameraProxyHandle;
                SceneProxyHandle visualizationSceneProxyHandle;
                if (sceneViewWindow)
                {
                    EditorCamera& editorCamera = sceneViewWindow->getViewport().getCamera();
                    visualizationCameraProxyHandle = editorCamera.getProxyHandle();
                    visualizationSceneProxyHandle = editorCamera.getAttachedSceneProxyHandle();
                }

                ImDrawData* drawData = mPendingDrawData;
                Context::get().getRenderFrontend().enqueueLambda(
                    [drawData, prepareTextures = std::move(prepareTextures), visualizationCameraProxyHandle, visualizationSceneProxyHandle, visualizationItems = std::move(visualizationItems)](FrameContext& context) mutable
                    {
                        // Editor 这个 lambda 排在 Scene::tick() 那次 enqueueLambda(建好这一帧的
                        // 渲染图、CameraProxy::getSceneViewGroup() 也在其中被调用过一次)之后执行,
                        // 所以这里读到的 getLastSceneView() 就是这一帧刚缓存的新值, 不存在延迟。
                        CameraProxy* cameraProxy = context.getProxyManager().getProxy(visualizationCameraProxyHandle);
                        SceneProxy* sceneProxy = context.getProxyManager().getProxy(visualizationSceneProxyHandle);
                        if (cameraProxy && sceneProxy)
                        {
                            if (const hyp::SceneView* sceneView = cameraProxy->getLastSceneView())
                            {
                                VisualizationPass::addToRenderGraph(context.getRenderGraph(), Context::get().getRendererContext(), *sceneProxy, *sceneView, *cameraProxy, std::move(visualizationItems));
                            }
                        }

                        context.getRenderGraph().addPresentPass<EditorPresentData>(
                            "EditorPresent"_hs,
                            [drawData, &prepareTextures](rg::RenderGraph::Builder& builder, EditorPresentData& data)
                            {
                                data.drawData = drawData;
                                data.prepareTextures = std::move(prepareTextures);
                            },
                            [](rg::ExecuteContext& context, EditorPresentData& data)
                            {
                                // 这些纹理只给 ImGui 用, 从不绑到任何 Material 上, 没有别的地方
                                // 会替它们调 TextureProxy::prepare() 把 pending upload 传上 GPU——
                                // 这里手动补一次。必须在 beginPass() 之前做(barrier 不能出现在
                                // render pass 内部), 所以这个 present pass 传了 Skip_Begin_Pass,
                                // 自己手动控制 beginPass 的时机。
                                auto& cmdList = context.getCommandList();
                                auto& proxyManager = context.getRenderBackend().getProxyManager();
                                for (TextureProxyHandle handle : data.prepareTextures)
                                {
                                    if (TextureProxy* proxy = proxyManager.getProxy(handle))
                                        proxy->prepare(cmdList);
                                }

                                context.beginPass();
                                ImGuiBackend::renderDrawData(cmdList, data.drawData);
                            },
                            rg::PassFlags::Skip_Begin_Pass
                        );
                    }
                );
            }
        );
    }

    Editor::~Editor()
    {
        mTabManager.clear();
        mCurrentScene = nullptr;
        mThumbnailProvider.shutdown();
        mEngine.shutdown();
        ImGuiBackend::shutdown();
        ImGuiFrontend::shutdown();
    }

    void Editor::run()
    {
        mEngine.run();
    }

    struct Editor::SceneSnapshot
    {
        Guid rootGuid;
        std::vector<uint8_t> bodyData;
        std::vector<uint8_t> bulkSection;
        std::vector<AssetImportEntry> importEntries;
        std::vector<AssetExportEntry> exportEntries;
    };

    Editor::SceneSnapshot Editor::captureSceneSnapshot(Scene* scene)
    {
        SceneSnapshot snapshot;
        snapshot.rootGuid = scene->getGuid();

        SerializeContext saveContext(false, snapshot.rootGuid);
        BinarySaver saver;
        Serializer saveSerializer(saver, saveContext);
        Object* rootObject = scene;
        saveSerializer.serializeObject(rootObject);

        snapshot.bodyData = saver.pack();
        snapshot.bulkSection = saveContext.getBulkSection();
        snapshot.importEntries = saveContext.getImportEntries();
        snapshot.exportEntries = saveContext.getExportEntries();
        return snapshot;
    }

    TRef<Scene> Editor::restoreSceneFromSnapshot(const SceneSnapshot& snapshot)
    {
        SerializeContext loadContext(true, snapshot.rootGuid);
        loadContext.setImportEntries(snapshot.importEntries);
        loadContext.setExportEntries(snapshot.exportEntries);
        loadContext.setBulkSection(snapshot.bulkSection);

        // 场景引用的其它资产(Mesh/Material 等)在 import 表里只有 guid, 这里跟
        // Asset::load() 一样按 guid 查 AssetManager 解析成活对象——不同的是 Scene 不是
        // 真正的 Asset 文件, 不需要经过 AssetHeader/AssetImportEntry::assetPath 那一层。
        auto& assetManager = Context::get().getAssetManager();
        for (const AssetImportEntry& importEntry : snapshot.importEntries)
        {
            Asset* asset = assetManager.getAsset(importEntry.guid);
            loadContext.registerKnownObject(importEntry.guid, asset ? asset->getOrLoadRootObject().get() : nullptr);
        }

        BinaryLoader loader;
        loader.unpack(snapshot.bodyData);
        Serializer loadSerializer(loader, loadContext);
        Object* loadedRoot = nullptr;
        loadSerializer.serializeObject(loadedRoot);

        return static_cast<Scene*>(loadedRoot);
    }

    Asset& Editor::resolveDefaultSceneAsset(Engine& engine)
    {
        Config& config = engine.get<Config>();
        AssetManager& assetManager = engine.get<AssetManager>();

        const std::string& defaultScenePath = config.getDefaultScenePath();
        if (!defaultScenePath.empty())
        {
            Asset* asset = assetManager.getAsset(defaultScenePath);
            if (asset && asset->getRootClass() == Scene::staticClass())
                return *asset;
        }

        constexpr const char* kDefaultScenePath = "Project/DefaultScene";

        Asset* existingAsset = assetManager.getAsset(kDefaultScenePath);
        if (existingAsset && existingAsset->getRootClass() == Scene::staticClass())
        {
            config.setDefaultScenePath(existingAsset->getPath());
            return *existingAsset;
        }

        TRef<Scene> emptyScene = createObject<Scene>(nullptr);
        Asset* asset = assetManager.createAsset(kDefaultScenePath, emptyScene.get());
        assert(asset && "Project/DefaultScene is already taken by a different asset");
        asset->save();
        config.setDefaultScenePath(asset->getPath());
        return *asset;
    }

    void Editor::play()
    {
        assert(mCurrentScene->getPlayState() == PlayState::Stoped);

        mEditSceneSnapshot = std::make_unique<SceneSnapshot>(captureSceneSnapshot(mCurrentScene.get()));
        if (SceneView* sceneView = mTabManager.getMainTab().getSceneView())
            sceneView->getViewport().detachCamera();

        // 同一时间只能有一份 Scene 实例存在, 避免 Play/Edit 两棵树的同一批 guid 同时注册
        // 进 GuidRegistry——mEngine 自己也用 TRef 握着当前场景(见 Engine::setScene()),
        // 光把 mCurrentScene 置空引用还没清完, 必须连它一起置空才会真正触发销毁。
        mCurrentScene = nullptr;
        mEngine.setScene(nullptr);

        mCurrentScene = restoreSceneFromSnapshot(*mEditSceneSnapshot);
        mEngine.setScene(mCurrentScene.get());
        mCurrentScene->setPlayState(PlayState::Playing);
        mSelection.clear();

        // Edit 模式的历史挪到一边存着, 换一份全新的空栈给 Play 期间用——Play 期间允许照常
        // 编辑、照常 undo/redo, 但这些记录随 Play 场景一起是一次性的, 不需要跟 Edit 模式的
        // 历史混在一起。
        mPlaySavedUndoStack = std::move(mUndoStack);
        mUndoStack = UndoStack();
    }

    void Editor::pause()
    {
        assert(mCurrentScene->getPlayState() != PlayState::Stoped);
        mCurrentScene->setPlayState(PlayState::Paused);
    }

    void Editor::resume()
    {
        assert(mCurrentScene->getPlayState() != PlayState::Stoped);
        mCurrentScene->setPlayState(PlayState::Playing);
    }

    void Editor::stop()
    {
        assert(mCurrentScene->getPlayState() != PlayState::Stoped);
        assert(mEditSceneSnapshot);
        if (SceneView* sceneView = mTabManager.getMainTab().getSceneView())
            sceneView->getViewport().detachCamera();

        // 同一时间只能有一份 Scene 实例存在, 原因同 play()——先把 Play 场景连同 mEngine
        // 自己握的那份引用一起清空(Scene::~Scene() 销毁场上所有 Entity/Component/
        // SceneProxy), 保证 Play 场景那批 guid 真的从 GuidRegistry 里注销了, 再反序列化
        // 出新的编辑场景, 不会跟还没注销的旧 guid 冲突。
        mCurrentScene = nullptr;
        mEngine.setScene(nullptr);

        mCurrentScene = restoreSceneFromSnapshot(*mEditSceneSnapshot);
        mEditSceneSnapshot = nullptr;
        mEngine.setScene(mCurrentScene.get());
        mSelection.clear();

        // 换回 Play 前保存的历史——Play 期间那份连同它积累的记录一起被这次移动赋值覆盖掉,
        // 直接析构, 不需要手动清。
        mUndoStack = std::move(mPlaySavedUndoStack);
    }

    void Editor::stepFrame()
    {
        assert(mCurrentScene->getPlayState() == PlayState::Paused);
        mCurrentScene->requestStep();
    }

    PlayState Editor::getPlayState() const
    {
        Scene* scene = mEngine.getScene();
        return scene ? scene->getPlayState() : PlayState::Stoped;
    }

    void Editor::saveCurrentScene()
    {
        assert(getPlayState() == PlayState::Stoped);

        mCurrentSceneAsset.setRootObject(mCurrentScene.get());
        bool saved = mCurrentSceneAsset.save();
        assert(saved);
        mCurrentSceneAsset.forceUnload();
    }
}
