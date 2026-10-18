#include "AssetEditorRegistry.h"

#include <Core/Asset/Asset.h>
#include <Core/Reflection/Class.h>

namespace hyp::ed
{
    AssetEditorRegistry& AssetEditorRegistry::get()
    {
        static AssetEditorRegistry instance;
        return instance;
    }

    void AssetEditorRegistry::registerFactory(Class* assetRootClass, Factory factory)
    {
        mFactories[assetRootClass] = std::move(factory);
    }

    bool AssetEditorRegistry::tryOpenEditor(Asset& asset, AssetEditorTabHost& tabHost, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) const
    {
        auto it = mFactories.find(asset.getRootClass());
        if (it == mFactories.end())
            return false;

        Guid guid = asset.getRootGuid();
        if (AssetEditorTab* existing = tabHost.findAssetTab(guid))
        {
            existing->requestFocus();
            return true;
        }

        std::unique_ptr<AssetEditorTab> tab = it->second(asset, iconProvider, assetBrowser);
        tab->requestFocus();
        tabHost.addAssetTab(std::move(tab));
        return true;
    }
}
