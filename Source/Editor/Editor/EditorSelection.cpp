#include "EditorSelection.h"

#include <Engine/Framework/Scene.h>

#include <algorithm>

namespace hyp::ed
{
    void EditorSelection::syncScene(Scene* scene)
    {
        if (mScene != scene)
        {
            mScene = scene;
            clear();
            return;
        }

        if (!mScene)
        {
            clear();
            return;
        }

        std::erase_if(
            mSelectedEntityIds,
            [this](Guid entityId)
            {
                return mScene->findEntity(entityId) == nullptr;
            }
        );
        std::erase_if(
            mSelectedFolders,
            [this](HString folderName)
            {
                return mScene->findFolder(folderName) == nullptr;
            }
        );
    }

    void EditorSelection::clear() noexcept
    {
        mSelectedEntityIds.clear();
        mSelectedFolders.clear();
    }

    void EditorSelection::selectOnly(Guid entityId)
    {
        clear();
        setSelected(entityId, true);
    }

    void EditorSelection::setSelected(Guid entityId, bool selected)
    {
        auto it = std::find(mSelectedEntityIds.begin(), mSelectedEntityIds.end(), entityId);
        if (!selected)
        {
            if (it != mSelectedEntityIds.end())
                mSelectedEntityIds.erase(it);
            return;
        }

        if (it != mSelectedEntityIds.end())
            return;
        if (!entityId.isValid() || !mScene || !mScene->findEntity(entityId))
            return;

        mSelectedEntityIds.emplace_back(entityId);
    }

    bool EditorSelection::isSelected(Guid entityId) const noexcept
    {
        return std::find(mSelectedEntityIds.begin(), mSelectedEntityIds.end(), entityId) !=
            mSelectedEntityIds.end();
    }

    void EditorSelection::setFolderSelected(HString folderName, bool selected)
    {
        auto it = std::find(mSelectedFolders.begin(), mSelectedFolders.end(), folderName);
        if (!selected)
        {
            if (it != mSelectedFolders.end())
                mSelectedFolders.erase(it);
            return;
        }

        if (it != mSelectedFolders.end())
            return;
        if (!mScene || !mScene->findFolder(folderName))
            return;

        mSelectedFolders.emplace_back(folderName);
    }

    bool EditorSelection::isFolderSelected(HString folderName) const noexcept
    {
        return std::find(mSelectedFolders.begin(), mSelectedFolders.end(), folderName) != mSelectedFolders.end();
    }

    void EditorSelection::replaceSelectedFolder(HString oldName, HString newName)
    {
        auto it = std::find(mSelectedFolders.begin(), mSelectedFolders.end(), oldName);
        if (it != mSelectedFolders.end())
            *it = newName;
    }

    Entity* EditorSelection::getSingleSelectedEntity() const noexcept
    {
        if (!mScene || mSelectedEntityIds.size() != 1)
            return nullptr;
        return mScene->findEntity(mSelectedEntityIds.front());
    }
}
