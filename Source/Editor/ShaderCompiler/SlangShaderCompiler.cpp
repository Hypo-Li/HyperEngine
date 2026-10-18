#include "SlangShaderCompiler.h"

#include <Core/Common/LogSystem.h>

#include <slang.h>
#include <slang-com-ptr.h>

#include <cassert>
#include <regex>
#include <string_view>

using namespace slang;
using namespace Slang;

namespace hyp
{
    static ComPtr<IGlobalSession> getGlobalSession()
    {
        static ComPtr<IGlobalSession> globalSession;
        if (!globalSession)
        {
            createGlobalSession(globalSession.writeRef());
        }
        return globalSession;
    }

    static const char* getSpirvProfile(const rhi::ShaderCompileTarget& target)
    {
        assert(target.codeFormat == rhi::ShaderCodeFormat::Spirv);

        if (target.majorVersion == 1)
        {
            switch (target.minorVersion)
            {
            case 0: return "spirv_1_0";
            case 3: return "spirv_1_3";
            case 5: return "spirv_1_5";
            case 6: return "spirv_1_6";
            }
        }

        LOG_ERROR("Unsupported SPIR-V target version {}.{}", target.majorVersion, target.minorVersion);
        assert(false && "Unsupported SPIR-V target version");
        return "spirv_1_0";
    }

    // TODO: 等 vendor 的 Slang 版本原生支持 [[vk::push_constant]] 到 WGSL `immediate` 地址空间
    // (见 gpuweb PR #5423)的翻译之后, 删掉这个函数和调用点。现在 Slang 吐出来的是一个没有
    // @group/@binding 的裸 var<uniform>, WGSL 规范要求资源变量必须带这两个属性, Dawn 编译期
    // 直接报 "resource variables require '@group' and '@binding' attributes"。
    // CompilePassShader.cmake 对预编译的 PassShader 已经做了同样的字符串替换(把这一条声明patch
    // 成 var<immediate>), 这里是运行时编译材质 shader(Opaque/Translucent 等)时的等价实现,
    // 按材质源码里 [[vk::push_constant]] 声明的字段名做定点替换, 不是无差别的全局文本替换。
    static std::string patchPushConstantForWgsl(const char* source, std::string wgslText)
    {
        static const std::regex kPushConstantDeclRegex(
            R"(\[\[vk::push_constant\]\][ \t\r\n]*[A-Za-z_][A-Za-z0-9_]*[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*;)"
        );
        std::cmatch declMatch;
        if (!std::regex_search(source, declMatch, kPushConstantDeclRegex))
            return wgslText;

        std::string pushConstantName = declMatch[1].str();
        std::regex uniformDeclRegex("var<uniform> (" + pushConstantName + "_[0-9]+) :");
        wgslText = std::regex_replace(wgslText, uniformDeclRegex, "var<immediate> $1 :");
        return "requires immediate_address_space;\n" + wgslText;
    }

    // 占位 location 必须和 Defines.slang 里 PRIMITIVE_ID 的 TEXCOORD 序号一致
    static constexpr std::string_view kPrimitiveIdPlaceholder = "@location(15)";

    static std::string patchPrimitiveIdForWgsl(std::string wgslText)
    {
        size_t placeholderPos = wgslText.find(kPrimitiveIdPlaceholder);
        if (placeholderPos == std::string::npos)
            return wgslText;

        if (wgslText.find(kPrimitiveIdPlaceholder, placeholderPos + kPrimitiveIdPlaceholder.size()) != std::string::npos)
        {
            LOG_ERROR("WGSL contains more than one primitive id placeholder, patch skipped");
            return wgslText;
        }

        static constexpr std::string_view kFlatInterpolation = "@interpolate(flat) ";
        size_t replaceBegin = placeholderPos;
        if (placeholderPos >= kFlatInterpolation.size() &&
            std::string_view(wgslText).substr(placeholderPos - kFlatInterpolation.size(), kFlatInterpolation.size()) == kFlatInterpolation)
        {
            replaceBegin = placeholderPos - kFlatInterpolation.size();
        }

        wgslText.replace(replaceBegin, placeholderPos + kPrimitiveIdPlaceholder.size() - replaceBegin, "@builtin(primitive_index)");
        return "enable primitive_index;\n" + wgslText;
    }

