#pragma once
#include "AssetEditorWindow.h"
#include "SlangCodeEditor.h"

#include <Core/Common/Guid.h>
#include <Engine/Material/MaterialTemplate.h>
#include <Engine/Texture/SampleState.h>

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

namespace hyp
{
    class AssetManager;
}

namespace hyp::ed
{
    class ThumbnailProvider;
    class AssetBrowser;
    class UndoStack;

    class MaterialTemplateEditor final : public AssetEditorWindow
    {
    public:
        MaterialTemplateEditor(Asset& asset, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);
        ~MaterialTemplateEditor() override;

        // library(直接或经由它引用的库间接)是不是被这个模板用到了——
        // MaterialFunctionLibraryEditor 在库 Apply 之后, 靠这个决定要不要通知这个窗口刷新
        bool referencesFunctionLibrary(const MaterialFunctionLibrary* library) const noexcept;

        // 重新拼两个 SlangCodeEditor 的 preamble(材质函数库那部分变了)并 setPreamble()——
        // 由 MaterialFunctionLibraryEditor 在库 Apply 之后, 对 referencesFunctionLibrary()
        // 为真的窗口调用
        void refreshFunctionLibraryPreamble();

        // 资产编辑窗口不再池化在一个共享 manager 里之后, MaterialFunctionLibraryEditor 靠这个
        // 跨 tab 找到所有打开着的 MaterialTemplateEditor——构造/析构时自动登记/注销, 不需要
        // 认识 tab 或者任何窗口管理设施。
        static void forEachInstance(const std::function<void(MaterialTemplateEditor&)>& callback);

        // MaterialTemplateEditorTab::hasUnsavedChanges() 用: 关闭 tab 前要不要弹确认框——
        // 只看"还没 Apply", 不看"Apply 过了但还没存盘", 见 EditorTab::hasUnsavedChanges()
        // 的注释。也是 Apply 按钮本身是否可点的依据: 模板自身的 DirtyFlags(属性改了但还没
        // apply)、源码缓存改了但还没应用(mSourcesModified)、参数工作副本改了但还没应用
        // (mParametersModified), 三者任一为真就需要 apply。
        bool needsApply() const noexcept;

        // MaterialTemplateEditorTab::applyPendingChanges() 用: 关闭确认框点了"Apply"时调用。
        void applyPendingChanges();

        // 目前只有 drawApplyAndSave() 的 Save 按钮读它, 跟"要不要弹关闭确认框"无关——已经
        // Apply 过的改动关闭 tab 不会丢, 不需要用这个再挡一次。
        bool needsSave() const noexcept;

    protected:
        void onDraw() override;

    private:
        void drawGeneralProperties();
        void drawParameterGroupManagement();
        void drawParameterList();
        void drawAddParameterRow();
        void drawMaterialFunctionLibraries();
        void drawPrivateTextures();
        void drawPrivateTextureValueWidget(uint32_t index, const MaterialParameter& texture, AssetManager& assetManager);
        void drawSources();
        void drawApplyAndSave();

        void drawParameterValueWidget(MaterialParameter& parameter, AssetManager& assetManager);
        void drawNewParameterDefaultValueWidget(AssetManager& assetManager);
        void resetNewParameterDefaultValue();

        // 在 mPendingParameters(按名字)里找一个参数的值——trackParameterValue()/
        // trackTextureParameterValue() 的 undo/redo 回调, 以及 drawParameterValueWidget()
        // 都通过这个来改值, 而不是直接持有下标或指针: mPendingParameters 会随 Remove/Add
        // 变化, 下标会漂移、指针可能因为 vector 重新分配而悬空, 名字是唯一稳定的身份。
        // 找不到(比如撤销发生在参数已经被 Remove 之后)返回 nullptr, 调用方要能安全地什么都不做。
        MaterialParameterValue* findPendingParameterValue(HString name);

        // Apply 之前 mMaterialTemplate 和 mPendingParameters 之间的差异一次性回放: 模板里有、
        // mPendingParameters 里没有的按名字 removeParameter(), mPendingParameters 里有的逐个
        // addParameter()(模板里还没有)或 setParameterDefaultValue()(模板里已经有, 更新值),
        // 回放完再用模板最新的参数列表重新同步一遍 mPendingParameters, 不假定每一步都必然成功。
        void reconcilePendingParameters();

