#include "Hierarchy.h"
#include "UndoStack.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Core/Serialization/BinaryLoader.h>
#include <Core/Serialization/BinarySaver.h>
#include <Core/Serialization/SerializeContext.h>
#include <Core/Serialization/Serializer.h>
#include <Engine/Component/Camera/Camera.h>
#include <Engine/Component/Collider/Collider.h>
#include <Engine/Component/Environment/Atmosphere.h>
#include <Engine/Component/Environment/SkyBackground.h>
#include <Engine/Component/Joint/Joint.h>
#include <Engine/Component/Light/DirectionalLight.h>
#include <Engine/Component/Light/PointLight.h>
#include <Engine/Component/Light/SkyLight.h>
#include <Engine/Component/Light/SpotLight.h>
#include <Engine/Component/PrefabInstance.h>
#include <Engine/Component/Renderer/GSplatRenderer.h>
#include <Engine/Component/Renderer/SkeletalMeshRenderer.h>
#include <Engine/Component/Renderer/SpriteRenderer.h>
#include <Engine/Component/Renderer/StaticMeshRenderer.h>
#include <Engine/Component/Renderer/TextRenderer.h>
#include <Engine/Context.h>

#include <HypImGui/IconsMaterialSymbols.h>
#include <HypImGui/ImGuiFrontend.h>
#include <HypImGui/ImGuiWidgets.h>

#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>

namespace hyp::ed
{
    namespace
    {
        // ImGui 默认的 IndentSpacing(21px)对着 Entity 层级这种可能嵌套好几层的树来说偏宽,
        // 缩进几层之后名字就被挤到很靠右了。只在这个窗口的树绘制期间局部覆盖, 不改全局 style,
        // 避免影响其它地方(比如某些 ComponentPanel 里如果也用了 TreeNode)的缩进。96 DPI 下的
        // 设计基准值, 用到的地方要乘 ImGuiFrontend::getDpiScale()。
        constexpr float kTreeIndentSpacingBase = 12.0f;

        // 图标字体烘焙到 24px(见 ImGuiFrontend.cpp), 比正文的 16px 基准字体高——MergeMode
        // 不会把这个 ImFont 的行高跟着改大(图标字形借用正文字体自己的行高空间画), 默认行高
        // 按普通文本算的话图标会被上下裁切。行高单独给够图标的空间, 96 DPI 下的设计基准值。
        constexpr float kRowHeightBase = 20.0f;

        // 整行属于某个 Prefab 实例子树(根节点自己 + 它下面所有子孙)时统一用这个颜色染名字
        // 文字——只有根节点额外挂图标, 子孙节点靠这个颜色跟普通 Entity 区分开, 见
        // Hierarchy::drawEntityNode()。
        constexpr ImVec4 kPrefabTintColor = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);

        // drag/drop payload 只当类型标记用, 不传真正的数据(见 Hierarchy::beginRowDrag), 所以传
        // nullptr/0 字节就够。
        constexpr const char* kRowsDragDropType = "HIERARCHY_ROWS";

        constexpr size_t kAppendIndex = std::numeric_limits<size_t>::max();

        // 一行里上下各占这个比例的高度算"插到它之前/之后", 中间剩下的部分算"挂成它的子节点"。
        constexpr float kDropEdgeFraction = 0.25f;

        enum class DropZone { Before, Onto, After };

        DropZone classifyDropZone(const ImVec2& nodeMin, const ImVec2& nodeMax)
        {
            const float edgeHeight = (nodeMax.y - nodeMin.y) * kDropEdgeFraction;
            const float mouseY = ImGui::GetMousePos().y;
            if (mouseY < nodeMin.y + edgeHeight)
                return DropZone::Before;
            if (mouseY > nodeMax.y - edgeHeight)
                return DropZone::After;
            return DropZone::Onto;
        }

        struct AfterEdgeLevel
        {
            bool firstChild;
            int levelsUp;
        };

        // 下沿落点按鼠标横向位置选最近的一级: 每一级的指示线起点相隔一个缩进步长, 鼠标过了两级中点就切换。
        // contentX 是这一行图标的起点, 也是"同级之后"这一级的线起点。
        AfterEdgeLevel classifyAfterEdge(float contentX, bool canBeFirstChild, int maxLevelsUp)
        {
            const float offsetInLevels = (ImGui::GetMousePos().x - contentX) / ImGui::GetStyle().IndentSpacing;
            if (canBeFirstChild && offsetInLevels >= 0.5f)
                return { true, 0 };
            const int levelsUp = static_cast<int>(std::round(-offsetInLevels));
            return { false, std::clamp(levelsUp, 0, maxLevelsUp) };
        }

        bool isLastChildEntity(Entity* entity)
        {
            Entity* parent = entity->getParent();
            return parent && parent->getChild(parent->getChildCount() - 1) == entity;
        }

        // Folder 先列子 Folder 再列成员 Entity, 所以只有没有成员时最后一个子 Folder 才是父级的最后一行
        bool isLastRowInParentFolder(const Scene& scene, HString folderName)
        {
            const Scene::Folder* parent = scene.findFolder(scene.getParentFolder(folderName));
            return parent && parent->entities.empty() && parent->childFolders.back() == folderName;
        }

        constexpr float kDropIndicatorThicknessBase = 2.0f;

        // 按 Entity 身上优先级最高的组件类型选图标, 跟组件的添加顺序无关; 没有任何可识别
        // 组件的(纯层级节点、只有 PrefabInstance 的)返回占位图标, 保证每一行的图标对齐。
        const char* getEntityTypeIcon(const Entity* entity)
        {
            struct Entry
            {
                Class* clazz;
                const char* icon;
                int priority;
            };
            static const std::vector<Entry> entries = {
                { Camera::staticClass(), ICON_MS_VIDEOCAM, 100},
                { DirectionalLight::staticClass(), ICON_MS_SUNNY, 90 },
                { PointLight::staticClass(), ICON_MS_LIGHTBULB, 90 },
                { SpotLight::staticClass(), ICON_MS_FLASHLIGHT_ON, 90 },
                { SkyLight::staticClass(), ICON_MS_WB_TWILIGHT, 90 },
                { StaticMeshRenderer::staticClass(), ICON_MS_DEPLOYED_CODE, 80 },
                { SkeletalMeshRenderer::staticClass(), ICON_MS_SKULL, 80 },
                { GSplatRenderer::staticClass(), ICON_MS_SCATTER_PLOT, 80 },
                { SpriteRenderer::staticClass(), ICON_MS_IMAGE, 80 },
                { TextRenderer::staticClass(), ICON_MS_TEXT_FIELDS, 80 },
                { Atmosphere::staticClass(), ICON_MS_SUNNY_SNOWING, 70 },
                { SkyBackground::staticClass(), ICON_MS_VRPANO, 60 },
                { Collider::staticClass(), ICON_MS_CATEGORY, 40 },
                { Joint::staticClass(), ICON_MS_LINK, 30 },
            };

            const char* bestIcon = nullptr;
            int bestPriority = -1;
            for (const TRef<Component>& component : entity->getAllComponents())
            {
                const Class* componentClass = component->getClass();
                for (const Entry& entry : entries)
                {
                    if (entry.priority > bestPriority && componentClass->isDerivedFrom(entry.clazz))
                    {
                        bestIcon = entry.icon;
                        bestPriority = entry.priority;
                    }
                }
            }
            return bestIcon ? bestIcon : ICON_MS_CIRCLE;
        }

        // 前景绘制列表不受 table 当前列的裁剪矩形限制, 这里手动裁到 Hierarchy 窗口范围内。
        // asRect 为真画 min/max 围成的框, 否则画 min 到 max 的一条线。
        void drawDropIndicator(const ImVec2& min, const ImVec2& max, bool asRect)
        {
            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            const ImVec2 windowMin = ImGui::GetWindowPos();
            const ImVec2 windowMax = ImVec2(windowMin.x + ImGui::GetWindowSize().x, windowMin.y + ImGui::GetWindowSize().y);
            const ImU32 dropIndicatorColor = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
            const float thickness = kDropIndicatorThicknessBase * ImGuiFrontend::getDpiScale();
            drawList->PushClipRect(windowMin, windowMax, true);
            if (asRect)
                drawList->AddRect(min, max, dropIndicatorColor, 0.0f, 0, thickness);
            else
                drawList->AddLine(min, max, dropIndicatorColor, thickness);
            drawList->PopClipRect();
        }

        Scene* findScene(Guid sceneGuid)
        {
            // Scene 按 guid 现查, 不直接捕获指针——Play/Stop 会把当前场景整个销毁重建, 捕获的
            // Scene* 到那时候就是悬空指针, 见 ComponentRef.h 的注释。
            Scene* scene = static_cast<Scene*>(Scene::staticClass()->findByGuid(sceneGuid));
            assert(scene);
            return scene;
        }

        struct PrefabNodeMapping
        {
            Guid templateNodeId;
            Class* runtimeClass;
            Guid runtimeGuid;
        };

        struct DeletedEntity
        {
            Guid entityGuid;
            Guid parentEntityGuid;
            size_t siblingIndex;
            HString folderName;
            std::optional<Guid> prefabAncestorGuid;
            std::vector<PrefabNodeMapping> prefabNodeMappings;
            std::vector<Guid> childGuids;
            std::vector<uint8_t> bodyData;
            std::vector<AssetImportEntry> importEntries;
            std::vector<AssetExportEntry> exportEntries;
            std::vector<uint8_t> bulkSection;
        };

        struct DeletedFolder
        {
            HString name;
            HString parentName;
            size_t index;
            std::vector<HString> childFolders;
            std::vector<Guid> memberIds;
        };

