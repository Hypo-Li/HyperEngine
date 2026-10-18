#include "PrefabTestWindow.h"
#include "Panel/ComponentPanelRegistry.h"

#include <Engine/Framework/Scene.h>

#include <HypImGui/IconsMaterialSymbols.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        constexpr char kScalarFormat[] = "%.3f";
    }

    PrefabTestWindow::PrefabTestWindow(Engine& engine, EditorSelection& selection, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        EditorWindow("Prefab Test", "PrefabTestWindow"),
        mEngine(engine),
        mSelection(selection),
        mIconProvider(iconProvider),
        mAssetBrowser(assetBrowser)
    {
        setIcon(ICON_MS_PACKAGE_2);
    }

    void PrefabTestWindow::onDraw()
    {
        if (!mTestPrefab)
        {
            Entity* selected = mSelection.getSingleSelectedEntity();
            ImGui::TextWrapped("Select one Entity in Hierarchy, then create a Prefab template from it.");
            ImGui::BeginDisabled(!selected);
            if (ImGui::Button("Create Prefab From Selection"))
                mTestPrefab = Prefab::createFromEntity(selected);
            ImGui::EndDisabled();
            return;
        }

        ImGui::Text("Template entity count: %zu", mTestPrefab->getEntities().size());

        Scene* scene = mEngine.getScene();
        ImGui::BeginDisabled(!scene);
        if (ImGui::Button("Instantiate Into Scene"))
        {
            Entity* instanceRoot = mTestPrefab->instantiate(scene);
            mSelection.selectOnly(instanceRoot->getGuid());
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Discard Template"))
        {
            mTestPrefab = nullptr;
            mSelectedTemplateEntity = nullptr;
            mComponentPanels.clear();
            return;
        }

        ImGui::Separator();
        drawTemplateEntityList();

        if (mSelectedTemplateEntity)
        {
            ImGui::Separator();
            drawSelectedEntityEditor();
        }

        ImGui::Separator();
        if (ImGui::Button("Apply Changes To Instances"))
            mTestPrefab->apply();
        ImGui::SameLine();
        ImGui::Text("(Version %u, Live Instances %zu)", mTestPrefab->getVersion(), mTestPrefab->getLiveInstances().size());
    }

    void PrefabTestWindow::drawTemplateEntityList()
    {
        ImGui::TextUnformatted("Template Entities:");
        for (const TRef<Entity>& templateEntity : mTestPrefab->getEntities())
        {
            std::string label(templateEntity->getName().toStringView());
            if (templateEntity.get() == mTestPrefab->getRootEntity())
                label += " (Root)";

            bool isSelected = mSelectedTemplateEntity == templateEntity.get();
            if (ImGui::Selectable(label.c_str(), isSelected))
                mSelectedTemplateEntity = templateEntity.get();
        }
    }

    void PrefabTestWindow::drawSelectedEntityEditor()
    {
        std::string editingLabel(mSelectedTemplateEntity->getName().toStringView());
        ImGui::Text("Editing: %s", editingLabel.c_str());

        Transform& transform = mSelectedTemplateEntity->getTransform();

        double3 position = transform.getLocalPosition();
        if (ImGui::DragScalarN("Local Position", ImGuiDataType_Double, &position.x, 3, 0.05f, nullptr, nullptr, kScalarFormat))
            transform.setLocalPosition(position);

        float3 eulerRotation = transform.getLocalRotationEuler();
        if (ImGui::DragFloat3("Local Rotation", &eulerRotation.x, 0.25f, 0.0f, 0.0f, kScalarFormat))
            transform.setLocalRotationEuler(eulerRotation);

        float3 scale = transform.getLocalScale();
        if (ImGui::DragFloat3("Local Scale", &scale.x, 0.01f, 0.0f, 0.0f, kScalarFormat))
            transform.setLocalScale(scale);

        // 根节点没有模板内部的 parent 可选(总体原则: 单根), 不画重新挂接的按钮组——但
        // Components 区块跟根不根节点无关, 不能被这条判断连带跳过。
        if (mSelectedTemplateEntity != mTestPrefab->getRootEntity())
        {
            ImGui::TextUnformatted("Reparent To:");
            for (const TRef<Entity>& candidate : mTestPrefab->getEntities())
            {
                if (candidate.get() == mSelectedTemplateEntity)
                    continue;

                ImGui::PushID(candidate.get());
                std::string candidateLabel(candidate->getName().toStringView());
                if (ImGui::Button(candidateLabel.c_str()))
                    mSelectedTemplateEntity->setParent(candidate.get(), ReparentMode::Keep_Local);
                ImGui::PopID();
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Components:");
        refreshComponentPanels();
        for (auto& panel : mComponentPanels)
            panel->draw();
    }

    void PrefabTestWindow::refreshComponentPanels()
    {
        std::span<const TRef<Component>> liveComponents = mSelectedTemplateEntity->getAllComponents();

        bool stillValid = liveComponents.size() == mComponentPanels.size();
        if (stillValid)
        {
            for (size_t i = 0; i < liveComponents.size(); ++i)
            {
                if (liveComponents[i].get() != &mComponentPanels[i]->getComponent())
                {
                    stillValid = false;
                    break;
                }
            }
        }
        if (stillValid)
            return;

        mComponentPanels.clear();
        mComponentPanels.reserve(liveComponents.size());
        for (const TRef<Component>& componentRef : liveComponents)
            mComponentPanels.push_back(ComponentPanelRegistry::get().createPanel(*componentRef.get(), mUndoStack, mIconProvider, mAssetBrowser));
    }
}
