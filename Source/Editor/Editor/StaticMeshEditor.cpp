#include "StaticMeshEditor.h"
#include "StaticMeshEditorTab.h"
#include "AssetComboEd.h"
#include "AssetEditorRegistry.h"
#include "UndoStack.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Engine/Component/Light/DirectionalLight.h>
#include <Engine/Component/Light/SkyLight.h>
#include <Engine/Component/Renderer/StaticMeshRenderer.h>
#include <Engine/Context.h>
#include <Engine/Framework/Entity.h>
#include <Engine/Material/Material.h>
#include <Engine/Texture/TextureCube.h>

#include <HypImGui/ImGuiPropertyWidgets.h>

#include <glm/glm.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace hyp::ed
{
    namespace
    {
        AssetEditorRegistrar<StaticMesh, StaticMeshEditorTab> gRegistrar;

        constexpr const char* kPreviewEnvironmentMapPath = "Engine/Textures/HDRI_Paul_Lobe_Haus";
        constexpr float kDetailsWidth = 320.0f;

        HString makeViewportName(Asset& asset)
        {
            std::string name = "StaticMeshEditor.Viewport." + asset.getRootGuid().toString();
            return HString(std::string_view(name));
        }
    }

    StaticMeshEditor::StaticMeshEditor(Asset& asset, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        AssetEditorWindow(asset, undoStack),
        mIconProvider(iconProvider),
        mAssetBrowser(assetBrowser),
        mMesh(asset.getOrLoadRootObject<StaticMesh>()),
        mViewport(makeViewportName(asset))
    {
        setWindowPadding(ImVec2(0.0f, 0.0f));

        if (!mMesh)
            return;

        buildPreviewScene();
        frameMesh();
    }

    void StaticMeshEditor::buildPreviewScene()
    {
        mPreviewScene = createObject<Scene>(nullptr);

        Entity* meshEntity = mPreviewScene->createEntity("Mesh");
        meshEntity->addComponent<StaticMeshRenderer>()->setMesh(mMesh.get());

        Entity* lightEntity = mPreviewScene->createEntity("Light");
        lightEntity->getTransform().setWorldRotationEuler(float3(35.0f, 0.0f, 35.0f));
        lightEntity->addComponent<DirectionalLight>();

        Entity* skyLightEntity = mPreviewScene->createEntity("SkyLight");
        SkyLight* skyLight = skyLightEntity->addComponent<SkyLight>();
        Asset* environmentMapAsset = Context::get().getAssetManager().getAsset(kPreviewEnvironmentMapPath);
        if (environmentMapAsset && environmentMapAsset->getRootClass() == TextureCube::staticClass())
        {
            skyLight->setLightSource(SkyLightSource::Environment_Map);
            skyLight->setEnvironmentMap(environmentMapAsset->getOrLoadRootObject<TextureCube>().get());
        }
    }

    void StaticMeshEditor::frameMesh()
    {
        const Boxf& bounds = mMesh->getOrUpdateBoundingBox();
        float3 center = bounds.getCenter();
        float radius = std::max(glm::length(bounds.getExtent()), 0.01f);

        EditorCamera& camera = mViewport.getCamera();
        float halfFovRadians = glm::radians(camera.getFov()) * 0.5f;
        float distance = radius / std::sin(halfFovRadians) * 1.2f;

        camera.setOrientation(float3(0.0f, 1.0f, 0.0f), float3(0.0f, 0.0f, 1.0f));
        camera.setPosition(double3(center.x, center.y - distance, center.z));
    }

    void StaticMeshEditor::onDraw()
    {
        if (!mMesh)
        {
            ImGui::TextDisabled("Static mesh failed to load.");
            return;
        }

        ImVec2 availSize = ImGui::GetContentRegionAvail();
        if (availSize.x <= 0.0f || availSize.y <= 0.0f)
            return;

        float viewportWidth = std::max(availSize.x - kDetailsWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f);

        ImGui::BeginChild("##Viewport", ImVec2(viewportWidth, availSize.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        drawViewport();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##Details", ImVec2(kDetailsWidth, availSize.y), ImGuiChildFlags_Borders);
        drawDetails();
        ImGui::EndChild();
    }

    void StaticMeshEditor::drawViewport()
    {
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        if (availSize.x <= 0.0f || availSize.y <= 0.0f)
            return;

        float deltaTime = ImGui::GetIO().DeltaTime;
        mViewport.draw(mPreviewScene.get(), availSize, deltaTime);
        mPreviewScene->tick(deltaTime);
    }

    void StaticMeshEditor::drawDetails()
    {
        drawMaterialSlots();
        ImGui::Spacing();
        drawSave();
    }

    void StaticMeshEditor::drawMaterialSlots()
    {
        ImGui::SeparatorText("Materials");

        uint32_t slotCount = mMesh->getMaterialCount();
        if (slotCount == 0)
        {
            ImGui::TextDisabled("No material slots.");
            return;
        }

        AssetManager& assetManager = Context::get().getAssetManager();
        if (!BeginPropertyTable("StaticMeshMaterialSlots", ImGuiTableFlags_BordersInnerH))
            return;

        for (uint32_t i = 0; i < slotCount; ++i)
        {
            std::string label = "Material " + std::to_string(i);
            BeginPropertyRow(label.c_str(), 2.0f);

            Material* material = mMesh->getMaterial(i);
            Guid guidBefore = material ? material->getGuid() : Guid();
            Guid guid = guidBefore;

            ImGui::PushID(static_cast<int>(i));
            bool changed = AssetComboEd("##Material", assetManager, Material::staticClass(), true, guid, mAssetBrowser, mIconProvider);
            ImGui::PopID();

            if (changed && guid.isValid() && !(guid == guidBefore))
            {
                applyMaterialSlot(i, guid);
                undoStack().push("Change Mesh Material"_hs,
                    [this, i, guidBefore]() { applyMaterialSlot(i, guidBefore); },
                    [this, i, guid]() { applyMaterialSlot(i, guid); });
            }
        }

        EndPropertyTable();
    }

    void StaticMeshEditor::drawSave()
    {
        ImGui::BeginDisabled(!getAsset().isDirty());
        if (ImGui::Button("Save"))
            getAsset().save();
        ImGui::EndDisabled();
    }

    void StaticMeshEditor::applyMaterialSlot(uint32_t slotIndex, Guid materialGuid)
    {
        Asset* materialAsset = materialGuid.isValid() ? Context::get().getAssetManager().getAsset(materialGuid) : nullptr;
        TRef<Material> material = materialAsset ? materialAsset->getOrLoadRootObject<Material>() : TRef<Material>();
        if (!material)
            return;

        mMesh->setMaterialSlot(slotIndex, material.get());
        getAsset().setDirty(true);
    }
}
