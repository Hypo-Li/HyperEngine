#include "MaterialTemplateEditor.h"
#include "MaterialTemplateEditorTab.h"
#include "AssetComboEd.h"
#include "AssetEditorRegistry.h"
#include "ThumbnailProvider.h"
#include "UndoStack.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Core/Reflection/Reflection.h>
#include <Engine/Context.h>
#include <Engine/Texture/Texture2D.h>
#include <Engine/Texture/Texture2DArray.h>
#include <Engine/Texture/Texture3D.h>
#include <Engine/Texture/TextureCube.h>

#include <HypImGui/ImGuiAssetCombo.h>
#include <HypImGui/ImGuiColorWidgets.h>
#include <HypImGui/ImGuiEnumWidgets.h>
#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <format>
#include <functional>
#include <utility>
#include <vector>

namespace hyp::ed
{
    namespace
    {
        AssetEditorRegistrar<MaterialTemplate, MaterialTemplateEditorTab> gRegistrar;

        // 跟 Shader/Common/MaterialIO.slang 手动保持一致——只用于编辑器里的语法高亮/自动补全/
        // 诊断, 跟真正编译 shader 时用的那份互相独立, 不存在包含关系。
        constexpr const char* kMaterialIOCommonDeclaration = R"(
enum ShadingModel : uint
{
    Unlit = 0,
    Standard = 1,
    Clear_Coat = 2,
    Cloth = 3,
};
)";

        // 只镜像 Shader/Common/MaterialBuiltins*.slang 的签名, 函数体用占位实现
        constexpr const char* kMaterialBuiltinsDeclaration = R"(
typedef float2 MatUV;
typedef Texture2D MatTexture2D;
typedef Texture2DArray MatTexture2DArray;
typedef Texture3D MatTexture3D;
typedef TextureCube MatTextureCube;

namespace Material
{
    float4 sampleTexture2DArray(MatTexture2DArray atlas, SamplerState atlasSampler, MatUV uv, float page) { return 0; }
    float3 unpackNormal(float2 xy) { return 0; }
    float remap(float value, float inputLow, float inputHigh, float targetLow, float targetHigh) { return 0; }
    MatUV bumpOffset(MatUV uv, float2 cameraVectorTS, float height, float heightRatio, float referencePlane) { return uv; }
    float fontChannelMedian(float3 value) { return 0; }
    float sampleFontDistance(MatTexture2DArray atlas, SamplerState atlasSampler, float page, MatUV uv, int sdfType) { return 0; }
    float fontScreenPxRange(float spreadUv, MatUV uv) { return 0; }
    float sampleFontCoverage(MatTexture2DArray atlas, SamplerState atlasSampler, float page, MatUV uv, int sdfType, float spreadUv, float edgeOffsetPx) { return 0; }
    float sampleFontCoverage(MatTexture2DArray atlas, SamplerState atlasSampler, float page, MatUV uv, int sdfType, float spreadUv) { return 0; }
}
)";

        constexpr const char* kSurfaceMaterialInputDeclaration = R"(
struct MaterialInput
{
    float3 normalWS;
    float4 tangentWS;
    float4 color;
    MatUV texCoords[4];
    float3 viewDirWS;
    float time;
    float4 positionRWS;
    float3x4 relativeWorldToObject;
    float twoSidedSign;
};
)";

        constexpr const char* kSkyMaterialInputDeclaration = R"(
struct MainLight
{
    float3 direction;
    float3 intensity;
};

struct MaterialInput
{
    float3 viewDirWS;
    float time;
    MainLight mainLights[2];
};
)";

        constexpr const char* kMaterialOutputOpening = R"(
struct MaterialOutput
{
)";

        constexpr const char* kMaterialOutputEmissiveField = "    float3 emissive;\n";

        constexpr const char* kMaterialOutputBaseFields = R"(    float3 emissive;
    float3 baseColor;
    float metallic;
    float roughness;
    float specular;
    float3 normal;
    float occlusion;
    float opacity;
)";

        constexpr const char* kMaterialOutputAnisotropyFields = R"(    float anisotropy;
    float3 anisotropyTangent;
)";

        constexpr const char* kMaterialOutputClearCoatFields = R"(    float clearCoat;
    float clearCoatRoughness;
    float3 clearCoatNormal;
)";

        constexpr const char* kMaterialOutputClothFields = R"(    float3 fuzzColor;
    float clothAmount;
)";

        constexpr const char* kMaterialOutputShadingModelField =
            "    ShadingModel shadingModel;\n";

        constexpr const char* kMaterialOutputClosingBrace = "};\n";

        // tree-sitter 不带预处理器, 这里手动镜像 MaterialIO.slang 里真实的 #if 条件
        std::string buildMaterialIODeclaration(MaterialDomain domain, ShadingModel shadingModel, bool isAnisotropic)
        {
            std::string decl = kMaterialBuiltinsDeclaration;
            decl += kMaterialIOCommonDeclaration;
            decl += domain == MaterialDomain::Sky ? kSkyMaterialInputDeclaration : kSurfaceMaterialInputDeclaration;
            decl += kMaterialOutputOpening;
            if (domain == MaterialDomain::Sky)
            {
                decl += kMaterialOutputEmissiveField;
                decl += kMaterialOutputClosingBrace;
                return decl;
            }
            decl += kMaterialOutputBaseFields;
            if (isAnisotropic)
                decl += kMaterialOutputAnisotropyFields;
            if (shadingModel == ShadingModel::Clear_Coat || shadingModel == ShadingModel::From_Material)
                decl += kMaterialOutputClearCoatFields;
            if (shadingModel == ShadingModel::Cloth || shadingModel == ShadingModel::From_Material)
                decl += kMaterialOutputClothFields;
            if (shadingModel == ShadingModel::From_Material)
                decl += kMaterialOutputShadingModelField;
            decl += kMaterialOutputClosingBrace;
            return decl;
        }

        // 近似 Shader/Common/VertexPositionOffset.slang 的 VertexPositionOffsetInput——
        // objectToWorld 这里用 float4x4 代替真实的 WorldMatrix(引擎大世界坐标 tile+offset
        // 重定基那一套, 见 WorldCoordinate.slang), 编辑器的语义分析不需要那份复杂度, 只要
        // evaluateVertexPositionOffset() 的参数类型能通过语法检查即可——preamble 内部自身的
        // 报错永远不会展示给用户(见 SlangdBackend::analyze() 里按 preambleLineCount 过滤负
        // 行号诊断的逻辑), 所以这里近似不精确也不会误导材质作者。
        constexpr const char* kVertexPositionOffsetIODeclaration = R"(
