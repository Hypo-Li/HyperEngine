#pragma once
#include "EditorWindow.h"
#include "EditorSelection.h"

#include <Engine/Engine.h>

#include <optional>
#include <span>
#include <vector>

namespace hyp
{
    class PrefabInstance;
}

namespace hyp::ed
{
    class UndoStack;

    class Hierarchy : public EditorWindow
    {
    public:
        Hierarchy(Engine& engine, EditorSelection& selection, UndoStack& undoStack);

        ~Hierarchy();

        void onDraw() override;

        // Hierarchy 自己没有独立的编辑历史——跟 Inspector/SceneView 编辑的是同一份场景,
        // 焦点停在这里时 Ctrl+Z/Ctrl+Y 也应该操作同一份共享 UndoStack。
        UndoStack* getUndoStack() noexcept override
        {
            return &mUndoStack;
        }

    private:
        // entityId 有效表示 Entity 行, 否则 folderName 非空表示 Folder 行。
        struct Row
        {
            Guid entityId;
            HString folderName;
        };

        // 实体被拖到某个位置: newParentId 无效表示成为根; 成为根时 folderName 是要放进的 Folder
        // (空 = 未归档)。index 是在目标容器(新父节点的子列表 / Folder 的成员列表 / 未归档根序列)
        // 里的位置, 取放下之前那一刻的列表状态, 被拖的实体自己还没摘出去; 追加到末尾用
        // kAppendIndex。
        struct DropRequest
        {
            Guid newParentId;
            HString folderName;
            size_t index;
        };

        // Folder 被拖到某个位置: newParentName 为空表示成为根 Folder, index 同上
        struct FolderDropRequest
        {
            HString newParentName;
            size_t index;
        };

        // 画树过程中收集的、要留到整棵树画完之后统一执行的操作——递归画树的过程中途改动
        // Scene 的 mEntities/mFolders 会让还没画完的那部分迭代失效, 跟 Inspector 删组件同样的
        // 道理。存 Guid/名字而不是指针: 执行时按 Guid/名字现查, 同一批里重复/已被前面一条连带
        // 删掉的条目会查不到而自动跳过, 不会拿着悬空指针重复处理。
        struct FrameActions
        {
            std::vector<Guid> entitiesToDelete;
            std::vector<HString> foldersToDelete;
            std::optional<DropRequest> entityDrop;
            std::optional<FolderDropRequest> folderDrop;
            std::optional<HString> folderToCreateUnder; // 有值就新建一个 Folder, 值是父 Folder 名(空 = 根)
        };

        // enclosingPrefabInstance: 从父节点递归传下来的, 这一支路径上最近的祖先(含自己)
        // 持有的 PrefabInstance——用它判断"这一行是不是某个 Prefab 实例子树的一部分", 不用
        // 每一行各自往上遍历 parent 链现查一遍, 递归本身已经是自顶向下, 顺路传下来是 O(1)。
        void drawEntityNode(Entity* entity, FrameActions& actions, PrefabInstance* enclosingPrefabInstance);

        // 画一个 Folder 行, 然后递归画它的子 Folder 和成员 Entity
        void drawFolderNode(Scene& scene, HString folderName, FrameActions& actions);

        // 被拖的实体里没有任何一个是 newParent 自己或它的祖先(否则会形成环), 并且满足
        // Mobility 规则: 拖的里面有 Static 时不能挂到 Dynamic 的父节点下。空父节点(根)总是允许。
        bool canDropUnder(const Entity* newParent) const;

        // 被拖的 Folder 里至少有一个能挂到 newParentName 下, 空(根)总是允许。
        bool canDropFoldersUnder(const Scene& scene, HString newParentName) const;

        // 把当前选中的 Entity 和 Folder 全部加入待删除列表。
        void collectSelectedForDelete(FrameActions& actions) const;

        // Entity 和 Folder 在同一对 undo/redo 闭包里处理, 一次删除只占一条 undo 记录。
        void deleteWithUndo(Scene& scene, std::span<const Guid> entityIds, std::span<const HString> folderNames);

