#pragma once
#include <limits>
#include <memory>
#include <span>
#include <unordered_map>

#include "Entity.h"

#include <RenderBase/ProxyHandle.h>
#include <Renderer/RendererDefines.h>

namespace hyp
{
    enum class PlayState
    {
        Stoped,
        Playing,
        Paused,
    };

    // Scene::mLumenEnabled 变化的订阅方——只有需要按 Lumen 开关懒加载/挂卸形状代理的组件
    // (比如 StaticMeshRenderer)才需要实现, 不属于 Component 通用生命周期的一部分。
    struct ILumenStateListener
    {
        virtual void onLumenEnabledChanged(bool enabled) = 0;
    };

    class Collider;
    class PhysicsScene;
    struct PhysicsOverlapEvent;

    struct SceneQueryParams
    {
        uint32_t layerMask = ~0u;
        bool includeTriggers = false;

        // 被忽略 Entity 身上的所有 Collider 都会被忽略。
        std::vector<Entity*> ignoredEntities;
        std::vector<Collider*> ignoredColliders;

        void addIgnoredEntity(Entity* entity)
        {
            ignoredEntities.push_back(entity);
        }

        void addIgnoredEntities(std::span<Entity* const> entities)
        {
            ignoredEntities.insert(ignoredEntities.end(), entities.begin(), entities.end());
        }

        void addIgnoredCollider(Collider* collider)
        {
            ignoredColliders.push_back(collider);
        }

        void addIgnoredColliders(std::span<Collider* const> colliders)
        {
            ignoredColliders.insert(ignoredColliders.end(), colliders.begin(), colliders.end());
        }
    };

    struct SceneRaycastHit
    {
        Collider* collider = nullptr;
        double3 position = double3(0.0);
        float3 normal = float3(0.0f);
        float distance = 0.0f;
    };

    class Scene : public Object
    {
        REFLECT_CLASS(Scene)
    public:
        // 只用于组织场景里的对象, 没有任何运行时语义; 设计见 Document/Scene_Folder_Design.md
        struct Folder
        {
            HString name;
            std::vector<HString> childFolders;
            std::vector<Entity*> entities;
        };

        Scene();

        virtual ~Scene() noexcept;

        void finalizeLoad() override;

        Entity* createEntity(HString name);

        // 把一个不是通过 createEntity() 创建的 Entity(比如 Copier 复制出来的副本)正式接入这个
        // Scene: 保证名字唯一、加入 mEntities、并把它身上已经存在的每个 Component 送进注册
        // 流程(跟 Entity::addComponent() 对新建组件做的事一样, 只是这里组件本身已经是复制好的
        // 现成数据, 只补"注册"这一步, 实际的 onRegister() 仍然照常延迟到下一次 tick() 里的
        // processPending() 触发)。调用方负责保证传入的 entity 在构造时(Copier::copy()/
        // copyObject() 的 newOwner 参数)已经以这个 Scene 作为 owner。
        void registerEntity(Entity* entity);

        std::span<const TRef<Entity>> getEntities() const noexcept
        {
            return mEntities;
        }

        Entity* findEntity(Guid guid) const noexcept;

        static constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();

        // 只处理根 Entity: 有父节点的传进来是调用错误(get 返回 kInvalidIndex, move 返回 false),
        // 子节点的顺序归 Transform。下标是在同级根里的位置: 在某个 Folder 里就是它在 Folder
        // 成员列表里的位置, 否则是它在未归档根序列里的位置; index 取移动之后的最终位置
        size_t getRootEntityIndex(const Entity* entity) const noexcept;

        bool moveRootEntity(Entity* entity, size_t index);

        std::span<const HString> getRootFolders() const noexcept
        {
            return mRootFolders;
        }

        const Folder* findFolder(HString name) const noexcept;

        // 空 = 未归档
        HString getFolderOfEntity(const Entity* entity) const noexcept;

        // 空 = 根 Folder 或者不存在
        HString getParentFolder(HString folderName) const noexcept;

        // 在父 Folder(或根)的子列表里的位置, 不存在返回 kInvalidIndex
        size_t getFolderIndex(HString name) const noexcept;

        // 返回去重之后的最终名字, 名字为空或者父 Folder 不存在返回空。新 Folder 追加到末尾
        HString createFolder(HString name, HString parentName = {});

        // 返回去重之后的最终名字, 找不到或者新名字为空返回空
        HString renameFolder(HString oldName, HString newName);

