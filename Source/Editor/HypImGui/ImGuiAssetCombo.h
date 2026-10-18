#pragma once
#include <imgui.h>

#include <Core/Common/Guid.h>

#include <functional>

namespace hyp
{
    class AssetManager;
    class Asset;
    class Class;

    // AssetCombo() 的图标、AssetBrowser 资产条目共用的拖拽 payload 类型——rootClass 用来
    // 在 drop 端做类型校验(Class::isDerivedFrom()), 不需要现查一次 Asset。子类可以拖到
    // 认自己或祖先类的目标上, 反过来不行——校验规则见 AssetCombo() 里 filterClass/
    // includeDerived 的用法, drop 端复用的是同一条判断, 跟下拉列表里能选到的资产完全一致。
    // icon 是拖拽发起那一刻源头(AssetBrowser 的格子、AssetCombo 的图标)自己已经解析好、
    // 正在显示的那个纹理——悬浮预览直接拿它画(见 Editor.cpp), 不需要另外按 rootClass
    // 重新查一次图标。ImTextureID 底层是 ImU64(见 imgui.h), 平凡可拷贝, memcpy 安全。
    inline constexpr const char* kAssetDragDropType = "HYP_ASSET";

    struct AssetDragDropPayload
    {
        Guid guid;
        Class* rootClass = nullptr;
        ImTextureID icon = 0;
    };

    // Guid 引用型字段的通用选择控件: 列出 assetManager 里 rootClass == filterClass(或者
    // includeDerived=true 时, rootClass 是 filterClass 的派生类)的所有资产, 选中后回填 value。
    // filterClass 传 nullptr 表示不按类型过滤, 列出全部资产(此时忽略 includeDerived)。
    //
    // iconForAsset 可选: 想在每一项文字前面画一个图标(比如资产浏览器用的通用图标)就传一个
    // 回调, 不传(默认为空)就是纯文字列表, 传了返回 0 视为"这一项没有图标"。这个控件本身
    // 不关心图标纹理从哪加载、什么时候被 Vulkan prepare 上 GPU——那是编辑器那边的事(见
    // ThumbnailProvider)。
    //
    // extraFilter 可选: filterClass/includeDerived 之外需要按资产内容排除候选项时传一个
    // 谓词(比如"选了这一项会形成循环引用"), 返回 false 的资产不出现在下拉列表里——不传
    // (默认为空)就只按类型过滤, 跟原来行为一致。
    //
    // 返回 true 表示这一帧用户选了新的资产(value 已经被改写)。
    bool AssetCombo(
        const char* strId,
        AssetManager& assetManager,
        Class* filterClass,
        bool includeDerived,
        Guid& value,
        const std::function<ImTextureID(Asset*)>& iconForAsset = nullptr,
        const std::function<bool(Asset*)>& extraFilter = nullptr
    );

    // 带缩略图的 AssetCombo(iconForAsset 非空时)会把所在行撑到两行高——调用方如果把它摆进
    // BeginPropertyTable/自建的表格里, 需要拿这个高度去把同一行里的其它单行内容(label 文字、
    // Reset/Remove 按钮)垂直居中, 不然会贴在撑高之后的行顶部。iconForAsset 为空时 AssetCombo
    // 退化成单行布局, 这种情况下调用方不需要用到这个值。
    float GetAssetComboRowHeight() noexcept;
}