    std::vector<std::vector<uint8_t>> SlangShaderCompiler::compileToSpirvs(
        const char* name,
        const char* source,
        std::span<const ShaderStage> stages,
        const char* fileName,
        const std::filesystem::path& shaderDir,
        const rhi::ShaderCompileTarget& target,
        std::span<const ShaderMacro> macros,
        bool optimize,
        bool withDebugInfo
    )
    {
        std::vector<std::vector<uint8_t>> result;
        result.reserve(stages.size());

        ComPtr<IGlobalSession> globalSession = getGlobalSession();

        SessionDesc sessionDesc = {};
        sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
        sessionDesc.fileSystem = nullptr;

        TargetDesc targetDesc = {};
        targetDesc.format = SLANG_SPIRV;
        targetDesc.profile = globalSession->findProfile(getSpirvProfile(target));
        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;

        std::vector<PreprocessorMacroDesc> macroDescs;
        macroDescs.reserve(macros.size() + 1);
        macroDescs.emplace_back("RHI_BACKEND", "RHI_BACKEND_VULKAN");
        for (const auto& m : macros)
        {
            macroDescs.emplace_back(m.name.toCString(), m.value.toCString());
        }
        sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macroDescs.size());
        sessionDesc.preprocessorMacros = macroDescs.data();

        std::string shaderDirStr = shaderDir.string();
        std::array<const char*, 1> searchPaths = { shaderDirStr.c_str() };
        sessionDesc.searchPathCount = static_cast<SlangInt>(searchPaths.size());
        sessionDesc.searchPaths = searchPaths.data();

        std::array<CompilerOptionEntry, 2> options;
        {
            CompilerOptionEntry& optimizationEntry = options[0];
            optimizationEntry.name = CompilerOptionName::Optimization;
            optimizationEntry.value.kind = CompilerOptionValueKind::Int;
            optimizationEntry.value.intValue0 = optimize ? SLANG_OPTIMIZATION_LEVEL_MAXIMAL : SLANG_OPTIMIZATION_LEVEL_NONE;

            CompilerOptionEntry& debugInfoEntry = options[1];
            debugInfoEntry.name = CompilerOptionName::DebugInformation;
            debugInfoEntry.value.kind = CompilerOptionValueKind::Int;
            debugInfoEntry.value.intValue0 = withDebugInfo ? SLANG_DEBUG_INFO_LEVEL_MAXIMAL : SLANG_DEBUG_INFO_LEVEL_NONE;
        }
        sessionDesc.compilerOptionEntries = options.data();
        sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(options.size());

        ComPtr<ISession> session;
        globalSession->createSession(sessionDesc, session.writeRef());

        ComPtr<IBlob> diagnosticBlob;
        ComPtr<IModule> module(session->loadModuleFromSourceString(
            name,
            fileName,
            source,
            diagnosticBlob.writeRef()
        ));

        if (!module && (diagnosticBlob && diagnosticBlob->getBufferSize() > 0))
        {
            LOG_ERROR("{}", (const char*)diagnosticBlob->getBufferPointer());
            return result;
        }

        for (size_t i = 0; i < stages.size(); i++)
        {
            SlangStage slangStage = SLANG_STAGE_NONE;
            const char* entryPointName = nullptr;
            switch (stages[i]) {
            case ShaderStage::Vertex:
                slangStage = SLANG_STAGE_VERTEX;
                entryPointName = "VertexMain";
                break;
            case ShaderStage::Fragment:
                slangStage = SLANG_STAGE_FRAGMENT;
                entryPointName = "FragmentMain";
                break;
            case ShaderStage::Compute:
                slangStage = SLANG_STAGE_COMPUTE;
                entryPointName = "ComputeMain";
                break;
            }

            diagnosticBlob.setNull();
            ComPtr<IEntryPoint> entryPoint;
            module->findAndCheckEntryPoint(entryPointName, slangStage, entryPoint.writeRef(), diagnosticBlob.writeRef());
            if (!entryPoint)
            {
                if (diagnosticBlob && diagnosticBlob->getBufferSize() > 0)
                {
                    LOG_ERROR("{}", (const char*)diagnosticBlob->getBufferPointer());
                }
                result.emplace_back();
                continue;
            }

            IComponentType* components[] = { module, entryPoint };
            ComPtr<IComponentType> program;
            session->createCompositeComponentType(components, std::size(components), program.writeRef(), diagnosticBlob.writeRef());

            diagnosticBlob.setNull();
            ComPtr<IBlob> spirvBlob;
            SlangResult entryPointCodeResult = program->getEntryPointCode(0, 0, spirvBlob.writeRef(), diagnosticBlob.writeRef());

            if (SLANG_FAILED(entryPointCodeResult))
            {
                if (diagnosticBlob && diagnosticBlob->getBufferSize() > 0)
                {
                    LOG_ERROR("{}", (const char*)diagnosticBlob->getBufferPointer());
                }
                result.emplace_back();
                continue;
            }

            const uint8_t* begin = (const uint8_t*)spirvBlob->getBufferPointer();
            const uint8_t* end = begin + spirvBlob->getBufferSize();
            result.emplace_back(begin, end);
        }

