#include "AssetBrowser.h"
#include "AssetEditorRegistry.h"
#include "FontImportDialog.h"
#include "GLTFImportDialog.h"
#include "Texture2DImportDialog.h"
#include "TextureCubeImportDialog.h"

#include <Engine/Context.h>
#include <Engine/Material/MaterialFunctionLibrary.h>
#include <Engine/Material/MaterialInstance.h>
#include <Engine/Material/MaterialTemplate.h>
#include <HypImGui/ImGuiFrontend.h>
#include <HypImGui/ImGuiWidgets.h>
#include <HypImGui/IconsMaterialSymbols.h>
#include <HypImGui/ImGuiAssetCombo.h>

#include <imgui_stdlib.h>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <shlobj.h>
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#endif

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdlib>
#include <string_view>

namespace hyp::ed
{
    namespace
    {
        // 都是 96 DPI 下的设计基准值(像素), 用到的地方要乘 ImGuiFrontend::getDpiScale()——
        // 跟 ImGui 内置控件不一样, 这些是本文件手写像素画格子/树缩进/分隔条用的, 不会被
        // ImGuiStyle::ScaleAllSizes() 那次全局缩放覆盖到。
        constexpr float kCellPaddingBase = 8.0f;
        constexpr float kTreeIndentSpacingBase = 12.0f;
        constexpr float kTreeSplitterWidthBase = 4.0f;
        constexpr float kMinPaneWidthBase = 80.0f;

        // 跟 UE Content Browser 缩略图下方名字的排版一致: 最多三行, 换行时尽量保留整个"词"——
        // 但资产/文件名基本不含空格(SM_Body、T_BaseColor_01 这类), 所以这里的"词"不是按空格分,
        // 是按下划线和 camelCase 大小写转换分(见 splitIntoWords)。三行还放不下的部分用 ... 截断。
        size_t utf8CharLength(char leadByte)
        {
            unsigned char b = static_cast<unsigned char>(leadByte);
            if ((b & 0x80) == 0x00) return 1;
            if ((b & 0xE0) == 0xC0) return 2;
            if ((b & 0xF0) == 0xE0) return 3;
            if ((b & 0xF8) == 0xF0) return 4;
            return 1; // 非法前导字节, 当单字节处理, 不阻塞截断逻辑
        }

        // 按 UTF-8 码点边界(不是字节)找"截到这里再加上...刚好不超过 maxWidth"的最长前缀——
        // 按字节二分会在多字节字符(比如中文名字)中间截断, 产生乱码
        std::string truncateWithEllipsis(const std::string& name, float maxWidth)
        {
            if (ImGui::CalcTextSize(name.c_str()).x <= maxWidth)
                return name;

            constexpr const char* kEllipsis = "...";
            float ellipsisWidth = ImGui::CalcTextSize(kEllipsis).x;
            if (ellipsisWidth >= maxWidth)
                return kEllipsis;

            size_t cut = 0;
            while (cut < name.size())
            {
                size_t next = cut + utf8CharLength(name[cut]);
                if (next > name.size())
                    break;
                if (ImGui::CalcTextSize(name.c_str(), name.c_str() + next).x + ellipsisWidth > maxWidth)
                    break;
                cut = next;
            }
            return name.substr(0, cut) + kEllipsis;
        }

        constexpr int kMaxLabelLines = 3;

        // 分词边界规则(近似 UE 的做法): 下划线之后另起一段(下划线本身留在前一段末尾,
        // "SM_Body" -> "SM_" / "Body"); 小写或数字后面接大写时断一次(camelCase, "BaseColor"
        // -> "Base" / "Color"); 连续大写字母后面接小写时, 断点放在这串大写字母的最后一个字符
        // 之前, 让紧跟着的完整单词自成一段而不是被拆开("HTMLParser" -> "HTML" / "Parser")
        std::vector<std::string_view> splitIntoWords(std::string_view name)
        {
            std::vector<std::string_view> words;
            size_t start = 0;
            for (size_t i = 1; i < name.size(); ++i)
            {
                unsigned char prev = static_cast<unsigned char>(name[i - 1]);
                unsigned char curr = static_cast<unsigned char>(name[i]);
                bool boundary =
                    prev == '_' ||
                    (std::islower(prev) && std::isupper(curr)) ||
                    (std::isupper(prev) && std::isupper(curr) && i + 1 < name.size() && std::islower(static_cast<unsigned char>(name[i + 1])));

                if (boundary)
                {
                    words.push_back(name.substr(start, i - start));
                    start = i;
                }
            }
            if (start < name.size())
                words.push_back(name.substr(start));
            return words;
        }

        // 贪心按行塞词: 一个词只要还能塞进当前行就塞, 塞不下就换下一行, 最多排 kMaxLabelLines 行。
        // 排到行数上限后如果还有词剩下(或者单个词本身就比一行还宽, 没有分词点可用), 对最后一行
        // 做省略号截断
        std::vector<std::string> wrapLabelIntoLines(const std::string& name, float maxWidth)
        {
            std::vector<std::string_view> words = splitIntoWords(name);
            std::vector<std::string> lines;

            size_t wordIndex = 0;
            while (wordIndex < words.size() && static_cast<int>(lines.size()) < kMaxLabelLines)
            {
                std::string line(words[wordIndex]);
                ++wordIndex;

                if (ImGui::CalcTextSize(line.c_str()).x <= maxWidth)
                {
                    while (wordIndex < words.size())
                    {
                        std::string candidate = line + std::string(words[wordIndex]);
                        if (ImGui::CalcTextSize(candidate.c_str()).x > maxWidth)
                            break;
                        line = std::move(candidate);
                        ++wordIndex;
                    }
                }

                lines.push_back(std::move(line));
            }

            bool hasLeftoverWords = wordIndex < words.size();
            bool lastLineTooWide = !lines.empty() && ImGui::CalcTextSize(lines.back().c_str()).x > maxWidth;
            if ((hasLeftoverWords || lastLineTooWide) && !lines.empty())
                lines.back() = truncateWithEllipsis(lines.back(), maxWidth);

            return lines;
        }

        // 新建材质模板预填的占位源码——MaterialSource 是编译期依赖, 留空会编译失败
        constexpr const char* kDefaultMaterialSource =
            "void evaluateMaterial(in MaterialInput mi, inout MaterialOutput mo)\n"
            "{\n"
            "}\n";
    }

