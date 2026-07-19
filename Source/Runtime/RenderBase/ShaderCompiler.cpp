#include "ShaderCompiler.h"

#include <Core/Common/LogSystem.h>

#include <slang.h>
#include <slang-com-ptr.h>

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

    std::vector<std::vector<uint8_t>> ShaderCompiler::compileToSpirvs(
        const char* name,
        const char* source,
        std::span<const ShaderStage> stages,
        const char* fileName,
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
        targetDesc.profile = globalSession->findProfile("spirv_1_5");
        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;

        std::vector<PreprocessorMacroDesc> macroDescs;
        macroDescs.reserve(macros.size());
        for (const auto& m : macros)
        {
            macroDescs.emplace_back(m.name.toCString(), m.value.toCString());
        }
        sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macroDescs.size());
        sessionDesc.preprocessorMacros = macroDescs.data();

        std::array<const char*, 1> searchPaths = { SHADER_DIR };
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

    std::vector<std::vector<uint8_t>> ShaderCompiler::compileToWgsls(
        const char* name,
        const char* source,
        std::span<const ShaderStage> stages,
        const char* fileName,
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
        targetDesc.format = SLANG_WGSL;
        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;

        std::vector<PreprocessorMacroDesc> macroDescs;
        macroDescs.reserve(macros.size() + 1);
        macroDescs.emplace_back("RHI_BACKEND_WEBGPU", "1");
        for (const auto& m : macros)
        {
            macroDescs.emplace_back(m.name.toCString(), m.value.toCString());
        }
        sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macroDescs.size());
        sessionDesc.preprocessorMacros = macroDescs.data();

        std::array<const char*, 1> searchPaths = { SHADER_DIR };
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
            size_t length = std::strlen(text);
            const uint8_t* begin = reinterpret_cast<const uint8_t*>(text);
            const uint8_t* end = begin + length + 1;
            result.emplace_back(begin, end);
        }

        return result;
    }

    std::vector<std::string> ShaderCompiler::getDependencyFiles(
        const char* name,
        const char* source,
        const char* fileName,
        std::span<const ShaderMacro> macros,
        bool forWgsl
    )
    {
        std::vector<std::string> result;

        ComPtr<IGlobalSession> globalSession = getGlobalSession();

        SessionDesc sessionDesc = {};
        sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
        sessionDesc.fileSystem = nullptr;

        TargetDesc targetDesc = {};
        if (forWgsl)
        {
            targetDesc.format = SLANG_WGSL;
        }
        else
        {
            targetDesc.format = SLANG_SPIRV;
            targetDesc.profile = globalSession->findProfile("spirv_1_5");
        }
        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;

        std::vector<PreprocessorMacroDesc> macroDescs;
        macroDescs.reserve(macros.size() + 1);
        if (forWgsl)
        {
            macroDescs.emplace_back("RHI_BACKEND_WEBGPU", "1");
        }
        for (const auto& m : macros)
        {
            macroDescs.emplace_back(m.name.toCString(), m.value.toCString());
        }
        sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macroDescs.size());
        sessionDesc.preprocessorMacros = macroDescs.data();

        std::array<const char*, 1> searchPaths = { SHADER_DIR };
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


