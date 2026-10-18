#include "RenderTarget.h"
#include "../Context.h"

#include <RenderBase/TextureProxy.h>
#include <RenderBase/RenderFrontend.h>

#include <algorithm>

namespace hyp
{
    REGISTER_CLASS(RenderTarget)

    RenderTarget::RenderTarget()
    {

    }

    RenderTarget::~RenderTarget() noexcept
    {
    }

    void RenderTarget::setName(HString name) noexcept
    {
        if (mName == name)
            return;

        mName = name;
        mDirty = true;
    }

    void RenderTarget::setSize(uint32_t width, uint32_t height) noexcept
    {
        // 0 宽/高会在 createTextureProxy() 里造出一个永远建不出真实 GPU 资源的
        // TextureProxyHandle —— handle 本身从分配器的角度看仍然"valid", 但渲染线程的
        // ProxyPool 里根本没有对应的对象, 后续任何 getProxy(handle)->getTextureView()
        // 都会解引用到不存在的 proxy。夹到至少 1x1, 从源头上不让这种尺寸进入 Description,
        // 而不是指望每个消费者(比如 ImGui 视口在窗口折叠/停靠瞬间读到的 0 尺寸)都记得
        // 自己过滤 0。
        width = std::max(width, 1u);
        height = std::max(height, 1u);

        if (getDescription().width == width && getDescription().height == height)
            return;

        Description desc = getDescription();
        desc.width = width;
        desc.height = height;
        setDescription(desc);
        mDirty = true;
    }

    void RenderTarget::setFormat(PixelFormat format) noexcept
    {
        if (getDescription().format == format)
            return;

        Description desc = getDescription();
        desc.format = format;
        setDescription(desc);
        mDirty = true;
    }

    void RenderTarget::apply()
    {
        if (mDirty)
        {
            destroyTextureProxy(getTextureProxyHandle());
            createTextureProxy(getTextureProxyHandle());
        }

        mDirty = false;
    }

    void RenderTarget::createTextureProxy(TextureProxyHandle proxyHandle)
    {
        if (getDescription().width == 0 || getDescription().height == 0)
            return;

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle,
            desc = getDescription(),
            rhiFormat = getRHITextureFormat()](FrameContext& context) mutable
            {
                rhi::TextureDesc textureDesc;
                textureDesc.type = rhi::TextureType::Texture_2D;
                textureDesc.format = rhiFormat;
                textureDesc.extent = { desc.width, desc.height, 1 };
                textureDesc.mipCount = 1;
                textureDesc.layerCount = 1;
                textureDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Color_Attachment;

                context.getProxyManager().createProxy(proxyHandle, textureDesc);
            }
        );
    }

    template <> Type* Reflection::createType<RenderTarget>()
    {
        auto c = new TClass<RenderTarget, Texture>("RenderTarget");
        c->setDisplayName("Render Target");
        c->addProperty("Name", &RenderTarget::mName);
        return c;
    }

}
