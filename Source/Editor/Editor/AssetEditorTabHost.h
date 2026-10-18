#pragma once
#include <Core/Common/Guid.h>

#include <memory>

namespace hyp::ed
{
    class AssetEditorTab;

    // AssetBrowser 双击资产时通过这个接口找/开对应的标签页, 不需要认识 EditorTabManager
    // 具体怎么管理标签页列表——EditorTabManager 是目前唯一的实现方。
    class AssetEditorTabHost
    {
    public:
        virtual ~AssetEditorTabHost() = default;

        // 按 Guid 找已经打开的资产 tab, 没有就返回 nullptr。
        virtual AssetEditorTab* findAssetTab(Guid guid) = 0;

        // 转移一个新建 tab 的所有权给 host——具体是哪个 Tab 子类由调用方(知道资产类型的
        // AssetEditorRegistrar)决定, host 不需要认识它是哪个子类。
        virtual void addAssetTab(std::unique_ptr<AssetEditorTab> tab) = 0;

        // AssetBrowser::revealAsset() 用: 不管调用方(AssetComboEd 的 Browse 按钮)当前在
        // 哪个资产 tab 里, 定位到某个资产都要切回 Main tab 才能真的看见 AssetBrowser 跳过去。
        virtual void activateMainTab() = 0;
    };
}
