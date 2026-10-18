#pragma once
#include <HypImGui/ImGuiAssetCombo.h>

namespace hyp::ed
{
    class AssetBrowser;
    class ThumbnailProvider;

    // AssetCombo() 的 Editor 层包装: 在两行缩略图布局空出来的第二行(见 ImGuiAssetCombo.cpp
    // 里"以后要加按钮的地方"那句注释)追加两个跟 AssetBrowser 联动的固定按钮——
    // "Browse" 把 AssetBrowser 定位并选中到这个引用当前指向的资产;
    // "Use Selected" 把 AssetBrowser 当前选中的同类型资产回填进 value。
    // 这两个按钮的行为不因调用方而变, 所以收进这一层包装, 各个 Panel 不需要各自拼一遍。
    //
    // AssetCombo() 本身完全不改——不需要知道 AssetBrowser 的存在, 依赖方向不变
    // (Runtime/HypImGui 不认识 Editor 的具体窗口类); 这里只是调用完之后, 在它已经留好、
    // 尚未使用的空白区域里用 SetCursorScreenPos() 追加绘制, 画完再把光标复位, 不影响
    // 调用方后续的布局。
    //
    // 缩略图统一交给 thumbnailProvider 按资产现查(贴图类型的引用会显示真实内容缩略图,
    // 取不到时回退默认占位图), 调用方不需要自己拼 iconForAsset 回调——这也是这个包装
    // 相比直接调 hyp::AssetCombo() 存在的意义之一, 见 AssetComboEd.cpp 里的实现。
    //
    // extraFilter 透传给 hyp::AssetCombo(), 见那边的说明。
    bool AssetComboEd(
        const char* strId,
        AssetManager& assetManager,
        Class* filterClass,
        bool includeDerived,
        Guid& value,
        AssetBrowser& assetBrowser,
        ThumbnailProvider& thumbnailProvider,
        const std::function<bool(Asset*)>& extraFilter = nullptr
    );
}