        // parentName 为空表示移到根。目标是自己或自己的后代、任一方不存在时返回 false
        bool moveFolderToFolder(HString name, HString parentName);

        // 调整在父 Folder(或根)的子列表里的位置, index 取移动之后的最终位置
        bool moveFolder(HString name, size_t index);

        // 子 Folder 和成员并入父级(没有父级则子 Folder 成为根、成员变成未归档)
        bool removeFolder(HString name);

        // 单归属, 自动从旧 Folder 摘掉, 追加到新 Folder 末尾; folderName 为空等价于
        // removeEntityFromFolder。有父节点的 Entity 不能归档
        bool moveEntityToFolder(Entity* entity, HString folderName);

        // 返回之前是否在某个 Folder 里。变成未归档之后追加到未归档根序列末尾
        bool removeEntityFromFolder(Entity* entity);

        // Entity 的父节点变了时由 Entity 调用
        void onEntityReparented(Entity* entity);

        // 只返回已经 onRegister() 过、还没 onUnregister() 的组件。
        Component* findComponent(Guid guid) const noexcept;

        void setEntityName(Entity* entity, HString name);

        // 立刻从 getEntities()/findEntity()/名字索引里移除, 只有组件反注册延迟到下一次
        // processPending(), 所以不能在遍历 getEntities() 的过程中调用
        bool destroyEntity(Entity* entity, bool destroyChildren = false);

        void registerComponent(Component* component);

        void unregisterComponent(Component* component);

        // 只应在 component 当前确实待在 oldGroup 对应的数组里时调用(Registered 或
        // Pending_Unregister 状态)——Component::setTickGroup() 自己负责判断这个前提成不成立。
        void moveComponentTickGroup(Component* component, TickGroup oldGroup, TickGroup newGroup);

        void tick(float deltaTime);

        void registerTransformListener(Transform* transform, ITransformListener* listener);

        void unregisterTransformListener(Transform* transform, ITransformListener* listener);

        // 注册后除了立即生效的这次 setLumenEnabled() 调用, 不会补发"当前已经是什么状态"
        // 这一份历史通知——调用方自己在 onRegister() 里用 isLumenEnabled() 读一次当前值。
        void registerLumenStateListener(ILumenStateListener* listener);

        void unregisterLumenStateListener(ILumenStateListener* listener);

        SceneProxyHandle getOrCreateSceneProxy();

        // 转发给渲染线程的 SceneProxy::setLumenEnabled()——显式开关, 不是靠"有没有 mesh
        // 烘焙过 Card 数据"隐式推出来的; 真正的创建(两张 2048x2048 atlas 纹理)延迟到渲染
        // 线程下一次 SceneProxy::update() 才发生, 不是调用这个函数的当下
        void setLumenEnabled(bool enabled);

        // 游戏线程侧的同一份开关状态(渲染线程那份在 SceneProxy 里, 两边不共享存储)——
        // 给游戏线程决定"要不要现在就烘焙某个 mesh 的 MDF"用(StaticMeshRenderer 懒加载
        // 的判据), 渲染线程从不查这个
        bool isLumenEnabled() const noexcept
        {
            return mLumenEnabled;
        }

        // 游戏线程这份拷贝只给编辑器 UI 回读, 真正生效的是渲染线程 SceneProxy 上的那份
        void setSceneVisualizeMode(SceneVisualizeMode mode);

        SceneVisualizeMode getSceneVisualizeMode() const noexcept
        {
            return mSceneVisualizeMode;
        }

        // 跟 SceneVisualizeMode 正交的独立开关: 命中着色用 GDF 粗测距还是候选物体的 MDF
        // 精确 sphere tracing, 见 hyp::SceneVisualizeMode 的注释。游戏线程这份拷贝同样只给
        // 编辑器 UI 回读, 真正生效的是渲染线程 SceneProxy 上的那份
        void setUseMeshDistanceField(bool useMeshDistanceField);

        bool getUseMeshDistanceField() const noexcept
        {
            return mUseMeshDistanceField;
        }

        // 屏幕探针追踪的调试开关, 打开后跳过屏幕 HZB 追踪
        void setDisableScreenTrace(bool disableScreenTrace);

        bool getDisableScreenTrace() const noexcept
        {
            return mDisableScreenTrace;
        }

        void setPlayState(PlayState state);

        PlayState getPlayState() const noexcept
        {
            return mPlayState;
        }

