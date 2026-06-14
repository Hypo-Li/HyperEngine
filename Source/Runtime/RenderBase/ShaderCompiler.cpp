#include "ShaderCompiler.h"

#include <Core/Common/LogSystem.h>

#include <slang/slang.h>
#include <slang/slang-com-ptr.h>

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

    std::vector<std::vector<uint32_t>> ShaderCompiler::compileToSpirvs(
        const char* name,
        const char* source,
        std::span<const ShaderStage> stages,
        const char* fileName,
        std::span<const ShaderMacro> macros,
        bool optimize,
        bool withDebugInfo
    )
    {
        std::vector<std::vector<uint32_t>> result;
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
        sessionDesc.preprocessorMacros = macroDescs.data();
        sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macroDescs.size());

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

        sessionDesc.searchPaths = nullptr;
        sessionDesc.searchPathCount = 0;

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
            case ShaderStage::Geometry:
                entryPointName = "GeometryMain";
                slangStage = SLANG_STAGE_GEOMETRY;
                break;
            case ShaderStage::TessControl:
                slangStage = SLANG_STAGE_HULL;
                entryPointName = "HullMain";
                break;
            case ShaderStage::TessEvaluation:
                slangStage = SLANG_STAGE_DOMAIN;
                entryPointName = "DomainMain";
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

            const uint32_t* begin = (const uint32_t*)spirvBlob->getBufferPointer();
            const uint32_t* end = begin + (spirvBlob->getBufferSize() / sizeof(uint32_t));
            result.emplace_back(begin, end);
        }

        return result;
    }
}


