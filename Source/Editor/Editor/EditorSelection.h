#pragma once

#include <Core/Common/Guid.h>
#include <Core/Common/HString.h>

#include <cstddef>
#include <span>
#include <vector>

namespace hyp
{
    class Entity;
    class Scene;
}

namespace hyp::ed
{
    // 编辑器级共享选择状态。只保存稳定的 Guid, 不用裸指针或 TRef 延长已经从 Scene
    // 删除的 Entity 生命周期。Hierarchy 写入选择, Inspector/SceneView 等窗口读取。
    class EditorSelection
    {
    public:
        void syncScene(Scene* scene);

        void clear() noexcept;

        void selectOnly(Guid entityId);

        void setSelected(Guid entityId, bool selected);

        bool isSelected(Guid entityId) const noexcept;

        size_t getSelectedCount() const noexcept
        {
            return mSelectedEntityIds.size();
        }

        Entity* getSingleSelectedEntity() const noexcept;

        // Hierarchy 拖拽多选 reparent 用: 拖的行如果本身在选中集合里, 就把整个选中集合一起
        // 拖过去, 需要能拿到完整列表, 不只是数量/单选。
        std::span<const Guid> getSelectedEntityIds() const noexcept
        {
            return mSelectedEntityIds;
        }

        void setFolderSelected(HString folderName, bool selected);

        bool isFolderSelected(HString folderName) const noexcept;

        std::span<const HString> getSelectedFolders() const noexcept
        {
            return mSelectedFolders;
        }

        void replaceSelectedFolder(HString oldName, HString newName);

    private:
        Scene* mScene = nullptr;
        std::vector<Guid> mSelectedEntityIds;
        std::vector<HString> mSelectedFolders;
    };
}
