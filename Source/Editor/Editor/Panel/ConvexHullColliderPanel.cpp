#include "ConvexHullColliderPanel.h"
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
        ComponentPanelRegistrar<ConvexHullCollider, ConvexHullColliderPanel> gRegistrar;

        TRef<ConvexHull> resolveConvexHullAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<ConvexHull>() : TRef<ConvexHull>();
        }
    }

    ConvexHullColliderPanel::ConvexHullColliderPanel(ConvexHullCollider& convexHullCollider) noexcept :
        ColliderPanel(convexHullCollider),
        mConvexHullCollider(convexHullCollider)
    {
    }

    void ConvexHullColliderPanel::onDraw()
    {
        if (!BeginPropertyTable("ConvexHullColliderProperties", ImGuiTableFlags_BordersInnerH))
            return;

        ColliderPanel::onDraw();

        ComponentRef convexHullColliderRef = ComponentRef::capture(mConvexHullCollider);

        AssetManager& assetManager = Context::get().getAssetManager();
        BeginPropertyRow("Hull", 2.0f); // AssetCombo 是两行缩略图布局
        ConvexHull* hull = mConvexHullCollider.getHull();
        Guid hullGuidBefore = hull ? hull->getGuid() : Guid();
        Guid hullGuid = hullGuidBefore;
        bool hullChanged = AssetComboEd("##Hull", assetManager, ConvexHull::staticClass(), true, hullGuid, getAssetBrowser(), getIconProvider());
        hullChanged |= DrawPropertyReset("Hull", hullGuid, Guid());
        if (hullChanged)
        {
            mConvexHullCollider.setHull(resolveConvexHullAsset(assetManager, hullGuid).get());

            getUndoStack().push("Change Hull"_hs,
                [ref = convexHullColliderRef, guid = hullGuidBefore]()
                {
                    ref.resolveAs<ConvexHullCollider>()->setHull(resolveConvexHullAsset(Context::get().getAssetManager(), guid).get());
                },
                [ref = convexHullColliderRef, guid = hullGuid]()
                {
                    ref.resolveAs<ConvexHullCollider>()->setHull(resolveConvexHullAsset(Context::get().getAssetManager(), guid).get());
                });
        }

        EndPropertyTable();
    }
}