        // Entity 和 Folder 在同一对 undo/redo 闭包里处理, 一次拖放只占一条 undo 记录。
        void moveWithUndo(
            Scene& scene,
            std::span<const Guid> entityIds, const std::optional<DropRequest>& entityDrop,
            std::span<const HString> folderNames, const std::optional<FolderDropRequest>& folderDrop
        );

        void createFolderWithUndo(Scene& scene, HString parentName);

        // 被拖的行在选中集合里就拖整个选中集合, 否则只拖这一行; 随被拖 Folder 一起走的内容不再单独算。
        void beginRowDrag(const Row& draggedRow);

        // 被点击的行在选中集合里就作用于整个选中集合, 否则只作用于这一行; Folder 展开成它的全部递归成员。
        std::vector<Guid> gatherVisibilityTargets(const Scene& scene, Entity* clickedEntity, HString clickedFolder) const;

        // 把 rootIds 里每个 Entity 连同它的整棵 Transform 子树设成 visible, 合并成一条 undo。
        void applyVisibilityWithUndo(Scene& scene, std::span<const Guid> rootIds, bool visible);

        // 新建一个空 Entity(根节点, 初始位置 (0, 0, 0)), 用序列化快照记录 undo/redo 以保证 redo 出来的还是同一个 Guid。
        void createEntityWithUndo(Scene& scene);

        // 重命名走"内联编辑"交互, 跟 AssetBrowser 的资产重命名同一套思路: beginRename() 记下
        // 正在编辑哪个 Entity、用当前名字预填编辑框; drawEntityNode() 里如果发现自己就是正在
        // 编辑的那个, 用 InputText 换掉 TreeNodeEx 的文字。真正的改名延迟到整棵树画完之后
        // 通过 mPendingRenameCommit 统一执行, 原因同 entitiesToDelete。
        void beginRename(Guid entityId, std::string_view currentName);
        void commitRename(Scene& scene, Guid entityId, const std::string& newName);
        void cancelRename();

        // Folder 的重命名走同样的内联编辑交互, 状态独立存放
        void beginFolderRename(HString folderName);
        void commitFolderRename(Scene& scene, HString oldName, const std::string& newName);
        void cancelFolderRename();

        void applySelectionRequests(
            ImGuiMultiSelectIO* multiSelect,
            std::span<const Row> visibleRows
        );

        // 选中集合里 Entity 和 Folder 加起来的数量
        size_t getSelectedItemCount() const;

    private:
        Engine& mEngine;
        EditorSelection& mSelection;
        UndoStack& mUndoStack;
        std::vector<Row> mVisibleRows;

        // 当前拖拽的 Entity / Folder 集合, payload 本身不携带数据(见 beginRowDrag)。
        std::vector<Guid> mDraggedEntityIds;
        std::vector<HString> mDraggedFolderNames;
        bool mDraggedContainsStatic = false;

        // 每帧在 onDraw() 开头按当前 DPI 和 style 算出来, drawEntityNode() 递归用
        float mRowHeight = 0.0f;

        // 状态列里一个图标槽的宽度, 同样每帧在 onDraw() 开头算出来
        float mIconSlotWidth = 0.0f;

        Guid mRenamingEntityId; // 无效 = 当前没有任何一项在被重命名
        std::string mRenameBuffer;
        bool mRenameFocusPending = false; // 刚进入编辑态的那一帧要抢一次键盘焦点

        struct PendingRename
        {
            Guid entityId;
            std::string newName;
        };
        std::optional<PendingRename> mPendingRenameCommit;

        HString mRenamingFolderName; // 空 = 当前没有任何 Folder 在被重命名
        std::string mFolderRenameBuffer;
        bool mFolderRenameFocusPending = false;

        struct PendingFolderRename
        {
            HString oldName;
            std::string newName;
        };
        std::optional<PendingFolderRename> mPendingFolderRenameCommit;
    };
}
