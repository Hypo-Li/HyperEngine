#pragma once
#include <Core/Object/Object.h>
#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    class Material : public Object
    {
        REFLECT_CLASS(Material)
    public:
        Material();

        ~Material() noexcept;

        MaterialProxyHandle getOrCreateMaterialProxy();

        virtual void onParameterResourceChanged() = 0;

    protected:
        MaterialProxyHandle getMaterialProxyHandle() const noexcept
        {
            return mProxyHandle;
        }

        virtual void createMaterialProxy(MaterialProxyHandle handle) = 0;

        void destroyMaterialProxy(MaterialProxyHandle handle);

    private:
        MaterialProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<Material>();
}
