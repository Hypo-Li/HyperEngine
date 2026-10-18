#include "SkeletalMeshRendererPanel.h"
#include "../AssetComboEd.h"
#include "../ThumbnailProvider.h"
#include "../ComponentRef.h"
#include "../UndoStack.h"
#include "ComponentPanelRegistry.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Core/Reflection/Class.h>
#include <Engine/Animation/Animation.h>
#include <Engine/Context.h>
#include <Engine/Material/Material.h>
#include <Engine/Mesh/SkeletalMesh.h>

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
        ComponentPanelRegistrar<SkeletalMeshRenderer, SkeletalMeshRendererPanel> gRegistrar;

        // undo/redo 的 lambda 要活到很久以后才可能被调用, 不能捕获 TRef<资产> 这类"当前解析
        // 结果"——只捕获 Guid, 每次真正需要用的时候(包括这里第一次赋值)才现查 AssetManager,
        // 跟 StaticMeshRendererPanel 里同名函数是同一个道理
        TRef<SkeletalMesh> resolveMeshAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<SkeletalMesh>() : TRef<SkeletalMesh>();
        }

        TRef<Material> resolveMaterialAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<Material>() : TRef<Material>();
        }

        TRef<Animation> resolveAnimationAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<Animation>() : TRef<Animation>();
        }
    }

    SkeletalMeshRendererPanel::SkeletalMeshRendererPanel(SkeletalMeshRenderer& meshRenderer) noexcept :
        IComponentPanel(meshRenderer),
        mMeshRenderer(meshRenderer)
    {
    }

    void SkeletalMeshRendererPanel::onDraw()
    {
        AssetManager& assetManager = Context::get().getAssetManager();
        // undo/redo 闭包要活到很久以后才可能被调用, 这期间这个组件(甚至它挂着的 Entity)
        // 完全可能已经被删除又被 undo 恢复成新地址的对象——不能直接捕获 &mMeshRenderer,
        // ComponentRef 只存 Scene*/Entity Guid/Component Guid 这些稳定身份, 每次现查一遍。
        ComponentRef meshRendererRef = ComponentRef::capture(mMeshRenderer);

        auto pushReset = [&](HString name, auto setter, const auto& before, const auto& after)
        {
            getUndoStack().push(name,
                [setter, before]() mutable { setter(before); },
                [setter, after]() mutable { setter(after); });
        };

        if (BeginPropertyTable("SkeletalMeshRendererProperties", ImGuiTableFlags_BordersInnerH))
        {
            BeginPropertyRow("Mesh", 2.0f); // AssetCombo 是两行缩略图布局
            SkeletalMesh* mesh = mMeshRenderer.getMesh();
            Guid meshGuidBefore = mesh ? mesh->getGuid() : Guid();
            Guid meshGuid = meshGuidBefore;
            bool meshChanged = AssetComboEd("##Mesh", assetManager, SkeletalMesh::staticClass(), false, meshGuid, getAssetBrowser(), getIconProvider());
            // SkeletalMeshRenderer 没有"默认网格"这个概念, 空引用就是默认值, 跟
            // StaticMeshRendererPanel 的 Mesh 行同一个道理
            meshChanged |= DrawPropertyReset("Mesh", meshGuid, Guid());
            if (meshChanged)
            {
                // setMesh() 走的是共享的 MeshRenderer::onMeshChanged(), 会无条件清空
                // mOverrideMaterials——所以撤销"换 Mesh"要在真正切换前把当前每个槽位的覆盖材质
                // Guid 存一份快照, undo 时先切回旧 Mesh 再逐槽塞回去, 见 StaticMeshRendererPanel
                // 里同一段逻辑的说明
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
                        SkeletalMeshRenderer* meshRenderer = ref.resolveAs<SkeletalMeshRenderer>();
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
                        ref.resolveAs<SkeletalMeshRenderer>()->setMesh(resolveMeshAsset(Context::get().getAssetManager(), guid).get());
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

                // 材质槽没有被覆盖时的"默认值"是网格自己在这个槽位上原本指定的材质, 不是空引用,
                // 跟 StaticMeshRendererPanel 的 Material 行同一个道理
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
                            ref.resolveAs<SkeletalMeshRenderer>()->setOverrideMaterial(index, resolveMaterialAsset(Context::get().getAssetManager(), guid).get());
                        },
                        [ref = meshRendererRef, index = i, guid = overrideGuidAfter]()
                        {
                            ref.resolveAs<SkeletalMeshRenderer>()->setOverrideMaterial(index, resolveMaterialAsset(Context::get().getAssetManager(), guid).get());
                        });
                }
                ImGui::PopID();
            }

            auto& defaultRenderer = *static_cast<const SkeletalMeshRenderer*>(SkeletalMeshRenderer::staticClass()->getDefaultObject());

            BeginPropertyRow("Sort Order");
            int sortOrderBefore = mMeshRenderer.getSortOrder();
            int sortOrder = sortOrderBefore;
            bool sortOrderChanged = ImGui::DragInt("##SortOrder", &sortOrder, 1.0f, -128, 127);
            auto sortOrderSetter = [ref = meshRendererRef](int v) { ref.resolveAs<SkeletalMeshRenderer>()->setSortOrder(static_cast<int8_t>(v)); };
            getUndoStack().trackGesture("Change Sort Order"_hs, sortOrderBefore, sortOrder, sortOrderSetter);
            if (sortOrderChanged)
                mMeshRenderer.setSortOrder(static_cast<int8_t>(sortOrder));
            int sortOrderBeforeReset = sortOrder;
            if (DrawPropertyReset("Sort Order", sortOrder, static_cast<int>(defaultRenderer.getSortOrder())))
            {
                mMeshRenderer.setSortOrder(static_cast<int8_t>(sortOrder));
                pushReset("Reset Sort Order"_hs, sortOrderSetter, sortOrderBeforeReset, sortOrder);
            }

            BeginPropertyRow("Cast Shadow");
            bool castShadowBefore = mMeshRenderer.isCastShadow();
            bool castShadow = castShadowBefore;
            bool castShadowChanged = ImGui::Checkbox("##CastShadow", &castShadow);
            auto castShadowSetter = [ref = meshRendererRef](bool v) { ref.resolveAs<SkeletalMeshRenderer>()->setCastShadow(v); };
            getUndoStack().trackGesture("Change Cast Shadow"_hs, castShadowBefore, castShadow, castShadowSetter);
            if (castShadowChanged)
                mMeshRenderer.setCastShadow(castShadow);
            bool castShadowBeforeReset = castShadow;
            if (DrawPropertyReset("Cast Shadow", castShadow, defaultRenderer.isCastShadow()))
            {
                mMeshRenderer.setCastShadow(castShadow);
                pushReset("Reset Cast Shadow"_hs, castShadowSetter, castShadowBeforeReset, castShadow);
            }

            BeginPropertyRow("Animation", 2.0f); // AssetCombo 是两行缩略图布局
            Animation* animation = mMeshRenderer.getAnimation();
            Guid animationGuidBefore = animation ? animation->getGuid() : Guid();
            Guid animationGuid = animationGuidBefore;
            // 用 Animation 这个基类过滤(includeDerived=true)——以后 BlendSpace 之类的子类资产
            // 会自动一起出现在下拉里, 不需要这个面板跟着改
            bool animationChanged = AssetComboEd("##Animation", assetManager, Animation::staticClass(), true, animationGuid, getAssetBrowser(), getIconProvider());
            animationChanged |= DrawPropertyReset("Animation", animationGuid, Guid());
            if (animationChanged)
            {
                // setAnimation() 内部已经会把播放时间归零, 这里不需要另外处理
                mMeshRenderer.setAnimation(resolveAnimationAsset(assetManager, animationGuid).get());

                getUndoStack().push("Change Animation"_hs,
                    [ref = meshRendererRef, guid = animationGuidBefore]()
                    {
                        ref.resolveAs<SkeletalMeshRenderer>()->setAnimation(resolveAnimationAsset(Context::get().getAssetManager(), guid).get());
                    },
                    [ref = meshRendererRef, guid = animationGuid]()
                    {
                        ref.resolveAs<SkeletalMeshRenderer>()->setAnimation(resolveAnimationAsset(Context::get().getAssetManager(), guid).get());
                    });
            }

            // 没有 Animation 时 Playing/Playback Speed 不影响任何东西(onTick() 直接用 bind
            // pose), 灰化提示这两项当前不生效, 而不是照常可编辑却看不出效果
            ImGui::BeginDisabled(mMeshRenderer.getAnimation() == nullptr);

            BeginPropertyRow("Playing");
            bool playingBefore = mMeshRenderer.isPlaying();
            bool playing = playingBefore;
            bool playingChanged = ImGui::Checkbox("##Playing", &playing);
            auto playingSetter = [ref = meshRendererRef](bool v) { ref.resolveAs<SkeletalMeshRenderer>()->setPlaying(v); };
            getUndoStack().trackGesture("Change Playing"_hs, playingBefore, playing, playingSetter);
            if (playingChanged)
                mMeshRenderer.setPlaying(playing);
            bool playingBeforeReset = playing;
            if (DrawPropertyReset("Playing", playing, defaultRenderer.isPlaying()))
            {
                mMeshRenderer.setPlaying(playing);
                pushReset("Reset Playing"_hs, playingSetter, playingBeforeReset, playing);
            }

            BeginPropertyRow("Playback Speed");
            float speedBefore = mMeshRenderer.getPlaybackSpeed();
            float speed = speedBefore;
            bool speedChanged = ImGui::DragFloat("##PlaybackSpeed", &speed, 0.01f, 0.0f, 0.0f, "%.2f");
            auto speedSetter = [ref = meshRendererRef](float v) { ref.resolveAs<SkeletalMeshRenderer>()->setPlaybackSpeed(v); };
            getUndoStack().trackGesture("Change Playback Speed"_hs, speedBefore, speed, speedSetter);
            if (speedChanged)
                mMeshRenderer.setPlaybackSpeed(speed);
            float speedBeforeReset = speed;
            if (DrawPropertyReset("Playback Speed", speed, defaultRenderer.getPlaybackSpeed()))
            {
                mMeshRenderer.setPlaybackSpeed(speed);
                pushReset("Reset Playback Speed"_hs, speedSetter, speedBeforeReset, speed);
            }

            ImGui::EndDisabled();

            EndPropertyTable();
        }
    }
}