        struct DeleteSnapshot
        {
            Guid sceneGuid;
            std::vector<DeletedEntity> entities;
            std::vector<DeletedFolder> folders;
        };

        DeletedEntity captureAndDestroyEntity(Scene& scene, Entity* entity)
        {
            DeletedEntity deleted;
            deleted.entityGuid = entity->getGuid();
            Entity* parentEntity = entity->getParent();
            deleted.parentEntityGuid = parentEntity ? parentEntity->getGuid() : Guid();
            deleted.siblingIndex = entity->getSiblingIndex();
            deleted.folderName = scene.getFolderOfEntity(entity);

            // destroyEntity(entity, false) 会把这些子节点重新挂到 entity 原来的 parent 下面
            // (是 root 就挂回 root)——这是绝大多数 DCC/引擎删除节点的默认行为, 但也意味着
            // "撤销删除"不能只管恢复 entity 自己, 还得把这些子节点重新收回来, 所以要在真正
            // 删除之前先把它们的 Guid 记下来。
            deleted.childGuids.reserve(entity->getChildCount());
            for (size_t i = 0; i < entity->getChildCount(); ++i)
                deleted.childGuids.push_back(entity->getChild(i)->getGuid());

            // entity 自己持有 PrefabInstance 时, 删的是整个实例, 不算"实例内部节点被删",
            // 不需要 removeNode()——onUnregister() 自己会从 Prefab::mLiveInstances 摘除。
            // 只有 entity 是某个祖先实例追踪的节点时, 才需要保持 mNodeMap 和 removeNode()
            // 语义一致——entity 自己的 Components 会跟 entity 一起被销毁, 必须连同 entity
            // 自己一并摘掉, 只摘 entity 那一条会漏掉它身上每个 Component 各自的映射, 那些
            // 才是真正会变悬空的 Object*(entity 自己的映射摘不摘, 效果上都会因为 entity
            // 被销毁而失效, 但 Component 的映射如果不摘, 下次这个 Prefab 实例被序列化时
            // 会把悬空指针当活对象读)。
            Entity* prefabAncestor = nullptr;
            PrefabInstance* prefabInstance = nullptr;
            for (Entity* ancestor = entity->getParent(); ancestor; ancestor = ancestor->getParent())
            {
                if (PrefabInstance* instance = ancestor->getComponent<PrefabInstance>())
                {
                    prefabAncestor = ancestor;
                    prefabInstance = instance;
                    break;
                }
            }

            if (prefabInstance)
            {
                if (std::optional<Guid> templateNodeId = prefabInstance->findTemplateNodeId(entity))
                    deleted.prefabNodeMappings.push_back({ *templateNodeId, entity->getClass(), entity->getGuid() });
                for (const TRef<Component>& component : entity->getAllComponents())
                {
                    if (std::optional<Guid> templateNodeId = prefabInstance->findTemplateNodeId(component.get()))
                        deleted.prefabNodeMappings.push_back({ *templateNodeId, component->getClass(), component->getGuid() });
                }
                if (!deleted.prefabNodeMappings.empty())
                    deleted.prefabAncestorGuid = prefabAncestor->getGuid();
            }

            // rootGuid 传 entity 自己的 Guid: 它自己、它的 Transform、它的 Components 都会被
            // 归到 export(Transform/Components 是 entity 的下级, 身体正常写进去)。owner Scene、
            // (如果有 parent 的话)parent 的 Transform、以及 entity 自己 Transform.Children 里
            // 那些子节点的 Transform, 都不是这棵树的一部分, 会被归到 import——都不是真正的
            // Asset, undo 时直接把这些活对象喂给 registerKnownObject()。其它引用(组件上的
            // Mesh/Material 这类)才是真正的外部 Asset, 交给 AssetManager 解析。
            SerializeContext saveContext(false, deleted.entityGuid);
            BinarySaver saver;
            Serializer saveSerializer(saver, saveContext);
            Object* rootObject = entity;
            saveSerializer.serializeObject(rootObject);

            deleted.bodyData = saver.pack();
            deleted.importEntries = saveContext.getImportEntries();
            deleted.exportEntries = saveContext.getExportEntries();
            deleted.bulkSection = saveContext.getBulkSection();

            for (const PrefabNodeMapping& mapping : deleted.prefabNodeMappings)
                prefabInstance->removeNode(mapping.templateNodeId);

            scene.destroyEntity(entity, false);
            return deleted;
        }

        void restoreDeletedEntity(Scene& scene, const DeletedEntity& deleted)
        {
            Entity* parentEntity = deleted.parentEntityGuid.isValid() ? scene.findEntity(deleted.parentEntityGuid) : nullptr;

            SerializeContext loadContext(true, deleted.entityGuid);
            loadContext.setImportEntries(deleted.importEntries);
            loadContext.setExportEntries(deleted.exportEntries);
            loadContext.setBulkSection(deleted.bulkSection);

            // Scene、parent 的 Transform、以及删除时被顺带塞给 parent 的那些子节点的
            // Transform, 都是调用方(这里)已经知道具体是谁的活对象, 现查现注册, 不需要
            // 等下面那个"剩下的当资产解析"的循环再去反过来匹配它们是谁。
            loadContext.registerKnownObject(scene.getGuid(), &scene);
            if (parentEntity)
                loadContext.registerKnownObject(parentEntity->getTransform().getGuid(), &parentEntity->getTransform());
            for (Guid childGuid : deleted.childGuids)
            {
                if (Entity* child = scene.findEntity(childGuid))
                    loadContext.registerKnownObject(child->getTransform().getGuid(), &child->getTransform());
            }

            AssetManager& assetManager = Context::get().getAssetManager();
            for (const AssetImportEntry& importEntry : deleted.importEntries)
            {
                if (loadContext.isKnownObject(importEntry.guid))
                    continue;
                // 这个引用指向的资产可能在 Entity 被删除之后又被删掉了(资产删除现在还
                // 没有自己的 undo 记录)——查不到就注册一个 nullptr, 让这个引用退化成
                // 空引用, 不能假设它一定还在、断言崩掉。
                Asset* asset = assetManager.getAsset(importEntry.guid);
                if (!asset)
                    LOG_WARN("Delete Entity undo: referenced asset {} no longer exists, reference will be empty", importEntry.guid.toString());
                loadContext.registerKnownObject(importEntry.guid, asset ? asset->getOrLoadRootObject().get() : nullptr);
            }

            BinaryLoader loader;
            loader.unpack(deleted.bodyData);
            Serializer loadSerializer(loader, loadContext);
            Object* restoredObject = nullptr;
            loadSerializer.serializeObject(restoredObject);
            Entity* restoredEntity = static_cast<Entity*>(restoredObject);

            // 挂回原来的 parent、把原来的子节点重新收回来这两半, Transform::finalizeLoad()
            // 在 serializeObject() 内部已经处理好了(Parent/Children 都是正常反射属性,
            // 已经解析成上面注册的这些活对象; finalizeLoad 把 mChildren 里"当时"那份
            // 数据当真、反过来纠正每个子节点自己的 mParent, 见 Transform.cpp 的注释),
            // 这里不需要再手动 setParent 一次。
            scene.registerEntity(restoredEntity);
            scene.moveEntityToFolder(restoredEntity, deleted.folderName);
            restoredEntity->setSiblingIndex(deleted.siblingIndex);

            if (!deleted.prefabNodeMappings.empty())
            {
                Entity* ancestorEntity = scene.findEntity(*deleted.prefabAncestorGuid);
                assert(ancestorEntity);
                PrefabInstance* instance = ancestorEntity->getComponent<PrefabInstance>();
                assert(instance);
                for (const PrefabNodeMapping& mapping : deleted.prefabNodeMappings)
                {
                    Object* runtimeNode = mapping.runtimeClass->findByGuid(mapping.runtimeGuid);
                    assert(runtimeNode);
                    instance->mapNode(mapping.templateNodeId, runtimeNode);
                }
            }
        }

        void redoDeleteEntity(Scene& scene, const DeletedEntity& deleted)
        {
            Entity* entity = scene.findEntity(deleted.entityGuid);
            assert(entity);

            if (!deleted.prefabNodeMappings.empty())
            {
                Entity* ancestorEntity = scene.findEntity(*deleted.prefabAncestorGuid);
                assert(ancestorEntity);
                PrefabInstance* instance = ancestorEntity->getComponent<PrefabInstance>();
                assert(instance);
                for (const PrefabNodeMapping& mapping : deleted.prefabNodeMappings)
                    instance->removeNode(mapping.templateNodeId);
            }

            scene.destroyEntity(entity, false);
        }

        std::optional<DeletedFolder> captureAndRemoveFolder(Scene& scene, HString name)
        {
            const Scene::Folder* folder = scene.findFolder(name);
            if (!folder)
                return std::nullopt;

            DeletedFolder deleted;
            deleted.name = name;
            deleted.parentName = scene.getParentFolder(name);
            deleted.index = scene.getFolderIndex(name);
            deleted.childFolders = folder->childFolders;
            deleted.memberIds.reserve(folder->entities.size());
            for (const Entity* member : folder->entities)
                deleted.memberIds.push_back(member->getGuid());

            scene.removeFolder(name);
            return deleted;
        }

        void restoreDeletedFolder(Scene& scene, const DeletedFolder& deleted)
        {
            scene.createFolder(deleted.name, deleted.parentName);
            scene.moveFolder(deleted.name, deleted.index);
            for (HString childFolder : deleted.childFolders)
                scene.moveFolderToFolder(childFolder, deleted.name);
            for (Guid memberId : deleted.memberIds)
            {
                if (Entity* member = scene.findEntity(memberId))
                    scene.moveEntityToFolder(member, deleted.name);
            }
        }