        bool isPlaying() const noexcept
        {
            return mPlayState == PlayState::Playing;
        }

        // 只应在 Paused 时调用: 让紧接着的下一次 tick() 强制跑一次 onTick(), 用那一帧真实的
        // deltaTime, 跑完自动清掉标记, 之后继续冻结——编辑器"单步"按钮用。
        void requestStep();

        // 只在 Playing/Paused 期间存在(setPlayState() 里懒创建/销毁)——编辑模式不需要模拟
        // 物理, 没有 PhysicsScene 时返回空。
        PhysicsScene* getPhysicsScene() const noexcept
        {
            return mPhysicsScene.get();
        }

        void endColliderOverlaps(Collider* collider);

        // 线段 [start, end] 上最近的命中。
        bool raycast(double3 start, double3 end, SceneRaycastHit& outHit, const SceneQueryParams& params = {}) const;

        std::vector<Collider*> overlapSphere(double3 center, float radius, const SceneQueryParams& params = {}) const;

    private:
        void dispatchOverlapEvents(std::span<const PhysicsOverlapEvent> events);

        HString makeUniqueName(HString name);

        HString makeUniqueFolderName(HString name) const;

        Folder* findFolderMutable(HString name) noexcept;

        // 找到存放 Folder 名字的那个列表: parentName 为空是 mRootFolders, 否则是父 Folder 的
        // childFolders。父 Folder 必须存在
        std::vector<HString>& folderListOf(HString parentName);

        // folder 是 ancestor 自己或者它的后代
        bool isFolderInSubtree(HString ancestor, HString folder) const noexcept;

        // 从所属 Folder 摘掉并擦索引, 不动 mEntities 里的位置
        void unfileEntity(const Entity* entity);

        // finalizeLoad() 里清洗存盘数据并重建 mFolderOfEntity, 不假设文件一定是干净的
        void sanitizeFolders();

        void processPending();

        void tickComponentGroup(TickGroup group, float deltaTime);

        void createSceneProxy(SceneProxyHandle handle);

        void destroySceneProxy(SceneProxyHandle handle);

    private:
        std::vector<TRef<Entity>> mEntities;
        // findEntity() 按 guid 查的热路径——mEntities 本身还是权威的所有权列表(Entity 的
        // 生命周期靠它的 TRef 管), 这份只是配合它维护的一份查找加速索引, 不单独持有引用。
        std::unordered_map<Guid, Entity*> mEntityByGuid;
        std::unordered_set<HString> mExistingNames;

        std::vector<Folder> mFolders;
        std::vector<HString> mRootFolders;
        // 不反射, finalizeLoad() 里重建; 键是已归档的根 Entity, 值是所属 Folder 的名字
        std::unordered_map<const Entity*, HString> mFolderOfEntity;

        // 只在组件 onRegister()~onUnregister() 之间存在, 在 processPending() 里跟这两个
        // 调用一起维护, 不是组件一加进 Entity 就有。
        std::unordered_map<Guid, Component*> mComponentByGuid;

        using Components = std::vector<TRef<Component>>;
        static constexpr size_t kTickGroupCount = static_cast<size_t>(TickGroup::Count);
        Components mComponentTickGroups[kTickGroupCount];

        std::vector<Component*> mPendingRegisterComponents;
        std::vector<Component*> mPendingUnregisterComponents;
        std::vector<TRef<Entity>> mPendingDestroyEntities;

        struct TransformDependency
        {
            Transform* transform;
            uint32_t cachedVersion;
            ITransformListener* listener;

            inline void notifyIfUpdated()
            {
                if (transform->getVersion() != cachedVersion)
                {
                    listener->onTransformUpdate(*transform);
                    cachedVersion = transform->getVersion();
                }
            }
        };
        std::vector<TransformDependency> mTransformDependencies;

        std::vector<ILumenStateListener*> mLumenStateListeners;

        SceneProxyHandle mProxyHandle;

        PlayState mPlayState = PlayState::Stoped;
        bool mStepRequested = false;
        bool mLumenEnabled = false;
        SceneVisualizeMode mSceneVisualizeMode = SceneVisualizeMode::None;
        bool mUseMeshDistanceField = false;
        bool mDisableScreenTrace = false;
        std::shared_ptr<PhysicsScene> mPhysicsScene;
    };

    template <> Type* Reflection::createType<Scene::Folder>();
    template <> Type* Reflection::createType<Scene>();
}
