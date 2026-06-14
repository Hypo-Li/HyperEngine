#pragma once
#include "../Framework/Component.h"
#include "../Framework/Transform.h"
#include "../Mesh/Mesh.h"

#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    class MeshRenderer : public Component, public ITransformListener
    {
        REFLECT_CLASS(MeshRenderer)
    public:
        MeshRenderer() = default;

        ~MeshRenderer() = default;

        void onRegister() override;

        void onUnregister() override;

        void onVisible() override;

        void onInvisible() override;

        void onTick(float deltaTime) override;

        void onTransformUpdate(const Transform& transform) override;

        void setMesh(Mesh* newMesh);

        Mesh* getMesh() const noexcept
        {
            return mMesh;
        }

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

        MeshRenderProxyHandle getProxyHandle() const noexcept
        {
            return mProxyHandle;
        }

    private:
        void updateProxyDrawItems();

        void setProxyVisible(bool visible);

    private:
        TRef<Mesh> mMesh;
        std::vector<TRef<Material>> mOverrideMaterials;

        int8_t mSortOrder = 0;
        bool mCastShadows = true;

        MeshRenderProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<MeshRenderer>();
}
