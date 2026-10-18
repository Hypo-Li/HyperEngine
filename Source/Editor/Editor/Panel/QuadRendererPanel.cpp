#include "QuadRendererPanel.h"
#include "../AssetComboEd.h"
#include "../ComponentRef.h"
#include "../ThumbnailProvider.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Engine/Context.h>

#include <HypImGui/ImGuiColorWidgets.h>
#include <HypImGui/ImGuiEnumWidgets.h>

#include <Renderer/QuadRenderProxy.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        TRef<Material> resolveMaterialAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<Material>() : TRef<Material>();
        }
    }

    QuadRendererPanel::QuadRendererPanel(QuadRenderer& quadRenderer) noexcept :
        IComponentPanel(quadRenderer),
        mQuadRenderer(quadRenderer)
    {
    }

    void QuadRendererPanel::drawCommonRows()
    {
        AssetManager& assetManager = Context::get().getAssetManager();
        ComponentRef ref = ComponentRef::capture(mQuadRenderer);
        const auto& defaults = *static_cast<const QuadRenderer*>(mQuadRenderer.getClass()->getDefaultObject());

        BeginPropertyRow("Material", 2.0f);
        Material* material = mQuadRenderer.getMaterial();
        Guid materialGuidBefore = material ? material->getGuid() : Guid();
        Guid materialGuid = materialGuidBefore;
        bool materialChanged = AssetComboEd(
            "##Material", assetManager, Material::staticClass(), true, materialGuid, getAssetBrowser(), getIconProvider());
        materialChanged |= DrawPropertyReset("Material", materialGuid, Guid());
        if (materialChanged)
        {
            mQuadRenderer.setMaterial(resolveMaterialAsset(assetManager, materialGuid).get());
            getUndoStack().push("Change Material"_hs,
                [ref, guid = materialGuidBefore]()
                {
                    ref.resolveAs<QuadRenderer>()->setMaterial(resolveMaterialAsset(Context::get().getAssetManager(), guid).get());
                },
                [ref, guid = materialGuid]()
                {
                    ref.resolveAs<QuadRenderer>()->setMaterial(resolveMaterialAsset(Context::get().getAssetManager(), guid).get());
                });
        }

        drawProperty<float2>("Anchor", mQuadRenderer.getAnchor(), defaults.getAnchor(),
            [](float2& value) { return ImGui::DragFloat2("##Anchor", &value.x, 0.01f, 0.0f, 1.0f, "%.3f"); },
            [ref](const float2& v) { ref.resolveAs<QuadRenderer>()->setAnchor(v); });

        drawProperty<float3>("Offset", mQuadRenderer.getOffset(), defaults.getOffset(),
            [](float3& value) { return ImGui::DragFloat3("##Offset", &value.x, 0.01f, 0.0f, 0.0f, "%.3f"); },
            [ref](const float3& v) { ref.resolveAs<QuadRenderer>()->setOffset(v); });

        drawProperty<QuadBillboardMode>("Billboard Mode", mQuadRenderer.getBillboardMode(), defaults.getBillboardMode(),
            [](QuadBillboardMode& value) { return EnumCombo("##BillboardMode", &value); },
            [ref](QuadBillboardMode v) { ref.resolveAs<QuadRenderer>()->setBillboardMode(v); });

        drawProperty<float>("Auto Scale Strength", mQuadRenderer.getAutoScaleStrength(), defaults.getAutoScaleStrength(),
            [](float& value) { return ImGui::DragFloat("##AutoScaleStrength", &value, 0.01f, 0.0f, 1.0f, "%.3f"); },
            [ref](float v) { ref.resolveAs<QuadRenderer>()->setAutoScaleStrength(v); });

        ImGui::BeginDisabled(mQuadRenderer.getAutoScaleStrength() <= 0.0f);

        drawProperty<float>("Reference Distance", mQuadRenderer.getReferenceDistance(), defaults.getReferenceDistance(),
            [](float& value) { return ImGui::DragFloat("##ReferenceDistance", &value, 0.1f, 0.001f, 100000.0f, "%.3f"); },
            [ref](float v) { ref.resolveAs<QuadRenderer>()->setReferenceDistance(v); });

        drawProperty<float>("Min Scale", mQuadRenderer.getMinScale(), defaults.getMinScale(),
            [](float& value) { return ImGui::DragFloat("##MinScale", &value, 0.01f, 0.0f, 1000.0f, "%.3f"); },
            [ref](float v) { ref.resolveAs<QuadRenderer>()->setMinScale(v); });

        drawProperty<float>("Max Scale", mQuadRenderer.getMaxScale(), defaults.getMaxScale(),
            [](float& value) { return ImGui::DragFloat("##MaxScale", &value, 0.1f, 0.0f, 100000.0f, "%.3f"); },
            [ref](float v) { ref.resolveAs<QuadRenderer>()->setMaxScale(v); });

        ImGui::EndDisabled();

        drawProperty<float>("Depth Bias", mQuadRenderer.getDepthBias(), defaults.getDepthBias(),
            [](float& value) { return ImGui::DragFloat("##DepthBias", &value, 0.001f, -1000.0f, 1000.0f, "%.4f"); },
            [ref](float v) { ref.resolveAs<QuadRenderer>()->setDepthBias(v); });

        drawProperty<float3>("Tint",float3(mQuadRenderer.getTint()), float3(defaults.getTint()),
            [](float3& value) { return LinearColorEdit3("##Tint", &value.x); },
            [ref](const float3& v)
            {
                QuadRenderer* quadRenderer = ref.resolveAs<QuadRenderer>();
                quadRenderer->setTint(float4(v, quadRenderer->getTint().w));
            });

        drawProperty<float>("Opacity", mQuadRenderer.getTint().w, defaults.getTint().w,
            [](float& value) { return ImGui::DragFloat("##Opacity", &value, 0.01f, 0.0f, 1.0f, "%.3f"); },
            [ref](float v)
            {
                QuadRenderer* quadRenderer = ref.resolveAs<QuadRenderer>();
                float4 tint = quadRenderer->getTint();
                tint.w = v;
                quadRenderer->setTint(tint);
            });

        drawProperty<int>("Sort Order", static_cast<int>(mQuadRenderer.getSortOrder()), static_cast<int>(defaults.getSortOrder()),
            [](int& value) { return ImGui::DragInt("##SortOrder", &value, 1.0f, -128, 127); },
            [ref](int v) { ref.resolveAs<QuadRenderer>()->setSortOrder(static_cast<int8_t>(v)); });

        drawProperty<bool>("Cast Shadow", mQuadRenderer.isCastShadow(), defaults.isCastShadow(),
            [](bool& value) { return ImGui::Checkbox("##CastShadow", &value); },
            [ref](bool v) { ref.resolveAs<QuadRenderer>()->setCastShadow(v); });
    }
}