    AssetBrowser::AssetBrowser(ThumbnailProvider& iconProvider, AssetEditorTabHost& tabHost) :
        EditorWindow("Asset Browser", "AssetBrowser"),
        mAssetManager(Context::get().getAssetManager()),
        mIconProvider(iconProvider),
        mTabHost(tabHost)
    {
        setIcon(ICON_MS_FOLDER_OPEN);

        float dpiScale = ImGuiFrontend::getDpiScale();
        mThumbnailSize *= dpiScale;
        mTreeWidth *= dpiScale;
    }

    AssetBrowser::~AssetBrowser()
    {
    }

    std::filesystem::path AssetBrowser::virtualPathToPhysicalDirectory(const std::string& virtualPath) const
    {
        // 转换逻辑本体挪到了 AssetManager::convertAssetDirectoryToPhysicalPath(), 这里只是
        // 转发——避免同一套 Engine/Project 根路径转换规则在两处各写一份、以后改一处忘了改
        // 另一处。
        return mAssetManager.convertAssetDirectoryToPhysicalPath(virtualPath);
    }

    std::vector<AssetBrowser::Entry> AssetBrowser::scanDirectory(const std::string& virtualPath) const
    {
        std::vector<Entry> entries;

        std::filesystem::path physicalDir = virtualPathToPhysicalDirectory(virtualPath);
        if (physicalDir.empty() || !std::filesystem::is_directory(physicalDir))
            return entries;

        std::error_code ec;
        for (const std::filesystem::directory_entry& dirEntry : std::filesystem::directory_iterator(physicalDir, ec))
        {
            // path::string() 在 Windows 上转的是当前 ANSI 代码页, 不是 UTF-8——名字带中文时
            // 拿到的是 GBK 字节, 既在 ImGui 里显示成乱码, 拼出来的 virtualPath 也跟 AssetManager
            // 里登记的 UTF-8 路径对不上。u8string() 是 C++20 起标准保证按 UTF-8 转换的那个重载。
            auto pathToUtf8 = [](const std::filesystem::path& p)
            {
                std::u8string u8 = p.u8string();
                return std::string(u8.begin(), u8.end());
            };

            if (dirEntry.is_directory())
            {
                Entry entry;
                entry.name = pathToUtf8(dirEntry.path().filename());
                entry.virtualPath = virtualPath + "/" + entry.name;
                entry.isDirectory = true;
                entries.push_back(std::move(entry));
            }
            else if (dirEntry.is_regular_file() && dirEntry.path().extension() == ".asset")
            {
                std::string assetPath = mAssetManager.convertPhysicalPathToAssetPath(dirEntry.path());
                Asset* asset = mAssetManager.getAsset(assetPath);
                if (!asset)
                    continue; // AssetManager 只在 initialize() 时扫描过一次; 编辑器运行期间从外部
                              // 新增的 .asset 文件还没有登记, 这里没办法展示——刷新缓存解决不了这个问题,
                              // 需要 AssetManager 自己提供"运行期登记新资产"的接口才行

                Entry entry;
                entry.name = pathToUtf8(dirEntry.path().stem());
                entry.virtualPath = std::move(assetPath);
                entry.isDirectory = false;
                entry.guid = asset->getRootGuid();
                entries.push_back(std::move(entry));
            }
        }

        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
        {
            if (a.isDirectory != b.isDirectory)
                return a.isDirectory > b.isDirectory;
            return a.name < b.name;
        });

