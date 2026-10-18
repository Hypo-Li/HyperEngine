#include "SkyBackgroundPanel.h"
#include "../AssetComboEd.h"
#include "../ThumbnailProvider.h"
#include "../ComponentRef.h"
#include "../UndoStack.h"
#include "ComponentPanelRegistry.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Engine/Context.h>
#include <Engine/Material/Material.h>

#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<SkyBackground, SkyBackgroundPanel> gRegistrar;

        TRef<Material> resolveMaterialAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<Material>() : TRef<Material>();
        }
    }

    SkyBackgroundPanel::SkyBackgroundPanel(SkyBackground& skyBackground) noexcept :
        IComponentPanel(skyBackground),
        mSkyBackground(skyBackground)
    {
    }

    void SkyBackgroundPanel::onDraw()
    {
        AssetManager& assetManager = Context::get().getAssetManager();
        ComponentRef skyBackgroundRef = ComponentRef::capture(mSkyBackground);

        auto pushReset = [&](HString name, auto setter, const auto& before, const auto& after)
        {
            getUndoStack().push(name,
                [setter, before]() mutable { setter(before); },
                [setter, after]() mutable { setter(after); });
        };

        if (!BeginPropertyTable("SkyBackgroundProperties", ImGuiTableFlags_BordersInnerH))
            return;

        auto& ddl = *static_cast<const SkyBackground*>(SkyBackground::staticClass()->getDefaultObject());

        BeginPropertyRow("Material", 2.0f);
        Material* material = mSkyBackground.getMaterial();
        Guid materialGuidBefore = material ? material->getGuid() : Guid();
        Guid materialGuid = materialGuidBefore;
        bool materialChanged = AssetComboEd(
            "##Material", assetManager, Material::staticClass(), true, materialGuid, getAssetBrowser(), getIconProvider());
        materialChanged |= DrawPropertyReset("Material", materialGuid, Guid());
        if (materialChanged)
        {
            mSkyBackground.setMaterial(resolveMaterialAsset(assetManager, materialGuid).get());
            getUndoStack().push("Change Material"_hs,
                [ref = skyBackgroundRef, guid = materialGuidBefore]()
                {
                    ref.resolveAs<SkyBackground>()->setMaterial(resolveMaterialAsset(Context::get().getAssetManager(), guid).get());
                },
                [ref = skyBackgroundRef, guid = materialGuid]()
                {
                    ref.resolveAs<SkyBackground>()->setMaterial(resolveMaterialAsset(Context::get().getAssetManager(), guid).get());
                });
        }

        BeginPropertyRow("Intensity");
        float intensityBefore = mSkyBackground.getIntensity();
        float intensity = intensityBefore;
        bool intensityChanged = ImGui::DragFloat("##Intensity", &intensity, 0.05f, 0.0f, 100.0f, "%.3f");
        auto intensitySetter = [ref = skyBackgroundRef](float v) { ref.resolveAs<SkyBackground>()->setIntensity(v); };
        getUndoStack().trackGesture("Change Intensity"_hs, intensityBefore, intensity, intensitySetter);
        if (intensityChanged)
            mSkyBackground.setIntensity(intensity);
        float intensityBeforeReset = intensity;
        if (DrawPropertyReset("Intensity", intensity, ddl.getIntensity()))
        {
            mSkyBackground.setIntensity(intensity);
            pushReset("Reset Intensity"_hs, intensitySetter, intensityBeforeReset, intensity);
        }

        EndPropertyTable();
    }
}
