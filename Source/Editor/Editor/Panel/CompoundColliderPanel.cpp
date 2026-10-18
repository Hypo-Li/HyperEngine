#include "CompoundColliderPanel.h"
#include "../AssetComboEd.h"
#include "../ThumbnailProvider.h"
#include "../ComponentRef.h"
#include "ComponentPanelRegistry.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Engine/Context.h>

#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<CompoundCollider, CompoundColliderPanel> gRegistrar;

        TRef<CompoundCollision> resolveCompoundCollisionAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<CompoundCollision>() : TRef<CompoundCollision>();
        }
    }

    CompoundColliderPanel::CompoundColliderPanel(CompoundCollider& compoundCollider) noexcept :
        ColliderPanel(compoundCollider),
        mCompoundCollider(compoundCollider)
    {
    }

    void CompoundColliderPanel::onDraw()
    {
        if (!BeginPropertyTable("CompoundColliderProperties", ImGuiTableFlags_BordersInnerH))
            return;

        ColliderPanel::onDraw();

        ComponentRef compoundColliderRef = ComponentRef::capture(mCompoundCollider);

        AssetManager& assetManager = Context::get().getAssetManager();
        BeginPropertyRow("Compound Collision", 2.0f); // AssetCombo 是两行缩略图布局
        CompoundCollision* compoundCollision = mCompoundCollider.getCompoundCollision();
        Guid compoundCollisionGuidBefore = compoundCollision ? compoundCollision->getGuid() : Guid();
        Guid compoundCollisionGuid = compoundCollisionGuidBefore;
        bool compoundCollisionChanged = AssetComboEd("##CompoundCollision", assetManager, CompoundCollision::staticClass(), true, compoundCollisionGuid, getAssetBrowser(), getIconProvider());
        compoundCollisionChanged |= DrawPropertyReset("Compound Collision", compoundCollisionGuid, Guid());
        if (compoundCollisionChanged)
        {
            mCompoundCollider.setCompoundCollision(resolveCompoundCollisionAsset(assetManager, compoundCollisionGuid).get());

            getUndoStack().push("Change Compound Collision"_hs,
                [ref = compoundColliderRef, guid = compoundCollisionGuidBefore]()
                {
                    ref.resolveAs<CompoundCollider>()->setCompoundCollision(resolveCompoundCollisionAsset(Context::get().getAssetManager(), guid).get());
                },
                [ref = compoundColliderRef, guid = compoundCollisionGuid]()
                {
                    ref.resolveAs<CompoundCollider>()->setCompoundCollision(resolveCompoundCollisionAsset(Context::get().getAssetManager(), guid).get());
                });
        }

        EndPropertyTable();
    }
}