        return entries;
    }

    const std::vector<AssetBrowser::Entry>& AssetBrowser::getOrScanDirectory(const std::string& virtualPath)
    {
        auto it = mDirectoryCache.find(virtualPath);
        if (it != mDirectoryCache.end())
            return it->second;
        return mDirectoryCache.emplace(virtualPath, scanDirectory(virtualPath)).first->second;
    }

    void AssetBrowser::invalidateDirectory(const std::string& virtualPath)
    {
        mDirectoryCache.erase(virtualPath);
    }

    void AssetBrowser::navigateTo(std::string virtualPath)
    {
        if (virtualPath == mCurrentPath)
            return;

        mHistory.resize(mHistoryIndex + 1);
        mHistory.push_back(virtualPath);
        mHistoryIndex = mHistory.size() - 1;
        mCurrentPath = std::move(virtualPath);
        mSelectedPaths.clear();
        mLastClickedIndex = -1;
    }

    void AssetBrowser::goBack()
    {
        if (mHistoryIndex == 0)
            return;
        --mHistoryIndex;
        mCurrentPath = mHistory[mHistoryIndex];
        mSelectedPaths.clear();
        mLastClickedIndex = -1;
    }

    void AssetBrowser::goForward()
    {
        if (mHistoryIndex + 1 >= mHistory.size())
            return;
        ++mHistoryIndex;
        mCurrentPath = mHistory[mHistoryIndex];
        mSelectedPaths.clear();
        mLastClickedIndex = -1;
    }

    void AssetBrowser::goUp()
    {
        size_t slash = mCurrentPath.find_last_of('/');
        if (slash == std::string::npos)
            return; // 已经在根("Engine"/"Project")上, 没有上一级
        navigateTo(mCurrentPath.substr(0, slash));
    }

    void AssetBrowser::revealAsset(Guid guid)
    {
        Asset* asset = mAssetManager.getAsset(guid);
        if (!asset)
            return;

        const std::string& path = asset->getPath();
        size_t slash = path.find_last_of('/');
        // 根资产(直接摆在 "Engine"/"Project" 下、virtualPath 里没有 '/')理论上不会出现——
        // 所有资产至少挂在一个根目录名下面——但没有的话就不挪动当前目录, 只处理选中状态。
        if (slash != std::string::npos)
            navigateTo(path.substr(0, slash));

        mSelectedPaths.clear();
        mSelectedPaths.insert(path);
        mLastClickedIndex = -1;
        requestFocus();
        mTabHost.activateMainTab();
    }

    std::optional<Guid> AssetBrowser::getSingleSelectedAssetGuid() const
    {
        if (mSelectedPaths.size() != 1)
            return std::nullopt;

        // 选中状态只可能来自当前目录网格里的 Entry(navigateTo/goBack/goForward 都会在
        // 离开当前目录时清空 mSelectedPaths), 所以只需要在 mCurrentPath 已缓存的条目里找。
        auto it = mDirectoryCache.find(mCurrentPath);
        if (it == mDirectoryCache.end())
            return std::nullopt;

        const std::string& selectedPath = *mSelectedPaths.begin();
        for (const Entry& entry : it->second)
        {
            if (!entry.isDirectory && entry.virtualPath == selectedPath)
                return entry.guid;
        }
        return std::nullopt;
    }

    void AssetBrowser::onDraw()
    {
        drawToolbar();

        std::erase_if(mImportDialogs, [](const std::unique_ptr<ImportDialog>& dialog) { return !dialog->hasPendingImport(); });
        for (std::unique_ptr<ImportDialog>& dialog : mImportDialogs)
            if (std::optional<ImportDialog::Result> result = dialog->draw())
                importResult(*result);

        ImGui::Separator();

        // 左右两块都是独立的 BeginChild, 宽度完全由我们自己每帧传入的数值决定——不像属性表
        // 那个 ImGui Table 列宽案例, 这里没有"手动拖过一次就自己焊死、不认后续传入值"的坑,
        // 直接一个持久化的成员(mTreeWidth, AssetBrowser 本身跨帧存活)配一条自绘拖拽条即可。
        float dpiScale = ImGuiFrontend::getDpiScale();
        float minPaneWidth = kMinPaneWidthBase * dpiScale;
        float treeSplitterWidth = kTreeSplitterWidthBase * dpiScale;

        float paneHeight = ImGui::GetContentRegionAvail().y;
        float availWidth = ImGui::GetContentRegionAvail().x;
        mTreeWidth = std::clamp(mTreeWidth, minPaneWidth, std::max(minPaneWidth, availWidth - minPaneWidth - treeSplitterWidth));

        if (ImGui::BeginChild("AssetBrowser.Tree", ImVec2(mTreeWidth, paneHeight), ImGuiChildFlags_Borders))
            drawFolderTree();
        ImGui::EndChild();

        // 两个 SameLine 都传 0 间距: 默认 SameLine() 会在前后各加一份 ItemSpacing.x, 跟
        // 两块 Child 之间"本来就有的 padding"叠在一起, 拖拽条看起来比它实际的
        // kTreeSplitterWidthBase 宽出一大截。
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushID("##TreeSplitter");
        ImGui::InvisibleButton("##split", ImVec2(treeSplitterWidth, paneHeight));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive())
            mTreeWidth += ImGui::GetIO().MouseDelta.x;
        ImGui::PopID();
        ImGui::SameLine(0.0f, 0.0f);

        if (ImGui::BeginChild("AssetBrowser.Content", ImVec2(0, paneHeight), ImGuiChildFlags_Borders))
            drawContentGrid();
        ImGui::EndChild();
    }

    void AssetBrowser::drawToolbar()
    {
        float dpiScale = ImGuiFrontend::getDpiScale();

        // 第一行: 后退/前进/上一级 + 根切换 + 面包屑——这些都是"路径导航", 跟 UE Content
        // Browser 一样放在同一行。第二行才是 Refresh/搜索/缩略图大小这些跟"当前在哪"无关的
        // 过滤/显示选项。
        bool canGoBack = mHistoryIndex > 0;
        bool canGoForward = mHistoryIndex + 1 < mHistory.size();
        bool canGoUp = mCurrentPath.find('/') != std::string::npos;

        ImGui::BeginDisabled(!canGoBack);
        if (TextOnlyButton(ICON_MS_ARROW_BACK))
            goBack();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!canGoForward);
        if (TextOnlyButton(ICON_MS_ARROW_FORWARD))
            goForward();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!canGoUp);
        if (TextOnlyButton(ICON_MS_ARROW_UPWARD))
            goUp();
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (TextOnlyButton(ICON_MS_REFRESH))
            mDirectoryCache.clear();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f * dpiScale);
        bool isProjectRoot = mCurrentPath.starts_with("Project");
        int rootIndex = isProjectRoot ? 1 : 0;
        const char* rootNames[] = { "Engine", "Project" };
        if (ImGui::Combo("##Root", &rootIndex, rootNames, 2))
            navigateTo(rootNames[rootIndex]);

        ImGui::SameLine();
        drawBreadcrumb();

        bool addClicked = ImGui::Button(ICON_MS_ADD "Add");
        ImVec2 addButtonMin = ImGui::GetItemRectMin();
        ImVec2 addButtonMax = ImGui::GetItemRectMax();
        if (addClicked)
        {
            // 菜单左边紧贴按钮右边、按钮在菜单里垂直居中——pivot(0, 0.5) 让下面这个坐标落在
            // 菜单左边界的垂直中点上, 而不是 SetNextWindowPos 默认的左上角。跟面包屑分隔符弹出菜单
            // 一样, 只有确定这一帧真的会 BeginPopup 成功(即将要 OpenPopup)才能调用它——见那边
            // 的注释。
            ImVec2 anchor(addButtonMax.x, (addButtonMin.y + addButtonMax.y) * 0.5f);
            ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(0.0f, 0.5f));
            ImGui::OpenPopup("AssetBrowser.AddAsset");
        }
        drawAddAssetMenu();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f * dpiScale);
        char searchBuffer[128];
        std::snprintf(searchBuffer, sizeof(searchBuffer), "%s", mSearchFilter.c_str());
        if (ImGui::InputTextWithHint("##Search", "Search...", searchBuffer, sizeof(searchBuffer)))
            mSearchFilter = searchBuffer;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f * dpiScale);
        float thumbnailSize = mThumbnailSize;
        if (ImGui::SliderFloat("##ThumbnailSize", &thumbnailSize, 32.0f * dpiScale, 160.0f * dpiScale, "%.0f"))
            mThumbnailSize = thumbnailSize;
    }

    // 每种资产类型对应哪个 C++ 类是硬编码在这里的 MenuItem 分支里, 不是走反射遍历
    // "所有可创建的 Class", 加新类型直接照抄一个分支即可。
    void AssetBrowser::drawAddAssetMenu()
    {
        if (!ImGui::BeginPopup("AssetBrowser.AddAsset"))
            return;

        if (ImGui::MenuItem("Material Template"))
        {
            TRef<MaterialTemplate> materialTemplate = createObject<MaterialTemplate>(nullptr);
            materialTemplate->setMaterialSource(kDefaultMaterialSource);
            materialTemplate->apply();
            createNewAsset(mCurrentPath, "NewMaterialTemplate", materialTemplate);
        }
        if (ImGui::MenuItem("Material Instance"))
        {
            TRef<MaterialInstance> materialInstance = createObject<MaterialInstance>(nullptr);
            createNewAsset(mCurrentPath, "NewMaterialInstance", materialInstance);
        }
        if (ImGui::MenuItem("Material Function Library"))
        {
            TRef<MaterialFunctionLibrary> library = createObject<MaterialFunctionLibrary>(nullptr);
            createNewAsset(mCurrentPath, "NewMaterialFunctionLibrary", library);
        }
        if (ImGui::MenuItem("Texture 2D"))
        {
            auto dialog = std::make_unique<Texture2DImportDialog>();
            dialog->beginImport();
            mImportDialogs.push_back(std::move(dialog));
        }
        if (ImGui::MenuItem("Texture Cube"))
        {
            auto dialog = std::make_unique<TextureCubeImportDialog>();
            dialog->beginImport();
            mImportDialogs.push_back(std::move(dialog));
        }
        if (ImGui::MenuItem("Font"))
        {
            auto dialog = std::make_unique<FontImportDialog>();
            dialog->beginImport();
            mImportDialogs.push_back(std::move(dialog));
        }
        // 菜单项叫"Mesh"而不是"Static Mesh"/"Skeletal Mesh"——一个 glTF 文件里每个 mesh 是否
        // 带骨骼各不相同, 产出 StaticMesh 还是 SkeletalMesh 由 GLTFLoader::load() 按每个 mesh
        // 自动判定(见 GLTFImportDialog 的 Import Skinning 选项), 不是用户在这里提前选的。
        // 目前唯一的实现是 GLTFImportDialog(只认 .gltf/.glb)——以后要加别的网格格式, 应该是
        // 新增一个格式专属的 XxxImportDialog, 这里改成先弹通用文件框再按后缀分派到对应
        // dialog, 不是把 GLTFImportDialog 泛化成什么都能导的"MeshImportDialog"。
        if (ImGui::MenuItem("Mesh"))
        {
            auto dialog = std::make_unique<GLTFImportDialog>();
            dialog->beginImport();
            mImportDialogs.push_back(std::move(dialog));
        }

        ImGui::EndPopup();
    }

    void AssetBrowser::drawBreadcrumb()
    {
        std::vector<std::string_view> segments;
        size_t start = 0;
        while (start <= mCurrentPath.size())
        {
            size_t slash = mCurrentPath.find('/', start);
            if (slash == std::string::npos)
            {
                segments.push_back(std::string_view(mCurrentPath).substr(start));
                break;
            }
            segments.push_back(std::string_view(mCurrentPath).substr(start, slash - start));
            start = slash + 1;
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImGuiStyle& style = ImGui::GetStyle();
        float segPaddingX = 6.0f * ImGuiFrontend::getDpiScale();
        float sepSpacing = 4.0f * ImGuiFrontend::getDpiScale();
        static const float sepWidth = ImGui::CalcTextSize(ICON_MS_CHEVRON_RIGHT).x;

        // 宽度固定撑满这一行, 不随路径深浅变化; 高度用 GetFrameHeight()/style.FramePadding, 跟同一行其它控件对齐
        ImVec2 frameMin = ImGui::GetCursorScreenPos();
        ImVec2 frameSize(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
        ImVec2 frameMax(frameMin.x + frameSize.x, frameMin.y + frameSize.y);

        drawList->AddRectFilled(frameMin, frameMax, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
        drawList->AddRect(frameMin, frameMax, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding);

        // 宽度固定之后, 路径深了就可能放不下——直接裁掉超出的部分, 不做省略号/滚动这类更复杂
        // 的处理(项目内路径深度有限, 真遇到超长再按需加)。裁剪矩形本身已经能防止画出界,
        // 下面提前 break 只是不必要地再去创建根本看不见的 InvisibleButton。
        drawList->PushClipRect(frameMin, frameMax, true);

        float textY = frameMin.y + style.FramePadding.y;
        float cursorX = frameMin.x + style.FramePadding.x;
        std::string accumulated;
        for (size_t i = 0; i < segments.size(); ++i)
        {
            if (i > 0)
            {
                // 分隔符左边这一段(此时 accumulated 还没加上 "/", 正好就是它)是分隔符弹出菜单
                // 要列子目录的那个目录
                std::string parentPath = accumulated;
                accumulated += "/";

                float sepClickWidth = sepWidth + sepSpacing * 2.0f;
                ImVec2 sepMin(cursorX, frameMin.y);
                ImVec2 sepMax(cursorX + sepClickWidth, frameMax.y);

                if (sepMin.x >= frameMax.x)
                    break;

                char popupId[32];
                std::snprintf(popupId, sizeof(popupId), "##sepPopup%d", static_cast<int>(i));

                ImGui::PushID(static_cast<int>(i));
                ImGui::PushID("Separator");
                ImGui::SetCursorScreenPos(sepMin);
                ImGui::InvisibleButton("##sep", ImVec2(sepMax.x - sepMin.x, sepMax.y - sepMin.y));
                bool sepHovered = ImGui::IsItemHovered();
                bool sepClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                ImGui::PopID();
                ImGui::PopID();

                if (sepHovered)
                    drawList->AddRectFilled(sepMin, sepMax, ImGui::GetColorU32(ImGuiCol_HeaderHovered), 3.0f);
                drawList->AddText(ImVec2(cursorX + sepSpacing, textY), ImGui::GetColorU32(ImGuiCol_Text), ICON_MS_CHEVRON_RIGHT);

                if (sepClicked)
                {
                    // SetNextWindowPos 只能在确定这一帧真的会 BeginPopup 成功的时候调用——
                    // 如果每帧都无条件调用, 一旦这个分隔符的 popup 没打开, BeginPopup 内部根本
                    // 不会走到 Begin(), 这份位置数据就不会被消费, 会串给这一帧里下一个真正
                    // Begin 的窗口, 导致完全不相关的窗口被挪位置——放进 sepClicked 分支里就没
                    // 这个问题, 且弹出后一直到关闭前位置也不需要每帧重新设置。
                    ImGui::SetNextWindowPos(ImVec2(sepMax.x, frameMax.y));
                    ImGui::OpenPopup(popupId);
                }

                if (ImGui::BeginPopup(popupId, ImGuiWindowFlags_None))
                {
                    const std::vector<Entry>& children = getOrScanDirectory(parentPath);
                    bool anyChild = false;
                    for (const Entry& child : children)
                    {
                        if (!child.isDirectory)
                            continue;
                        anyChild = true;
                        if (ImGui::MenuItem(child.name.c_str()))
                            navigateTo(child.virtualPath);
                    }
                    if (!anyChild)
                        ImGui::TextDisabled("No subfolders");
                    ImGui::EndPopup();
                }

                cursorX = sepMax.x;
            }
            accumulated += segments[i];

            // 手写一个扁平的"面包屑段": 只有悬浮时才画一个淡淡的圆角背景, 不带按钮那种常驻的
            // 边框/立体感——这样一眼看上去是路径文字, 不是"一排按钮"。所有段统一用正常文字
            // 颜色(之前给非当前目录用暗色, 反而会让人以为那些目录点不了, 传递了错误的信息)。
            std::string label(segments[i]);

            if (cursorX >= frameMax.x)
                break;

            float textWidth = ImGui::CalcTextSize(label.c_str()).x;
            ImVec2 itemMin(cursorX, frameMin.y);
            ImVec2 itemMax(cursorX + textWidth + segPaddingX * 2.0f, frameMax.y);

            ImGui::PushID(static_cast<int>(i));
            ImGui::SetCursorScreenPos(itemMin);
            ImGui::InvisibleButton("##crumb", ImVec2(itemMax.x - itemMin.x, itemMax.y - itemMin.y));
            bool hovered = ImGui::IsItemHovered();
            bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            ImGui::PopID();

            if (hovered)
                drawList->AddRectFilled(itemMin, itemMax, ImGui::GetColorU32(ImGuiCol_HeaderHovered), 3.0f);

            drawList->AddText(ImVec2(cursorX + segPaddingX, textY), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());

            if (clicked)
                navigateTo(accumulated);

            cursorX = itemMax.x + sepSpacing;
        }

        drawList->PopClipRect();

        // 手写小组件收尾的标准做法: 用一个跟外框位置/尺寸完全一致的 Dummy 重新"报备"一次给
        // ImGui 的布局系统, 覆盖掉上面循环里每个分段各自 InvisibleButton 遗留下的光标状态——
        // 保证 drawToolbar() 里紧跟在这个函数后面的控件(不管是 SameLine 还是直接换行)都落在
        // 正确位置, 不受中间这些手动摆放坐标的影响。
        ImGui::SetCursorScreenPos(frameMin);
        ImGui::Dummy(frameSize);
    }

    void AssetBrowser::drawFolderTree()
    {
        // 默认的 IndentSpacing(21px)对文件夹树这种可能嵌套好几层的场景偏宽, 缩进几层之后
        // 名字就被挤到很靠右——只在这棵树绘制期间局部覆盖, 不改全局 style。
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, kTreeIndentSpacingBase * ImGuiFrontend::getDpiScale());
        drawFolderTreeNode("Engine", "Engine");
        drawFolderTreeNode("Project", "Project");
        ImGui::PopStyleVar();
    }

    void AssetBrowser::drawFolderTreeNode(const std::string& virtualPath, const std::string& displayName)
    {
        const std::vector<Entry>& entries = getOrScanDirectory(virtualPath);

        bool hasSubfolders = std::any_of(entries.begin(), entries.end(), [](const Entry& e) { return e.isDirectory; });

        ImGuiTreeNodeFlags nodeFlags =
            ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_OpenOnDoubleClick |
            ImGuiTreeNodeFlags_SpanFullWidth;
        if (!hasSubfolders)
            nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (virtualPath == mCurrentPath)
            nodeFlags |= ImGuiTreeNodeFlags_Selected;

        bool open = ImGui::TreeNodeEx(virtualPath.c_str(), nodeFlags, "%s", displayName.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            navigateTo(virtualPath);

        if (hasSubfolders && open)
        {
            for (const Entry& entry : entries)
            {
                if (entry.isDirectory)
                    drawFolderTreeNode(entry.virtualPath, entry.name);
            }
            ImGui::TreePop();
        }
    }

    void AssetBrowser::drawContentGrid()
    {
        mIconProvider.resetThumbnailBudget();

        const std::vector<Entry>& allEntries = getOrScanDirectory(mCurrentPath);

        std::vector<const Entry*> visibleEntries;
        visibleEntries.reserve(allEntries.size());
        for (const Entry& entry : allEntries)
        {
            if (mSearchFilter.empty() || entry.name.find(mSearchFilter) != std::string::npos)
                visibleEntries.push_back(&entry);
        }

        float cellWidth = mThumbnailSize + kCellPaddingBase * ImGuiFrontend::getDpiScale() * 2.0f;
        float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
        float availWidth = ImGui::GetContentRegionAvail().x;
        int columns = std::max(1, static_cast<int>((availWidth + itemSpacing) / (cellWidth + itemSpacing)));

        bool clickedVoid = ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        // F2 重命名: 只在恰好选中一项、这块网格本身有焦点、当前没有别的项正在被编辑时触发。
        if (mRenamingPath.empty() && mSelectedPaths.size() == 1 && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2, false))
        {
            const std::string& selectedPath = *mSelectedPaths.begin();
            for (const Entry& entry : allEntries)
            {
                if (entry.virtualPath == selectedPath)
                {
                    beginRename(entry);
                    break;
                }
            }
        }

        for (int index = 0; index < static_cast<int>(visibleEntries.size()); ++index)
        {
            const Entry& entry = *visibleEntries[index];
            bool selected = mSelectedPaths.contains(entry.virtualPath);

            if (drawEntry(entry, selected))
            {
                handleEntryClick(entry.virtualPath, index);
                clickedVoid = false;
            }

            if ((index + 1) % columns != 0)
                ImGui::SameLine();
        }

        if (clickedVoid && !ImGui::IsAnyItemHovered())
        {
            mSelectedPaths.clear();
            mLastClickedIndex = -1;
        }

        // 循环彻底结束、不会再有代码访问 allEntries/visibleEntries 之后才能真正执行删除/
        // 重命名——这两个操作成功时都会让 mDirectoryCache 里对应的 vector 失效, allEntries
        // 正是指向这个 vector 的引用, visibleEntries 里全是指向它元素的裸指针, 循环中途执行
        // 会导致后续迭代访问已释放内存(同 Inspector.cpp 里 componentToRemove 的道理)。
        if (mPendingDelete)
        {
            deleteEntry(*mPendingDelete);
            mPendingDelete.reset();
        }
        if (mPendingRenameCommit)
        {
            commitRename(mPendingRenameCommit->entry, mPendingRenameCommit->newName);
            mPendingRenameCommit.reset();
        }

        drawEmptyAreaContextMenu();
    }

    void AssetBrowser::handleEntryClick(const std::string& virtualPath, int flatIndex)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl)
        {
            if (mSelectedPaths.contains(virtualPath))
                mSelectedPaths.erase(virtualPath);
            else
                mSelectedPaths.insert(virtualPath);
        }
        else
        {
            mSelectedPaths.clear();
            mSelectedPaths.insert(virtualPath);
        }
        mLastClickedIndex = flatIndex;
    }

    bool AssetBrowser::drawEntry(const Entry& entry, bool selected)
    {
        ImGui::PushID(entry.virtualPath.c_str());

        float dpiScale = ImGuiFrontend::getDpiScale();
        float cellPadding = kCellPaddingBase * dpiScale;
        float lineHeight = ImGui::GetTextLineHeight();
        float cellWidth = mThumbnailSize + cellPadding * 2.0f;
        float cellHeight = mThumbnailSize + cellPadding * 2.0f + kMaxLabelLines * lineHeight + 4.0f * dpiScale;
        ImVec2 cellSize(cellWidth, cellHeight);
        ImVec2 cellMin = ImGui::GetCursorScreenPos();
        ImVec2 cellMax(cellMin.x + cellSize.x, cellMin.y + cellSize.y);

        bool isRenaming = (entry.virtualPath == mRenamingPath);

        ImGui::InvisibleButton("##hit", cellSize);
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

        Asset* asset = entry.isDirectory ? nullptr : mAssetManager.getAsset(entry.guid);

        ImTextureID thumbnailId = asset ? mIconProvider.getThumbnail(asset) : ImTextureID(0);

        // 拖给 AssetCombo 的图标用——只有资产(不是目录)才能当拖拽源。悬浮提示用的图标
        // 跟这一格实际画出来的保持一致(有真实缩略图就用它, 没有就跟格子一样回退默认图),
        // 不能永远固定用默认图, 否则会跟格子本身显示的内容对不上。
        // SourceNoPreviewTooltip: 悬浮提示统一在 Editor.cpp 里按 payload 数据画一份, 不
        // 依赖这一格这一帧还在不在(切换标签页导致这一格没被画到时, ImGui 会掉退化成
        // "..."提示, 见那边的注释)。
        if (asset && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip))
        {
            ImTextureID dragIcon = thumbnailId ? thumbnailId : mIconProvider.getDefaultThumbnail();
            AssetDragDropPayload payload{ entry.guid, asset->getRootClass(), dragIcon };
            ImGui::SetDragDropPayload(kAssetDragDropType, &payload, sizeof(payload));
            ImGui::EndDragDropSource();
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (selected || hovered)
        {
            ImU32 bgColor = ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered);
            drawList->AddRectFilled(cellMin, cellMax, bgColor, 4.0f);
        }

        ImVec2 iconMin(cellMin.x + cellPadding, cellMin.y + cellPadding);
        ImVec2 iconMax(iconMin.x + mThumbnailSize, iconMin.y + mThumbnailSize);

        ImTextureID iconId = thumbnailId ? thumbnailId : (entry.isDirectory ? mIconProvider.getFolderIcon() : mIconProvider.getDefaultThumbnail());
        if (iconId)
            drawList->AddImage(iconId, iconMin, iconMax);

        if (asset && asset->isDirty())
        {
            float starSize = mThumbnailSize * 0.2f;
            ImVec2 center(iconMin.x + starSize * 0.5f, iconMax.y - starSize * 0.5f);
            ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(starSize, FLT_MAX, 0.0f, ICON_MS_EMERGENCY);
            ImVec2 starPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
            drawList->AddText(nullptr, starSize, starPos, IM_COL32(255, 220, 80, 255), ICON_MS_EMERGENCY);
        }

        if (isRenaming)
        {
            // 内联重命名: 用真实的 InputText 换掉静态文字, 位置/宽度跟标签区对齐。
            // IsItemDeactivated() 覆盖了回车、点别处失焦、Esc(ImGui 对 InputText 的默认行为是
            // Esc 把内容还原成激活时的值再失焦)这三种情况——不管走哪条路都统一交给
            // commitRename() 处理, 它内部会判断"名字没变"直接当无操作放弃, 不需要在这里
            // 单独分支处理 Esc/取消。
            ImVec2 inputPos(cellMin.x + 2.0f, iconMax.y + 4.0f);
            ImGui::SetCursorScreenPos(inputPos);
            ImGui::SetNextItemWidth(cellWidth - 4.0f);
            if (mRenameFocusPending)
            {
                ImGui::SetKeyboardFocusHere();
                mRenameFocusPending = false;
            }

            constexpr ImGuiInputTextFlags renameFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
            // 默认 FrameBg* 半透明, 叠在选中高亮矩形上会透出蓝色, 这里强制改成不透明 WindowBg。
            ImVec4 windowBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
            windowBg.w = 1.0f;
            ImGui::PushStyleColor(ImGuiCol_FrameBg, windowBg);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, windowBg);
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, windowBg);
            bool enterPressed = ImGui::InputText("##rename", &mRenameBuffer, renameFlags);
            ImGui::PopStyleColor(3);
            if (enterPressed || ImGui::IsItemDeactivated())
            {
                // 真正执行改名(以及它触发的缓存失效)不能在这里就做——见 mPendingRenameCommit
                // 的注释, 这里只退出编辑态(纯 UI 状态, 不碰缓存, 立刻做是安全的)。
                mPendingRenameCommit = PendingRename{ entry, mRenameBuffer };
                mRenamingPath.clear();
            }

            // 光复位 CursorPos 不够——下一个格子的 SameLine() 实际读的是 CursorPosPrevLine/
            // CurrLineSize 这几个字段, 它们已经被 InputText 自己的 ItemSize() 调用改写过了。
            // 要用一个真实的 item 调用(Dummy)才能把这一整套状态强制覆盖回 InvisibleButton
            // 本该留下的样子, 单纯 SetCursorScreenPos 只是治标不治本(跟面包屑/属性表分隔条
            // 那两处最终采用的手法一致)。
            ImGui::SetCursorScreenPos(cellMin);
            ImGui::Dummy(cellSize);
        }
        else
        {
            float labelMaxWidth = cellWidth - cellPadding;
            std::vector<std::string> labelLines = wrapLabelIntoLines(entry.name, labelMaxWidth);
            bool labelTruncated = false;
            float lineY = iconMax.y + 4.0f;
            for (const std::string& line : labelLines)
            {
                ImVec2 lineSize = ImGui::CalcTextSize(line.c_str());
                ImVec2 linePos(cellMin.x + (cellWidth - lineSize.x) * 0.5f, lineY);
                drawList->AddText(linePos, ImGui::GetColorU32(ImGuiCol_Text), line.c_str());
                lineY += lineHeight;
                if (line.ends_with("..."))
                    labelTruncated = true;
            }

            if (labelTruncated && hovered)
                ImGui::SetTooltip("%s", entry.name.c_str());
        }

        if (doubleClicked)
        {
            if (entry.isDirectory)
            {
                navigateTo(entry.virtualPath);
            }
            else if (Asset* asset = mAssetManager.getAsset(entry.virtualPath))
            {
                // 没有为这个资产的 rootClass 注册专属编辑器(比如 Mesh 目前就没有)时
                // tryOpenEditor() 什么都不做——双击这类资产暂时没有反应, 不是 bug。
                AssetEditorRegistry::get().tryOpenEditor(*asset, mTabHost, mIconProvider, *this);
            }
        }

        if (!isRenaming && ImGui::BeginPopupContextItem("##ctx"))
        {
            std::filesystem::path physicalPath = entry.isDirectory
                ? virtualPathToPhysicalDirectory(entry.virtualPath)
                : mAssetManager.convertAssetPathToPhysicalPath(entry.virtualPath);

            if (ImGui::MenuItem("Show in Explorer"))
                showInExplorer(physicalPath);

            if (ImGui::MenuItem("Rename", "F2"))
                beginRename(entry);

            bool canDelete = true;
            if (entry.isDirectory)
            {
                std::error_code ec;
                canDelete = std::filesystem::is_empty(physicalPath, ec) && !ec;
            }
            if (ImGui::MenuItem("Delete", nullptr, false, canDelete))
                mPendingDelete = entry; // 真正执行挪到 drawContentGrid() 循环结束后, 见那边的注释
            if (entry.isDirectory && !canDelete)
                ImGui::SetItemTooltip("Folder is not empty");

            ImGui::EndPopup();
        }

        ImGui::PopID();
        return clicked;
    }

    void AssetBrowser::drawEmptyAreaContextMenu()
    {
        if (ImGui::BeginPopupContextWindow("##EmptyCtx", ImGuiPopupFlags_NoOpenOverExistingPopup | ImGuiPopupFlags_MouseButtonRight))
        {
            if (ImGui::MenuItem("New Folder"))
                createNewFolder();
            ImGui::EndPopup();
        }
    }

    void AssetBrowser::createNewFolder()
    {
        std::filesystem::path physicalDir = virtualPathToPhysicalDirectory(mCurrentPath);
        if (physicalDir.empty())
            return;

        std::string baseName = "New Folder";
        std::string name = baseName;
        int counter = 1;
        while (std::filesystem::exists(physicalDir / name))
            name = baseName + " (" + std::to_string(counter++) + ")";

        std::error_code ec;
        std::filesystem::create_directory(physicalDir / name, ec);
        invalidateDirectory(mCurrentPath);

        selectAndBeginRename(Entry{ name, mCurrentPath + "/" + name, true, Guid{} });
    }

    Asset* AssetBrowser::registerNewAsset(const std::string& parentPath, const std::string& baseName, Object* rootObject)
    {
        // 名字探测规则跟 createNewFolder() 一样, 但探测的是 AssetManager 里已登记的虚拟
        // 路径而不是物理文件是否存在——createAsset() 本身就是靠 mPathAssetMap 判断路径是否
        // 已被占用的(见 AssetManager::createAsset()), 探测逻辑跟它保持同一个真相来源。
        std::string name = baseName;
        int counter = 1;
        while (mAssetManager.getAsset(parentPath + "/" + name))
            name = baseName + " (" + std::to_string(counter++) + ")";

        return mAssetManager.createAsset(parentPath + "/" + name, rootObject);
    }

    void AssetBrowser::createNewAsset(const std::string& parentPath, const std::string& baseName, Object* rootObject)
    {
        if (Asset* asset = registerNewAsset(parentPath, baseName, rootObject))
        {
            asset->save();
            invalidateDirectory(parentPath);

            const std::string& virtualPath = asset->getPath();
            size_t slash = virtualPath.find_last_of('/');
            std::string name = slash == std::string::npos ? virtualPath : virtualPath.substr(slash + 1);
            selectAndBeginRename(Entry{ std::move(name), virtualPath, false, asset->getRootGuid() });
        }
    }

    void AssetBrowser::importResult(ImportDialog::Result& result)
    {
        if (result.entries.empty())
            return;

        std::string parentPath = mCurrentPath;
        if (!result.folderName.empty())
        {
            std::filesystem::path physicalCurrentDir = virtualPathToPhysicalDirectory(mCurrentPath);
            if (physicalCurrentDir.empty())
                return;

            std::string folderName = result.folderName;
            int counter = 1;
            // folderName 来自导入源文件的名字(比如 glTF 文件名), 可能带中文——std::filesystem::path
            // 的 std::string 构造/operator/ 在 Windows 上是按 ANSI 代码页解释字节的, 不是 UTF-8,
            // 必须过一遍 u8string 绕开, 否则含非 ASCII 字符时行为未定义、严重时直接抛异常。
            auto toPhysicalPath = [](const std::string& name) { return std::filesystem::path(std::u8string(name.begin(), name.end())); };
            while (std::filesystem::exists(physicalCurrentDir / toPhysicalPath(folderName)))
                folderName = result.folderName + " (" + std::to_string(counter++) + ")";

            std::error_code ec;
            std::filesystem::create_directories(physicalCurrentDir / toPhysicalPath(folderName), ec);

            parentPath = mCurrentPath + "/" + folderName;
        }

        // 先把这一批全部注册进 AssetManager(建好 Guid, 但先不 save()), 再统一 save()——entries
        // 之间可能互相引用(mesh 的子网格指向 material, material 指向 texture), Asset::save()
        // 序列化跨资产引用时要求被引用对象已经在 AssetManager 里登记过。如果按 entries 原始
        // 顺序边建边存, 排在前面的对象(比如 mesh)存的时候, 排在后面才会建的对象(material)还
        // 没登记, Asset::save() 里 assert(asset) 直接炸——全部先建完再存, 不管谁引用谁都没有
        // 这个问题。
        std::vector<Asset*> createdAssets;
        createdAssets.reserve(result.entries.size());
        for (const ImportDialog::Result::Entry& entry : result.entries)
        {
            // entry.name 允许带一层子目录前缀(比如 "Meshes/Cube")——Asset::save() 自己不会
            // 创建目录, 直接往不存在的路径 ofstream 会静默失败, 落盘前得先把物理子目录建好。
            size_t slash = entry.name.find_last_of('/');
            if (slash != std::string::npos)
            {
                std::filesystem::path physicalEntryDir = virtualPathToPhysicalDirectory(parentPath + "/" + entry.name.substr(0, slash));
                if (!physicalEntryDir.empty())
                {
                    std::error_code ec;
                    std::filesystem::create_directories(physicalEntryDir, ec);
                }
            }

            if (Asset* asset = registerNewAsset(parentPath, entry.name, entry.rootObject))
                createdAssets.push_back(asset);
        }

        for (Asset* asset : createdAssets)
            asset->save();

        // 导入出来的对象是直接在内存里 new 出来再填数据的, 从没走过反序列化, 任何"只在
        // postLoad() 里算一次"的派生数据(比如 Skeleton::mInverseBindMatrices)这时候都还是
        // 空的。save() 之后想立刻 tryUnload() 补上, 但 result.entries 里的 TRef 这时候还
        // 活着(每个 entry.rootObject 一份引用), tryUnload() 的 refcount 检查会失败——
        // 先清空 entries 释放这份引用(这批往后也用不到 rootObject 了), 再尝试卸载, 下次
        // 谁用到这个资产走 getOrLoadRootObject() 正常触发 load()/postLoad()。tryUnload()
        // 自己会看 refcount, 这一批之间有交叉引用(比如 mesh 引用了同批导入的 material)
        // 时对应资产会跳过卸载, 不会导致悬空
        result.entries.clear();
        for (Asset* asset : createdAssets)
            asset->tryUnload();

        invalidateDirectory(mCurrentPath);
    }

    void AssetBrowser::deleteEntry(const Entry& entry)
    {
        if (entry.isDirectory)
        {
            std::filesystem::path physicalDir = virtualPathToPhysicalDirectory(entry.virtualPath);
            std::error_code ec;
            if (!std::filesystem::is_empty(physicalDir, ec) || ec)
                return;
            std::filesystem::remove(physicalDir, ec);
            invalidateDirectory(entry.virtualPath);
        }
        else
        {
            Asset* asset = mAssetManager.getAsset(entry.virtualPath);
            if (!asset || !mAssetManager.tryDeleteAsset(asset))
                return;
        }

        size_t slash = entry.virtualPath.find_last_of('/');
        std::string parentPath = slash == std::string::npos ? entry.virtualPath : entry.virtualPath.substr(0, slash);
        invalidateDirectory(parentPath);
        mSelectedPaths.erase(entry.virtualPath);
    }

    void AssetBrowser::selectAndBeginRename(const Entry& entry)
    {
        mSelectedPaths.clear();
        mSelectedPaths.insert(entry.virtualPath);
        mLastClickedIndex = -1;
        beginRename(entry);
    }

    void AssetBrowser::beginRename(const Entry& entry)
    {
        mRenamingPath = entry.virtualPath;
        mRenameBuffer = entry.name;
        mRenameFocusPending = true;
    }

    void AssetBrowser::commitRename(const Entry& entry, const std::string& newName)
    {
        // '/' 会破坏虚拟路径的分段规则, 其它几个是 Windows 文件名本身就不允许的字符——统一
        // 拒绝, 不做自动替换/剔除那种"帮用户改"的处理, 用户会觉得莫名其妙。
        bool hasInvalidChar = newName.find_first_of("\\/:*?\"<>|") != std::string::npos;
        if (!newName.empty() && !hasInvalidChar && newName != entry.name)
        {
            size_t slash = entry.virtualPath.find_last_of('/');
            std::string parentPath = slash == std::string::npos ? entry.virtualPath : entry.virtualPath.substr(0, slash);
            std::string newVirtualPath = parentPath + "/" + newName;

            bool success = false;
            if (entry.isDirectory)
            {
                success = mAssetManager.renameDirectory(entry.virtualPath, newVirtualPath);
            }
            else
            {
                Asset* asset = mAssetManager.getAsset(entry.virtualPath);
                success = asset && mAssetManager.moveAsset(asset, newVirtualPath);
            }

            if (success)
            {
                // 目录重命名会连带影响任意深度的子目录/资产, 精确失效每一条太麻烦也容易漏,
                // 直接把整个浏览器缓存清空最简单——目录重命名不是高频操作, 不值得为它单独写
                // 一套"递归重新映射所有子目录缓存 key"的逻辑。单个资产重命名只影响它所在的
                // 这一层目录列表, 用 invalidateDirectory 精确失效就够了。
                if (entry.isDirectory)
                    mDirectoryCache.clear();
                else
                    invalidateDirectory(parentPath);

                mSelectedPaths.erase(entry.virtualPath);
                mSelectedPaths.insert(newVirtualPath);
            }
        }

        cancelRename();
    }

    void AssetBrowser::cancelRename()
    {
        mRenamingPath.clear();
    }

    void AssetBrowser::showInExplorer(const std::filesystem::path& physicalPath)
    {
#ifdef _WIN32
        // explorer.exe /select,"path" 这种走 ShellExecute 命令行参数的方式在不少 Windows
        // 配置下并不可靠——资源管理器是单实例进程, 这个参数经常被吞掉, 表现为每次都打开默认的
        // 主页/快速访问, 定位不到目标文件/文件夹。改用专门为这个用途设计的 Shell API,
        // 绕开命令行参数解析这一层。SHOpenFolderAndSelectItems 要求调用线程已初始化 COM。
        //
        // make_preferred() 不能省: std::filesystem::path 在 MSVC 上不会自动把已经存在的 '/'
        // 规范化成 '\\', 只有 operator/ 新插入的那一个分隔符会用 preferred_separator——
        // mEngineAssetRoot/mProjectAssetRoot 来自 config.json, 原样写的是正斜杠, 拼上文件名后
        // 就会拼出"前面全 / 、只有最后一段 \\"这种混合分隔符路径。std::filesystem 的普通文件
        // 操作(exists/create_directory 等)对分隔符方向不敏感, 不会暴露这个问题, 但
        // ILCreateFromPathW 走的是 Shell 命名空间解析, 对混合分隔符很敏感, 解析失败直接返回
        // NULL——这才是"点了完全没反应"的真正原因。
        std::filesystem::path normalizedPath = physicalPath;
        normalizedPath.make_preferred();

        HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(normalizedPath.c_str());
        if (pidl)
        {
            SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
            ILFree(pidl);
        }

        // 只有这次调用真的让 COM 从"未初始化"变成"已初始化"(S_OK/S_FALSE)才需要配对
        // CoUninitialize——RPC_E_CHANGED_MODE 说明这个线程已经被别的代码用不同并发模型初始化
        // 过了, 那次初始化不归这里管, 不应该替它 Uninitialize。
        if (comResult == S_OK || comResult == S_FALSE)
            CoUninitialize();
#else
        std::string command = "xdg-open \"" + physicalPath.parent_path().string() + "\"";
        std::system(command.c_str());
#endif
    }
}