        // 拖拽框/勾选框这类值改变了才推 undo, 用参数名(不是下标)当身份——参数在
        // mPendingParameters 里的下标会因为别的参数被删除(Remove 按钮, 会接入 undo, 但同样是
        // 立即生效于本地缓冲)而漂移, 用名字通过 findPendingParameterValue() 查找, undo/redo
        // 执行的时候永远是"现查一次当前下标", 不会被中途的增删影响。
        template <typename T>
        void trackParameterValue(HString name, const T& before, const T& current);

        // 参数集合(不是单个字段)有增删时统一走这个: 标脏 + 重建两个 SlangCodeEditor 的
        // preamble。值编辑(拖数值/换贴图)不影响参数名/类型集合, 不需要重建 preamble, 不走
        // 这个函数, 单独 mParametersModified = true 即可。
        void onParameterSetChanged();

        // 参数按组分桶的缓存(下标指向 mPendingParameters), 大小 = getParameterGroups().size()+1
        // (最后一项固定是 Common 桶)。分组归属在参数/组列表没变的这几帧里是不变的, 每帧重新
        // 扫一遍(尤其是按组数 x 参数数重复过滤)是没必要的开销——用脏标记控制只在真的会影响
        // 分桶结果的地方(参数增删、某个参数改组、组列表增删改名/挪动、Apply 回放完
        // mPendingParameters)重算一次, 平时拖 slider/改颜色这类不影响归组的高频编辑不会碰它。
        mutable bool mParameterGroupBucketsDirty = true;
        mutable std::vector<std::vector<uint32_t>> mParameterGroupBuckets;
        void rebuildParameterGroupBucketsIfDirty() const;

        // 纹理参数(TRef<Texture> + 可选的 SampleState 覆盖)按 Guid 生成快照——跟其它
        // 地方对资产引用做 undo 的原则一致: 不捕获 TRef/Asset*, 只捕获 Guid, undo/redo
        // 真正执行时才现查 AssetManager, 避免捕获的资产在这期间被卸载/重新加载。
        struct TextureParamSnapshot
        {
            Guid textureGuid;
            bool hasOverride = false;
            SampleState overrideState;

            friend bool operator==(const TextureParamSnapshot&, const TextureParamSnapshot&) = default;
        };

        template <typename TextureT>
        TextureParamSnapshot makeTextureSnapshot(const TRef<TextureT>& texture, const std::optional<SampleState>& sampleStateOverride) const;

        template <typename TextureT>
        void trackTextureParameterValue(HString name, const TextureParamSnapshot& before, const TextureParamSnapshot& current);

        // 画 AssetCombo + 采样器覆盖设置(点开一个小按钮弹出), 直接在 texture/
        // sampleStateOverride 这两个引用上原地改——调用方(参数行/新增参数行)自己决定
        // changed 之后要做什么(写回 MaterialTemplate, 还是只是暂存在待新增的默认值里)。
        template <typename TextureT>
        bool drawTextureValue(TRef<TextureT>& texture, std::optional<SampleState>& sampleStateOverride, AssetManager& assetManager);

        bool drawFontValue(TRef<Font>& font, AssetManager& assetManager);

        // preamble 拼装: MaterialSource/VertexPositionOffsetSource 各自的输入结构体声明
        // (MaterialInput/MaterialOutput、VertexPositionOffsetInput 的近似版本) + 当前参数列表
        // 生成的一段声明, 只要求"编辑时不误报未声明标识符", 不要求跟 RenderBase 实际拼给
        // slang 编译器的 shader 逐字节一致(bindless 绑定方式、寄存器分配等这里都不关心)。
        // 参数部分读的是 mPendingParameters(还没 Apply 的工作副本), 不是
        // mMaterialTemplate->getParameters()——这样材质源码里引用一个刚新增、还没点 Apply 的
        // 参数名不会被误报未声明标识符, 纯粹是编辑器自己内部的语义分析用途, 不会泄露给
        // MaterialInstanceEditor 等外部读者(那些必须只看 Apply 之后的模板状态)。
        // 依赖 mMaterialTemplate/mPendingParameters 已经构造完成——只能在它们之后调用(包括
        // 构造函数初始化列表里, 依据的是成员声明顺序而不是初始化列表书写顺序, 所以
        // mMaterialTemplate、mPendingParameters 必须仍然声明在下面两个 SlangCodeEditor 成员
        // 之前)。
        std::string buildMaterialSourcePreamble() const;
        std::string buildVertexPositionOffsetPreamble() const;