        return result;
    }

    std::vector<std::vector<uint8_t>> SlangShaderCompiler::compileToWgsls(
        const char* name,
        const char* source,
        std::span<const ShaderStage> stages,
        const char* fileName,
        const std::filesystem::path& shaderDir,
        const rhi::ShaderCompileTarget& target,
        std::span<const ShaderMacro> macros,
        bool optimize,
        bool withDebugInfo
    )
    {
        assert(target.codeFormat == rhi::ShaderCodeFormat::Wgsl);

        std::vector<std::vector<uint8_t>> result;
        result.reserve(stages.size());

        ComPtr<IGlobalSession> globalSession = getGlobalSession();

        SessionDesc sessionDesc = {};
        sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
        sessionDesc.fileSystem = nullptr;

        TargetDesc targetDesc = {};
        targetDesc.format = SLANG_WGSL;
        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;

        std::vector<PreprocessorMacroDesc> macroDescs;
        macroDescs.reserve(macros.size() + 1);
        macroDescs.emplace_back("RHI_BACKEND", "RHI_BACKEND_WEBGPU");
        for (const auto& m : macros)
        {
            macroDescs.emplace_back(m.name.toCString(), m.value.toCString());
        }
        sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macroDescs.size());
        sessionDesc.preprocessorMacros = macroDescs.data();

        std::string shaderDirStr = shaderDir.string();
        std::array<const char*, 1> searchPaths = { shaderDirStr.c_str() };
        sessionDesc.searchPathCount = static_cast<SlangInt>(searchPaths.size());
        sessionDesc.searchPaths = searchPaths.data();

        std::array<CompilerOptionEntry, 2> options;
        {
            CompilerOptionEntry& optimizationEntry = options[0];
            optimizationEntry.name = CompilerOptionName::Optimization;
            optimizationEntry.value.kind = CompilerOptionValueKind::Int;
            optimizationEntry.value.intValue0 = optimize ? SLANG_OPTIMIZATION_LEVEL_MAXIMAL : SLANG_OPTIMIZATION_LEVEL_NONE;

            CompilerOptionEntry& debugInfoEntry = options[1];
            debugInfoEntry.name = CompilerOptionName::DebugInformation;
            debugInfoEntry.value.kind = CompilerOptionValueKind::Int;
            debugInfoEntry.value.intValue0 = withDebugInfo ? SLANG_DEBUG_INFO_LEVEL_MAXIMAL : SLANG_DEBUG_INFO_LEVEL_NONE;
        }
        sessionDesc.compilerOptionEntries = options.data();
        sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(options.size());

        ComPtr<ISession> session;
        globalSession->createSession(sessionDesc, session.writeRef());

        ComPtr<IBlob> diagnosticBlob;
        ComPtr<IModule> module(session->loadModuleFromSourceString(
            name,
            fileName,
            source,
            diagnosticBlob.writeRef()
        ));

        if (!module && (diagnosticBlob && diagnosticBlob->getBufferSize() > 0))
        {
            LOG_ERROR("{}", (const char*)diagnosticBlob->getBufferPointer());
            return result;
        }

        for (size_t i = 0; i < stages.size(); i++)
        {
            SlangStage slangStage = SLANG_STAGE_NONE;
            const char* entryPointName = nullptr;
            switch (stages[i]) {
            case ShaderStage::Vertex:
                slangStage = SLANG_STAGE_VERTEX;
                entryPointName = "VertexMain";
                break;
            case ShaderStage::Fragment:
                slangStage = SLANG_STAGE_FRAGMENT;
                entryPointName = "FragmentMain";
                break;
            case ShaderStage::Compute:
                slangStage = SLANG_STAGE_COMPUTE;
                entryPointName = "ComputeMain";
                break;
            }

            diagnosticBlob.setNull();
            ComPtr<IEntryPoint> entryPoint;
            module->findAndCheckEntryPoint(entryPointName, slangStage, entryPoint.writeRef(), diagnosticBlob.writeRef());
            if (!entryPoint)
            {
                if (diagnosticBlob && diagnosticBlob->getBufferSize() > 0)
                {
                    LOG_ERROR("{}", (const char*)diagnosticBlob->getBufferPointer());
                }
                result.emplace_back();
                continue;
            }
            ComPtr<IComponentType> renamedEntryPoint;
            SlangResult renameResult = entryPoint->renameEntryPoint("main", renamedEntryPoint.writeRef());

            IComponentType* components[] = { module, renamedEntryPoint };
            ComPtr<IComponentType> program;
            session->createCompositeComponentType(components, std::size(components), program.writeRef(), diagnosticBlob.writeRef());

            diagnosticBlob.setNull();
            ComPtr<IBlob> wgslBlob;
            SlangResult entryPointCodeResult = program->getEntryPointCode(0, 0, wgslBlob.writeRef(), diagnosticBlob.writeRef());

            if (SLANG_FAILED(entryPointCodeResult))
            {
                if (diagnosticBlob && diagnosticBlob->getBufferSize() > 0)
                {
                    LOG_ERROR("{}", (const char*)diagnosticBlob->getBufferPointer());
                }
                result.emplace_back();
                continue;
            }

            const char* text = static_cast<const char*>(wgslBlob->getBufferPointer());
            std::string wgslText = patchPrimitiveIdForWgsl(patchPushConstantForWgsl(source, std::string(text)));
            const uint8_t* begin = reinterpret_cast<const uint8_t*>(wgslText.data());
            const uint8_t* end = begin + wgslText.size() + 1;
            result.emplace_back(begin, end);
        }

        return result;
    }

    std::vector<std::string> SlangShaderCompiler::getDependencyFiles(
        const char* name,
        const char* source,
        const char* fileName,
        const std::filesystem::path& shaderDir,
        std::span<const ShaderMacro> macros,
        const rhi::ShaderCompileTarget& target
    )
    {
        std::vector<std::string> result;

        ComPtr<IGlobalSession> globalSession = getGlobalSession();

        SessionDesc sessionDesc = {};
        sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
        sessionDesc.fileSystem = nullptr;

        TargetDesc targetDesc = {};
        if (target.codeFormat == rhi::ShaderCodeFormat::Wgsl)
        {
            targetDesc.format = SLANG_WGSL;
        }
        else
        {
            targetDesc.format = SLANG_SPIRV;
            targetDesc.profile = globalSession->findProfile(getSpirvProfile(target));
        }
        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;

        std::vector<PreprocessorMacroDesc> macroDescs;
        macroDescs.reserve(macros.size() + 1);
        macroDescs.emplace_back(
            "RHI_BACKEND",
            target.codeFormat == rhi::ShaderCodeFormat::Wgsl ? "RHI_BACKEND_WEBGPU" : "RHI_BACKEND_VULKAN"
        );
        for (const auto& m : macros)
        {
            macroDescs.emplace_back(m.name.toCString(), m.value.toCString());
        }
        sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macroDescs.size());
        sessionDesc.preprocessorMacros = macroDescs.data();

        std::string shaderDirStr = shaderDir.string();
        std::array<const char*, 1> searchPaths = { shaderDirStr.c_str() };
        sessionDesc.searchPathCount = static_cast<SlangInt>(searchPaths.size());
        sessionDesc.searchPaths = searchPaths.data();

        ComPtr<ISession> session;
        globalSession->createSession(sessionDesc, session.writeRef());

        ComPtr<IBlob> diagnosticBlob;
        ComPtr<IModule> module(session->loadModuleFromSourceString(
            name,
            fileName,
            source,
            diagnosticBlob.writeRef()
        ));

        if (!module)
        {
            if (diagnosticBlob && diagnosticBlob->getBufferSize() > 0)
            {
                LOG_ERROR("{}", (const char*)diagnosticBlob->getBufferPointer());
            }
            return result;
        }

        SlangInt32 dependencyCount = module->getDependencyFileCount();
        result.reserve(static_cast<size_t>(dependencyCount));
        for (SlangInt32 i = 0; i < dependencyCount; ++i)
        {
            result.emplace_back(module->getDependencyFilePath(i));
        }

        return result;
    }
}
