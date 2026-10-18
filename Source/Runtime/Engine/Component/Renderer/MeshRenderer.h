#pragma once
#include "../../Framework/Component.h"
#include "../../Framework/Transform.h"
#include "../../Mesh/Mesh.h"

#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    // StaticMeshRenderer/SkeletalMeshRenderer 共享的部分: 挂渲染 Proxy、维护 DrawItems、
    // 材质槽位覆盖、包围盒/可见性/Mobility 同步渲染代理——这些跟"具体是哪种 Mesh"无关。
    // Lumen SDF/Card 形状代理只对 StaticMesh 成立, 留在 StaticMeshRenderer 自己那边;
    // 动画播放只对 SkeletalMeshRenderer 成立, 留在 SkeletalMeshRenderer 自己那边。
    class MeshRenderer : public Component, public ITransformListener, public IMeshMaterialListener
    {
        REFLECT_CLASS(MeshRenderer)
    public:
        MeshRenderer() = default;

        virtual ~MeshRenderer() = 0;

        void onRegister() override;

        void onUnregister() override;

        void onVisibilityChanged(bool visible) override;

        void onTransformUpdate(const Transform& transform) override;

        void onMobilityChanged(Mobility mobility) override;

        void onMeshMaterialsChanged() override;

        void setOverrideMaterial(uint32_t index, Material* material);

        uint32_t getOverrideMaterialCount() const noexcept
        {
            return static_cast<uint32_t>(mOverrideMaterials.size());
        }

        Material* getOverrideMaterial(uint32_t index) const noexcept
        {
            assert(index < mOverrideMaterials.size());
            return mOverrideMaterials[index];
        }

        Material* getEffectiveMaterial(uint32_t index) const noexcept;

        void setSortOrder(int8_t sortOrder);

        int8_t getSortOrder() const noexcept
        {
            return mSortOrder;
        }

        void setCastShadow(bool castShadow);

        bool isCastShadow() const noexcept
        {
            return mCastShadow;
        }

        Boxd computeWorldBoundingBox();

        // LOD0 的 submesh 数量——PickPass 的候选收集(逻辑线程)按这个数字给每个 submesh 分配
        // 一个 pickId, 跟 updateProxyDrawItems() 里 getDrawItemsLOD0() 的下标一一对应。没有
        // mesh 时返回 0
        uint32_t getSubmeshCount() const noexcept;

        MeshRenderProxyHandle getProxyHandle() const noexcept
        {
            return mProxyHandle;
        }

    protected:
        // 子类返回自己持有的具体 mesh(TRef<StaticMesh>/TRef<SkeletalMesh> 隐式转 Mesh*)——
        // 具体 mesh 字段的类型必须留在子类自己的反射属性里(比如编辑器要能把 StaticMeshRenderer
        // 的 Mesh 槽位限制成只能拖 StaticMesh 资产), 不能收到这个共享基类里变成笼统的 Mesh*
        virtual Mesh* getMesh() const noexcept = 0;

        // 子类把新 mesh 存进自己的 typed 字段之后调这个, 做公共的那部分: 重建材质槽位覆盖表 +
        // updateProxyDrawItems() + 刷新包围盒。oldMesh 是换之前的 mesh, 调用方要保证它在这次调用
        // 期间仍然存活
        void onMeshChanged(Mesh* oldMesh);

        void updateProxyDrawItems();

    private:
        void attachMeshListener();

        void detachMeshListener(Mesh* mesh);

    private:
        // 空项表示这个槽位跟随 Mesh 自己的材质
        std::vector<TRef<Material>> mOverrideMaterials;
        int8_t mSortOrder = 0;
        bool mCastShadow = true;

        bool mListeningToMesh = false;
        MeshRenderProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<MeshRenderer>();
}
