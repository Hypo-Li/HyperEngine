#pragma once
#include "Physics/PhysicsLayer.h"
#include "Texture/SampleState.h"

#include <RHI/Context.h>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace hyp
{
    // 引擎启动时用到的各个目录/窗口参数, 从可执行文件所在目录下的 config.json 读取; 文件不存在、
    // 或某个字段缺失时, 对应值退回到默认值(目录退回到可执行文件所在目录下的默认相对路径),
    // 保证不带 config.json 也能直接运行(比如把整个输出目录拷到别的机器上, 不带任何配置文件)。
    class Config
    {
    public:
        Config();
        ~Config();

        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

        const std::filesystem::path& getEngineAssetDir() const noexcept { return mEngineAssetDir; }
        const std::filesystem::path& getProjectAssetDir() const noexcept { return mProjectAssetDir; }
        const std::filesystem::path& getResourceDir() const noexcept { return mResourceDir; }
        const std::filesystem::path& getShaderDir() const noexcept { return mShaderDir; }

        uint32_t getWindowWidth() const noexcept { return mWindowWidth; }
        uint32_t getWindowHeight() const noexcept { return mWindowHeight; }
        const std::string& getWindowTitle() const noexcept { return mWindowTitle; }

        rhi::Backend getRHIBackend() const noexcept { return mRHIBackend; }

        const GlobalSamplerSettings& getGlobalSamplerSettings() const noexcept { return mGlobalSamplerSettings; }

        const PhysicsLayerSettings& getPhysicsLayerSettings() const noexcept { return mPhysicsLayerSettings; }

        const std::string& getDefaultScenePath() const noexcept { return mDefaultScenePath; }

        // 更新内存里的值, 并把完整 config.json(保留其它已有字段)重新写回可执行文件目录——
        // 供编辑器在"首次运行自动建了一个默认场景资产"时把这个路径记下来, 下次启动能直接复用。
        void setDefaultScenePath(const std::string& assetPath);

    private:
        std::filesystem::path mExeDir;
        // Config() 解析出来的完整 json 树——setDefaultScenePath() 只改其中一个字段再整体写回,
        // 不能凭已经拆出来的这些成员字段反推出用户手写在文件里的其它字段。
        std::unique_ptr<nlohmann::json> mJson;

        std::filesystem::path mEngineAssetDir;
        std::filesystem::path mProjectAssetDir;
        std::filesystem::path mResourceDir;
        std::filesystem::path mShaderDir;

        uint32_t mWindowWidth = 1280;
        uint32_t mWindowHeight = 720;
        std::string mWindowTitle = "Hyper Engine";

        rhi::Backend mRHIBackend = rhi::Backend::Vulkan;

        GlobalSamplerSettings mGlobalSamplerSettings;

        PhysicsLayerSettings mPhysicsLayerSettings;

        std::string mDefaultScenePath;
    };
}
