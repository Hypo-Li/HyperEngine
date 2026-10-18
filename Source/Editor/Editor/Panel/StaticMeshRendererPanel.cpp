#include "StaticMeshRendererPanel.h"
#include "../AssetComboEd.h"
#include "../ThumbnailProvider.h"
#include "../ComponentRef.h"
#include "../UndoStack.h"
#include "ComponentPanelRegistry.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Core/Reflection/Class.h>
#include <Engine/Context.h>
#include <Engine/Material/Material.h>
#include <Engine/Mesh/StaticMesh.h>

#include <HypImGui/ImGuiAssetCombo.h>
#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<StaticMeshRenderer, StaticMeshRendererPanel> gRegistrar;

        // undo/redo 的 lambda 要活到很久以后才可能被调用(用户按下 Ctrl+Z 之前经过了多少帧
        // 都有可能), 所以不能捕获 Asset*/TRef<StaticMesh> 这类"当前解析结果"——资产可能在这期间被
        // 卸载/重新加载。只捕获 Guid, 每次真正需要用的时候(包括这里第一次赋值)才现查
        // AssetManager
        TRef<StaticMesh> resolveMeshAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<StaticMesh>() : TRef<StaticMesh>();
        }

        TRef<Material> resolveMaterialAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<Material>() : TRef<Material>();
        }
    }

    StaticMeshRendererPanel::StaticMeshRendererPanel(StaticMeshRenderer& meshRenderer) noexcept :
        IComponentPanel(meshRenderer),
        mMeshRenderer(meshRenderer)
    {
    }

    void StaticMeshRendererPanel::onDraw()
    {
        AssetManager& assetManager = Context::get().getAssetManager();
        // undo/redo 闭包要活到很久以后才可能被调用, 这期间这个组件(甚至它挂着的 Entity)
        // 完全可能已经被删除又被 undo 恢复成新地址的对象——不能直接捕获 &mMeshRenderer,
        // ComponentRef 只存 Scene*/Entity Guid/Component Guid 这些稳定身份, 每次现查一遍。
        ComponentRef meshRendererRef = ComponentRef::capture(mMeshRenderer);

        if (BeginPropertyTable("StaticMeshRendererProperties", ImGuiTableFlags_BordersInnerH))
        {
            BeginPropertyRow("Mesh", 2.0f); // AssetCombo 是两行缩略图布局
            StaticMesh* mesh = mMeshRenderer.getMesh();
            Guid meshGuidBefore = mesh ? mesh->getGuid() : Guid();
            Guid meshGuid = meshGuidBefore;
            bool meshChanged = AssetComboEd("##Mesh", assetManager, StaticMesh::staticClass(), false, meshGuid, getAssetBrowser(), getIconProvider());
            // MeshRenderer 自己没有"默认网格"这个概念——一个新建的 MeshRenderer 就是空引用,
            // 所以这里的默认值是 Guid()(空), 不是随便定的; 换成有默认资产概念的组件字段,
            // defaultValue 就传那个资产的 Guid, DrawPropertyReset 本身不关心传的是什么。
            meshChanged |= DrawPropertyReset("Mesh", meshGuid, Guid());
            if (meshChanged)
            {
                // setMesh() 会无条件清空 mOverrideMaterials(见 StaticMeshRenderer.cpp), 不保留
                // 换 Mesh 之前每个槽位上的覆盖——所以撤销"换 Mesh"如果只重新 setMesh(旧 Mesh),
                // 会把旧 Mesh 上原本的材质覆盖也一起丢掉。这里要在真正切换前把当前每个槽位的
                // 覆盖材质 Guid 存一份快照, undo 时先切回旧 Mesh 再逐槽塞回去。重做方向不需要
                // 存快照——redo 就是重新 setMesh(新 Mesh), 结果本来就是全部跟随新 Mesh, 是
                // 确定性的。
                uint32_t overrideMaterialCountBeforeMeshChange = mMeshRenderer.getOverrideMaterialCount();
                std::vector<Guid> overrideMaterialGuidsBeforeMeshChange;
                overrideMaterialGuidsBeforeMeshChange.reserve(overrideMaterialCountBeforeMeshChange);
                for (uint32_t i = 0; i < overrideMaterialCountBeforeMeshChange; ++i)
                {
                    Material* overrideMaterial = mMeshRenderer.getOverrideMaterial(i);
                    overrideMaterialGuidsBeforeMeshChange.push_back(overrideMaterial ? overrideMaterial->getGuid() : Guid());
                }

                mMeshRenderer.setMesh(resolveMeshAsset(assetManager, meshGuid).get());

                getUndoStack().push("Change Mesh"_hs,
                    [ref = meshRendererRef, guid = meshGuidBefore, overrideMaterialGuids = std::move(overrideMaterialGuidsBeforeMeshChange)]()
                    {
                        StaticMeshRenderer* meshRenderer = ref.resolveAs<StaticMeshRenderer>();
                        meshRenderer->setMesh(resolveMeshAsset(Context::get().getAssetManager(), guid).get());
                        AssetManager& assetManager = Context::get().getAssetManager();
                        uint32_t restoreCount = std::min<uint32_t>(static_cast<uint32_t>(overrideMaterialGuids.size()), meshRenderer->getOverrideMaterialCount());
                        for (uint32_t i = 0; i < restoreCount; ++i)
                        {
                            meshRenderer->setOverrideMaterial(i, resolveMaterialAsset(assetManager, overrideMaterialGuids[i]).get());
                        }
                    },
                    [ref = meshRendererRef, guid = meshGuid]()
                    {
                        ref.resolveAs<StaticMeshRenderer>()->setMesh(resolveMeshAsset(Context::get().getAssetManager(), guid).get());
                    });
            }

            // Mesh 如果刚被换掉, 这里要重新取一次当前 Mesh, 下面算每个材质槽的"默认值"才是对的。
            mesh = mMeshRenderer.getMesh();
            uint32_t materialCount = mMeshRenderer.getOverrideMaterialCount();
            for (uint32_t i = 0; i < materialCount; ++i)
            {
                std::string label = "Material " + std::to_string(i);
                BeginPropertyRow(label.c_str(), 2.0f); // AssetCombo 是两行缩略图布局

                Material* overrideMaterial = mMeshRenderer.getOverrideMaterial(i);
                Guid overrideGuidBefore = overrideMaterial ? overrideMaterial->getGuid() : Guid();
                Material* effectiveMaterial = mMeshRenderer.getEffectiveMaterial(i);
                Guid materialGuid = effectiveMaterial ? effectiveMaterial->getGuid() : Guid();

                // 材质槽没有被覆盖时的"默认值"不是空引用, 是网格自己在这个槽位上原本指定的
                // 材质——这正是"资产引用的默认值不一定是空"的例子。
                Material* defaultMaterial = (mesh && i < mesh->getMaterialCount()) ? mesh->getMaterial(i) : nullptr;
                Guid defaultMaterialGuid = defaultMaterial ? defaultMaterial->getGuid() : Guid();

                ImGui::PushID(static_cast<int>(i));
                bool materialChanged = AssetComboEd("##Material", assetManager, Material::staticClass(), true, materialGuid, getAssetBrowser(), getIconProvider());
                materialChanged |= DrawPropertyReset(label.c_str(), materialGuid, defaultMaterialGuid);
                if (materialChanged)
                {
                    Guid overrideGuidAfter = (materialGuid == defaultMaterialGuid) ? Guid() : materialGuid;
                    mMeshRenderer.setOverrideMaterial(i, resolveMaterialAsset(assetManager, overrideGuidAfter).get());

                    getUndoStack().push("Change Material"_hs,
                        [ref = meshRendererRef, index = i, guid = overrideGuidBefore]()
                        {
                            ref.resolveAs<StaticMeshRenderer>()->setOverrideMaterial(index, resolveMaterialAsset(Context::get().getAssetManager(), guid).get());
                        },
                        [ref = meshRendererRef, index = i, guid = overrideGuidAfter]()
                        {
                            ref.resolveAs<StaticMeshRenderer>()->setOverrideMaterial(index, resolveMaterialAsset(Context::get().getAssetManager(), guid).get());
                        });
                }
                ImGui::PopID();
            }

            auto& defaultRenderer = *static_cast<const StaticMeshRenderer*>(StaticMeshRenderer::staticClass()->getDefaultObject());

            BeginPropertyRow("Sort Order");
            int sortOrderBefore = mMeshRenderer.getSortOrder();
            int sortOrder = sortOrderBefore;
            bool sortOrderChanged = ImGui::DragInt("##SortOrder", &sortOrder, 1.0f, -128, 127);
            auto sortOrderSetter = [ref = meshRendererRef](int v) { ref.resolveAs<StaticMeshRenderer>()->setSortOrder(static_cast<int8_t>(v)); };
            getUndoStack().trackGesture("Change Sort Order"_hs, sortOrderBefore, sortOrder, sortOrderSetter);
            if (sortOrderChanged)
                mMeshRenderer.setSortOrder(static_cast<int8_t>(sortOrder));
            int sortOrderBeforeReset = sortOrder;
            if (DrawPropertyReset("Sort Order", sortOrder, static_cast<int>(defaultRenderer.getSortOrder())))
            {
                mMeshRenderer.setSortOrder(static_cast<int8_t>(sortOrder));
                getUndoStack().push("Reset Sort Order"_hs,
                    [sortOrderSetter, sortOrderBeforeReset]() { sortOrderSetter(sortOrderBeforeReset); },
                    [sortOrderSetter, sortOrder]() { sortOrderSetter(sortOrder); });
            }

            BeginPropertyRow("Cast Shadow");
            bool castShadowBefore = mMeshRenderer.isCastShadow();
            bool castShadow = castShadowBefore;
            bool castShadowChanged = ImGui::Checkbox("##CastShadow", &castShadow);
            auto castShadowSetter = [ref = meshRendererRef](bool v) { ref.resolveAs<StaticMeshRenderer>()->setCastShadow(v); };
            getUndoStack().trackGesture("Change Cast Shadow"_hs, castShadowBefore, castShadow, castShadowSetter);
            if (castShadowChanged)
                mMeshRenderer.setCastShadow(castShadow);
            bool castShadowBeforeReset = castShadow;
            if (DrawPropertyReset("Cast Shadow", castShadow, defaultRenderer.isCastShadow()))
            {
                mMeshRenderer.setCastShadow(castShadow);
                getUndoStack().push("Reset Cast Shadow"_hs,
                    [castShadowSetter, castShadowBeforeReset]() { castShadowSetter(castShadowBeforeReset); },
                    [castShadowSetter, castShadow]() { castShadowSetter(castShadow); });
            }

            EndPropertyTable();
        }
    }
}