        struct EntityMove
        {
            Guid entityId;
            Guid oldParentId;
            Guid newParentId;
            size_t oldSiblingIndex;
            size_t newSiblingIndex;
            // 只在 Entity 是根的时候有意义(空 = 未归档); 挂到别的 Entity 下会自动取消归档,
            // 所以 undo 不能指望归档关系自己回来
            HString oldFolder;
            HString newFolder;
            // 这次 reparent 把这个 Entity 从它原来追踪它的 PrefabInstance 子树里拽出去了
            // (F.28: 拖出子树视为删除), 两个都有值才表示发生过这件事——undo/redo 要跟着
            // 把 mNodeMap 的映射/摘除重放一遍, 不能只管 setParent() 那一半。
            std::optional<Guid> prefabAncestorGuid;
            std::optional<Guid> prefabTemplateNodeId;
        };

        struct FolderMove
        {
            HString name;
            HString oldParent;
            size_t oldIndex;
            HString newParent;
            size_t newIndex;
        };

        struct MoveSnapshot
        {
            Guid sceneGuid;
            std::vector<EntityMove> entityMoves;
            std::vector<FolderMove> folderMoves;
        };

        std::vector<EntityMove> applyEntityMoves(Scene& scene, std::span<const Guid> topLevelIds, Guid newParentId, HString targetFolder, size_t targetIndex)
        {
            std::vector<EntityMove> changed;
            changed.reserve(topLevelIds.size());

            Entity* newParent = newParentId.isValid() ? scene.findEntity(newParentId) : nullptr;

            // 成为根的时候才有归档的概念; 目标 Folder 不存在就当未归档
            const HString destinationFolder = (!newParent && scene.findFolder(targetFolder)) ? targetFolder : HString();

            // targetIndex 是放下之前那一刻的下标, 被拖的实体里已经在目标容器(同一个父节点, 或者同一个
            // Folder/未归档根序列)里、并且排在目标位置之前的会先被摘走, 目标位置要相应前移
            auto isInDestination = [&](Entity* entity)
            {
                return entity->getParent() == newParent && (newParent || scene.getFolderOfEntity(entity) == destinationFolder);
            };
            size_t baseIndex = targetIndex;
            if (targetIndex != kAppendIndex)
            {
                for (Guid entityId : topLevelIds)
                {
                    Entity* entity = scene.findEntity(entityId);
                    if (entity && isInDestination(entity) && entity->getSiblingIndex() < targetIndex)
                        --baseIndex;
                }
            }

            size_t placedCount = 0;
            for (Guid entityId : topLevelIds)
            {
                Entity* entity = scene.findEntity(entityId);
                if (!entity)
                    continue;

                Entity* oldParent = entity->getParent();
                Guid oldParentId = oldParent ? oldParent->getGuid() : Guid();
                size_t oldSiblingIndex = entity->getSiblingIndex();
                HString oldFolder = scene.getFolderOfEntity(entity);

                // 重新挂接之前, 先看这个 Entity 现在是不是某个祖先 PrefabInstance 追踪的节点——
                // 挂接之后再判断还连不连得通那个实例的根, 见下面。仍然连通(只是在同一棵子树内部
                // 挪位置)的情况暂不处理: 按 F.27 该记一条 Parent 的 override, 但 override 记录
                // 现在还没有任何编辑器入口会调用 addOverride(), 这里单独接一份没有意义。
                Entity* prefabAncestor = nullptr;
                PrefabInstance* prefabInstance = nullptr;
                for (Entity* ancestor = oldParent; ancestor; ancestor = ancestor->getParent())
                {
                    if (PrefabInstance* instance = ancestor->getComponent<PrefabInstance>())
                    {
                        prefabAncestor = ancestor;
                        prefabInstance = instance;
                        break;
                    }
                }
                std::optional<Guid> templateNodeId;
                if (prefabInstance)
                    templateNodeId = prefabInstance->findTemplateNodeId(entity);

                // 父节点变了才需要 setParent(), 它自带循环检测(目标是自己/自己的后代会被拒绝),
                // 返回 false 就跳过这一个, 不影响这批里的其它实体, 也不产生 undo 记录; 父节点没变
                // 就只是在同级里挪位置
                const bool parentChanged = oldParent != newParent;
                if (parentChanged && !entity->setParent(newParent, ReparentMode::Keep_World))
                    continue;

                bool folderChanged = false;
                if (!newParent && scene.getFolderOfEntity(entity) != destinationFolder)
                {
                    scene.moveEntityToFolder(entity, destinationFolder);
                    folderChanged = true;
                }

                entity->setSiblingIndex(targetIndex == kAppendIndex ? kAppendIndex : baseIndex + placedCount);
                ++placedCount;
                const size_t newSiblingIndex = entity->getSiblingIndex();
                if (!parentChanged && !folderChanged && newSiblingIndex == oldSiblingIndex)
                    continue;

                std::optional<Guid> prefabAncestorGuid;
                std::optional<Guid> prefabTemplateNodeId;
                if (parentChanged && templateNodeId)
                {
                    bool stillConnected = false;
                    for (Entity* ancestor = entity; ancestor; ancestor = ancestor->getParent())
                    {
                        if (ancestor == prefabAncestor)
                        {
                            stillConnected = true;
                            break;
                        }
                    }
                    if (!stillConnected)
                    {
                        prefabInstance->removeNode(*templateNodeId);
                        prefabAncestorGuid = prefabAncestor->getGuid();
                        prefabTemplateNodeId = templateNodeId;
                    }
                }

                changed.push_back({ entityId, oldParentId, newParentId, oldSiblingIndex, newSiblingIndex, oldFolder, destinationFolder, prefabAncestorGuid, prefabTemplateNodeId });
            }
            return changed;
        }

        void undoEntityMoves(Scene& scene, std::span<const EntityMove> moves)
        {
            // 倒序: oldSiblingIndex 是挂接这一个之前那一刻记下的, 那时前面几个已经被挪走了
            for (auto it = moves.rbegin(); it != moves.rend(); ++it)
            {
                const EntityMove& entry = *it;
                Entity* entity = scene.findEntity(entry.entityId);
                if (!entity)
                    continue;
                Entity* oldParent = entry.oldParentId.isValid() ? scene.findEntity(entry.oldParentId) : nullptr;
                entity->setParent(oldParent, ReparentMode::Keep_World);
                if (!oldParent)
                    scene.moveEntityToFolder(entity, entry.oldFolder);
                entity->setSiblingIndex(entry.oldSiblingIndex);

                if (entry.prefabTemplateNodeId)
                {
                    Entity* ancestorEntity = scene.findEntity(*entry.prefabAncestorGuid);
                    assert(ancestorEntity);
                    PrefabInstance* instance = ancestorEntity->getComponent<PrefabInstance>();
                    assert(instance);
                    instance->mapNode(*entry.prefabTemplateNodeId, entity);
                }
            }
        }

        void redoEntityMoves(Scene& scene, std::span<const EntityMove> moves)
        {
            for (const EntityMove& entry : moves)
            {
                Entity* entity = scene.findEntity(entry.entityId);
                if (!entity)
                    continue;
                Entity* newParent = entry.newParentId.isValid() ? scene.findEntity(entry.newParentId) : nullptr;
                entity->setParent(newParent, ReparentMode::Keep_World);
                if (!newParent)
                    scene.moveEntityToFolder(entity, entry.newFolder);
                entity->setSiblingIndex(entry.newSiblingIndex);

                if (entry.prefabTemplateNodeId)
                {
                    Entity* ancestorEntity = scene.findEntity(*entry.prefabAncestorGuid);
                    assert(ancestorEntity);
                    PrefabInstance* instance = ancestorEntity->getComponent<PrefabInstance>();
                    assert(instance);
                    instance->removeNode(*entry.prefabTemplateNodeId);
                }
            }
        }

        void placeFolder(Scene& scene, HString name, HString parentName, size_t index)
        {
            if (scene.getParentFolder(name) != parentName)
                scene.moveFolderToFolder(name, parentName);
            scene.moveFolder(name, index);
        }

        std::vector<FolderMove> applyFolderMoves(Scene& scene, std::span<const HString> topLevelNames, HString newParent, size_t targetIndex)
        {
            // targetIndex 是放下之前那一刻的下标, 同一个父级里被拖的 Folder 排在目标位置之前的会先被摘走
            size_t baseIndex = targetIndex;
            if (targetIndex != kAppendIndex)
            {
                for (HString name : topLevelNames)
                {
                    if (scene.findFolder(name) && scene.getParentFolder(name) == newParent && scene.getFolderIndex(name) < targetIndex)
                        --baseIndex;
                }
            }

            std::vector<FolderMove> changed;
            size_t placedCount = 0;
            for (HString name : topLevelNames)
            {
                if (!scene.findFolder(name))
                    continue;

                const HString oldParent = scene.getParentFolder(name);
                const size_t oldIndex = scene.getFolderIndex(name);
                if (oldParent != newParent && !scene.moveFolderToFolder(name, newParent))
                    continue;

                scene.moveFolder(name, targetIndex == kAppendIndex ? kAppendIndex : baseIndex + placedCount);
                ++placedCount;
                const size_t newIndex = scene.getFolderIndex(name);
                if (oldParent == newParent && newIndex == oldIndex)
                    continue;

                changed.push_back({ name, oldParent, oldIndex, newParent, newIndex });
            }
            return changed;
        }

        void undoFolderMoves(Scene& scene, std::span<const FolderMove> moves)
        {
            for (auto it = moves.rbegin(); it != moves.rend(); ++it)
                placeFolder(scene, it->name, it->oldParent, it->oldIndex);
        }

