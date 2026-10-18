#include "Inspector.h"
#include "AssetBrowser.h"
#include "ComponentRef.h"
#include "Panel/ComponentPanelRegistry.h"
#include "Panel/TransformPanel.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Core/Reflection/Class.h>
#include <Core/Serialization/BinaryLoader.h>
#include <Core/Serialization/BinarySaver.h>
#include <Core/Serialization/SerializeContext.h>
#include <Core/Serialization/Serializer.h>
#include <Engine/Context.h>
#include <Engine/Framework/Entity.h>
#include <Engine/Framework/Scene.h>

#include <HypImGui/IconsMaterialSymbols.h>
#include <HypImGui/ImGuiClassCombo.h>
#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        // Add/Remove Component 共用的快照: 序列化出的组件数据 + 足够重新定位它属于哪个
        // Entity 的身份信息(ComponentRef)。undo/redo 的闭包要活到很久以后才可能被调用,
        // 这期间对应的 Component、甚至 Entity 完全可能已经被删除、又被 undo 恢复成新地址的
        // 对象——所以只捕获这份快照 + ComponentRef, 不捕获 Component&/Entity*, 真正要用的
        // 时候现查一遍(见 attachComponentFromSnapshot()/detachComponentByRef())。
        struct ComponentSnapshot
        {
            ComponentRef ref;
            size_t componentIndex;
            std::vector<uint8_t> bodyData;
            std::vector<AssetImportEntry> importEntries;
            std::vector<AssetExportEntry> exportEntries;
            std::vector<uint8_t> bulkSection;
        };

        ComponentSnapshot captureComponentSnapshot(Component& component)
        {
            ComponentRef ref = ComponentRef::capture(component);

            // rootGuid 传 component 自己的 Guid: 它自己会被归到 export(身体正常写进去); 它的
            // owner(Entity)不是它那棵树的一部分, 会被归到 import——不是真正的 Asset, 不查
            // AssetManager, 直接把活的 Entity 喂给 registerKnownObject()。其它引用(Mesh/
            // Material 这类)才是真正的外部 Asset, 交给 AssetManager 解析(见
            // attachComponentFromSnapshot())。
            SerializeContext saveContext(false, ref.componentGuid);
            BinarySaver saver;
            Serializer saveSerializer(saver, saveContext);
            Object* rootObject = &component;
            saveSerializer.serializeObject(rootObject);

            return ComponentSnapshot{
                ref,
                component.getEntity()->getComponentIndex(&component),
                saver.pack(),
                saveContext.getImportEntries(),
                saveContext.getExportEntries(),
                saveContext.getBulkSection(),
            };
        }

        // 把快照重新变成一个真正的 Component 对象, 挂回它当初所属的 Entity——Guid、属性都
        // 跟捕获快照那一刻完全一致(不管是 Remove 的 undo 还是 Add 的 redo, 都是"重新变出
        // 跟原来一模一样的那个组件"这同一件事)。
        void attachComponentFromSnapshot(const ComponentSnapshot& snapshot)
        {
            Entity* entity = snapshot.ref.resolveEntity();

            AssetManager& assetManager = Context::get().getAssetManager();
            SerializeContext loadContext(true, snapshot.ref.componentGuid);
            loadContext.setImportEntries(snapshot.importEntries);
            loadContext.setExportEntries(snapshot.exportEntries);
            loadContext.setBulkSection(snapshot.bulkSection);
            for (const AssetImportEntry& importEntry : snapshot.importEntries)
            {
                if (importEntry.guid == snapshot.ref.entityRef.entityGuid)
                {
                    loadContext.registerKnownObject(importEntry.guid, entity);
                    continue;
                }
                // 这个引用指向的资产可能在组件被删除之后又被删掉了(资产删除现在还没有
                // 自己的 undo 记录)——查不到就注册一个 nullptr, 让这个引用退化成空引用,
                // 跟 Mesh/Material combo 本来就允许留空是同一个语义, 不能假设它一定还在、
                // 断言崩掉。
                Asset* asset = assetManager.getAsset(importEntry.guid);
                if (!asset)
                    LOG_WARN("Component undo/redo: referenced asset {} no longer exists, reference will be empty", importEntry.guid.toString());
                loadContext.registerKnownObject(importEntry.guid, asset ? asset->getOrLoadRootObject().get() : nullptr);
            }

            BinaryLoader loader;
            loader.unpack(snapshot.bodyData);
            Serializer loadSerializer(loader, loadContext);
            Object* restoredObject = nullptr;
            loadSerializer.serializeObject(restoredObject);
            Component* restoredComponent = static_cast<Component*>(restoredObject);
            entity->attachComponent(restoredComponent);
            entity->moveComponent(restoredComponent, snapshot.componentIndex);
        }

        // 按 ref 现查一遍活的 Component 再删——不能对着 undo/redo 恢复出来的新地址对象留
        // 一份旧指针。
        void detachComponentByRef(const ComponentRef& ref)
        {
            Component* component = ref.resolve();
            component->getEntity()->removeComponent(component);
        }
    }

    Inspector::Inspector(EditorSelection& selection, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        EditorWindow("Inspector", "Inspector"),
        mSelection(selection),
        mUndoStack(undoStack),
        mIconProvider(iconProvider),
        mAssetBrowser(assetBrowser)
    {
        setIcon(ICON_MS_TUNE);
    }

    void Inspector::onDraw()
    {
        size_t selectedCount = mSelection.getSelectedCount();
        if (selectedCount == 0 || selectedCount > 1)
        {
            mComponentPanels.clear();
            mTransformPanel.reset();
            mSelectedAddComponentClass = nullptr;
            if (selectedCount == 0)
                ImGui::TextDisabled("No Entity Selected");
            else
                ImGui::Text("%zu Entities Selected", selectedCount);
            return;
        }

        Entity* entity = mSelection.getSingleSelectedEntity();
        ImGui::SeparatorText("Entity");
        if (BeginPropertyTable("EntityProperties"))
        {
            BeginPropertyRow("Name");
            std::string_view name = entity->getName().toStringView();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name.data(), name.data() + name.size());
            EndPropertyTable();
        }

        ImGui::Spacing();

        Transform& transform = entity->getTransform();
        if (!mTransformPanel || &transform != &mTransformPanel->getTransform())
        {
            mTransformPanel = std::make_unique<TransformPanel>(transform, mTransformSpace, mScaleLocked, mUndoStack, *this);
        }
        mTransformPanel->draw();

        refreshComponentPanels(*entity);

        Component* componentToRemove = nullptr;
        IComponentPanel* reorderedPanel = nullptr;
        for (auto& panel : mComponentPanels)
        {
            const InspectorPanelAction action = panel->draw();
            if (action == InspectorPanelAction::Remove)
                componentToRemove = &panel->getComponent();
            else if (action == InspectorPanelAction::Reorder)
                reorderedPanel = panel.get();
        }

        // Removing after drawing avoids invalidating the component span mid-iteration.
        // mComponentPanels 本身会在下一帧 refreshComponentPanels() 里发现列表变短、自动重建。
        if (componentToRemove)
        {
            removeComponentWithUndo(*entity, componentToRemove);
        }
        else if (reorderedPanel)
        {
            const PanelReorderRequest request = reorderedPanel->takeReorderRequest();
            for (const TRef<Component>& component : entity->getAllComponents())
            {
                if (component->getGuid() == request.draggedGuid)
                {
                    moveComponentWithUndo(*component, reorderedPanel->getComponent(), request.insertAfter);
                    break;
                }
            }
        }

        ImGui::Spacing();

        float buttonWidth = ImGui::CalcTextSize("Add Component").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth);
        ClassCombo("##AddComponentType", Component::staticClass(), &mSelectedAddComponentClass);

        ImGui::SameLine();
        ImGui::BeginDisabled(mSelectedAddComponentClass == nullptr);
        if (ImGui::Button("Add Component"))
        {
            addComponentWithUndo(*entity, mSelectedAddComponentClass);
            mSelectedAddComponentClass = nullptr;
        }
        ImGui::EndDisabled();
    }

    void Inspector::removeComponentWithUndo(Entity& entity, Component* component)
    {
        ComponentSnapshot snapshot = captureComponentSnapshot(*component);
        entity.removeComponent(component);

        mUndoStack.push("Remove Component"_hs,
            [snapshot]() { attachComponentFromSnapshot(snapshot); },
            [ref = snapshot.ref]() { detachComponentByRef(ref); });
    }

    void Inspector::addComponentWithUndo(Entity& entity, Class* componentClass)
    {
        Component* component = entity.addComponent(componentClass);
        ComponentSnapshot snapshot = captureComponentSnapshot(*component);

        mUndoStack.push("Add Component"_hs,
            [ref = snapshot.ref]() { detachComponentByRef(ref); },
            [snapshot]() { attachComponentFromSnapshot(snapshot); });
    }

    void Inspector::moveComponentWithUndo(Component& dragged, Component& target, bool insertAfter)
    {
        Entity& entity = *dragged.getEntity();
        const size_t oldIndex = entity.getComponentIndex(&dragged);
        size_t newIndex = entity.getComponentIndex(&target) + (insertAfter ? 1 : 0);
        // moveComponent 的 index 是摘掉自己之后的最终位置
        if (oldIndex < newIndex)
            --newIndex;
        if (newIndex == oldIndex)
            return;

        entity.moveComponent(&dragged, newIndex);

        ComponentRef ref = ComponentRef::capture(dragged);
        mUndoStack.push("Move Component"_hs,
            [ref, oldIndex]() { ref.resolveEntity()->moveComponent(ref.resolve(), oldIndex); },
            [ref, newIndex]() { ref.resolveEntity()->moveComponent(ref.resolve(), newIndex); });
    }

    void Inspector::refreshComponentPanels(Entity& entity)
    {
        std::span<const TRef<Component>> liveComponents = entity.getAllComponents();

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
        {
            mComponentPanels.push_back(ComponentPanelRegistry::get().createPanel(*componentRef.get(), mUndoStack, mIconProvider, mAssetBrowser));
            mComponentPanels.back()->setReorderEnabled(true);
        }
    }
}