struct VertexPositionOffsetInput
{
    float3 positionOS;
    float3 normalOS;
    float time;
    float4x4 objectToWorld;
};
)";

        // Enable 开关打开时填进编辑区的起始内容——MaterialTemplate::hasVertexPositionOffset()
        // 靠源码是否为空判断"是否启用", 关闭开关时这段文本会被清空, 不会残留成误报的非空字符串
        constexpr const char* kDefaultVertexPositionOffsetSource =
            "float3 evaluateVertexPositionOffset(in VertexPositionOffsetInput vpoi)\n"
            "{\n"
            "    return float3(0, 0, 0);\n"
            "}\n";

        const char* slangParameterTypeName(MaterialParameterType type)
        {
            switch (type)
            {
                case MaterialParameterType::Bool: return "bool";
                case MaterialParameterType::Int: return "int";
                case MaterialParameterType::Float: return "float";
                case MaterialParameterType::Float2: return "float2";
                case MaterialParameterType::Float3: return "float3";
                case MaterialParameterType::Float4: return "float4";
                case MaterialParameterType::Texture_2D: return "MatTexture2D";
                case MaterialParameterType::Texture_2D_Array:
                case MaterialParameterType::Font: return "MatTexture2DArray";
                case MaterialParameterType::Texture_3D: return "MatTexture3D";
                case MaterialParameterType::Texture_Cube: return "MatTextureCube";
                default: return "float";
            }
        }

        // 独立于 RenderBase 实际拼 shader 用的 makeParameterDefines(见 Shader.cpp)——编辑器
        // 只要求材质作者按参数名引用参数时不被误报"未声明标识符", 不需要跟真实生成的 shader
        // 逐字节一致(bindless 纹理绑定方式、寄存器分配这些细节这里都不关心)。非纹理参数按
        // "uMaterialParameters.xxx" 走一遍宏, 纹理参数把裸名字(非 bindless 写法)和
        // "xxxTexture"/"xxxSampler"(bindless 写法)两套命名都声明出来, 材质作者不管用哪种
        // 约定书写都能解析。
        std::string buildParameterDeclarations(std::span<const MaterialParameter> parameters)
        {
            if (parameters.empty())
                return {};

            std::string structBody;
            std::string textureDeclarations;
            std::string macros;
            for (const MaterialParameter& parameter : parameters)
            {
                const MaterialParameterType type = parameter.value.getType();
                const std::string_view name = parameter.name.toStringView();
                if (type == MaterialParameterType::Font)
                {
                    const FontParameterNames fontNames = makeFontParameterNames(parameter.name);
                    const std::string sdfTypeName(fontNames.sdfType.toStringView());
                    const std::string spreadUvName(fontNames.spreadUv.toStringView());
                    std::format_to(std::back_inserter(structBody), "    int {};\n    float {};\n", sdfTypeName, spreadUvName);
                    std::format_to(std::back_inserter(macros), "#define {} uMaterialParameters.{}\n", sdfTypeName, sdfTypeName);
                    std::format_to(std::back_inserter(macros), "#define {} uMaterialParameters.{}\n", spreadUvName, spreadUvName);
                }
                if (isTextureParameterType(type) || type == MaterialParameterType::Font)
                {
                    const char* textureType = slangParameterTypeName(type);
                    std::format_to(std::back_inserter(textureDeclarations),
                        "{} {}Texture;\nSamplerState {}Sampler;\n", textureType, name, name);
                    std::format_to(std::back_inserter(macros), "#define {} {}Texture\n", name, name);
                    std::format_to(std::back_inserter(macros), "#define {}Texture {}Texture\n", name, name);
                    std::format_to(std::back_inserter(macros), "#define {}Sampler {}Sampler\n", name, name);
                }
                else
                {
                    std::format_to(std::back_inserter(structBody),
                        "    {} {};\n", slangParameterTypeName(type), name);
                    std::format_to(std::back_inserter(macros), "#define {} uMaterialParameters.{}\n", name, name);
                }
            }

            std::string declarations;
            if (!structBody.empty())
            {
                declarations += "struct MaterialParameters\n{\n";
                declarations += structBody;
                declarations += "};\nstatic MaterialParameters uMaterialParameters;\n";
            }
            declarations += textureDeclarations;
            declarations += macros;
            return declarations;
        }

        // Add/Remove Parameter 的 undo/redo 要能捕获整个 MaterialParameterValue, 但纹理类型
        // 不能捕获 TRef<Texture>(会在 undo 栈里一直拖着这个资产不放)——所以 value 里纹理类型
        // 的 texture 永远是 null(只留 type 和 sampleStateOverride, 这两样 MaterialParameterValue
        // 自己已经存得很干净, 不用另外重复记录), 真正的纹理引用单独存 Guid, undo/redo 执行时
        // 才现查 AssetManager 换回真正的 TRef——查不到(资产已经被删除/失效)就退化成 None。
        // 非纹理类型 value 就是完整的值本身, textureGuid 不参与。
        struct ParameterValueSnapshot
        {
            MaterialParameterValue value;
            Guid textureGuid;
        };

        // 把 SampledTextureT(SampledTexture2D 等四选一)按 TextureT 打包成上面的快照——
        // 四种纹理参数类型共用同一段逻辑, 只是 C++ 类型不同没法直接合并成一个 switch 分支。
        template <typename TextureT, typename SampledTextureT>
        ParameterValueSnapshot captureTextureParameterValueSnapshot(const SampledTextureT& v)
        {
            MaterialParameterValue nullTextureValue = v.sampleStateOverride
                ? MaterialParameterValue(static_cast<TextureT*>(nullptr), *v.sampleStateOverride)
                : MaterialParameterValue(static_cast<TextureT*>(nullptr));
            return ParameterValueSnapshot{ nullTextureValue, v.texture ? v.texture->getGuid() : Guid() };
        }

        ParameterValueSnapshot captureParameterValueSnapshot(const MaterialParameterValue& value)
        {
            switch (value.getType())
            {
                case MaterialParameterType::Texture_2D:
                    return captureTextureParameterValueSnapshot<Texture2D>(value.get<SampledTexture2D>());
                case MaterialParameterType::Texture_2D_Array:
                    return captureTextureParameterValueSnapshot<Texture2DArray>(value.get<SampledTexture2DArray>());
                case MaterialParameterType::Texture_3D:
                    return captureTextureParameterValueSnapshot<Texture3D>(value.get<SampledTexture3D>());
                case MaterialParameterType::Texture_Cube:
                    return captureTextureParameterValueSnapshot<TextureCube>(value.get<SampledTextureCube>());
                case MaterialParameterType::Font:
                {
                    const SampledFont& v = value.get<SampledFont>();
                    return ParameterValueSnapshot{ MaterialParameterValue(static_cast<Font*>(nullptr)), v.font ? v.font->getGuid() : Guid() };
                }
                default:
                    return ParameterValueSnapshot{ value, Guid() };
            }
        }

        // 跟 captureTextureParameterValueSnapshot 对称: SampledTextureT 只用来从
        // snapshot.value 里把 sampleStateOverride 取回来, 真正的纹理对象靠 textureGuid 现查。
        template <typename TextureT, typename SampledTextureT>
        MaterialParameterValue resolveTextureParameterValueSnapshot(const ParameterValueSnapshot& snapshot)
        {
            AssetManager& assetManager = Context::get().getAssetManager();
            // 失效的纹理资产(guid 无效, 或者查不到——已经被删除/移动)退化成 None(空纹理), 不
            // 假设撤销/重做发生的那一刻资产一定还在, 跟 MaterialTemplateEditor::
            // trackTextureParameterValue() 里对纹理引用的处理是同一个原则。
            Asset* asset = snapshot.textureGuid.isValid() ? assetManager.getAsset(snapshot.textureGuid) : nullptr;
            TRef<TextureT> texture = asset ? asset->getOrLoadRootObject<TextureT>() : TRef<TextureT>();
            const std::optional<SampleState>& sampleStateOverride = snapshot.value.get<SampledTextureT>().sampleStateOverride;
            return sampleStateOverride
                ? MaterialParameterValue(texture.get(), *sampleStateOverride)
                : MaterialParameterValue(texture.get());
        }

        MaterialParameterValue resolveParameterValueSnapshot(const ParameterValueSnapshot& snapshot)
        {
            switch (snapshot.value.getType())
            {
                case MaterialParameterType::Texture_2D:
                    return resolveTextureParameterValueSnapshot<Texture2D, SampledTexture2D>(snapshot);
                case MaterialParameterType::Texture_2D_Array:
                    return resolveTextureParameterValueSnapshot<Texture2DArray, SampledTexture2DArray>(snapshot);
                case MaterialParameterType::Texture_3D:
                    return resolveTextureParameterValueSnapshot<Texture3D, SampledTexture3D>(snapshot);
                case MaterialParameterType::Texture_Cube:
                    return resolveTextureParameterValueSnapshot<TextureCube, SampledTextureCube>(snapshot);
                case MaterialParameterType::Font:
                {
                    AssetManager& assetManager = Context::get().getAssetManager();
                    Asset* asset = snapshot.textureGuid.isValid() ? assetManager.getAsset(snapshot.textureGuid) : nullptr;
                    TRef<Font> font = asset ? asset->getOrLoadRootObject<Font>() : TRef<Font>();
                    return MaterialParameterValue(font.get());
                }
                default:
                    return snapshot.value;
            }
        }

        // Reset 按钮点击是一次性的离散动作, 不是拖拽手势, 走普通 push() 而不是
        // trackGesture()——而且必须在对应字段的 trackGesture() 之后单独调用, 顺序不能提前:
        // DrawPropertyReset() 只有值不等于默认值时才会真的画一个 Button, 这个 Button 一旦
        // 画出来就成了 ImGui 的"上一个 item", 提前调 trackGesture() 会读到 Reset 按钮的状态
        // 而不是前面那个控件的(AtmospherePanel/DirectionalLightPanel 都踩过这个坑)。
        template <typename T, typename Setter>
        void pushReset(UndoStack& undoStack, HString name, Setter setter, const T& before, const T& after)
        {
            undoStack.push(name,
                [setter, before]() mutable { setter(before); },
                [setter, after]() mutable { setter(after); });
        }

        // 空字符串 = "Common"。返回 true 时 group 已经被改成新选的值, 调用方负责推 undo/
        // 标脏/让分桶缓存失效——这个函数只管画 UI、不碰 undo 栈, 因为模板参数行和"新增参数"行
        // 两处调用点对"改变之后要做什么"的需求不一样(前者要接 undo, 后者只是暂存待创建状态)。
        bool drawParameterGroupCombo(const char* strId, HString& group, std::span<const HString> groups)
        {
            const char* preview = group.empty() ? "Common" : group.toCString();
            bool changed = false;
            if (ImGui::BeginCombo(strId, preview))
            {
                if (ImGui::Selectable("Common", group.empty()) && !group.empty())
                {
                    group = HString();
                    changed = true;
                }
                for (const HString& candidate : groups)
                {
                    ImGui::PushID(candidate.toCString());
                    bool selected = group == candidate;
                    if (ImGui::Selectable(candidate.toCString(), selected) && !selected)
                    {
                        group = candidate;
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        // 私有纹理的类型下拉只列纹理类型——跟 EnumCombo<MaterialParameterType> 那种会把
        // Bool/Int/Float 这些标量类型也列出来的通用枚举下拉不是同一个东西, addPrivateTexture()
        // 本来就没有非纹理重载, 下拉里出现选不了的选项没有意义。
        bool drawPrivateTextureTypeCombo(const char* strId, MaterialParameterType& type)
        {
            static constexpr MaterialParameterType kTextureTypes[] = {
                MaterialParameterType::Texture_2D,
                MaterialParameterType::Texture_2D_Array,
                MaterialParameterType::Texture_3D,
                MaterialParameterType::Texture_Cube,
            };
            bool changed = false;
            if (ImGui::BeginCombo(strId, slangParameterTypeName(type)))
            {
                for (MaterialParameterType candidate : kTextureTypes)
                {
                    bool selected = candidate == type;
                    if (ImGui::Selectable(slangParameterTypeName(candidate), selected) && !selected)
                    {
                        type = candidate;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }
    }

    MaterialTemplateEditor::MaterialTemplateEditor(Asset& asset, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        AssetEditorWindow(asset, undoStack),
        mIconProvider(iconProvider),
        mAssetBrowser(assetBrowser),
        mMaterialTemplate(asset.getOrLoadRootObject<MaterialTemplate>()),
        mPendingParameters(mMaterialTemplate->getParameters().begin(), mMaterialTemplate->getParameters().end()),
        mMaterialSourceEditor(buildMaterialSourcePreamble(), mMaterialTemplate->getMaterialSource()),
        mVertexPositionOffsetSourceEditor(buildVertexPositionOffsetPreamble(), mMaterialTemplate->getVertexPositionOffsetSource())
    {
        instances().push_back(this);
    }

    MaterialTemplateEditor::~MaterialTemplateEditor()
    {
        std::erase(instances(), this);
    }

    std::vector<MaterialTemplateEditor*>& MaterialTemplateEditor::instances()
    {
        static std::vector<MaterialTemplateEditor*> sInstances;
        return sInstances;
    }

    void MaterialTemplateEditor::forEachInstance(const std::function<void(MaterialTemplateEditor&)>& callback)
    {
        for (MaterialTemplateEditor* editor : instances())
            callback(*editor);
    }

    std::string MaterialTemplateEditor::buildMaterialSourcePreamble() const
    {
        return buildMaterialIODeclaration(mMaterialTemplate->getMaterialDomain(), mMaterialTemplate->getShadingModel(), mMaterialTemplate->isAnisotropic())
            + buildParameterDeclarations(mPendingParameters)
            + buildParameterDeclarations(mMaterialTemplate->getPrivateTextures())
            + resolveMaterialFunctionLibrarySources(mMaterialTemplate->getMaterialFunctionLibraries());
    }

    std::string MaterialTemplateEditor::buildVertexPositionOffsetPreamble() const
    {
        return std::string(kVertexPositionOffsetIODeclaration)
            + buildParameterDeclarations(mPendingParameters)
            + buildParameterDeclarations(mMaterialTemplate->getPrivateTextures())
            + resolveMaterialFunctionLibrarySources(mMaterialTemplate->getMaterialFunctionLibraries());
    }

    MaterialParameterValue* MaterialTemplateEditor::findPendingParameterValue(HString name)
    {
        auto it = std::find_if(mPendingParameters.begin(), mPendingParameters.end(),
            [name](const MaterialParameter& parameter) { return parameter.name == name; });
        return it != mPendingParameters.end() ? &it->value : nullptr;
    }

    void MaterialTemplateEditor::reconcilePendingParameters()
    {
        // 名字和类型都要匹配才算"还在待定列表里"——只比名字的话, 同名但改了类型的参数
        // (先删旧的、再建一个同名不同类型的新的, 都还没 Apply)会被误判成"没变", 进而走进
        // 下面的 setParameterDefaultValue() 分支, 而它自己内部类型不匹配时是静默不做任何
        // 事(见 MaterialTemplate.cpp), 类型变更就被整条链路默默吞掉了。
        auto isStillPending = [this](const MaterialParameter& existing)
        {
            return std::any_of(mPendingParameters.begin(), mPendingParameters.end(),
                [&existing](const MaterialParameter& pending)
                {
                    return pending.name == existing.name
                        && pending.value.getType() == existing.value.getType();
                });
        };

        // 先删——mMaterialTemplate->getParameters() 里有、mPendingParameters 里已经没有的。
        // 先收集名字再删, 不在遍历 getParameters() 的同时调用 removeParameter()(会让 span 失效)。
        std::vector<HString> toRemove;
        for (const MaterialParameter& existing : mMaterialTemplate->getParameters())
        {
            if (!isStillPending(existing))
                toRemove.push_back(existing.name);
        }
        for (const HString& name : toRemove)
            mMaterialTemplate->removeParameter(name);

        // 再逐个新增(模板里还没有的)或更新值(模板里已经有的)。
        for (const MaterialParameter& pending : mPendingParameters)
        {
            if (mMaterialTemplate->getParameterIndex(pending.name) >= mMaterialTemplate->getParameterCount())
                mMaterialTemplate->addParameter(pending.name, pending.value);
            else
                mMaterialTemplate->setParameterDefaultValue(pending.name, pending.value);

            // addParameter()/setParameterDefaultValue() 都不知道 group 字段的存在(签名里
            // 没有), 不补这一行的话 Apply 之后所有参数的分组都会被悄悄清空回 Common。
            mMaterialTemplate->setParameterGroup(pending.name, pending.group);
        }

        // 用模板最新的参数列表重新同步一遍工作副本——不假定上面每一步都必然成功(比如未来
        // addParameter()/setParameterDefaultValue() 增加了新的失败路径), 避免下一帧编辑器
        // 状态跟模板出现分歧。
        mPendingParameters.assign(mMaterialTemplate->getParameters().begin(), mMaterialTemplate->getParameters().end());
        mParameterGroupBucketsDirty = true;
    }

    void MaterialTemplateEditor::onParameterSetChanged()
    {
        mParametersModified = true;
        mParameterGroupBucketsDirty = true;
        mMaterialSourceEditor.setPreamble(buildMaterialSourcePreamble());
        mVertexPositionOffsetSourceEditor.setPreamble(buildVertexPositionOffsetPreamble());
    }

    void MaterialTemplateEditor::rebuildParameterGroupBucketsIfDirty() const
    {
        if (!mParameterGroupBucketsDirty)
            return;

        std::span<const HString> groups = mMaterialTemplate->getParameterGroups();
        mParameterGroupBuckets.assign(groups.size() + 1, {});
        for (uint32_t index = 0; index < mPendingParameters.size(); ++index)
        {
            const HString& group = mPendingParameters[index].group;
            auto it = std::find(groups.begin(), groups.end(), group);
            size_t bucketIndex = it != groups.end()
                ? static_cast<size_t>(std::distance(groups.begin(), it))
                : groups.size();
            mParameterGroupBuckets[bucketIndex].push_back(index);
        }
        mParameterGroupBucketsDirty = false;
    }

    void MaterialTemplateEditor::onDraw()
    {
        // isOpen() 为 false 时 EditorWindow::draw() 下一帧就不会再调 onDraw(), 这个判断天然只在关闭发生的那一帧触发一次
        if (!isOpen() && needsApply())
        {
            setOpen(true);
            ImGui::OpenPopup("Unapplied Changes##MaterialTemplateEditor");
        }
        if (ImGui::BeginPopupModal("Unapplied Changes##MaterialTemplateEditor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("You have unapplied changes. Apply them?");
            if (ImGui::Button("Yes"))
            {
                applyPendingChanges();
                setOpen(false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("No"))
            {
                setOpen(false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        drawGeneralProperties();
        ImGui::Spacing();
        drawParameterGroupManagement();
        ImGui::Spacing();
        drawParameterList();
        ImGui::Spacing();
        drawAddParameterRow();
        ImGui::Spacing();
        drawPrivateTextures();
        ImGui::Spacing();
        drawMaterialFunctionLibraries();
        ImGui::Spacing();
        drawSources();
        ImGui::Spacing();
        drawApplyAndSave();
    }

    void MaterialTemplateEditor::drawGeneralProperties()
    {
        if (!BeginPropertyTable("MaterialTemplateProperties", ImGuiTableFlags_BordersInnerH))
            return;

        MaterialTemplate& mt = *mMaterialTemplate;
        const MaterialTemplate& dmt = *static_cast<const MaterialTemplate*>(MaterialTemplate::staticClass()->getDefaultObject());

        BeginPropertyRow("Material Domain");
        MaterialDomain domainBefore = mt.getMaterialDomain();
        MaterialDomain domain = domainBefore;
        bool domainChanged = EnumCombo("##MaterialDomain", &domain);
        auto domainSetter = [this, &mt](MaterialDomain v)
        {
            mt.setMaterialDomain(v);
            mMaterialSourceEditor.setPreamble(buildMaterialSourcePreamble());
        };
        undoStack().trackGesture("Change Material Domain"_hs, domainBefore, domain, domainSetter);
        if (domainChanged)
            domainSetter(domain);
        MaterialDomain domainBeforeReset = domain;
        if (DrawPropertyReset("Material Domain", domain, dmt.getMaterialDomain()))
        {
            domainSetter(domain);
            pushReset(undoStack(), "Reset Material Domain"_hs, domainSetter, domainBeforeReset, domain);
        }

        ImGui::BeginDisabled(domain == MaterialDomain::Sky);

        BeginPropertyRow("Blend Mode");
        BlendMode blendModeBefore = mt.getBlendMode();
        BlendMode blendMode = blendModeBefore;
        bool blendModeChanged = EnumCombo("##BlendMode", &blendMode);
        auto blendModeSetter = [&mt](BlendMode v) { mt.setBlendMode(v); };
        undoStack().trackGesture("Change Blend Mode"_hs, blendModeBefore, blendMode, blendModeSetter);
        if (blendModeChanged)
            mt.setBlendMode(blendMode);
        BlendMode blendModeBeforeReset = blendMode;
        if (DrawPropertyReset("Blend Mode", blendMode, dmt.getBlendMode()))
        {
            mt.setBlendMode(blendMode);
            pushReset(undoStack(), "Reset Blend Mode"_hs, blendModeSetter, blendModeBeforeReset, blendMode);
        }

        BeginPropertyRow("Alpha Cutoff");
        float alphaCutoffBefore = mt.getAlphaCutoff();
        float alphaCutoff = alphaCutoffBefore;
        bool alphaCutoffChanged = ImGui::DragFloat("##AlphaCutoff", &alphaCutoff, 0.01f, 0.0f, 1.0f, "%.3f");
        auto alphaCutoffSetter = [&mt](float v) { mt.setAlphaCutoff(v); };
        undoStack().trackGesture("Change Alpha Cutoff"_hs, alphaCutoffBefore, alphaCutoff, alphaCutoffSetter);
        if (alphaCutoffChanged)
            mt.setAlphaCutoff(alphaCutoff);
        float alphaCutoffBeforeReset = alphaCutoff;
        if (DrawPropertyReset("Alpha Cutoff", alphaCutoff, dmt.getAlphaCutoff()))
        {
            mt.setAlphaCutoff(alphaCutoff);
            pushReset(undoStack(), "Reset Alpha Cutoff"_hs, alphaCutoffSetter, alphaCutoffBeforeReset, alphaCutoff);
        }

        BeginPropertyRow("Shading Model");
        ShadingModel shadingModelBefore = mt.getShadingModel();
        ShadingModel shadingModel = shadingModelBefore;
        bool shadingModelChanged = EnumCombo("##ShadingModel", &shadingModel);
        auto shadingModelSetter = [this, &mt](ShadingModel v)
        {
            mt.setShadingModel(v);
            mMaterialSourceEditor.setPreamble(buildMaterialSourcePreamble());
        };
        undoStack().trackGesture("Change Shading Model"_hs, shadingModelBefore, shadingModel, shadingModelSetter);
        if (shadingModelChanged)
            shadingModelSetter(shadingModel);
        ShadingModel shadingModelBeforeReset = shadingModel;
        if (DrawPropertyReset("Shading Model", shadingModel, dmt.getShadingModel()))
        {
            shadingModelSetter(shadingModel);
            pushReset(undoStack(), "Reset Shading Model"_hs, shadingModelSetter, shadingModelBeforeReset, shadingModel);
        }

        BeginPropertyRow("Double Sided");
        bool doubleSidedBefore = mt.isDoubleSided();
        bool doubleSided = doubleSidedBefore;
        bool doubleSidedChanged = ImGui::Checkbox("##DoubleSided", &doubleSided);
        auto doubleSidedSetter = [&mt](bool v) { mt.setDoubleSided(v); };
        undoStack().trackGesture("Change Double Sided"_hs, doubleSidedBefore, doubleSided, doubleSidedSetter);
        if (doubleSidedChanged)
            mt.setDoubleSided(doubleSided);
        bool doubleSidedBeforeReset = doubleSided;
        if (DrawPropertyReset("Double Sided", doubleSided, dmt.isDoubleSided()))
        {
            mt.setDoubleSided(doubleSided);
            pushReset(undoStack(), "Reset Double Sided"_hs, doubleSidedSetter, doubleSidedBeforeReset, doubleSided);
        }

        BeginPropertyRow("Tangent Space Normal");
        bool tangentSpaceNormalBefore = mt.isTangentSpaceNormal();
        bool tangentSpaceNormal = tangentSpaceNormalBefore;
        bool tangentSpaceNormalChanged = ImGui::Checkbox("##TangentSpaceNormal", &tangentSpaceNormal);
        auto tangentSpaceNormalSetter = [&mt](bool v) { mt.setTangentSpaceNormal(v); };
        undoStack().trackGesture("Change Tangent Space Normal"_hs, tangentSpaceNormalBefore, tangentSpaceNormal, tangentSpaceNormalSetter);
        if (tangentSpaceNormalChanged)
            mt.setTangentSpaceNormal(tangentSpaceNormal);
        bool tangentSpaceNormalBeforeReset = tangentSpaceNormal;
        if (DrawPropertyReset("Tangent Space Normal", tangentSpaceNormal, dmt.isTangentSpaceNormal()))
        {
            mt.setTangentSpaceNormal(tangentSpaceNormal);
            pushReset(undoStack(), "Reset Tangent Space Normal"_hs, tangentSpaceNormalSetter, tangentSpaceNormalBeforeReset, tangentSpaceNormal);
        }

        BeginPropertyRow("Anisotropy");
        bool anisotropyBefore = mt.isAnisotropic();
        bool anisotropy = anisotropyBefore;
        bool anisotropyChanged = ImGui::Checkbox("##Anisotropy", &anisotropy);
        auto anisotropySetter = [this, &mt](bool v)
        {
            mt.setAnisotropic(v);
            mMaterialSourceEditor.setPreamble(buildMaterialSourcePreamble());
        };
        undoStack().trackGesture("Change Anisotropy"_hs, anisotropyBefore, anisotropy, anisotropySetter);
        if (anisotropyChanged)
            anisotropySetter(anisotropy);
        bool anisotropyBeforeReset = anisotropy;
        if (DrawPropertyReset("Anisotropy", anisotropy, dmt.isAnisotropic()))
        {
            anisotropySetter(anisotropy);
            pushReset(undoStack(), "Reset Anisotropy"_hs, anisotropySetter, anisotropyBeforeReset, anisotropy);
        }

        ImGui::EndDisabled();

        EndPropertyTable();
    }

    void MaterialTemplateEditor::drawParameterGroupManagement()
    {
        ImGui::SeparatorText("Parameter Groups");

        // 组列表的增/删/改名/挪动都立即生效(直接改 mMaterialTemplate, 不走 mPendingParameters
        // 那套 pending+Apply), 因为组列表本身不影响 shader/preamble, 没有必要延迟——跟
        // doubleSided/tangentSpaceNormal 这类简单属性是同一个"立即生效 + undo"的模式, 不是
        // Add/Remove Parameter 那种结构性操作的模式。
        std::span<const HString> groups = mMaterialTemplate->getParameterGroups();
        for (uint32_t i = 0; i < groups.size(); ++i)
        {
            HString name = groups[i];
            ImGui::PushID(static_cast<int>(i));

            std::string editBuffer(name.toStringView());
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::InputText("##GroupName", &editBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
            {
                HString newName(editBuffer);
                if (newName != name && mMaterialTemplate->renameParameterGroup(name, newName))
                {
                    mParameterGroupBucketsDirty = true;
                    getAsset().setDirty(true);
                    undoStack().push("Rename Parameter Group"_hs,
                        [this, newName, name]() { mMaterialTemplate->renameParameterGroup(newName, name); mParameterGroupBucketsDirty = true; getAsset().setDirty(true); },
                        [this, name, newName]() { mMaterialTemplate->renameParameterGroup(name, newName); mParameterGroupBucketsDirty = true; getAsset().setDirty(true); });
                }
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(i == 0);
            if (TextOnlyButton(ICON_MS_ARROW_UPWARD))
            {
                mMaterialTemplate->swapParameterGroups(i, i - 1);
                mParameterGroupBucketsDirty = true;
                getAsset().setDirty(true);
                uint32_t a = i, b = i - 1;
                undoStack().push("Move Parameter Group Up"_hs,
                    [this, a, b]() { mMaterialTemplate->swapParameterGroups(a, b); mParameterGroupBucketsDirty = true; getAsset().setDirty(true); },
                    [this, a, b]() { mMaterialTemplate->swapParameterGroups(a, b); mParameterGroupBucketsDirty = true; getAsset().setDirty(true); });
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(i + 1 >= groups.size());
            if (TextOnlyButton(ICON_MS_ARROW_DOWNWARD))
            {
                mMaterialTemplate->swapParameterGroups(i, i + 1);
                mParameterGroupBucketsDirty = true;
                getAsset().setDirty(true);
                uint32_t a = i, b = i + 1;
                undoStack().push("Move Parameter Group Down"_hs,
                    [this, a, b]() { mMaterialTemplate->swapParameterGroups(a, b); mParameterGroupBucketsDirty = true; getAsset().setDirty(true); },
                    [this, a, b]() { mMaterialTemplate->swapParameterGroups(a, b); mParameterGroupBucketsDirty = true; getAsset().setDirty(true); });
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (TextOnlyButton(ICON_MS_CLOSE))
            {
                // 组删除时要把引用它的参数也一起处理, 不只是从组列表里摘掉这一项——
                // MaterialTemplate::removeParameterGroup() 会清空 mMaterialTemplate::mParameters
                // 里匹配的 group 字段(避免以后重建同名组时把这些参数悄悄"收编"回去, 见那边的
                // 注释), 但 mPendingParameters 是编辑器这边独立的工作副本, 分组归属实际上是
                // 以它(而不是 mMaterialTemplate::getParameters())为准在显示, 这里要跟着同步
                // 清一遍。捕获受影响的参数名列表是为了让 undo 能精确把它们的分组指回来, 不是
                // 只把组名加回列表就完事。
                std::vector<HString> affectedParams;
                for (const MaterialParameter& parameter : mPendingParameters)
                {
                    if (parameter.group == name)
                        affectedParams.push_back(parameter.name);
                }

                mMaterialTemplate->removeParameterGroup(name);
                for (MaterialParameter& parameter : mPendingParameters)
                {
                    if (parameter.group == name)
                        parameter.group = HString();
                }
                mParameterGroupBucketsDirty = true;
                getAsset().setDirty(true);

                // 组删除(以及带出来的参数归 Common)是立即生效 + undo 的操作, 不标
                // mParametersModified——理由跟上面 moveTo() 的注释一样, 这不是需要 Apply 的
                // 那类变化, 但仍然要标 Asset 脏(见 needsSave()——它不看 mParametersModified,
                // 只看 needsApply() || getAsset().isDirty(), 组变化完全绕开了 needsApply()
                // 那条路径, 不显式标脏的话 Save 按钮永远不会因为纯分组变化而亮起来)。undo 把
                // 组重新加回列表末尾, 不还原它删除前的下标——顺序只是显示细节, 跟 Remove
                // Parameter 的 undo 是同一个取舍(见 drawParameterList() 里的注释)。
                undoStack().push("Remove Parameter Group"_hs,
                    [this, name, affectedParams]()
                    {
                        mMaterialTemplate->addParameterGroup(name);
                        for (const HString& paramName : affectedParams)
                        {
                            mMaterialTemplate->setParameterGroup(paramName, name);
                            auto it = std::find_if(mPendingParameters.begin(), mPendingParameters.end(),
                                [&paramName](const MaterialParameter& p) { return p.name == paramName; });
                            if (it != mPendingParameters.end())
                                it->group = name;
                        }
                        mParameterGroupBucketsDirty = true;
                        getAsset().setDirty(true);
                    },
                    [this, name, affectedParams]()
                    {
                        mMaterialTemplate->removeParameterGroup(name);
                        for (const HString& paramName : affectedParams)
                        {
                            auto it = std::find_if(mPendingParameters.begin(), mPendingParameters.end(),
                                [&paramName](const MaterialParameter& p) { return p.name == paramName; });
                            if (it != mPendingParameters.end())
                                it->group = HString();
                        }
                        mParameterGroupBucketsDirty = true;
                        getAsset().setDirty(true);
                    });
            }

            ImGui::PopID();
        }

        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##NewGroupName", "Group Name", &mNewGroupName);
        ImGui::SameLine();
        ImGui::BeginDisabled(mNewGroupName.empty());
        if (ImGui::Button("Add Group"))
        {
            HString newName(mNewGroupName);
            if (mMaterialTemplate->addParameterGroup(newName))
            {
                mNewGroupName.clear();
                mParameterGroupBucketsDirty = true;
                getAsset().setDirty(true);
                undoStack().push("Add Parameter Group"_hs,
                    [this, newName]() { mMaterialTemplate->removeParameterGroup(newName); mParameterGroupBucketsDirty = true; getAsset().setDirty(true); },
                    [this, newName]() { mMaterialTemplate->addParameterGroup(newName); mParameterGroupBucketsDirty = true; getAsset().setDirty(true); });
            }
        }
        ImGui::EndDisabled();
    }

    void MaterialTemplateEditor::drawParameterList()
    {
        ImGui::SeparatorText("Material Parameters");

        AssetManager& assetManager = Context::get().getAssetManager();

        rebuildParameterGroupBucketsIfDirty();
        std::span<const HString> groups = mMaterialTemplate->getParameterGroups();

        // Remove 是结构性操作, 但跟 Inspector 里的 Add Component 那种真正不接 undo 的先例不同
        // (Inspector 的 Remove Component 其实是接了 undo 的, 见 Inspector::removeComponentWithUndo()),
        // 这里也接——真正执行要放到所有分组渲染完之后, erase 掉 mPendingParameters 里的一项,
        // 渲染过程中做的话会让正在遍历的下标缓存(mParameterGroupBuckets)失效。Remove 只改
        // mPendingParameters(工作副本), 不直接动 mMaterialTemplate——要等 Apply 才通过
        // reconcilePendingParameters() 写回, 理由见 mPendingParameters 的声明处注释。
        std::optional<HString> pendingRemove;

        // 每个组一个可折叠区块(照抄 InspectorPanel.cpp 的 IInspectorPanel::draw() 的
        // PushID+CollapsingHeader+PopID 形状), 区块标题用组名当 ID, 组内是一张独立的属性表——
        // 最后固定加一个 "Common" 桶, 承接没有分组/分组已被删除或改名导致悬空的参数, 不需要
        // 额外清理代码。
        auto drawBucket = [&](const char* label, const std::vector<uint32_t>& indices)
        {
            if (indices.empty())
                return;

            ImGui::PushID(label);
            bool expanded = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
            if (expanded && BeginPropertyTable("MaterialParameters", ImGuiTableFlags_BordersInnerH))
            {
                for (uint32_t index : indices)
                {
                    MaterialParameter& parameter = mPendingParameters[index];

                    // 纹理类型的参数行会被 drawTextureValue() 里的 AssetCombo(两行缩略图布局)
                    // 撑高, BeginPropertyRow() 的 rowMultiplier 按这个提前告诉它, 好把 Name 列
                    // 的文字和右侧按钮也一起垂直居中, 不然会贴在撑高之后的行顶部。
                    bool isTextureParam = parameter.value.getType() == MaterialParameterType::Texture_2D
                        || parameter.value.getType() == MaterialParameterType::Texture_2D_Array
                        || parameter.value.getType() == MaterialParameterType::Texture_3D
                        || parameter.value.getType() == MaterialParameterType::Texture_Cube
                        || parameter.value.getType() == MaterialParameterType::Font;

                    ImGui::PushID(parameter.name.toCString());
                    BeginPropertyRow(parameter.name.toCString(), isTextureParam ? 2.0f : 1.0f);

                    drawParameterValueWidget(parameter, assetManager);

                    ImGui::TableSetColumnIndex(2);
                    AlignVerticallyInRow(GetCurrentPropertyRowHeight(), ImGui::GetFrameHeight());
                    if (TextOnlyButton(ICON_MS_CLOSE))
                        pendingRemove = parameter.name;

                    // "移动到分组"改成右键菜单——列 2(Remove 按钮那列)是共享属性表里给 Reset
                    // 按钮预留的固定窄列, 塞不下一个能显示组名的下拉框。菜单挂在 Name 列上,
                    // 而不是直接调用 BeginPropertyRow() 之后的"上一个控件"——BeginPropertyRow()
                    // 内部画完 Name 标签就立刻切到列 1 画了一个分隔条 InvisibleButton, 等控制权
                    // 回到这里时"上一个控件"早就是那个分隔条而不是 Name 标签了, 所以这里改成
                    // 手动跳回列 0、在标签原地叠一个同大小的透明按钮专门接右键(标签本身只是
                    // TextUnformatted, 没有自己的交互, 叠加不会跟任何已有交互冲突)。
                    ImGui::TableSetColumnIndex(0);
                    ImVec2 labelCellPos = ImGui::GetCursorScreenPos();
                    ImVec2 labelCellSize(ImGui::GetContentRegionAvail().x, GetCurrentPropertyRowHeight());
                    ImGui::SetCursorScreenPos(labelCellPos);
                    ImGui::InvisibleButton("##GroupContextTarget", labelCellSize);
                    if (ImGui::BeginPopupContextItem("ParameterGroupContext"))
                    {
                        ImGui::TextDisabled("Move to Group");
                        ImGui::Separator();

                        // 换组立即生效, 不走 mParametersModified/Apply 那一套——Apply 是给
                        // addParameter/removeParameter/setParameterDefaultValue 这类会触发
                        // shader 重新拼接/编译的操作准备的, 换组只是纯 UI 元数据, 不该被绑进
                        // 同一个"攒起来一起点 Apply"的节奏里, 跟组列表本身的增删改名/挪动
                        // (drawParameterGroupManagement() 里)是同一个"立即生效 + undo"模式。
                        // mMaterialTemplate->setParameterGroup() 才是权威写入; mPendingParameters
                        // 这份工作副本只是 drawParameterList() 自己拿来渲染用的镜像, 跟着改一份
                        // 是为了不用等下一次 Apply/reconcile 才刷新显示。
                        auto moveTo = [&](HString newGroup)
                        {
                            HString paramName = parameter.name;
                            HString oldGroup = parameter.group;
                            parameter.group = newGroup;
                            mMaterialTemplate->setParameterGroup(paramName, newGroup);
                            mParameterGroupBucketsDirty = true;
                            getAsset().setDirty(true);
                            undoStack().push("Change Parameter Group"_hs,
                                [this, paramName, oldGroup]()
                                {
                                    mMaterialTemplate->setParameterGroup(paramName, oldGroup);
                                    auto it = std::find_if(mPendingParameters.begin(), mPendingParameters.end(),
                                        [&paramName](const MaterialParameter& p) { return p.name == paramName; });
                                    if (it != mPendingParameters.end())
                                        it->group = oldGroup;
                                    mParameterGroupBucketsDirty = true;
                                    getAsset().setDirty(true);
                                },
                                [this, paramName, newGroup]()
                                {
                                    mMaterialTemplate->setParameterGroup(paramName, newGroup);
                                    auto it = std::find_if(mPendingParameters.begin(), mPendingParameters.end(),
                                        [&paramName](const MaterialParameter& p) { return p.name == paramName; });
                                    if (it != mPendingParameters.end())
                                        it->group = newGroup;
                                    mParameterGroupBucketsDirty = true;
                                    getAsset().setDirty(true);
                                });
                        };

                        bool inCommon = parameter.group.empty();
                        if (ImGui::MenuItem("Common", nullptr, inCommon) && !inCommon)
                            moveTo(HString());
                        for (const HString& candidate : groups)
                        {
                            bool selected = parameter.group == candidate;
                            if (ImGui::MenuItem(candidate.toCString(), nullptr, selected) && !selected)
                                moveTo(candidate);
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }
                EndPropertyTable();
            }
            ImGui::PopID();
        };

        for (size_t i = 0; i < groups.size(); ++i)
            drawBucket(groups[i].toCString(), mParameterGroupBuckets[i]);
        drawBucket("Common", mParameterGroupBuckets.back());

        if (pendingRemove)
        {
            auto it = std::find_if(mPendingParameters.begin(), mPendingParameters.end(),
                [&pendingRemove](const MaterialParameter& parameter) { return parameter.name == *pendingRemove; });
            if (it != mPendingParameters.end())
            {
                HString removedName = it->name;
                HString removedGroup = it->group;
                ParameterValueSnapshot removedSnapshot = captureParameterValueSnapshot(it->value);

                mPendingParameters.erase(it);
                onParameterSetChanged();

                // undo 把参数重新 push_back 到末尾, 不还原它删除前的下标——顺序只是显示上的
                // 细节, 不值得为了保序去多存一个下标、多写一段按下标插入的逻辑。
                undoStack().push("Remove Parameter"_hs,
                    [this, removedName, removedGroup, removedSnapshot]()
                    {
                        MaterialParameter parameter;
                        parameter.name = removedName;
                        parameter.value = resolveParameterValueSnapshot(removedSnapshot);
                        parameter.group = removedGroup;
                        mPendingParameters.push_back(std::move(parameter));
                        onParameterSetChanged();
                    },
                    [this, removedName]()
                    {
                        std::erase_if(mPendingParameters, [&removedName](const MaterialParameter& parameter)
                        {
                            return parameter.name == removedName;
                        });
                        onParameterSetChanged();
                    });
            }
        }
    }

    void MaterialTemplateEditor::drawParameterValueWidget(MaterialParameter& parameter, AssetManager& assetManager)
    {
        HString name = parameter.name;
        const MaterialParameterValue& before = parameter.value;

        switch (before.getType())
        {
            case MaterialParameterType::Bool:
            {
                bool beforeValue = before.get<bool>();
                bool v = beforeValue;
                bool changed = ImGui::Checkbox("##Value", &v);
                trackParameterValue(name, beforeValue, v);
                if (changed)
                {
                    parameter.value.set(v);
                    mParametersModified = true;
                }
                break;
            }
            case MaterialParameterType::Int:
            {
                int beforeValue = before.get<int>();
                int v = beforeValue;
                bool changed = ImGui::InputInt("##Value", &v);
                trackParameterValue(name, beforeValue, v);
                if (changed)
                {
                    parameter.value.set(v);
                    mParametersModified = true;
                }
                break;
            }
            case MaterialParameterType::Float:
            {
                float beforeValue = before.get<float>();
                float v = beforeValue;
                bool changed = ImGui::DragFloat("##Value", &v);
                trackParameterValue(name, beforeValue, v);
                if (changed)
                {
                    parameter.value.set(v);
                    mParametersModified = true;
                }
                break;
            }
            case MaterialParameterType::Float2:
            {
                float2 beforeValue = before.get<float2>();
                float2 v = beforeValue;
                bool changed = ImGui::DragFloat2("##Value", &v.x);
                trackParameterValue(name, beforeValue, v);
                if (changed)
                {
                    parameter.value.set(v);
                    mParametersModified = true;
                }
                break;
            }
            case MaterialParameterType::Float3:
            {
                float3 beforeValue = before.get<float3>();
                float3 v = beforeValue;
                bool changed = LinearColorEdit3("##Value", &v.x);
                trackParameterValue(name, beforeValue, v);
                if (changed)
                {
                    parameter.value.set(v);
                    mParametersModified = true;
                }
                break;
            }
            case MaterialParameterType::Float4:
            {
                float4 beforeValue = before.get<float4>();
                float4 v = beforeValue;
                bool changed = LinearColorEdit4("##Value", &v.x);
                trackParameterValue(name, beforeValue, v);
                if (changed)
                {
                    parameter.value.set(v);
                    mParametersModified = true;
                }
                break;
            }
            case MaterialParameterType::Texture_2D:
            {
                SampledTexture2D v = before.get<SampledTexture2D>();
                TextureParamSnapshot beforeSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                bool changed = drawTextureValue<Texture2D>(v.texture, v.sampleStateOverride, assetManager);
                TextureParamSnapshot currentSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                trackTextureParameterValue<Texture2D>(name, beforeSnapshot, currentSnapshot);
                if (changed)
                {
                    parameter.value = v.sampleStateOverride
                        ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride)
                        : MaterialParameterValue(v.texture.get());
                    mParametersModified = true;
                }
                break;
            }
            case MaterialParameterType::Texture_2D_Array:
            {
                SampledTexture2DArray v = before.get<SampledTexture2DArray>();
                TextureParamSnapshot beforeSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                bool changed = drawTextureValue<Texture2DArray>(v.texture, v.sampleStateOverride, assetManager);
                TextureParamSnapshot currentSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                trackTextureParameterValue<Texture2DArray>(name, beforeSnapshot, currentSnapshot);
                if (changed)
                {
                    parameter.value = v.sampleStateOverride
                        ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride)
                        : MaterialParameterValue(v.texture.get());
                    mParametersModified = true;
                }
                break;
            }
            case MaterialParameterType::Texture_3D:
            {
                SampledTexture3D v = before.get<SampledTexture3D>();
                TextureParamSnapshot beforeSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                bool changed = drawTextureValue<Texture3D>(v.texture, v.sampleStateOverride, assetManager);
                TextureParamSnapshot currentSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                trackTextureParameterValue<Texture3D>(name, beforeSnapshot, currentSnapshot);
                if (changed)
                {
                    parameter.value = v.sampleStateOverride
                        ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride)
                        : MaterialParameterValue(v.texture.get());
                    mParametersModified = true;
                }
                break;
            }
            case MaterialParameterType::Texture_Cube:
            {
                SampledTextureCube v = before.get<SampledTextureCube>();
                TextureParamSnapshot beforeSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                bool changed = drawTextureValue<TextureCube>(v.texture, v.sampleStateOverride, assetManager);
                TextureParamSnapshot currentSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                trackTextureParameterValue<TextureCube>(name, beforeSnapshot, currentSnapshot);
                if (changed)
                {
                    parameter.value = v.sampleStateOverride
                        ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride)
                        : MaterialParameterValue(v.texture.get());
                    mParametersModified = true;
                }
                break;
            }
            case MaterialParameterType::Font:
            {
                SampledFont v = before.get<SampledFont>();
                Guid beforeGuid = v.font ? v.font->getGuid() : Guid();
                bool changed = drawFontValue(v.font, assetManager);
                Guid currentGuid = v.font ? v.font->getGuid() : Guid();
                undoStack().trackGesture("Change Parameter"_hs, beforeGuid, currentGuid,
                    [this, name](const Guid& guid)
                    {
                        MaterialParameterValue* value = findPendingParameterValue(name);
                        if (!value)
                            return;
                        AssetManager& assetManager = Context::get().getAssetManager();
                        Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
                        TRef<Font> font = asset ? asset->getOrLoadRootObject<Font>() : TRef<Font>();
                        *value = MaterialParameterValue(font.get());
                        mParametersModified = true;
                    });
                if (changed)
                {
                    parameter.value = MaterialParameterValue(v.font.get());
                    mParametersModified = true;
                }
                break;
            }
            default:
                break;
        }
    }

    template <typename T>
    void MaterialTemplateEditor::trackParameterValue(HString name, const T& before, const T& current)
    {
        undoStack().trackGesture("Change Parameter"_hs, before, current,
            [this, name](const T& v)
            {
                if (MaterialParameterValue* value = findPendingParameterValue(name))
                {
                    value->set(v);
                    mParametersModified = true;
                }
            });
    }

    template <typename TextureT>
    MaterialTemplateEditor::TextureParamSnapshot MaterialTemplateEditor::makeTextureSnapshot(const TRef<TextureT>& texture, const std::optional<SampleState>& sampleStateOverride) const
    {
        return {
            texture ? texture->getGuid() : Guid(),
            sampleStateOverride.has_value(),
            sampleStateOverride.value_or(SampleState{})
        };
    }

    template <typename TextureT>
    void MaterialTemplateEditor::trackTextureParameterValue(HString name, const TextureParamSnapshot& before, const TextureParamSnapshot& current)
    {
        undoStack().trackGesture("Change Parameter"_hs, before, current,
            [this, name](const TextureParamSnapshot& s)
            {
                MaterialParameterValue* value = findPendingParameterValue(name);
                if (!value)
                    return;
                AssetManager& assetManager = Context::get().getAssetManager();
                Asset* asset = s.textureGuid.isValid() ? assetManager.getAsset(s.textureGuid) : nullptr;
                TRef<TextureT> texture = asset ? asset->getOrLoadRootObject<TextureT>() : TRef<TextureT>();
                *value = s.hasOverride
                    ? MaterialParameterValue(texture.get(), s.overrideState)
                    : MaterialParameterValue(texture.get());
                mParametersModified = true;
            });
    }

    template <typename TextureT>
    bool MaterialTemplateEditor::drawTextureValue(TRef<TextureT>& texture, std::optional<SampleState>& sampleStateOverride, AssetManager& assetManager)
    {
        Guid guid = texture ? texture->getGuid() : Guid();
        bool changed = AssetComboEd("##Texture", assetManager, TextureT::staticClass(), false, guid, mAssetBrowser, mIconProvider);
        if (changed)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            texture = asset ? asset->getOrLoadRootObject<TextureT>() : TRef<TextureT>();
        }

        ImGui::SameLine();
        if (TextOnlyButton(ICON_MS_MORE_HORIZ))
            ImGui::OpenPopup("SamplerOverride");

        if (ImGui::BeginPopup("SamplerOverride"))
        {
            bool hasOverride = sampleStateOverride.has_value();
            if (ImGui::Checkbox("Override Sampler", &hasOverride))
            {
                changed = true;
                sampleStateOverride = hasOverride ? std::optional<SampleState>(SampleState{}) : std::nullopt;
            }
            if (hasOverride)
            {
                SampleState& state = *sampleStateOverride;
                changed |= EnumCombo("##Filter", &state.filter);
                changed |= EnumCombo("##AddressU", &state.addressModeU);
                changed |= EnumCombo("##AddressV", &state.addressModeV);
                changed |= EnumCombo("##AddressW", &state.addressModeW);
            }
            ImGui::EndPopup();
        }
        return changed;
    }

    bool MaterialTemplateEditor::drawFontValue(TRef<Font>& font, AssetManager& assetManager)
    {
        Guid guid = font ? font->getGuid() : Guid();
        bool changed = AssetComboEd("##Font", assetManager, Font::staticClass(), false, guid, mAssetBrowser, mIconProvider);
        if (changed)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            font = asset ? asset->getOrLoadRootObject<Font>() : TRef<Font>();
        }
        return changed;
    }

    void MaterialTemplateEditor::resetNewParameterDefaultValue()
    {
        switch (mNewParameterType)
        {
            case MaterialParameterType::Bool:
                mNewParameterDefaultValue = MaterialParameterValue(false);
                break;
            case MaterialParameterType::Int:
                mNewParameterDefaultValue = MaterialParameterValue(0);
                break;
            case MaterialParameterType::Float:
                mNewParameterDefaultValue = MaterialParameterValue(0.0f);
                break;
            case MaterialParameterType::Float2:
                mNewParameterDefaultValue = MaterialParameterValue(float2(0.0f));
                break;
            case MaterialParameterType::Float3:
                mNewParameterDefaultValue = MaterialParameterValue(float3(0.0f));
                break;
            case MaterialParameterType::Float4:
                mNewParameterDefaultValue = MaterialParameterValue(float4(0.0f));
                break;
            case MaterialParameterType::Texture_2D:
                mNewParameterDefaultValue = MaterialParameterValue(static_cast<Texture2D*>(nullptr));
                break;
            case MaterialParameterType::Texture_2D_Array:
                mNewParameterDefaultValue = MaterialParameterValue(static_cast<Texture2DArray*>(nullptr));
                break;
            case MaterialParameterType::Texture_3D:
                mNewParameterDefaultValue = MaterialParameterValue(static_cast<Texture3D*>(nullptr));
                break;
            case MaterialParameterType::Texture_Cube:
                mNewParameterDefaultValue = MaterialParameterValue(static_cast<TextureCube*>(nullptr));
                break;
            case MaterialParameterType::Font:
                mNewParameterDefaultValue = MaterialParameterValue(static_cast<Font*>(nullptr));
                break;
            default:
                break;
        }
    }

    void MaterialTemplateEditor::drawNewParameterDefaultValueWidget(AssetManager& assetManager)
    {
        switch (mNewParameterType)
        {
            case MaterialParameterType::Bool:
            {
                bool v = mNewParameterDefaultValue.get<bool>();
                if (ImGui::Checkbox("##NewParamDefault", &v))
                    mNewParameterDefaultValue.set(v);
                break;
            }
            case MaterialParameterType::Int:
            {
                int v = mNewParameterDefaultValue.get<int>();
                if (ImGui::InputInt("##NewParamDefault", &v))
                    mNewParameterDefaultValue.set(v);
                break;
            }
            case MaterialParameterType::Float:
            {
                float v = mNewParameterDefaultValue.get<float>();
                if (ImGui::DragFloat("##NewParamDefault", &v))
                    mNewParameterDefaultValue.set(v);
                break;
            }
            case MaterialParameterType::Float2:
            {
                float2 v = mNewParameterDefaultValue.get<float2>();
                if (ImGui::DragFloat2("##NewParamDefault", &v.x))
                    mNewParameterDefaultValue.set(v);
                break;
            }
            case MaterialParameterType::Float3:
            {
                float3 v = mNewParameterDefaultValue.get<float3>();
                if (LinearColorEdit3("##NewParamDefault", &v.x))
                    mNewParameterDefaultValue.set(v);
                break;
            }
            case MaterialParameterType::Float4:
            {
                float4 v = mNewParameterDefaultValue.get<float4>();
                if (LinearColorEdit4("##NewParamDefault", &v.x))
                    mNewParameterDefaultValue.set(v);
                break;
            }
            case MaterialParameterType::Texture_2D:
            {
                SampledTexture2D v = mNewParameterDefaultValue.get<SampledTexture2D>();
                if (drawTextureValue<Texture2D>(v.texture, v.sampleStateOverride, assetManager))
                {
                    mNewParameterDefaultValue = v.sampleStateOverride
                        ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride)
                        : MaterialParameterValue(v.texture.get());
                }
                break;
            }
            case MaterialParameterType::Texture_2D_Array:
            {
                SampledTexture2DArray v = mNewParameterDefaultValue.get<SampledTexture2DArray>();
                if (drawTextureValue<Texture2DArray>(v.texture, v.sampleStateOverride, assetManager))
                {
                    mNewParameterDefaultValue = v.sampleStateOverride
                        ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride)
                        : MaterialParameterValue(v.texture.get());
                }
                break;
            }
            case MaterialParameterType::Texture_3D:
            {
                SampledTexture3D v = mNewParameterDefaultValue.get<SampledTexture3D>();
                if (drawTextureValue<Texture3D>(v.texture, v.sampleStateOverride, assetManager))
                {
                    mNewParameterDefaultValue = v.sampleStateOverride
                        ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride)
                        : MaterialParameterValue(v.texture.get());
                }
                break;
            }
            case MaterialParameterType::Texture_Cube:
            {
                SampledTextureCube v = mNewParameterDefaultValue.get<SampledTextureCube>();
                if (drawTextureValue<TextureCube>(v.texture, v.sampleStateOverride, assetManager))
                {
                    mNewParameterDefaultValue = v.sampleStateOverride
                        ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride)
                        : MaterialParameterValue(v.texture.get());
                }
                break;
            }
            case MaterialParameterType::Font:
            {
                SampledFont v = mNewParameterDefaultValue.get<SampledFont>();
                if (drawFontValue(v.font, assetManager))
                    mNewParameterDefaultValue = MaterialParameterValue(v.font.get());
                break;
            }
            default:
                break;
        }
    }

    void MaterialTemplateEditor::drawAddParameterRow()
    {
        AssetManager& assetManager = Context::get().getAssetManager();

        // 用固定列宽的表格, 不再靠 SameLine() 一路接下去——纹理类型的默认值控件
        // (drawTextureValue() 里先是 AssetComboEd() 那个两行缩略图/Browse/Use Selected 的
        // 复合布局, 后面还紧跟着一个"..."采样器覆盖按钮和它自己的 SameLine())中间夹杂了好几层
        // 手动 SetCursorScreenPos 和嵌套的 SameLine(), SameLine() 认的是"最后一个控件"的矩形,
        // 不是整块复合控件的可视范围——不管是直接 SameLine() 还是外面套一层 BeginGroup/
        // EndGroup, 都会让 Add 按钮错位甚至整个不出现(用 BeginGroup 包过一次, 结果是这样)。
        // 表格的列切换是绝对定位, 不依赖"上一个控件画在哪"这种隐式状态, 天然规避掉这一整类
        // 问题, 每种参数类型下 Add 按钮都稳定出现在第 4 列。
        // Name/Group/Type/Value 按比例(2:1.5:2:3)撑满整行, Add 固定宽度不参与分配——跟
        // BeginPropertyTable() 里 Reset 列的做法一致(混用 WidthStretch/WidthFixed 需要
        // 表格整体开 SizingStretchProp, 否则 WidthStretch 列不会去抢 WidthFixed 列让出的
        // 剩余空间)。
        if (ImGui::BeginTable("AddParameterRow", 5, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("##Group", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("##Type", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            static const float kAddButtonWidth = ImGui::CalcTextSize("Add").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::TableSetupColumn("##Add", ImGuiTableColumnFlags_WidthFixed, kAddButtonWidth);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##NewParamName", "Parameter Name", &mNewParameterName);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            drawParameterGroupCombo("##NewParamGroup", mNewParameterGroup, mMaterialTemplate->getParameterGroups());

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1.0f);
            // 类型换了, 默认值也要跟着换成新类型的零值/空值——不然 MaterialParameterValue 内部
            // 存的类型跟 mNewParameterType 对不上, 上面画默认值控件的 switch 会用错误的类型读。
            if (EnumCombo("##NewParamType", &mNewParameterType))
                resetNewParameterDefaultValue();

            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-1.0f);
            drawNewParameterDefaultValueWidget(assetManager);

            ImGui::TableSetColumnIndex(4);
            ImGui::BeginDisabled(mNewParameterName.empty());
            if (ImGui::Button("Add"))
            {
                // 名字重复(跟 mPendingParameters 里已有的比, 不是跟 mMaterialTemplate 比——
                // 工作副本才是当前"看得到"的参数集合)就什么都不做, 也不需要额外提示, 名字栏
                // 保留原样让用户改一下重试就行。只落在 mPendingParameters 上, 要等 Apply 才
                // 通过 reconcilePendingParameters() 真正写回模板。
                HString newName(mNewParameterName);
                auto occupiedNames = [](const MaterialParameter& parameter)
                {
                    std::vector<HString> names = { parameter.name };
                    if (parameter.value.getType() == MaterialParameterType::Font)
                    {
                        const FontParameterNames fontNames = makeFontParameterNames(parameter.name);
                        names.push_back(fontNames.sdfType);
                        names.push_back(fontNames.spreadUv);
                    }
                    return names;
                };
                MaterialParameter newParameter;
                newParameter.name = newName;
                newParameter.value = mNewParameterDefaultValue;
                const std::vector<HString> newNames = occupiedNames(newParameter);
                bool duplicate = std::any_of(mPendingParameters.begin(), mPendingParameters.end(),
                    [&](const MaterialParameter& parameter)
                    {
                        for (HString occupied : occupiedNames(parameter))
                        {
                            if (std::find(newNames.begin(), newNames.end(), occupied) != newNames.end())
                                return true;
                        }
                        return false;
                    });
                if (!duplicate)
                {
                    MaterialParameter& parameter = mPendingParameters.emplace_back();
                    parameter.name = newName;
                    parameter.value = mNewParameterDefaultValue;
                    parameter.group = mNewParameterGroup;

                    mNewParameterName.clear();
                    onParameterSetChanged();

                    ParameterValueSnapshot addedSnapshot = captureParameterValueSnapshot(parameter.value);
                    HString addedGroup = parameter.group;
                    undoStack().push("Add Parameter"_hs,
                        [this, newName]()
                        {
                            std::erase_if(mPendingParameters, [&newName](const MaterialParameter& parameter)
                            {
                                return parameter.name == newName;
                            });
                            onParameterSetChanged();
                        },
                        [this, newName, addedGroup, addedSnapshot]()
                        {
                            MaterialParameter parameter;
                            parameter.name = newName;
                            parameter.value = resolveParameterValueSnapshot(addedSnapshot);
                            parameter.group = addedGroup;
                            mPendingParameters.push_back(std::move(parameter));
                            onParameterSetChanged();
                        });
                }
            }
            ImGui::EndDisabled();

            ImGui::EndTable();
        }
    }

    void MaterialTemplateEditor::drawMaterialFunctionLibraries()
    {
        ImGui::SeparatorText("Material Function Libraries");

        AssetManager& assetManager = Context::get().getAssetManager();

        auto resolveLibraryAsset = [&assetManager](Guid guid) -> MaterialFunctionLibrary*
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<MaterialFunctionLibrary>().get() : nullptr;
        };

        // 排除掉选了就会失败的候选项(已经引用过), 不用等用户点 Add 才发现选不了
        auto canAdd = [this](Asset* asset) -> bool
        {
            MaterialFunctionLibrary* candidate = asset->getOrLoadRootObject<MaterialFunctionLibrary>().get();
            return mMaterialTemplate->canAddMaterialFunctionLibrary(candidate);
        };

        std::span<const TRef<MaterialFunctionLibrary>> imported = mMaterialTemplate->getMaterialFunctionLibraries();
        MaterialFunctionLibrary* toRemove = nullptr;
        for (uint32_t i = 0; i < imported.size(); ++i)
        {
            MaterialFunctionLibrary* library = imported[i].get();
            Guid guid = library->getGuid();
            ImGui::PushID(static_cast<int>(i));
            bool changed = AssetComboEd("##MaterialFunctionLibrary", assetManager, MaterialFunctionLibrary::staticClass(), false, guid, mAssetBrowser, mIconProvider, canAdd);
            ImGui::SameLine();
            bool removeClicked = ImGui::Button("Remove");
            ImGui::PopID();

            if (removeClicked)
            {
                toRemove = library;
            }
            else if (changed)
            {
                // 把这一行换成引用另一个库——先试着把新的加进去, 成功了才把旧的摘掉;
                // 加不进去(比如会成环)原样保留旧的引用, 不留下"看着换了、其实没生效"的中间态
                MaterialFunctionLibrary* replacement = resolveLibraryAsset(guid);
                if (replacement && mMaterialTemplate->addMaterialFunctionLibrary(replacement))
                {
                    mMaterialTemplate->removeMaterialFunctionLibrary(library);
                    mMaterialFunctionLibraryError.clear();
                    refreshFunctionLibraryPreamble();
                }
                else
                {
                    mMaterialFunctionLibraryError = "Can't switch to that library (empty or already referenced).";
                }
            }
        }
        if (toRemove)
        {
            mMaterialTemplate->removeMaterialFunctionLibrary(toRemove);
            refreshFunctionLibraryPreamble();
        }

        ImGui::PushID("NewMaterialFunctionLibrary");
        AssetComboEd("##NewMaterialFunctionLibrary", assetManager, MaterialFunctionLibrary::staticClass(), false, mNewMaterialFunctionLibraryGuid, mAssetBrowser, mIconProvider, canAdd);
        ImGui::SameLine();
        if (ImGui::Button("Add"))
        {
            MaterialFunctionLibrary* toAdd = resolveLibraryAsset(mNewMaterialFunctionLibraryGuid);
            if (toAdd && mMaterialTemplate->addMaterialFunctionLibrary(toAdd))
            {
                mNewMaterialFunctionLibraryGuid = Guid();
                mMaterialFunctionLibraryError.clear();
                refreshFunctionLibraryPreamble();
            }
            else
            {
                mMaterialFunctionLibraryError = "Can't add that library (empty or already referenced).";
            }
        }
        ImGui::PopID();

        if (!mMaterialFunctionLibraryError.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", mMaterialFunctionLibraryError.c_str());
    }

    void MaterialTemplateEditor::drawPrivateTextureValueWidget(uint32_t index, const MaterialParameter& texture, AssetManager& assetManager)
    {
        switch (texture.value.getType())
        {
            case MaterialParameterType::Texture_2D:
            {
                SampledTexture2D v = texture.value.get<SampledTexture2D>();
                if (drawTextureValue<Texture2D>(v.texture, v.sampleStateOverride, assetManager))
                {
                    mMaterialTemplate->setPrivateTextureValue(index, v.sampleStateOverride
                        ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride)
                        : MaterialParameterValue(v.texture.get()));
                }
                break;
            }
            case MaterialParameterType::Texture_2D_Array:
            {
                SampledTexture2DArray v = texture.value.get<SampledTexture2DArray>();
                if (drawTextureValue<Texture2DArray>(v.texture, v.sampleStateOverride, assetManager))
                {
                    mMaterialTemplate->setPrivateTextureValue(index, v.sampleStateOverride
                        ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride)
                        : MaterialParameterValue(v.texture.get()));
                }
                break;
            }
            case MaterialParameterType::Texture_3D:
            {
                SampledTexture3D v = texture.value.get<SampledTexture3D>();
                if (drawTextureValue<Texture3D>(v.texture, v.sampleStateOverride, assetManager))
                {
                    mMaterialTemplate->setPrivateTextureValue(index, v.sampleStateOverride
                        ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride)
                        : MaterialParameterValue(v.texture.get()));
                }
                break;
            }
            case MaterialParameterType::Texture_Cube:
            {
                SampledTextureCube v = texture.value.get<SampledTextureCube>();
                if (drawTextureValue<TextureCube>(v.texture, v.sampleStateOverride, assetManager))
                {
                    mMaterialTemplate->setPrivateTextureValue(index, v.sampleStateOverride
                        ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride)
                        : MaterialParameterValue(v.texture.get()));
                }
                break;
            }
            default:
                break;
        }
    }

    // 私有纹理没有 mPendingParameters 那样的工作副本——增删/改值直接作用于
    // mMaterialTemplate, 跟 drawMaterialFunctionLibraries() 同一种"立即生效"模式, 见
    // MaterialTemplateEditor.h 里 mNewPrivateTextureName 的注释。
    void MaterialTemplateEditor::drawPrivateTextures()
    {
        ImGui::SeparatorText("Private Textures");

        AssetManager& assetManager = Context::get().getAssetManager();

        std::span<const MaterialParameter> privateTextures = mMaterialTemplate->getPrivateTextures();
        if (!privateTextures.empty() && BeginPropertyTable("PrivateTextures", ImGuiTableFlags_BordersInnerH))
        {
            HString toRemove;
            for (uint32_t i = 0; i < privateTextures.size(); ++i)
            {
                const MaterialParameter& texture = privateTextures[i];
                ImGui::PushID(texture.name.toCString());
                BeginPropertyRow(texture.name.toCString(), 2.0f);

                drawPrivateTextureValueWidget(i, texture, assetManager);

                ImGui::TableSetColumnIndex(2);
                AlignVerticallyInRow(GetCurrentPropertyRowHeight(), ImGui::GetFrameHeight());
                if (TextOnlyButton(ICON_MS_CLOSE))
                    toRemove = texture.name;

                ImGui::PopID();
            }
            EndPropertyTable();

            if (!toRemove.empty())
            {
                mMaterialTemplate->removePrivateTexture(toRemove);
                refreshFunctionLibraryPreamble();
            }
        }

        if (ImGui::BeginTable("AddPrivateTextureRow", 4, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("##Type", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            static const float kAddButtonWidth = ImGui::CalcTextSize("Add").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::TableSetupColumn("##Add", ImGuiTableColumnFlags_WidthFixed, kAddButtonWidth);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##NewPrivateTextureName", "Texture Name", &mNewPrivateTextureName);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            // 类型换了, 待新增值也要跟着换成新类型的空纹理——理由跟 drawAddParameterRow() 里
            // 换 mNewParameterType 后调 resetNewParameterDefaultValue() 一样。
            if (drawPrivateTextureTypeCombo("##NewPrivateTextureType", mNewPrivateTextureType))
            {
                switch (mNewPrivateTextureType)
                {
                    case MaterialParameterType::Texture_2D:
                        mNewPrivateTextureValue = MaterialParameterValue(static_cast<Texture2D*>(nullptr));
                        break;
                    case MaterialParameterType::Texture_2D_Array:
                        mNewPrivateTextureValue = MaterialParameterValue(static_cast<Texture2DArray*>(nullptr));
                        break;
                    case MaterialParameterType::Texture_3D:
                        mNewPrivateTextureValue = MaterialParameterValue(static_cast<Texture3D*>(nullptr));
                        break;
                    case MaterialParameterType::Texture_Cube:
                        mNewPrivateTextureValue = MaterialParameterValue(static_cast<TextureCube*>(nullptr));
                        break;
                    default:
                        break;
                }
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1.0f);
            switch (mNewPrivateTextureType)
            {
                case MaterialParameterType::Texture_2D:
                {
                    SampledTexture2D v = mNewPrivateTextureValue.get<SampledTexture2D>();
                    if (drawTextureValue<Texture2D>(v.texture, v.sampleStateOverride, assetManager))
                        mNewPrivateTextureValue = v.sampleStateOverride
                            ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride) : MaterialParameterValue(v.texture.get());
                    break;
                }
                case MaterialParameterType::Texture_2D_Array:
                {
                    SampledTexture2DArray v = mNewPrivateTextureValue.get<SampledTexture2DArray>();
                    if (drawTextureValue<Texture2DArray>(v.texture, v.sampleStateOverride, assetManager))
                        mNewPrivateTextureValue = v.sampleStateOverride
                            ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride) : MaterialParameterValue(v.texture.get());
                    break;
                }
                case MaterialParameterType::Texture_3D:
                {
                    SampledTexture3D v = mNewPrivateTextureValue.get<SampledTexture3D>();
                    if (drawTextureValue<Texture3D>(v.texture, v.sampleStateOverride, assetManager))
                        mNewPrivateTextureValue = v.sampleStateOverride
                            ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride) : MaterialParameterValue(v.texture.get());
                    break;
                }
                case MaterialParameterType::Texture_Cube:
                {
                    SampledTextureCube v = mNewPrivateTextureValue.get<SampledTextureCube>();
                    if (drawTextureValue<TextureCube>(v.texture, v.sampleStateOverride, assetManager))
                        mNewPrivateTextureValue = v.sampleStateOverride
                            ? MaterialParameterValue(v.texture.get(), *v.sampleStateOverride) : MaterialParameterValue(v.texture.get());
                    break;
                }
                default:
                    break;
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::BeginDisabled(mNewPrivateTextureName.empty());
            if (ImGui::Button("Add"))
            {
                HString newName(mNewPrivateTextureName);
                bool added = false;
                switch (mNewPrivateTextureType)
                {
                    case MaterialParameterType::Texture_2D:
                        added = mMaterialTemplate->addPrivateTexture(newName, mNewPrivateTextureValue.get<SampledTexture2D>().texture.get(),
                            mNewPrivateTextureValue.get<SampledTexture2D>().sampleStateOverride);
                        break;
                    case MaterialParameterType::Texture_2D_Array:
                        added = mMaterialTemplate->addPrivateTexture(newName, mNewPrivateTextureValue.get<SampledTexture2DArray>().texture.get(),
                            mNewPrivateTextureValue.get<SampledTexture2DArray>().sampleStateOverride);
                        break;
                    case MaterialParameterType::Texture_3D:
                        added = mMaterialTemplate->addPrivateTexture(newName, mNewPrivateTextureValue.get<SampledTexture3D>().texture.get(),
                            mNewPrivateTextureValue.get<SampledTexture3D>().sampleStateOverride);
                        break;
                    case MaterialParameterType::Texture_Cube:
                        added = mMaterialTemplate->addPrivateTexture(newName, mNewPrivateTextureValue.get<SampledTextureCube>().texture.get(),
                            mNewPrivateTextureValue.get<SampledTextureCube>().sampleStateOverride);
                        break;
                    default:
                        break;
                }

                if (added)
                {
                    mNewPrivateTextureName.clear();
                    mPrivateTextureError.clear();
                    refreshFunctionLibraryPreamble();
                }
                else
                {
                    mPrivateTextureError = "Can't add that texture (empty name, or already used by a parameter/private texture).";
                }
            }
            ImGui::EndDisabled();

            ImGui::EndTable();
        }

        if (!mPrivateTextureError.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", mPrivateTextureError.c_str());
    }

    bool MaterialTemplateEditor::referencesFunctionLibrary(const MaterialFunctionLibrary* library) const noexcept
    {
        std::function<bool(const MaterialFunctionLibrary*)> visits = [&](const MaterialFunctionLibrary* candidate) -> bool
        {
            if (candidate == library)
                return true;
            for (const TRef<MaterialFunctionLibrary>& imported : candidate->getImportedLibraries())
            {
                if (visits(imported.get()))
                    return true;
            }
            return false;
        };

        for (const TRef<MaterialFunctionLibrary>& root : mMaterialTemplate->getMaterialFunctionLibraries())
        {
            if (visits(root.get()))
                return true;
        }
        return false;
    }

    void MaterialTemplateEditor::refreshFunctionLibraryPreamble()
    {
        mMaterialSourceEditor.setPreamble(buildMaterialSourcePreamble());
        mVertexPositionOffsetSourceEditor.setPreamble(buildVertexPositionOffsetPreamble());
    }

    void MaterialTemplateEditor::drawSources()
    {
        ImGui::SeparatorText("Material Source");
        // ImGuiChildFlags_ResizeY 给这块编辑框自带一条可拖拽的底边(用法照抄
        // TextureEditor::drawPreview() 的 PreviewChild)——尺寸由 ImGui 按这个 child 的 ID
        // 记忆, 不需要自己维护一个高度成员。子编辑器里的 render() 传 (-1,-1) 直接铺满这个
        // child, 由外层 child 的高度决定编辑框实际大小
        if (ImGui::BeginChild("MaterialSourceEditorChild", ImVec2(-1.0f, 240.0f), ImGuiChildFlags_ResizeY))
        {
            mSourcesModified |= mMaterialSourceEditor.render("##MaterialSourceEditor", ImVec2(-1.0f, -1.0f));
        }
        ImGui::EndChild();

        ImGui::SeparatorText("Vertex Position Offset Source");
        ImGui::BeginDisabled(mMaterialTemplate->getMaterialDomain() == MaterialDomain::Sky);
        bool hasVertexPositionOffset = !mVertexPositionOffsetSourceEditor.getText().empty();
        if (ImGui::Checkbox("Enable##VertexPositionOffset", &hasVertexPositionOffset))
        {
            mVertexPositionOffsetSourceEditor.setText(hasVertexPositionOffset ? kDefaultVertexPositionOffsetSource : std::string());
            mSourcesModified = true;
        }
        if (hasVertexPositionOffset)
        {
            if (ImGui::BeginChild("VertexPositionOffsetEditorChild", ImVec2(-1.0f, 160.0f), ImGuiChildFlags_ResizeY))
            {
                mSourcesModified |= mVertexPositionOffsetSourceEditor.render("##VertexPositionOffsetEditor", ImVec2(-1.0f, -1.0f));
            }
            ImGui::EndChild();
        }
        ImGui::EndDisabled();
    }

    bool MaterialTemplateEditor::needsApply() const noexcept
    {
        return mMaterialTemplate->getDirtyFlags() != MaterialTemplate::DirtyFlags::None || mSourcesModified || mParametersModified;
    }

    bool MaterialTemplateEditor::needsSave() const noexcept
    {
        return needsApply() || getAsset().isDirty();
    }

    void MaterialTemplateEditor::applyPendingChanges()
    {
        reconcilePendingParameters();
        mMaterialTemplate->setMaterialSource(mMaterialSourceEditor.getText());
        mMaterialTemplate->setVertexPositionOffsetSource(mVertexPositionOffsetSourceEditor.getText());
        mMaterialTemplate->apply();
        mSourcesModified = false;
        mParametersModified = false;
        getAsset().setDirty(true);
    }

    void MaterialTemplateEditor::drawApplyAndSave()
    {
        ImGui::BeginDisabled(!needsApply());
        if (ImGui::Button("Apply"))
        {
            applyPendingChanges();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!needsSave());
        if (ImGui::Button("Save"))
        {
            // 没点过 Apply 也允许直接 Save, 这里补上避免存盘存到还没写回的旧状态
            if (needsApply())
            {
                applyPendingChanges();
            }
            getAsset().save();
        }
        ImGui::EndDisabled();
    }
}
