#pragma once
#include "RenderBackend.h"
#include "Shader.h"

#include <RHI/BindGroup.h>

#include <span>
#include <string_view>
#include <vector>

namespace hyp
{
    // 从 CompilePassShader.cmake 反射生成的 PassShaderBindingDesc[](见对应 .generated.h 里的
    // k<Name>_Bindings)直接建 BindGroupLayout, 取代过去每个 Pass 手写 BindGroupLayoutEntry[]
    // 的做法——binding/type/access/textureViewType/textureSampleType/stageFlags 这些信息现在
    // 只在 .slang 源码里的 [[vk::binding(N, 2)]] 声明一份, 不需要再在 C++ 里抄一份保持同步。
    inline TRef<rhi::BindGroupLayout> createPassBindGroupLayout(
        RenderBackend& renderBackend,
        std::span<const rhi::PassShaderBindingDesc> bindings)
    {
        std::vector<rhi::BindGroupLayoutEntry> entries;
        entries.reserve(bindings.size());
        for (const rhi::PassShaderBindingDesc& desc : bindings)
        {
            entries.push_back(desc.toBindGroupLayoutEntry());
        }
        return renderBackend.getBindGroupLayoutCache().getOrCreate(entries);
    }

    // 拼出 CompilePassShader.cmake 按 k<Name>_<Stage>_Spirv / k<Name>_<Stage>_Wgsl 规则生成的这对符号名,
    // 展开成 makeComputeBytecode/makeVertexFragmentBytecode 要的 span+string_view 参数对,调用点不用手写全名
    #define GET_PASS_SHADER_BYTECODE(name, stage) \
        std::span(generated::k##name##_##stage##_Spirv), std::string_view(generated::k##name##_##stage##_Wgsl)

    // 按当前后端从 spirv/wgsl 中选出实际要用的那一份,包成 PassShaderStageBytecode 的原始字节 span,
    // 不做拷贝(底层是构建期生成头文件里的 static 常量数组,生命周期覆盖整个程序)
    inline std::span<const uint8_t> selectStageBytecode(rhi::Backend backend, std::span<const uint32_t> spirv, std::string_view wgsl)
    {
        if (backend == rhi::Backend::WebGPU)
        {
            // 保留末尾的 '\0',和 SlangShaderCompiler::compileToWgsls 运行时编译出来的格式一致
            const uint8_t* begin = reinterpret_cast<const uint8_t*>(wgsl.data());
            return std::span<const uint8_t>(begin, wgsl.size() + 1);
        }
        const uint8_t* begin = reinterpret_cast<const uint8_t*>(spirv.data());
        return std::span<const uint8_t>(begin, spirv.size_bytes());
    }

    // 构建期预编译产出的字节码(见 CMake/CompilePassShader.cmake 生成的 <Name>.generated.h)按当前后端
    // 选好格式,包装成 PassShader 构造函数要的 PassShaderBytecode,vertex+fragment 或纯 compute 各一个重载
    inline PassShaderBytecode makeComputeBytecode(rhi::Backend backend, std::span<const uint32_t> spirv, std::string_view wgsl)
    {
        PassShaderBytecode bytecode;
        bytecode.codeFormat = backend == rhi::Backend::WebGPU ? rhi::ShaderCodeFormat::Wgsl : rhi::ShaderCodeFormat::Spirv;
        bytecode.compute = selectStageBytecode(backend, spirv, wgsl);
        return bytecode;
    }

    inline PassShaderBytecode makeVertexFragmentBytecode(
        rhi::Backend backend,
        std::span<const uint32_t> vertexSpirv, std::string_view vertexWgsl,
        std::span<const uint32_t> fragmentSpirv, std::string_view fragmentWgsl)
    {
        PassShaderBytecode bytecode;
        bytecode.codeFormat = backend == rhi::Backend::WebGPU ? rhi::ShaderCodeFormat::Wgsl : rhi::ShaderCodeFormat::Spirv;
        bytecode.vertex = selectStageBytecode(backend, vertexSpirv, vertexWgsl);
        bytecode.fragment = selectStageBytecode(backend, fragmentSpirv, fragmentWgsl);
        return bytecode;
    }
}
