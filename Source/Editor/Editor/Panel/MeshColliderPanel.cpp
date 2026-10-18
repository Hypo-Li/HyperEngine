#include "MeshColliderPanel.h"
#include "../AssetComboEd.h"
#include "../ThumbnailProvider.h"
#include "../ComponentRef.h"
#include "ComponentPanelRegistry.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Engine/Context.h>

#include <HypImGui/ImGuiPropertyWidgets.h>

#include <algorithm>
#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<MeshCollider, MeshColliderPanel> gRegistrar;

        TRef<StaticMesh> resolveStaticMeshAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<StaticMesh>() : TRef<StaticMesh>();
        }
    }

    MeshColliderPanel::MeshColliderPanel(MeshCollider& meshCollider) noexcept :
        ColliderPanel(meshCollider),
        mMeshCollider(meshCollider)
    {
    }

    void MeshColliderPanel::onDraw()
    {
        if (!BeginPropertyTable("MeshColliderProperties", ImGuiTableFlags_BordersInnerH))
            return;

        ColliderPanel::onDraw();

        ComponentRef meshColliderRef = ComponentRef::capture(mMeshCollider);

        AssetManager& assetManager = Context::get().getAssetManager();
        BeginPropertyRow("Mesh", 2.0f); // AssetCombo 是两行缩略图布局
        StaticMesh* mesh = mMeshCollider.getMesh();
        Guid meshGuidBefore = mesh ? mesh->getGuid() : Guid();
        Guid meshGuid = meshGuidBefore;
        bool meshChanged = AssetComboEd("##Mesh", assetManager, StaticMesh::staticClass(), true, meshGuid, getAssetBrowser(), getIconProvider());
        meshChanged |= DrawPropertyReset("Mesh", meshGuid, Guid());
        if (meshChanged)
        {
            mMeshCollider.setMesh(resolveStaticMeshAsset(assetManager, meshGuid).get());

            getUndoStack().push("Change Mesh"_hs,
                [ref = meshColliderRef, guid = meshGuidBefore]()
                {
                    ref.resolveAs<MeshCollider>()->setMesh(resolveStaticMeshAsset(Context::get().getAssetManager(), guid).get());
                },
                [ref = meshColliderRef, guid = meshGuid]()
                {
                    ref.resolveAs<MeshCollider>()->setMesh(resolveStaticMeshAsset(Context::get().getAssetManager(), guid).get());
                });
        }

        mesh = mMeshCollider.getMesh(); // 上面可能刚换过 mesh
        int lodLevelMax = mesh && mesh->getLODCount() > 0 ? static_cast<int>(mesh->getLODCount()) - 1 : 0;

        BeginPropertyRow("LOD Level");
        int lodLevelBefore = static_cast<int>(mMeshCollider.getLODLevel());
        int lodLevel = lodLevelBefore;
        bool lodLevelChanged = ImGui::DragInt("##LODLevel", &lodLevel, 1.0f, 0, lodLevelMax);
        lodLevel = std::clamp(lodLevel, 0, lodLevelMax);
        auto lodLevelSetter = [ref = meshColliderRef](int v) { ref.resolveAs<MeshCollider>()->setLODLevel(static_cast<uint32_t>(v)); };
        getUndoStack().trackGesture("Change LOD Level"_hs, lodLevelBefore, lodLevel, lodLevelSetter);
        if (lodLevelChanged)
            mMeshCollider.setLODLevel(static_cast<uint32_t>(lodLevel));
        int lodLevelBeforeReset = lodLevel;
        if (DrawPropertyReset("LOD Level", lodLevel, 0))
        {
            mMeshCollider.setLODLevel(static_cast<uint32_t>(lodLevel));
            pushReset("Reset LOD Level"_hs, lodLevelSetter, lodLevelBeforeReset, lodLevel);
        }

        EndPropertyTable();
    }
}