        void redoFolderMoves(Scene& scene, std::span<const FolderMove> moves)
        {
            for (const FolderMove& entry : moves)
                placeFolder(scene, entry.name, entry.newParent, entry.newIndex);
        }
    }

    Hierarchy::Hierarchy(Engine& engine, EditorSelection& selection, UndoStack& undoStack) :
        EditorWindow("Hierarchy", "Hierarchy"),
        mEngine(engine),
        mSelection(selection),
        mUndoStack(undoStack)
    {
        setIcon(ICON_MS_ACCOUNT_TREE);
        setWindowPadding(ImVec2(0.0f, 0.0f));
    }

    Hierarchy::~Hierarchy()
    {
    }

    void Hierarchy::onDraw()
    {
        Scene* scene = mEngine.getScene();
        if (!scene)
        {
            mVisibleRows.clear();
            ImGui::TextDisabled("No Scene");
            return;
        }

        constexpr ImGuiTableFlags tableFlags =
            ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_NoSavedSettings;
        // 行间不留单元格内边距: 间隙不属于任何一行, 拖放落进去既没有目标也画不出指示线;
        // 原来的内边距折进行高, 行距不变。
        const float cellPaddingY = ImGui::GetStyle().CellPadding.y;
        mRowHeight = kRowHeightBase * ImGuiFrontend::getDpiScale() + 2.0f * cellPaddingY;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetStyle().CellPadding.x, 0.0f));

        // 让 table 撑满窗口剩余高度(而不是按内容高度收缩), 视觉上跟其它列表类窗口一致——
        // 空白处点击能不能清空选中, 实际取决于下面 selectionFlags 用的是 ScopeWindow 还是
        // ScopeRect, 跟这里的尺寸无关, 见那边的注释。
        if (!ImGui::BeginTable("EntityHierarchy", 2, tableFlags, ImGui::GetContentRegionAvail()))
        {
            ImGui::PopStyleVar();
            return;
        }

        ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthStretch);
        // 状态列并排两个图标槽: 左边 Prefab 标记, 右边可见性按钮
        mIconSlotWidth = ImGui::CalcTextSize(ICON_MS_VISIBILITY).x;
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 2.0f * mIconSlotWidth + 2.0f * ImGui::GetStyle().CellPadding.x);

        // 不要 ScopeRect: 看了 imgui_widgets.cpp::EndMultiSelect() 的实现, ClearOnClickVoid
        // 判断"点在空白处"时, 默认(ScopeWindow)是整个窗口范围, 但只要挂了 ScopeRect,
        // 判定范围就会被收窄成"紧贴所有已绘制行的最小包围盒", 最后一行下面的空白反而被排除在
        // "空白"之外, 点了没反应。ScopeRect 是给"同一个窗口里调了多次 BeginMultiSelect"的场景
        // 用来隔开各自空白判定范围的, 这里只调了一次, 用不上。
        constexpr ImGuiMultiSelectFlags selectionFlags =
            ImGuiMultiSelectFlags_ClearOnEscape |
            ImGuiMultiSelectFlags_ClearOnClickVoid;
        ImGuiMultiSelectIO* multiSelect = ImGui::BeginMultiSelect(
            selectionFlags,
            static_cast<int>(getSelectedItemCount()),
            -1
        );
        applySelectionRequests(multiSelect, mVisibleRows);

        mVisibleRows.clear();
        FrameActions actions;

        // F2 重命名: 只在恰好选中一项、这个窗口本身有焦点、当前没有别的项正在被编辑时触发。
        const bool isRenamingAnything = mRenamingEntityId.isValid() || !mRenamingFolderName.empty();
        if (!isRenamingAnything && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2, false) && getSelectedItemCount() == 1)
        {
            if (Entity* selectedEntity = mSelection.getSingleSelectedEntity())
                beginRename(selectedEntity->getGuid(), selectedEntity->getName().toStringView());
            else
                beginFolderRename(mSelection.getSelectedFolders().front());
        }

        // Delete 删除选中项: 触发条件跟 F2 一样(窗口有焦点、当前没有正在编辑的重命名——
        // 否则 Delete 键应该落进重命名输入框里去编辑文字)。多选时整批一起删, 跟拖拽 reparent
        // 支持多选保持一致。删除本身同样延迟到整棵树画完之后做, 原因见下面 entitiesToDelete。
        if (!isRenamingAnything && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            collectSelectedForDelete(actions);

        // 先画根 Folder(带着各自的子 Folder 和成员), 再画未归档的根 Entity
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, kTreeIndentSpacingBase * ImGuiFrontend::getDpiScale());
        for (HString folderName : scene->getRootFolders())
            drawFolderNode(*scene, folderName, actions);
        for (const TRef<Entity>& entity : scene->getEntities())
        {
            if (!entity->getParent() && scene->getFolderOfEntity(entity.get()).empty())
                drawEntityNode(entity.get(), actions, nullptr);
        }
        ImGui::PopStyleVar();

        // 树画完之后, 剩余空白区域整体接收 drop——拖到这里表示"变成最后一个根节点",
        // 不受下面 Mobility 规则限制(Static 对象总是能变成根节点)。table 是按
        // ImGui::GetContentRegionAvail() 固定尺寸建的(见上面 BeginTable 调用), 这里
        // GetContentRegionAvail().y 就是从当前行到 table 底部还剩多少——用它铺满剩余高度,
        // 不要只给一条固定高度的窄条(树很短时下面会露出一截跟这条窄条不搭的空白, 很难看);
        // 树本身已经比 table 还高、没有剩余空间的话, 退化成一行最小高度, 保证拖过去时还有
        // 地方接 drop。
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        float remainingHeight = std::max(ImGui::GetContentRegionAvail().y, ImGui::GetTextLineHeightWithSpacing());
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, remainingHeight));
        if (ImGui::BeginDragDropTarget())
        {
            if (ImGui::AcceptDragDropPayload(kRowsDragDropType))
            {
                if (!mDraggedEntityIds.empty())
                    actions.entityDrop = DropRequest{ Guid(), HString(), kAppendIndex };
                if (!mDraggedFolderNames.empty())
                    actions.folderDrop = FolderDropRequest{ HString(), kAppendIndex };
            }
            ImGui::EndDragDropTarget();
        }

        // 挂在这个 Dummy(代表空白区域)上, 跟每一行 Entity 自己的 BeginPopupContextItem()
        // 是同一种做法——右键空白处新建 Entity 或 Folder。
        if (ImGui::BeginPopupContextItem("##HierarchyEmptyContext"))
        {
            if (ImGui::MenuItem("Create Entity"))
                createEntityWithUndo(*scene);
            if (ImGui::MenuItem("Create Folder"))
                actions.folderToCreateUnder = HString();
            ImGui::EndPopup();
        }

        multiSelect = ImGui::EndMultiSelect();
        applySelectionRequests(multiSelect, mVisibleRows);
        ImGui::EndTable();
        ImGui::PopStyleVar();

        // 删除整棵树画完之后再做, 不在递归画树的过程中途改动 Scene::mEntities——道理
        // 跟 Inspector 删组件那边一样(见那边的注释)。deleteWithUndo 内部按 Guid 现查: 同一批里
        // 父节点先被删、子节点被 destroyEntity(entity, false) 重新挂到祖父节点之后仍然活着,
        // 还能查到并继续删; 真被连带删掉的(或重复记录的)条目查不到, 直接跳过。拖拽 reparent 是
        // 同一个道理, 延迟到这里统一处理。
        if (!actions.entitiesToDelete.empty() || !actions.foldersToDelete.empty())
            deleteWithUndo(*scene, actions.entitiesToDelete, actions.foldersToDelete);

        if (actions.entityDrop || actions.folderDrop)
            moveWithUndo(*scene, mDraggedEntityIds, actions.entityDrop, mDraggedFolderNames, actions.folderDrop);

        if (actions.folderToCreateUnder)
            createFolderWithUndo(*scene, *actions.folderToCreateUnder);

        if (mPendingRenameCommit)
        {
            commitRename(*scene, mPendingRenameCommit->entityId, mPendingRenameCommit->newName);
            mPendingRenameCommit.reset();
        }

        if (mPendingFolderRenameCommit)
        {
            commitFolderRename(*scene, mPendingFolderRenameCommit->oldName, mPendingFolderRenameCommit->newName);
            mPendingFolderRenameCommit.reset();
        }
    }

    void Hierarchy::createEntityWithUndo(Scene& scene)
    {
        Entity* entity = scene.createEntity("Entity");
        entity->getTransform().setLocalPosition(double3(0.0, 0.0, 0.0));
        Guid entityGuid = entity->getGuid();

        // 跟 captureAndDestroyEntity 的快照逻辑对称: 新建的 Entity 没有 parent、没有 Component,
        // 序列化出的 import/export entry 理论上应为空, 但还是走同一套流程, 保证 redo 出来的
        // 是同一个 Guid, 而不是又调一次 createEntity() 生成的新对象。
        SerializeContext saveContext(false, entityGuid);
        BinarySaver saver;
        Serializer saveSerializer(saver, saveContext);
        Object* rootObject = entity;
        saveSerializer.serializeObject(rootObject);

        std::vector<uint8_t> bodyData = saver.pack();
        std::vector<AssetImportEntry> importEntries = saveContext.getImportEntries();
        std::vector<AssetExportEntry> exportEntries = saveContext.getExportEntries();
        std::vector<uint8_t> bulkSection = saveContext.getBulkSection();

        // Scene 按 guid 现查, 不直接捕获指针——Play/Stop 会把当前场景整个销毁重建, 捕获的
        // Scene* 到那时候就是悬空指针, 见 ComponentRef.h 的注释。
        Guid sceneGuid = scene.getGuid();
        mUndoStack.push("Create Entity"_hs,
            [sceneGuid, entityGuid]()
            {
                Scene* scenePtr = static_cast<Scene*>(Scene::staticClass()->findByGuid(sceneGuid));
                assert(scenePtr);
                Entity* entity = scenePtr->findEntity(entityGuid);
                assert(entity);
                scenePtr->destroyEntity(entity, false);
            },
            [sceneGuid, entityGuid, bodyData, importEntries, exportEntries, bulkSection]()
            {
                Scene* scenePtr = static_cast<Scene*>(Scene::staticClass()->findByGuid(sceneGuid));
                assert(scenePtr);

                SerializeContext loadContext(true, entityGuid);
                loadContext.setImportEntries(importEntries);
                loadContext.setExportEntries(exportEntries);
                loadContext.setBulkSection(bulkSection);
                loadContext.registerKnownObject(scenePtr->getGuid(), scenePtr);

                BinaryLoader loader;
                loader.unpack(bodyData);
                Serializer loadSerializer(loader, loadContext);
                Object* restoredObject = nullptr;
                loadSerializer.serializeObject(restoredObject);
                Entity* restoredEntity = static_cast<Entity*>(restoredObject);
                scenePtr->registerEntity(restoredEntity);
            });
    }

    void Hierarchy::beginRename(Guid entityId, std::string_view currentName)
    {
        mRenamingEntityId = entityId;
        mRenameBuffer = currentName;
        mRenameFocusPending = true;
    }

    void Hierarchy::commitRename(Scene& scene, Guid entityId, const std::string& newName)
    {
        Entity* entity = scene.findEntity(entityId);
        if (entity && !newName.empty() && newName != entity->getName().toStringView())
        {
            HString oldName = entity->getName();
            entity->setName(HString(newName));
            // makeUniqueName() 可能因为重名把它改写成带数字后缀的另一个名字, redo 要用这个
            // 实际生效的结果, 不能直接存用户输入的 newName。
            HString finalName = entity->getName();

            Guid sceneGuid = scene.getGuid();
            mUndoStack.push("Rename Entity"_hs,
                [sceneGuid, entityId, oldName]()
                {
                    Scene* scenePtr = static_cast<Scene*>(Scene::staticClass()->findByGuid(sceneGuid));
                    assert(scenePtr);
                    Entity* entity = scenePtr->findEntity(entityId);
                    assert(entity);
                    entity->setName(oldName);
                },
                [sceneGuid, entityId, finalName]()
                {
                    Scene* scenePtr = static_cast<Scene*>(Scene::staticClass()->findByGuid(sceneGuid));
                    assert(scenePtr);
                    Entity* entity = scenePtr->findEntity(entityId);
                    assert(entity);
                    entity->setName(finalName);
                });
        }

        cancelRename();
    }

    void Hierarchy::cancelRename()
    {
        mRenamingEntityId = Guid();
    }

    void Hierarchy::collectSelectedForDelete(FrameActions& actions) const
    {
        for (Guid entityId : mSelection.getSelectedEntityIds())
            actions.entitiesToDelete.push_back(entityId);
        for (HString folderName : mSelection.getSelectedFolders())
            actions.foldersToDelete.push_back(folderName);
    }

    void Hierarchy::deleteWithUndo(Scene& scene, std::span<const Guid> entityIds, std::span<const HString> folderNames)
    {
        auto snapshot = std::make_shared<DeleteSnapshot>();
        snapshot->sceneGuid = scene.getGuid();

        for (Guid entityId : entityIds)
        {
            if (Entity* entity = scene.findEntity(entityId))
                snapshot->entities.push_back(captureAndDestroyEntity(scene, entity));
        }
        for (HString folderName : folderNames)
        {
            if (std::optional<DeletedFolder> deleted = captureAndRemoveFolder(scene, folderName))
                snapshot->folders.push_back(std::move(*deleted));
        }

        if (snapshot->entities.empty() && snapshot->folders.empty())
            return;

        mUndoStack.push("Delete"_hs,
            [snapshot]()
            {
                Scene& scene = *findScene(snapshot->sceneGuid);
                for (auto it = snapshot->folders.rbegin(); it != snapshot->folders.rend(); ++it)
                    restoreDeletedFolder(scene, *it);
                for (auto it = snapshot->entities.rbegin(); it != snapshot->entities.rend(); ++it)
                    restoreDeletedEntity(scene, *it);
            },
            [snapshot]()
            {
                Scene& scene = *findScene(snapshot->sceneGuid);
                for (const DeletedEntity& deleted : snapshot->entities)
                    redoDeleteEntity(scene, deleted);
                for (const DeletedFolder& deleted : snapshot->folders)
                    scene.removeFolder(deleted.name);
            });
    }

    void Hierarchy::moveWithUndo(
        Scene& scene,
        std::span<const Guid> entityIds, const std::optional<DropRequest>& entityDrop,
        std::span<const HString> folderNames, const std::optional<FolderDropRequest>& folderDrop
    )
    {
        auto snapshot = std::make_shared<MoveSnapshot>();
        snapshot->sceneGuid = scene.getGuid();

        auto rowOrder = [this](auto matches)
        {
            return static_cast<size_t>(std::find_if(mVisibleRows.begin(), mVisibleRows.end(), matches) - mVisibleRows.begin());
        };

        if (entityDrop)
        {
            // 多选里如果某个 entity 的祖先也在这批被拖的集合里, 就把它跳过——祖先重新挂到新父节点
            // 时会带着它一起过去, 不跳过的话会把它从祖先底下拽出来直接挂到新父节点, 打散子树结构
            // (比如同时选中一个 Entity 和它的子节点一起拖, 子节点不应该被拽出来跟父节点平级)。
            std::vector<Guid> topLevelIds;
            topLevelIds.reserve(entityIds.size());
            for (Guid entityId : entityIds)
            {
                Entity* entity = scene.findEntity(entityId);
                if (!entity)
                    continue;

                bool hasSelectedAncestor = false;
                for (Entity* ancestor = entity->getParent(); ancestor; ancestor = ancestor->getParent())
                {
                    if (std::find(entityIds.begin(), entityIds.end(), ancestor->getGuid()) != entityIds.end())
                    {
                        hasSelectedAncestor = true;
                        break;
                    }
                }
                if (!hasSelectedAncestor)
                    topLevelIds.push_back(entityId);
            }

            // 按画面上的先后顺序整块插入, 不按选中集合里的点击顺序; 不在画面上的排最后
            std::stable_sort(topLevelIds.begin(), topLevelIds.end(),
                [&](Guid lhs, Guid rhs)
                {
                    return rowOrder([lhs](const Row& row) { return row.entityId == lhs; }) <
                        rowOrder([rhs](const Row& row) { return row.entityId == rhs; });
                });

            snapshot->entityMoves = applyEntityMoves(scene, topLevelIds, entityDrop->newParentId, entityDrop->folderName, entityDrop->index);
        }

        if (folderDrop)
        {
            std::vector<HString> topLevelNames;
            topLevelNames.reserve(folderNames.size());
            for (HString name : folderNames)
            {
                if (!scene.findFolder(name))
                    continue;

                bool hasDraggedAncestor = false;
                for (HString ancestor = scene.getParentFolder(name); !ancestor.empty(); ancestor = scene.getParentFolder(ancestor))
                {
                    if (std::find(folderNames.begin(), folderNames.end(), ancestor) != folderNames.end())
                    {
                        hasDraggedAncestor = true;
                        break;
                    }
                }
                if (!hasDraggedAncestor)
                    topLevelNames.push_back(name);
            }

            std::stable_sort(topLevelNames.begin(), topLevelNames.end(),
                [&](HString lhs, HString rhs)
                {
                    return rowOrder([lhs](const Row& row) { return row.folderName == lhs; }) <
                        rowOrder([rhs](const Row& row) { return row.folderName == rhs; });
                });

            snapshot->folderMoves = applyFolderMoves(scene, topLevelNames, folderDrop->newParentName, folderDrop->index);
        }

        if (snapshot->entityMoves.empty() && snapshot->folderMoves.empty())
            return;

        mUndoStack.push("Move"_hs,
            [snapshot]()
            {
                Scene& scene = *findScene(snapshot->sceneGuid);
                undoFolderMoves(scene, snapshot->folderMoves);
                undoEntityMoves(scene, snapshot->entityMoves);
            },
            [snapshot]()
            {
                Scene& scene = *findScene(snapshot->sceneGuid);
                redoEntityMoves(scene, snapshot->entityMoves);
                redoFolderMoves(scene, snapshot->folderMoves);
            });
    }

    void Hierarchy::createFolderWithUndo(Scene& scene, HString parentName)
    {
        const HString finalName = scene.createFolder("Folder", parentName);
        if (finalName.empty())
            return;

        const size_t index = scene.getFolderIndex(finalName);
        beginFolderRename(finalName);

        Guid sceneGuid = scene.getGuid();
        mUndoStack.push("Create Folder"_hs,
            [sceneGuid, finalName]()
            {
                Scene* scenePtr = static_cast<Scene*>(Scene::staticClass()->findByGuid(sceneGuid));
                assert(scenePtr);
                scenePtr->removeFolder(finalName);
            },
            [sceneGuid, finalName, parentName, index]()
            {
                Scene* scenePtr = static_cast<Scene*>(Scene::staticClass()->findByGuid(sceneGuid));
                assert(scenePtr);
                scenePtr->createFolder(finalName, parentName);
                scenePtr->moveFolder(finalName, index);
            });
    }

    void Hierarchy::beginFolderRename(HString folderName)
    {
        mRenamingFolderName = folderName;
        mFolderRenameBuffer = std::string(folderName.toStringView());
        mFolderRenameFocusPending = true;
    }

    void Hierarchy::commitFolderRename(Scene& scene, HString oldName, const std::string& newName)
    {
        if (!newName.empty() && scene.findFolder(oldName))
        {
            const HString finalName = scene.renameFolder(oldName, HString(newName));
            if (!finalName.empty() && finalName != oldName)
            {
                mSelection.replaceSelectedFolder(oldName, finalName);
                Guid sceneGuid = scene.getGuid();
                mUndoStack.push("Rename Folder"_hs,
                    [sceneGuid, oldName, finalName]()
                    {
                        Scene* scenePtr = static_cast<Scene*>(Scene::staticClass()->findByGuid(sceneGuid));
                        assert(scenePtr);
                        scenePtr->renameFolder(finalName, oldName);
                    },
                    [sceneGuid, oldName, finalName]()
                    {
                        Scene* scenePtr = static_cast<Scene*>(Scene::staticClass()->findByGuid(sceneGuid));
                        assert(scenePtr);
                        scenePtr->renameFolder(oldName, finalName);
                    });
            }
        }

        cancelFolderRename();
    }

    void Hierarchy::cancelFolderRename()
    {
        mRenamingFolderName = HString();
    }

    void Hierarchy::beginRowDrag(const Row& draggedRow)
    {
        // 每一帧 BeginDragDropSource() 为真时都会重新算一遍(不是只在拖拽开始那一帧算一次)——
        // 拖拽期间鼠标一直按着, 选中集合实际不会变, 重新算一遍的开销可以忽略, 换来的是不用
        // 单独维护"拖拽是否刚开始"这个状态机。
        Scene* scene = mEngine.getScene();
        std::vector<Guid> entityIds;
        std::vector<HString> folderNames;
        const bool draggedIsSelected = draggedRow.entityId.isValid()
            ? mSelection.isSelected(draggedRow.entityId)
            : mSelection.isFolderSelected(draggedRow.folderName);
        if (draggedIsSelected)
        {
            std::span<const Guid> selectedEntities = mSelection.getSelectedEntityIds();
            entityIds.assign(selectedEntities.begin(), selectedEntities.end());
            std::span<const HString> selectedFolders = mSelection.getSelectedFolders();
            folderNames.assign(selectedFolders.begin(), selectedFolders.end());
        }
        else if (draggedRow.entityId.isValid())
        {
            entityIds.push_back(draggedRow.entityId);
        }
        else
        {
            folderNames.push_back(draggedRow.folderName);
        }

        auto isInsideDraggedFolder = [&](HString folderName)
        {
            for (HString current = folderName; !current.empty(); current = scene->getParentFolder(current))
            {
                if (std::find(folderNames.begin(), folderNames.end(), current) != folderNames.end())
                    return true;
            }
            return false;
        };

        mDraggedFolderNames.clear();
        for (HString folderName : folderNames)
        {
            if (!isInsideDraggedFolder(scene->getParentFolder(folderName)))
                mDraggedFolderNames.push_back(folderName);
        }

        mDraggedEntityIds.clear();
        mDraggedContainsStatic = false;
        for (Guid entityId : entityIds)
        {
            Entity* entity = scene->findEntity(entityId);
            if (!entity)
                continue;

            Entity* rootAncestor = entity;
            while (rootAncestor->getParent())
                rootAncestor = rootAncestor->getParent();
            if (isInsideDraggedFolder(scene->getFolderOfEntity(rootAncestor)))
                continue;

            mDraggedEntityIds.push_back(entityId);
            if (entity->getMobility() == Mobility::Static)
                mDraggedContainsStatic = true;
        }
    }

    std::vector<Guid> Hierarchy::gatherVisibilityTargets(const Scene& scene, Entity* clickedEntity, HString clickedFolder) const
    {
        std::vector<Guid> targets;
        std::function<void(const Scene::Folder&)> gatherFolder = [&](const Scene::Folder& folder)
        {
            for (const Entity* member : folder.entities)
                targets.push_back(member->getGuid());
            for (HString childName : folder.childFolders)
            {
                if (const Scene::Folder* child = scene.findFolder(childName))
                    gatherFolder(*child);
            }
        };

        const bool clickedIsSelected = clickedEntity
            ? mSelection.isSelected(clickedEntity->getGuid())
            : mSelection.isFolderSelected(clickedFolder);
        if (!clickedIsSelected)
        {
            if (clickedEntity)
                targets.push_back(clickedEntity->getGuid());
            else if (const Scene::Folder* folder = scene.findFolder(clickedFolder))
                gatherFolder(*folder);
            return targets;
        }

        std::span<const Guid> selectedEntities = mSelection.getSelectedEntityIds();
        targets.assign(selectedEntities.begin(), selectedEntities.end());
        for (HString folderName : mSelection.getSelectedFolders())
        {
            if (const Scene::Folder* folder = scene.findFolder(folderName))
                gatherFolder(*folder);
        }
        return targets;
    }

    void Hierarchy::applyVisibilityWithUndo(Scene& scene, std::span<const Guid> targetIds, bool newVisible)
    {
        struct Change
        {
            Guid entityId;
            bool oldVisible;
        };
        std::vector<Change> changes;
        std::function<void(Entity*)> applyToSubtree = [&](Entity* entity)
        {
            if (entity->isVisible() != newVisible)
            {
                changes.push_back({ entity->getGuid(), entity->isVisible() });
                entity->setVisible(newVisible);
            }
            for (size_t childIndex = 0; childIndex < entity->getChildCount(); ++childIndex)
                applyToSubtree(entity->getChild(childIndex));
        };
        for (Guid entityId : targetIds)
        {
            if (Entity* entity = scene.findEntity(entityId))
                applyToSubtree(entity);
        }
        if (changes.empty())
            return;

        Guid sceneGuid = scene.getGuid();
        mUndoStack.push("Toggle Visibility"_hs,
            [sceneGuid, changes]()
            {
                Scene* scenePtr = static_cast<Scene*>(Scene::staticClass()->findByGuid(sceneGuid));
                assert(scenePtr);
                for (const Change& change : changes)
                {
                    Entity* entity = scenePtr->findEntity(change.entityId);
                    assert(entity);
                    entity->setVisible(change.oldVisible);
                }
            },
            [sceneGuid, changes, newVisible]()
            {
                Scene* scenePtr = static_cast<Scene*>(Scene::staticClass()->findByGuid(sceneGuid));
                assert(scenePtr);
                for (const Change& change : changes)
                {
                    Entity* entity = scenePtr->findEntity(change.entityId);
                    assert(entity);
                    entity->setVisible(newVisible);
                }
            });
    }

    bool Hierarchy::canDropUnder(const Entity* newParent) const
    {
        for (const Entity* ancestor = newParent; ancestor; ancestor = ancestor->getParent())
        {
            if (std::find(mDraggedEntityIds.begin(), mDraggedEntityIds.end(), ancestor->getGuid()) != mDraggedEntityIds.end())
                return false;
        }
        return !(newParent && mDraggedContainsStatic && newParent->getMobility() == Mobility::Dynamic);
    }

    bool Hierarchy::canDropFoldersUnder(const Scene& scene, HString newParentName) const
    {
        if (newParentName.empty())
            return true;

        for (HString dragged : mDraggedFolderNames)
        {
            bool isLegal = true;
            for (HString current = newParentName; !current.empty(); current = scene.getParentFolder(current))
            {
                if (current == dragged)
                {
                    isLegal = false;
                    break;
                }
            }
            if (isLegal)
                return true;
        }
        return false;
    }

    void Hierarchy::drawEntityNode(Entity* entity, FrameActions& actions, PrefabInstance* enclosingPrefabInstance)
    {
        const int visibleIndex = static_cast<int>(mVisibleRows.size());
        const Guid entityId = entity->getGuid();
        mVisibleRows.push_back(Row{ entityId, HString() });

        // thisInstance 非空说明这一行本身就是某个 Prefab 实例的根; effectivePrefabInstance
        // 非空说明这一行真正被那个 Prefab 实例追踪(mNodeMap 里查得到, 或者被 markNodeAdded()
        // 标记过), 决定要不要染色。只挂在 Transform 子树里、没被追踪的普通 Entity(比如用户
        // 手动拖过去的)不算——这里一旦判定"不追踪", 往下传的也是 nullptr, 它自己的子孙
        // 同样不会被误染色, 不需要每一层各自重新判断。
        PrefabInstance* thisInstance = entity->getComponent<PrefabInstance>();
        PrefabInstance* effectivePrefabInstance = thisInstance
            ? thisInstance
            : (enclosingPrefabInstance &&
                (enclosingPrefabInstance->findTemplateNodeId(entity) || enclosingPrefabInstance->isNodeAdded(entity))
                    ? enclosingPrefabInstance
                    : nullptr);

        const float rowHeight = mRowHeight;
        ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
        ImGui::TableSetColumnIndex(0);

        // 当前有拖拽在进行、拖的实体里包含 Static, 且这一行是 Dynamic——整行 disable:
        // 拖拽期间没法展开/选中/接收 drop, 文字也跟着变灰。只包住这一行自己的控件,
        // 不能连子节点的递归一起包进来, 否则会连带把子节点也 disable 掉, 但子节点的
        // Mobility 跟父节点无关, 应该各自独立判断。
        bool isDisabledTarget = false;
        if (const ImGuiPayload* payload = ImGui::GetDragDropPayload())
        {
            if (payload->IsDataType(kRowsDragDropType) &&
                mDraggedContainsStatic &&
                entity->getMobility() == Mobility::Dynamic)
            {
                isDisabledTarget = true;
            }
        }
        if (isDisabledTarget)
            ImGui::BeginDisabled();

        const bool hasChildren = entity->getChildCount() > 0;
        ImGuiTreeNodeFlags nodeFlags =
            ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_OpenOnDoubleClick |
            ImGuiTreeNodeFlags_SpanFullWidth |
            ImGuiTreeNodeFlags_SpanAllColumns |
            ImGuiTreeNodeFlags_NavLeftJumpsToParent |
            // 没有这个 flag, TreeNode 的高度(连带文字位置、SpanFullWidth 高亮框)不看
            // style.FramePadding.y, 下面临时调大的 FramePadding.y 不会生效。
            ImGuiTreeNodeFlags_FramePadding |
            ImGuiTreeNodeFlags_AllowOverlap |
            ImGuiTreeNodeFlags_DefaultOpen;
        if (!hasChildren)
            nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (mSelection.isSelected(entityId))
            nodeFlags |= ImGuiTreeNodeFlags_Selected;

        bool isRenaming = (entityId == mRenamingEntityId);
        std::string_view name = entity->getName().toStringView();
        ImGui::SetNextItemSelectionUserData(visibleIndex);
        // ImGuiTreeNodeFlags_FramePadding 让 TreeNode 用 style.FramePadding 算高度并垂直
        // 居中文字——把 y 方向的 padding 临时改成"能撑到 rowHeight"的值, x 方向沿用原来的。
        float framePadY = std::max(0.0f, (rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, framePadY));
        if (effectivePrefabInstance)
            ImGui::PushStyleColor(ImGuiCol_Text, kPrefabTintColor);
        // 正在被重命名的这一行传空 label——身份(选中/展开状态)靠 entity 指针这个 id 维持,
        // 跟文字内容无关; TreeNodeEx 内部为箭头/缩进留出的宽度已经计入它自己的 ItemSize(),
        // 紧跟的 SameLine() 会正确接到文字本该开始的位置, 不需要手动算偏移。
        const char* typeIcon = getEntityTypeIcon(entity);
        const float contentX = ImGui::GetCursorScreenPos().x + ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2.0f;
        bool open;
        if (isRenaming)
            open = ImGui::TreeNodeEx(entity, nodeFlags, "%s", typeIcon);
        else
            open = ImGui::TreeNodeEx(entity, nodeFlags, "%s %.*s", typeIcon, static_cast<int>(name.size()), name.data());
        if (effectivePrefabInstance)
            ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        const ImVec2 nodeMin = ImGui::GetItemRectMin();
        const ImVec2 nodeMax = ImGui::GetItemRectMax();

        if (isRenaming)
        {
            // IsItemDeactivated() 覆盖了回车、点别处失焦、Esc(ImGui 对 InputText 的默认行为是
            // Esc 把内容还原成激活时的值再失焦)这三种情况——不管走哪条路都统一交给
            // commitRename() 处理, 它内部会判断"名字没变"直接当无操作放弃。
            // SameLine() 不传参数会额外插一段 style.ItemSpacing.x 间距——正常文字是 TreeNodeEx
            // 内部直接画的, 不吃这份间距, 传 0 才能让输入框贴在跟文字原本同一个起始位置。
            ImGui::SameLine(0.0f, 0.0f);
            if (mRenameFocusPending)
            {
                ImGui::SetKeyboardFocusHere();
                mRenameFocusPending = false;
            }
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            constexpr ImGuiInputTextFlags renameFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
            bool enterPressed = ImGui::InputText("##rename", &mRenameBuffer, renameFlags);
            if (enterPressed || ImGui::IsItemDeactivated())
            {
                // 真正执行改名不能在这里就做——原因跟 entitiesToDelete 一样, 递归画树的过程中途
                // 改 Scene::mEntities/mExistingNames 会让还没画完的部分迭代失效, 延迟到
                // onDraw() 整棵树画完之后统一处理。
                mPendingRenameCommit = PendingRename{ entityId, mRenameBuffer };
                mRenamingEntityId = Guid();
            }
        }

        if (ImGui::BeginDragDropSource())
        {
            beginRowDrag(Row{ entityId, HString() });
            ImGui::SetDragDropPayload(kRowsDragDropType, nullptr, 0);
            if (mDraggedEntityIds.size() == 1 && mDraggedFolderNames.empty())
                ImGui::Text("%.*s", static_cast<int>(name.size()), name.data());
            else
                ImGui::Text("%d Items", static_cast<int>(mDraggedEntityIds.size() + mDraggedFolderNames.size()));
            ImGui::EndDragDropSource();
        }

        // 一行分三个落点: 上沿 = 插到这一行之前, 下沿 = 插到之后, 中间 = 挂成它的子节点。
        // 每一种落点各自判断新父节点是否合法(isDisabledTarget 的整行置灰只代表"不能挂到这一行
        // 下面", 上下沿的新父节点是这一行的父节点, 不受它影响)。AcceptDragDropPayload() 不看
        // disabled 状态, 所以不合法的落点要自己不画指示、不接收。
        if (ImGui::BeginDragDropTarget())
        {
            // AcceptBeforeDelivery 让悬停期间每帧都能拿到 payload 用来画指示线, 松开鼠标那一帧
            // IsDelivery() 才为真; NoDrawDefaultRect 是因为高亮框/插入线自己画。
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                kRowsDragDropType,
                ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            if (payload && !mDraggedEntityIds.empty())
            {
                const DropZone zone = classifyDropZone(nodeMin, nodeMax);

                // 这一行是根 Entity 时, 放到它前后意味着进入它所在的容器(某个 Folder 或未归档根序列)
                Entity* targetParent = nullptr;
                HString targetFolder;
                size_t targetIndex = kAppendIndex;
                float lineStartX = contentX;
                if (zone == DropZone::Onto)
                {
                    targetParent = entity;
                }
                else
                {
                    bool isFirstChild = false;
                    Entity* anchor = entity;
                    if (zone == DropZone::After)
                    {
                        // 这一行是父级里最后一个子节点时, 它的下沿同时也是父级子树的下沿, 可以选更高的层级
                        int maxLevelsUp = 0;
                        for (Entity* current = entity; isLastChildEntity(current); current = current->getParent())
                            ++maxLevelsUp;

                        const AfterEdgeLevel level = classifyAfterEdge(contentX, hasChildren && open, maxLevelsUp);
                        isFirstChild = level.firstChild;
                        for (int i = 0; i < level.levelsUp; ++i)
                            anchor = anchor->getParent();
                        lineStartX = isFirstChild
                            ? contentX + ImGui::GetStyle().IndentSpacing
                            : contentX - static_cast<float>(level.levelsUp) * ImGui::GetStyle().IndentSpacing;
                    }

                    if (isFirstChild)
                    {
                        targetParent = entity;
                        targetIndex = 0;
                    }
                    else
                    {
                        targetParent = anchor->getParent();
                        if (!targetParent)
                            targetFolder = entity->getScene()->getFolderOfEntity(anchor);
                        targetIndex = anchor->getSiblingIndex() + (zone == DropZone::After ? 1 : 0);
                    }
                }

                if (canDropUnder(targetParent))
                {
                    if (zone == DropZone::Onto)
                    {
                        drawDropIndicator(nodeMin, nodeMax, true);
                    }
                    else
                    {
                        const float lineY = (zone == DropZone::Before) ? nodeMin.y : nodeMax.y;
                        drawDropIndicator(ImVec2(lineStartX, lineY), ImVec2(nodeMax.x, lineY), false);
                    }

                    if (payload->IsDelivery())
                    {
                        actions.entityDrop = DropRequest{ targetParent ? targetParent->getGuid() : Guid(), targetFolder, targetIndex };

                        // 一起拖的 Folder 跟着放进这一行所在的容器, 追加到末尾
                        const Entity* topAncestor = entity;
                        while (topAncestor->getParent())
                            topAncestor = topAncestor->getParent();
                        const HString containerFolder = entity->getScene()->getFolderOfEntity(topAncestor);
                        if (!mDraggedFolderNames.empty() && canDropFoldersUnder(*entity->getScene(), containerFolder))
                            actions.folderDrop = FolderDropRequest{ containerFolder, kAppendIndex };
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (!isRenaming && ImGui::BeginPopupContextItem())
        {
            // Rename 只能作用于单个对象: 只有恰好选中一项时才可点, 多选时置灰。
            // 菜单操作一律作用于当前选中集合, 跟 Delete 快捷键、拖拽的规则保持一致。
            const bool canRename = getSelectedItemCount() == 1;

            if (ImGui::MenuItem("Rename", "F2", false, canRename))
                beginRename(entityId, name);
            if (ImGui::MenuItem("Delete", "Del"))
                collectSelectedForDelete(actions);
            ImGui::EndPopup();
        }

        if (isDisabledTarget)
            ImGui::EndDisabled();

        // 状态列: 两个图标槽并排, 都用 AddText 在光标处直接画, 垂直基准完全一致
        ImGui::TableSetColumnIndex(1);
        const ImVec2 statusOrigin = ImGui::GetCursorPos();
        const float iconOffsetY = std::max(0.0f, (rowHeight - ImGui::CalcTextSize(ICON_MS_VISIBILITY).y) * 0.5f);

        if (thisInstance)
        {
            ImGui::SetCursorPos(ImVec2(statusOrigin.x, statusOrigin.y + iconOffsetY));
            ImGui::GetWindowDrawList()->AddText(ImGui::GetCursorScreenPos(), ImGui::GetColorU32(kPrefabTintColor), ICON_MS_PACKAGE_2);
        }

        if (Scene* scene = entity->getScene())
        {
            ImGui::PushID(entity);

            ImGui::SetCursorPos(ImVec2(statusOrigin.x + mIconSlotWidth, statusOrigin.y + iconOffsetY));
            const char* visibilityIcon = entity->isVisible() ? ICON_MS_VISIBILITY : ICON_MS_VISIBILITY_OFF;
            if (TextOnlyButton(visibilityIcon))
                applyVisibilityWithUndo(*scene, gatherVisibilityTargets(*scene, entity, HString()), !entity->isVisible());

            ImGui::PopID();
        }

        if (hasChildren && open)
        {
            for (size_t childIndex = 0; childIndex < entity->getChildCount(); ++childIndex)
                drawEntityNode(entity->getChild(childIndex), actions, effectivePrefabInstance);
            ImGui::TreePop();
        }
    }

    void Hierarchy::drawFolderNode(Scene& scene, HString folderName, FrameActions& actions)
    {
        const Scene::Folder* folder = scene.findFolder(folderName);
        if (!folder)
            return;

        const float rowHeight = mRowHeight;
        ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
        ImGui::TableSetColumnIndex(0);

        const std::string_view name = folderName.toStringView();
        const bool isRenaming = (folderName == mRenamingFolderName);
        const bool hasContent = !folder->childFolders.empty() || !folder->entities.empty();

        // 节点的展开状态靠这个字符串 ID 维持——改名之后 ID 变了, 展开状态会重置回默认展开。
        const std::string treeId = "##Folder:" + std::string(name);

        const int visibleIndex = static_cast<int>(mVisibleRows.size());
        mVisibleRows.push_back(Row{ Guid(), folderName });

        ImGuiTreeNodeFlags nodeFlags =
            ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_OpenOnDoubleClick |
            ImGuiTreeNodeFlags_SpanFullWidth |
            ImGuiTreeNodeFlags_SpanAllColumns |
            ImGuiTreeNodeFlags_NavLeftJumpsToParent |
            ImGuiTreeNodeFlags_FramePadding |
            ImGuiTreeNodeFlags_AllowOverlap |
            ImGuiTreeNodeFlags_DefaultOpen;
        if (!hasContent)
            nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (mSelection.isFolderSelected(folderName))
            nodeFlags |= ImGuiTreeNodeFlags_Selected;

        const float framePadY = std::max(0.0f, (rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::SetNextItemSelectionUserData(visibleIndex);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, framePadY));
        const float contentX = ImGui::GetCursorScreenPos().x + ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2.0f;
        bool open;
        if (isRenaming)
            open = ImGui::TreeNodeEx(treeId.c_str(), nodeFlags, "%s", ICON_MS_FOLDER);
        else
            open = ImGui::TreeNodeEx(treeId.c_str(), nodeFlags, "%s %.*s", ICON_MS_FOLDER, static_cast<int>(name.size()), name.data());
        ImGui::PopStyleVar();

        const ImVec2 nodeMin = ImGui::GetItemRectMin();
        const ImVec2 nodeMax = ImGui::GetItemRectMax();

        if (isRenaming)
        {
            ImGui::SameLine(0.0f, 0.0f);
            if (mFolderRenameFocusPending)
            {
                ImGui::SetKeyboardFocusHere();
                mFolderRenameFocusPending = false;
            }
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            constexpr ImGuiInputTextFlags renameFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
            const bool enterPressed = ImGui::InputText("##folderrename", &mFolderRenameBuffer, renameFlags);
            if (enterPressed || ImGui::IsItemDeactivated())
            {
                mPendingFolderRenameCommit = PendingFolderRename{ folderName, mFolderRenameBuffer };
                mRenamingFolderName = HString();
            }
        }

        if (ImGui::BeginDragDropSource())
        {
            beginRowDrag(Row{ Guid(), folderName });
            ImGui::SetDragDropPayload(kRowsDragDropType, nullptr, 0);
            if (mDraggedFolderNames.size() == 1 && mDraggedEntityIds.empty())
                ImGui::Text("%.*s", static_cast<int>(name.size()), name.data());
            else
                ImGui::Text("%d Items", static_cast<int>(mDraggedEntityIds.size() + mDraggedFolderNames.size()));
            ImGui::EndDragDropSource();
        }

        // 一起拖的 Entity 放进 Folder 这部分选定的父级。
        if (ImGui::BeginDragDropTarget())
        {
            constexpr ImGuiDragDropFlags acceptFlags = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kRowsDragDropType, acceptFlags))
            {
                const bool hasEntities = !mDraggedEntityIds.empty();
                const bool hasFolders = !mDraggedFolderNames.empty();
                const DropZone zone = hasFolders ? classifyDropZone(nodeMin, nodeMax) : DropZone::Onto;

                HString targetParent;
                size_t targetIndex = kAppendIndex;
                float lineStartX = contentX;
                if (zone == DropZone::Onto)
                {
                    targetParent = folderName;
                }
                else
                {
                    bool isFirstChild = false;
                    HString anchor = folderName;
                    if (zone == DropZone::After)
                    {
                        int maxLevelsUp = 0;
                        for (HString current = folderName; isLastRowInParentFolder(scene, current); current = scene.getParentFolder(current))
                            ++maxLevelsUp;

                        const AfterEdgeLevel level = classifyAfterEdge(contentX, open && hasContent, maxLevelsUp);
                        isFirstChild = level.firstChild;
                        for (int i = 0; i < level.levelsUp; ++i)
                            anchor = scene.getParentFolder(anchor);
                        lineStartX = isFirstChild
                            ? contentX + ImGui::GetStyle().IndentSpacing
                            : contentX - static_cast<float>(level.levelsUp) * ImGui::GetStyle().IndentSpacing;
                    }

                    if (isFirstChild)
                    {
                        targetParent = folderName;
                        targetIndex = 0;
                    }
                    else
                    {
                        targetParent = scene.getParentFolder(anchor);
                        targetIndex = scene.getFolderIndex(anchor) + (zone == DropZone::After ? 1 : 0);
                    }
                }

                const bool folderLegal = hasFolders && canDropFoldersUnder(scene, targetParent);
                if (hasEntities || folderLegal)
                {
                    if (zone == DropZone::Onto)
                    {
                        drawDropIndicator(nodeMin, nodeMax, true);
                    }
                    else
                    {
                        const float lineY = (zone == DropZone::Before) ? nodeMin.y : nodeMax.y;
                        drawDropIndicator(ImVec2(lineStartX, lineY), ImVec2(nodeMax.x, lineY), false);
                    }

                    if (payload->IsDelivery())
                    {
                        if (hasEntities)
                            actions.entityDrop = DropRequest{ Guid(), targetParent, kAppendIndex };
                        if (folderLegal)
                            actions.folderDrop = FolderDropRequest{ targetParent, targetIndex };
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (!isRenaming && ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("New Folder"))
                actions.folderToCreateUnder = folderName;
            if (ImGui::MenuItem("Rename", "F2", false, getSelectedItemCount() == 1))
                beginFolderRename(folderName);
            if (ImGui::MenuItem("Delete", "Del"))
                collectSelectedForDelete(actions);
            ImGui::EndPopup();
        }

        // 状态列: Folder 只有可见性按钮, 放在跟 Entity 行同一个槽位上。显示状态只看它自己
        // 直接归档的根 Entity, 不递归子 Folder; 没有直接成员时不画。
        const std::vector<Entity*>& members = folder->entities;
        if (!members.empty())
        {
            ImGui::TableSetColumnIndex(1);
            const ImVec2 statusOrigin = ImGui::GetCursorPos();
            const float iconOffsetY = std::max(0.0f, (rowHeight - ImGui::CalcTextSize(ICON_MS_VISIBILITY).y) * 0.5f);

            const bool anyVisible = std::any_of(members.begin(), members.end(), [](const Entity* member) { return member->isVisible(); });
            ImGui::PushID(treeId.c_str());
            ImGui::SetCursorPos(ImVec2(statusOrigin.x + mIconSlotWidth, statusOrigin.y + iconOffsetY));
            if (TextOnlyButton(anyVisible ? ICON_MS_VISIBILITY : ICON_MS_VISIBILITY_OFF))
                applyVisibilityWithUndo(scene, gatherVisibilityTargets(scene, nullptr, folderName), !anyVisible);
            ImGui::PopID();
        }

        if (hasContent && open)
        {
            for (HString childFolder : folder->childFolders)
                drawFolderNode(scene, childFolder, actions);
            for (Entity* member : folder->entities)
            {
                if (!member->getParent())
                    drawEntityNode(member, actions, nullptr);
            }
            ImGui::TreePop();
        }
    }

    void Hierarchy::applySelectionRequests(
        ImGuiMultiSelectIO* multiSelect,
        std::span<const Row> visibleRows
    )
    {
        if (!multiSelect)
            return;

        auto setRowSelected = [this](const Row& row, bool selected)
        {
            if (row.entityId.isValid())
                mSelection.setSelected(row.entityId, selected);
            else
                mSelection.setFolderSelected(row.folderName, selected);
        };

        for (const ImGuiSelectionRequest& request : multiSelect->Requests)
        {
            if (request.Type == ImGuiSelectionRequestType_SetAll)
            {
                if (!request.Selected)
                {
                    mSelection.clear();
                }
                else
                {
                    for (const Row& row : visibleRows)
                        setRowSelected(row, true);
                }
                continue;
            }

            if (request.Type != ImGuiSelectionRequestType_SetRange ||
                request.RangeFirstItem < 0 ||
                request.RangeLastItem < 0)
                continue;

            size_t first = static_cast<size_t>(request.RangeFirstItem);
            size_t last = static_cast<size_t>(request.RangeLastItem);
            if (first >= visibleRows.size() || last >= visibleRows.size())
                continue;
            if (first > last)
                std::swap(first, last);

            for (size_t index = first; index <= last; ++index)
                setRowSelected(visibleRows[index], request.Selected);
        }
    }

    size_t Hierarchy::getSelectedItemCount() const
    {
        return mSelection.getSelectedCount() + mSelection.getSelectedFolders().size();
    }
}
