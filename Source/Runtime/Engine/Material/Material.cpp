#include "Material.h"
#include "../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <RenderBase/MaterialProxy.h>

namespace hyp
{
    REGISTER_CLASS(Material)

    Material::Material()
    {

    }

    Material::~Material() noexcept
    {
        if (mProxyHandle.isValid())
        {
            destroyMaterialProxy(mProxyHandle);
            Context::get().getRenderFrontend().freeProxyHandle(mProxyHandle);
            mProxyHandle.reset();
        }
    }

    MaterialProxyHandle Material::getOrCreateMaterialProxy()
    {
        if (!mProxyHandle.isValid())
        {
            mProxyHandle = Context::get().getRenderFrontend().allocateProxyHandle<ProxyType::Material>();
            createMaterialProxy(mProxyHandle);
        }
        return mProxyHandle;
    }

    void Material::destroyMaterialProxy(MaterialProxyHandle handle)
    {
        assert(handle.isValid());
        Context::get().getRenderFrontend().enqueueLambda(
            [handle](RenderFrameContext& context)
            {
                context.getProxyManager().destroyProxy(handle);
            }
        );
    }

    template <> Type* Reflection::createType<Material>()
    {
        auto c = new TClass<Material>("Material");
        return c;
    }
}