    private:
        // forEachInstance() 用的登记表——构造函数里 push_back(this), 析构函数里 erase 掉,
        // 函数内 static 单例, 理由跟 AssetEditorRegistry::get() 一样, 规避跨翻译单元的静态
        // 初始化顺序问题。
        static std::vector<MaterialTemplateEditor*>& instances();

    private:
        ThumbnailProvider& mIconProvider;
        AssetBrowser& mAssetBrowser;
        TRef<MaterialTemplate> mMaterialTemplate;

        // 参数增删改的工作副本——修 bug: 之前 drawParameterValueWidget()/drawAddParameterRow()/
        // drawParameterList() 的 Remove 按钮都是直接改 mMaterialTemplate(setParameterDefaultValue()/
        // addParameter()/removeParameter() 都会立即生效并标脏, 没有像材质源码那样等 Apply 才写回),
        // 导致 MaterialInstanceEditor 等其它读 mMaterialTemplate->getParameters() 的地方会看到
        // 还没点 Apply 的中间状态。现在参数增删改统一先落在这份工作副本上, 只有点 Apply 时才
        // 通过 reconcilePendingParameters() 一次性回放进 mMaterialTemplate, 跟材质源码走
        // 同一套"编辑时只改本地缓冲, Apply 才写回"语义。
        // 构造时从 mMaterialTemplate->getParameters() 拷贝种子——声明必须在 mMaterialTemplate
        // 之后、两个 SlangCodeEditor 成员之前(preamble 拼装依赖它已经就绪)。
        std::vector<MaterialParameter> mPendingParameters;

        // Apply 之前不实际调用 setMaterialSource()/setVertexPositionOffsetSource()——这两个
        // setter 本身很轻(只是存字符串 + 标脏), 真正重的是 apply() 里的 shader 重新组装/编译,
        // 缓存在编辑器自己的文本缓冲里只是避免每敲一个字符就触发一次没必要的标脏, 具体值要等
        // Apply 按钮按下才真正写回 MaterialTemplate(见 drawApplyAndSave())。参数增删成功后
        // 要调用两者的 setPreamble()——否则编辑器的符号表/诊断还停留在旧参数列表上。
        SlangCodeEditor mMaterialSourceEditor;
        SlangCodeEditor mVertexPositionOffsetSourceEditor;

        // SlangCodeEditor::render() 本帧文本被改动时置 true, Apply 成功执行一次(缓存被
        // 写回模板)就置回 false——O(1) 的"改没改"标记, 不需要每帧比较字符串内容。
        bool mSourcesModified = false;

        // mPendingParameters 被改动(值改了/新增/删除)时置 true, Apply 成功回放进
        // mMaterialTemplate 后置回 false——跟 mSourcesModified 同样的 O(1) 标记, 参与
        // needsApply() 判断。
        bool mParametersModified = false;

        // 新增参数那一行的输入状态
        std::string mNewParameterName;
        MaterialParameterType mNewParameterType = MaterialParameterType::Float;
        MaterialParameterValue mNewParameterDefaultValue;
        HString mNewParameterGroup; // 空 = Common

        // 新增参数组那一行的输入状态
        std::string mNewGroupName;

        // "新增材质函数库"那一行的资产选择框状态; 添加失败(库为空/已经引用过/会成环)
        // 时的提示文案, 非空就画出来, 下一次成功操作后清空
        Guid mNewMaterialFunctionLibraryGuid;
        std::string mMaterialFunctionLibraryError;

        // "新增私有纹理"那一行的输入状态——私有纹理没有 mPendingParameters 那样的工作副本,
        // 增删/改值都直接作用于 mMaterialTemplate(跟 drawMaterialFunctionLibraries() 同一种
        // "立即生效"模式), 因为 addPrivateTexture()/removePrivateTexture() 只能通过名字/类型
        // 天然唯一确定一个槽位, 不像参数列表那样有下标漂移、需要工作副本这类问题。
        std::string mNewPrivateTextureName;
        MaterialParameterType mNewPrivateTextureType = MaterialParameterType::Texture_2D;
        MaterialParameterValue mNewPrivateTextureValue = MaterialParameterValue(static_cast<Texture2D*>(nullptr));
        std::string mPrivateTextureError;
    };
}
