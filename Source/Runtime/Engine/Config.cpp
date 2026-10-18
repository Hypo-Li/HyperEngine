#include "Config.h"

#include <Core/Common/LogSystem.h>
#include <Platform/Platform.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace hyp
{
    namespace
    {
        std::filesystem::path resolveDir(
            const nlohmann::json& json,
            const char* key,
            const std::filesystem::path& exeDir,
            const std::filesystem::path& defaultRelativeDir
        )
        {
            auto it = json.find(key);
            if (it != json.end() && it->is_string())
            {
                std::filesystem::path path = it->get<std::string>();
                if (path.is_relative())
                    path = exeDir / path;
                return path;
            }
            return exeDir / defaultRelativeDir;
        }

        template <typename T>
        T resolveValue(const nlohmann::json& json, const char* key, T defaultValue)
        {
            auto it = json.find(key);
            if (it != json.end() && it->is_number_unsigned())
            {
                return it->get<T>();
            }
            return defaultValue;
        }

        std::string resolveString(const nlohmann::json& json, const char* key, std::string defaultValue)
        {
            auto it = json.find(key);
            if (it != json.end() && it->is_string())
            {
                return it->get<std::string>();
            }
            return defaultValue;
        }

        float resolveFloat(const nlohmann::json& json, const char* key, float defaultValue)
        {
            auto it = json.find(key);
            if (it != json.end() && it->is_number())
            {
                return it->get<float>();
            }
            return defaultValue;
        }

        // config.json 里手写字符串, 不走反射(Filter::Global 这个值不该出现在这里——它是
        // "跟随这份全局设置"的意思, 全局设置自己不能又跟随自己)
        Filter resolveDefaultFilter(const nlohmann::json& json, const char* key, Filter defaultValue)
        {
            auto it = json.find(key);
            if (it == json.end() || !it->is_string())
                return defaultValue;

            const std::string value = it->get<std::string>();
            if (value == "Point") return Filter::Point;
            if (value == "Bilinear") return Filter::Bilinear;
            if (value == "Trilinear") return Filter::Trilinear;
            if (value == "Anisotropic") return Filter::Anisotropic;
            return defaultValue;
        }

        rhi::Backend resolveRHIBackend(const nlohmann::json& json, const char* key, rhi::Backend defaultValue)
        {
            auto it = json.find(key);
            if (it == json.end())
                return defaultValue;

            const std::string value = it->is_string() ? it->get<std::string>() : it->dump();
            if (value == "Vulkan") return rhi::Backend::Vulkan;
            if (value == "WebGPU") return rhi::Backend::WebGPU;

            LOG_WARN("config.json: invalid {} \"{}\" (expected \"Vulkan\" or \"WebGPU\"), using default", key, value);
            return defaultValue;
        }

        bool parsePhysicsLayerRelation(const std::string& value, PhysicsLayerRelation& outRelation)
        {
            if (value == "Ignore") { outRelation = PhysicsLayerRelation::Ignore; return true; }
            if (value == "Overlap") { outRelation = PhysicsLayerRelation::Overlap; return true; }
            if (value == "Block") { outRelation = PhysicsLayerRelation::Block; return true; }
            return false;
        }

        PhysicsLayerSettings resolvePhysicsLayerSettings(const nlohmann::json& json, const char* key)
        {
            PhysicsLayerSettings settings;

            auto it = json.find(key);
            if (it == json.end())
                return settings;

            if (!it->is_object())
            {
                LOG_WARN("config.json: {} must be an object, ignoring", key);
                return settings;
            }

            const HString defaultLayerName = PhysicsLayerSettings::getDefaultLayerName();
            auto isKnownLayer = [&settings, &defaultLayerName](HString name)
            {
                return name == defaultLayerName
                    || std::find(settings.layerNames.begin(), settings.layerNames.end(), name) != settings.layerNames.end();
            };

            auto layersIt = it->find("Layers");
            if (layersIt != it->end() && layersIt->is_array())
            {
                for (const nlohmann::json& item : *layersIt)
                {
                    if (!item.is_string() || item.get<std::string>().empty())
                    {
                        LOG_WARN("config.json: {}.Layers entries must be non-empty strings, skipping one", key);
                        continue;
                    }

                    HString name(item.get<std::string>());
                    if (name == defaultLayerName)
                    {
                        LOG_WARN("config.json: layer name \"{}\" is reserved, skipping", name.toCString());
                        continue;
                    }

                    if (isKnownLayer(name))
                    {
                        LOG_WARN("config.json: duplicate layer \"{}\", skipping", name.toCString());
                        continue;
                    }

                    if (settings.layerNames.size() + 1 >= kMaxPhysicsLayers)
                    {
                        LOG_WARN("config.json: at most {} layers including Default, ignoring the rest", kMaxPhysicsLayers);
                        break;
                    }

                    settings.layerNames.push_back(name);
                }
            }

            auto relationsIt = it->find("Relations");
            if (relationsIt != it->end() && relationsIt->is_array())
            {
                for (const nlohmann::json& item : *relationsIt)
                {
                    std::string a = item.is_object() ? resolveString(item, "A", "") : "";
                    std::string b = item.is_object() ? resolveString(item, "B", "") : "";
                    std::string relationString = item.is_object() ? resolveString(item, "Relation", "") : "";

                    PhysicsLayerRelationEntry entry;
                    entry.a = HString(a);
                    entry.b = HString(b);
                    if (!parsePhysicsLayerRelation(relationString, entry.relation))
                    {
                        LOG_WARN("config.json: invalid layer relation \"{}\" between \"{}\" and \"{}\", skipping", relationString, a, b);
                        continue;
                    }

                    if (!isKnownLayer(entry.a) || !isKnownLayer(entry.b))
                    {
                        LOG_WARN("config.json: layer relation references unknown layer (\"{}\", \"{}\"), skipping", a, b);
                        continue;
                    }

                    auto existing = std::find_if(settings.relations.begin(), settings.relations.end(),
                        [&entry](const PhysicsLayerRelationEntry& other)
                        {
                            return (other.a == entry.a && other.b == entry.b) || (other.a == entry.b && other.b == entry.a);
                        }
                    );
                    if (existing != settings.relations.end())
                    {
                        LOG_WARN("config.json: layer relation between \"{}\" and \"{}\" is listed twice, the later one wins", a, b);
                        existing->relation = entry.relation;
                        continue;
                    }

                    settings.relations.push_back(entry);
                }
            }

            return settings;
        }
    }

    Config::Config() :
        mExeDir(Platform::getExecutableDir()),
        mJson(std::make_unique<nlohmann::json>())
    {
        std::ifstream file(mExeDir / "config.json");
        if (file)
        {
            *mJson = nlohmann::json::parse(file, nullptr, /*allow_exceptions*/ false);
            if (mJson->is_discarded())
                *mJson = nlohmann::json{};
        }

        const nlohmann::json& json = *mJson;

        mEngineAssetDir = resolveDir(json, "EngineAssetDir", mExeDir, "Asset");
        mProjectAssetDir = resolveDir(json, "ProjectAssetDir", mExeDir, "Project/Asset");
        mResourceDir = resolveDir(json, "ResourceDir", mExeDir, "Resource");
        mShaderDir = resolveDir(json, "ShaderDir", mExeDir, "Shader");

        mWindowWidth = resolveValue<uint32_t>(json, "WindowWidth", mWindowWidth);
        mWindowHeight = resolveValue<uint32_t>(json, "WindowHeight", mWindowHeight);
        mWindowTitle = resolveString(json, "WindowTitle", mWindowTitle);

        mRHIBackend = resolveRHIBackend(json, "RHIBackend", mRHIBackend);

        mGlobalSamplerSettings.defaultFilter = resolveDefaultFilter(json, "DefaultTextureFilter", mGlobalSamplerSettings.defaultFilter);
        mGlobalSamplerSettings.maxAnisotropy = resolveFloat(json, "MaxAnisotropy", mGlobalSamplerSettings.maxAnisotropy);
        mGlobalSamplerSettings.mipLodBias = resolveFloat(json, "MipLodBias", mGlobalSamplerSettings.mipLodBias);

        mPhysicsLayerSettings = resolvePhysicsLayerSettings(json, "PhysicsLayers");

        mDefaultScenePath = resolveString(json, "DefaultScene", mDefaultScenePath);
    }

    Config::~Config() = default;

    void Config::setDefaultScenePath(const std::string& assetPath)
    {
        mDefaultScenePath = assetPath;
        (*mJson)["DefaultScene"] = assetPath;

        std::ofstream file(mExeDir / "config.json");
        if (file)
            file << mJson->dump(4);
    }
}
