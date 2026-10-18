#pragma once
#include "../Framework/Component.h"
#include "../Framework/Entity.h"
#include "../Framework/Prefab.h"

#include <optional>
#include <unordered_map>

namespace hyp
{
    class PrefabInstance : public Component
    {
        REFLECT_CLASS(PrefabInstance)
    public:
        struct Override
        {
            Guid templateNodeId;
            HString propertyName;
        };

        PrefabInstance() = default;

        virtual void onRegister() override;

        virtual void onUnregister() override;

        Prefab* getSourcePrefab() const noexcept
        {
            return mSourcePrefab.get();
        }

        void setSourcePrefab(Prefab* prefab) noexcept
        {
            mSourcePrefab = prefab;
        }

        // 把模板节点(Entity 或 Component)和这份实例里对应的运行时对象登记进来。
        void mapNode(Guid templateNodeId, Object* runtimeNode);

        Object* resolveNode(Guid templateNodeId) const;

        // 反向查找, 只在编辑器响应用户改动时才会被调用(人手动触发, 不是每帧跑的路径),
        // 线性扫描 mNodeMap 足够, 不需要为此单独维护一张反向表。
        std::optional<Guid> findTemplateNodeId(Object* node) const;

        void addOverride(Guid templateNodeId, HString propertyName);

        void removeOverride(Guid templateNodeId, HString propertyName);

        bool isOverridden(Guid templateNodeId, HString propertyName) const;

        std::span<const Override> getOverrides() const noexcept
        {
            return mOverrides;
        }

        void markNodeRemoved(Guid templateNodeId);

        bool isNodeRemoved(Guid templateNodeId) const;

        std::span<const Guid> getRemovedNodes() const noexcept
        {
            return mRemovedNodes;
        }

        // 从这份实例里删除/分离一个模板节点——用户对某个实例做的本地删除, 不影响模板资产
        // 本身。只改 mNodeMap/mRemovedNodes 这两张表, 不会自己调 destroyEntity()/
        // removeComponent() 之类会实际改动场景的接口。返回原来映射的运行时对象(查不到
        // 就是 nullptr), 要不要销毁、什么时候销毁, 由调用方决定。
        Object* removeNode(Guid templateNodeId);

        // 用户显式标记"这个本地新增的节点, 下次 applyToPrefab() 时要连同它一起提升进模板"——
        // 不自动推断, Prefab 模板改动会波及全项目所有引用它的实例, 哪些本地内容要被推广出去
        // 必须由用户明确决定。node(或者它所属的 Entity, 如果 node 是 Component)必须能顺着
        // Transform 父链走回这份实例的根 Entity, 走不到就说明它不属于这棵子树, 标记失败返回
        // false——模板是单根的, 没有办法表示一个跟这份实例不连通的节点。
        bool markNodeAdded(Object* node);

        void unmarkNodeAdded(Object* node);

        bool isNodeAdded(Object* node) const;

        std::span<Object* const> getAddedNodes() const noexcept
        {
            return mAddedNodes;
        }

        // 把模板当前的值同步到这份实例, 让这份实例跟模板保持一致——mOverrides 里记录的
        // 属性跳过不动, 模板新增的节点(还不在 mNodeMap 里, 且没被 markNodeRemoved 过)
        // 会被创建出来并登记进 mNodeMap, 见 instantiateNewNodesFromPrefab(); 模板删除的
        // 节点、被挪出这份实例子树的节点, 见 removeStaleNodesFromPrefab()——这两步都是
        // "跟模板保持一致"这个不变量的一部分, 不是可选步骤, 不交给调用方决定要不要做。
        // mSyncedVersion 跟 mSourcePrefab->getVersion() 已经一致时直接返回, 不重新走一遍。
        void updateFromPrefab();

        // 把 mOverrides 里记录的当前值写回模板资产对应节点, 写完清空这些记录——之后这些属性
        // 变成模板的新默认值, 其它引用同一个 Prefab 的实例该保留的 override 不受影响。
        void applyToPrefab();

    private:
        // 只写属性, 不带任何副作用(不调 postLoad/finalizeLoad/onRegister)——"写属性"和
        // "复活"这两件事要按整个 Entity(连同它全部 Component)分两个阶段做, 不能按节点
        // 交织在一起, 不然同一个 Component 会被重复复活。
        void writePropertiesFromTemplate(Object* templateNode, Object* runtimeNode);

        // rebuildObject() 会把对象打回刚构造的状态——override 的属性不存值(见 Override
        // 的注释), 打回默认状态会丢, 这里在 rebuildObject() 之前把当前值摘出来暂存。
        std::vector<std::pair<Property*, void*>> captureOverriddenValues(Guid templateNodeId, Object* runtimeNode) const;

        // writePropertiesFromTemplate() 写完模板新值之后调用, 把 captureOverriddenValues()
        // 存的值写回去, 释放暂存内存。
        void restoreOverriddenValues(Object* runtimeNode, std::vector<std::pair<Property*, void*>>& savedValues);

        // 处理模板新增的节点(新 Entity, 或者已有 Entity 身上新增的 Component)——用 Copier
        // 拷贝出来, 登记进 mNodeMap, 新 Component 还要挂到宿主 Entity 上。
        void instantiateNewNodesFromPrefab();

        // 处理两种要从这份实例摘除的节点: 模板已经删掉的——直接销毁对应的运行时对象
        // (Entity 走 Scene::destroyEntity(false), Component 走 Entity::removeComponent()),
        // 这是 updateFromPrefab() 要保证的"跟模板一致"这个不变量的一部分, 不交给调用方
        // 决定要不要做。还活着但被挪出这份实例子树的——只 unmap + markNodeRemoved, 不销毁,
        // 对象还在场景里, 只是不再属于这份 Prefab 实例的追踪范围。
        void removeStaleNodesFromPrefab();

        // applyToPrefab() 用: 把 mAddedNodes 里能提升的节点(Copier 拷贝进模板)提升进去,
        // 登记进 mNodeMap、从 mAddedNodes 摘除。宿主 Entity 既不在这批提升范围内、自己也还
        // 没被追踪的 Component, 这次跳过, 留在 mAddedNodes 里等下次。
        void promoteAddedNodesToPrefab();

        // entity 顺着 Transform 父链能不能走回这份实例的根 Entity(getEntity())——
        // removeStaleNodesFromPrefab() 的脱离子树检测、markNodeAdded() 的连通性校验共用
        // 这一份逻辑。
        bool isConnectedToInstanceRoot(Entity* entity) const;

    private:
        TRef<Prefab> mSourcePrefab;
        std::unordered_map<Guid, Object*> mNodeMap;
        std::vector<Override> mOverrides;
        std::vector<Guid> mRemovedNodes;

        // 用户标记过、下次 applyToPrefab() 要提升进模板的本地新增节点, 见 markNodeAdded()。
        std::vector<Object*> mAddedNodes;

        // 上次真正同步到的 mSourcePrefab 版本号(Prefab::getVersion())——反射属性, 场景存盘/
        // 反序列化要保留这个值, 不然每次重新加载场景都会因为它被打回默认值 0 而白白同步一次。
        uint32_t mSyncedVersion = 0;
    };

    template <> Type* Reflection::createType<PrefabInstance::Override>();
    template <> Type* Reflection::createType<PrefabInstance>();
}
