#pragma once
#include "EditorWindow.h"
#include "ThumbnailProvider.h"
#include "AssetEditorTabHost.h"
#include "ImportDialog.h"

#include <Core/Asset/AssetManager.h>
#include <Core/Common/Guid.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hyp::ed
{
    // Windows 资源管理器"大图标"视图的编辑器版本: 左侧文件夹树 + 右侧缩略图网格,
    // 浏览的是 AssetManager 的虚拟资产树(Engine/、Project/ 两个根下的文件夹 + .asset 文件),
    // 不是裸文件系统——非 .asset 文件不在这里展示, 见 scanDirectory() 的说明。
    class AssetBrowser : public EditorWindow
    {
    public:
        AssetBrowser(ThumbnailProvider& iconProvider, AssetEditorTabHost& tabHost);

        ~AssetBrowser();

        // AssetComboEd() 的"Browse"按钮用: 导航到 guid 对应资产所在的目录并选中它, 顺带
        // 请求窗口焦点——跟双击文件夹跳转是同一套 navigateTo()/mSelectedPaths, 只是入口从
        // "用户点了某个 Entry"换成"外部给了一个 Guid"。guid 查不到资产就什么都不做。
        void revealAsset(Guid guid);

        // AssetComboEd() 的"Use Selected"按钮用: 当前目录网格里如果正好选中了恰好一项、且
        // 它不是文件夹, 返回它的 Guid; 否则(没选、多选、选中的是文件夹)返回空。是否跟
        // AssetCombo 要求的类型匹配不在这里判断——调用方自己知道 filterClass/includeDerived,
        // 这里只负责"当前选中的是哪一个资产"这一件事。
        std::optional<Guid> getSingleSelectedAssetGuid() const;

    private:
        struct Entry
        {
            std::string name;
            std::string virtualPath;
            bool isDirectory = false;
            Guid guid; // 仅 !isDirectory 时有效
        };

        void onDraw() override;

        // 目录内容按虚拟路径缓存, 进入/展开时才第一次扫盘, 之后复用——浏览器自己造成的
        // 变化(新建/删除)精确地把对应目录的 entry 擦掉重新扫一次, 用户点 Refresh 时
        // 整表清空(清空本身不触发扫描, 后续访问才懒加载重新扫)。
        const std::vector<Entry>& getOrScanDirectory(const std::string& virtualPath);
        std::vector<Entry> scanDirectory(const std::string& virtualPath) const;
        std::filesystem::path virtualPathToPhysicalDirectory(const std::string& virtualPath) const;
        void invalidateDirectory(const std::string& virtualPath);

        void navigateTo(std::string virtualPath);
        void goBack();
        void goForward();
        void goUp();

        void drawToolbar();
        void drawAddAssetMenu();
        void drawBreadcrumb();
        void drawFolderTree();
        void drawFolderTreeNode(const std::string& virtualPath, const std::string& displayName);
        void drawContentGrid();
        // 返回true表示这一帧里这个格子被单击了(用于外层统一处理多选/范围选逻辑,
        // 格子本身不知道自己在网格里的 flatIndex)
        bool drawEntry(const Entry& entry, bool selected);
        void drawEmptyAreaContextMenu();

        void handleEntryClick(const std::string& virtualPath, int flatIndex);

        void createNewFolder();

        // 在 parentPath 目录下登记一个指定 rootObject 类型的资产(只调 AssetManager::createAsset(),
        // 不 save()), 名字从 baseName 开始、跟 createNewFolder() 一样按 " (1)"/" (2)" 探测第一个
        // 未被占用的虚拟路径。createAsset() 失败(目前只会在路径已被占用时发生, 但探测循环已经
        // 排除了这种情况)时返回 nullptr。rootObject 传入前必须已经持有至少一份引用(调用方的
        // TRef), 这里只是转发给 AssetManager::createAsset(), 不负责它的生命周期。
        Asset* registerNewAsset(const std::string& parentPath, const std::string& baseName, Object* rootObject);

        // registerNewAsset() 之后立刻 save()——单个资产的创建走这条(比如 Add 菜单新建
        // Material Template/Instance), 一批互相引用的资产不能用这个, 见 importResult() 的注释。
        void createNewAsset(const std::string& parentPath, const std::string& baseName, Object* rootObject);

        // ImportDialog::draw() 点了 Import 那一帧的产出——统一处理"要不要先建子文件夹"+
        // 逐个落盘, 单对象(Texture2DImportDialog)和多对象(GLTFImportDialog 一次产出好几个
        // mesh/material/texture)导入走同一条路径。
        // result 是非 const 引用: 落盘之后要自己把 entries 清空, 参数详见 .cpp 里的注释
        void importResult(ImportDialog::Result& result);

        void deleteEntry(const Entry& entry);
        void showInExplorer(const std::filesystem::path& physicalPath);

        // 重命名走"内联编辑"交互(跟 Windows 资源管理器 F2 一致): beginRename() 记下正在编辑
        // 哪一项、用当前名字预填编辑框; drawEntry() 里如果发现自己就是正在编辑的那一项,
        // 用 InputText 换掉静态文字。IsItemDeactivated() 覆盖了回车/点别处失焦/Esc(ImGui 对
        // InputText 的默认行为是 Esc 把内容还原成激活时的值再失焦)这几种情况, 统一调
        // commitRename()——名字没变(包括被 Esc 还原的情况)它内部会直接当无操作放弃。
        // 只对右侧内容网格生效, 左侧文件夹树不接入(它现在没有独立的选中概念)。
        void selectAndBeginRename(const Entry& entry);
        void beginRename(const Entry& entry);
        void commitRename(const Entry& entry, const std::string& newName);
        void cancelRename();

    private:
        AssetManager& mAssetManager;
        ThumbnailProvider& mIconProvider;
        AssetEditorTabHost& mTabHost;

        std::string mCurrentPath = "Engine";
        std::vector<std::string> mHistory{ "Engine" };
        size_t mHistoryIndex = 0;

        std::unordered_map<std::string, std::vector<Entry>> mDirectoryCache;

        std::string mSearchFilter;
        // 96 DPI 下的设计基准值——构造函数里按 ImGuiFrontend::getDpiScale() 改写成实际初值,
        // 这里的字面量只是"没有更好数据时的兜底默认值", 不代表实际生效的初始像素数。
        float mThumbnailSize = 72.0f;
        float mTreeWidth = 200.0f;

        std::unordered_set<std::string> mSelectedPaths;
        int mLastClickedIndex = -1;

        std::string mRenamingPath; // 空 = 当前没有任何一项在被重命名
        std::string mRenameBuffer;
        bool mRenameFocusPending = false; // 刚进入编辑态的那一帧要抢一次键盘焦点

        // Delete/重命名一旦成功都会让 mDirectoryCache 里对应的 vector 失效(erase 或整体
        // clear)——drawContentGrid() 的网格循环用的是指向这个 vector 元素的裸指针
        // (visibleEntries), 如果在循环中途就真的执行删除/重命名, 后续几次迭代会访问已经被
        // 释放的内存。所以这两个动作只在 drawEntry() 里记下"待办", 真正执行挪到
        // drawContentGrid() 循环结束之后, 跟 Inspector.cpp 里 componentToRemove 的"画完再删"
        // 是同一个道理。
        std::optional<Entry> mPendingDelete;
        struct PendingRename
        {
            Entry entry;
            std::string newName;
        };
        std::optional<PendingRename> mPendingRenameCommit;

        // 触发时 make_unique 塞进来, onDraw() 里 !hasPendingImport() 就 erase, 用完即扔。
        std::vector<std::unique_ptr<ImportDialog>> mImportDialogs;
    };
}
