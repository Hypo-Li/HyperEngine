#include "AssetEditorTab.h"

#include <Core/Asset/Asset.h>

namespace hyp::ed
{
    AssetEditorTab::AssetEditorTab(Asset& asset) :
        EditorTab(asset.getRootGuid().toString()),
        mAsset(asset)
    {
    }

    std::string AssetEditorTab::getTitle() const
    {
        return std::string(mAsset.getName().toStringView()) + (mAsset.isDirty() ? " *" : "");
    }

    Guid AssetEditorTab::getTargetGuid() const noexcept
    {
        return mAsset.getRootGuid();
    }
}
