#pragma once
#include "Entity.h"

#include <span>
#include <vector>

namespace hyp
{
    class PrefabInstance;
    class Scene;

    class Prefab : public Object
    {
        friend class PrefabInstance;
        REFLECT_CLASS(Prefab)
    public:
        Prefab() = default;

        // 反方向: 把场景里一个 Entity 连同它整棵子树(递归 Transform 子节点)拷贝进一份新建
        // 的 Prefab, 跟 instantiate() 是镜像操作。rootEntity 本身不会被改动, 也不会被注销出
        // 原来的场景。
        static TRef<Prefab> createFromEntity(Entity* rootEntity);

        // 返回的根 Entity 已经注册进 scene(含它整棵子树), 调用方不需要再手动注册。
        Entity* instantiate(Scene* scene);

        std::span<const TRef<Entity>> getEntities() const noexcept
        {
            return mEntities;
        }

        Entity* getRootEntity() const noexcept
        {
            return mRootEntity;
        }

        // 按模板节点自己的 Guid 找(Entity 或者它身上的某个 Component)——applyToPrefab()
        // 往模板写回覆盖值时用, 跟 PrefabInstance::mNodeMap 是相反的查找方向。
        Object* findNode(Guid nodeId) const;

        // 当前进程里, 有哪些活的 PrefabInstance 是从这个 Prefab 实例化出来的——修改并保存这个
        // Prefab 时用来找到需要立即同步的活实例; 未打开的场景文件不在这张表里, 它们下次加载时
        // 靠 Entity::finalizeLoad() 里的同步逻辑自动补齐, 不需要跟踪"哪些场景文件引用了这个
        // Prefab"这种全项目依赖索引。
        std::span<PrefabInstance* const> getLiveInstances() const noexcept
        {
            return mLiveInstances;
        }

        // PrefabInstance::mSyncedVersion 靠它判断"要不要重新同步"。
        uint32_t getVersion() const noexcept
        {
            return mVersion;
        }

        // 模板内容真正变化后调用(目前只有 PrefabInstance::applyToPrefab() 写回 override 这
        // 一条路径)——版本号先加一, 再把当前模板状态同步给所有活实例, 只对没在 Play 的场景
        // 生效, 正在跑的场景跳过, 不打扰运行中的 gameplay 逻辑(那些实例还是会在下次真正变活
        // 时, 比如场景重新加载, 通过 mSyncedVersion 的比对自然赶上)。版本号更新和广播必须
        // 绑在一起做, 不能拆成两步——分开的话容易漏调广播那一半, 版本号已经变了但活实例没收到通知。
        void apply();

    private:
        void addLiveInstance(PrefabInstance* instance);

        void removeLiveInstance(PrefabInstance* instance);

    private:
        std::vector<TRef<Entity>> mEntities;
        Entity* mRootEntity = nullptr;

        // 不参与反射, 纯运行时状态。
        std::vector<PrefabInstance*> mLiveInstances;

        // 不参与反射——只在这次进程运行期间跟 PrefabInstance::mSyncedVersion 比对用, 不需要
        // 跨会话持久化。
        uint32_t mVersion = 0;
    };

    template <> Type* Reflection::createType<Prefab>();
}
