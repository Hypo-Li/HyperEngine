#pragma once
#include <HypImGui/ImGuiTextureHandle.h>
#include <RenderBase/ProxyHandle.h>

#include <Core/Common/DerivedDataCache.h>
#include <Core/Common/Guid.h>

#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

namespace hyp
{
    class Asset;
}

namespace hyp::ed
{
    // 引擎里的"给我一张能代表这个东西的图"统一入口——两类图共用同一个类, 因为两者的 GPU
    // 生命周期管理(跟 Context/RenderBackend 绑定, 见下)完全一样, 分成两个类只会让这套
    // 生命周期代码重复一份:
    //   - Icon: 跟资产具体内容无关的通用占位符(文件夹、"拿不到缩略图时"的资产默认图),
    //     同类图全局共用一张, 构造时一次性加载。
    //   - Thumbnail: 按资产自己的实际内容生成的、每个资产各不相同的预览图(支持 Texture2D
    //     及 TextureCube, 后者还原成等距柱状投影), 懒生成 + 磁盘缓存 + 每帧限额, 见
    //     getThumbnail()。GPU 侧常驻表有数量上限, 超过时淘汰最久没被用到的, 见
    //     evictLeastRecentlyUsedThumbnails()。
    class ThumbnailProvider
    {
    public:
        ThumbnailProvider();

        // 必须在 Context/RenderBackend(VMA 分配器)销毁之前、由持有者(Editor)显式调用——
        // 见类注释。之前这两张图标是 AssetBrowser 的普通成员, 随 mWindowManager.clear() 在
        // Engine::shutdown() 之前被销毁, 时机是对的, 这里延续同样的做法。
        void shutdown();

        // 这里管理的所有纹理(两张通用图标 + 已生成的按资产缩略图)只给 ImGui::Image() 用,
        // 从不绑到任何 Material 上, 没有别的地方会替它们调 TextureProxy::prepare() 把
        // stageUpload() 排的数据传上 GPU——调用方(目前是 Editor.cpp 里的 EditorPresent pass)
        // 需要在自己的 command list 上手动补一次。
        void collectPrepareTextures(std::vector<TextureProxyHandle>& out);

        ImTextureID getFolderIcon() const noexcept
        {
            return mFolderIconHandle.get();
        }

        ImTextureID getDefaultThumbnail() const noexcept
        {
            return mAssetIconHandle.get();
        }

        // AssetBrowser 每帧开始时调一次, 重置"这一帧还能现生成几个缩略图"的配额——避免一次性
        // 打开一个几百张贴图都没生成过缩略图的文件夹时单帧卡顿, 见 getThumbnail() 的注释。
        void resetThumbnailBudget() noexcept;

        // 非 Texture2D(及子类)资产、像素格式暂不支持(见 .cpp)、或本帧生成配额已经用完时返回 0。
        // 返回非 0 时固定是 kMaxThumbnailEdge 见方像素, 非方形内容已经居中 pad 过。
        ImTextureID getThumbnail(Asset* asset);

    private:
        void loadIconTexture(const std::filesystem::path& pngPath, ImGuiTextureHandle& outHandle, TextureProxyHandle& outProxyHandle);

        // 磁盘缓存 key = guid + physicalPath 的最后修改时间, blob = PNG 字节——资产被 save()
        // 改写后 mtime 自然变化, 旧 key 查不到, 不需要额外的失效/版本号机制。
        uint128_t computeThumbnailKey(Guid guid, const std::filesystem::path& physicalPath) const;
        std::optional<std::vector<uint8_t>> tryLoadThumbnailPixels(Guid guid, const std::filesystem::path& physicalPath) const;
        void saveThumbnailPixels(Guid guid, const std::filesystem::path& physicalPath, std::span<const uint8_t> rgba8) const;

        // 只支持 8-bit UNORM 格式(R8/RG8/RGB8/RGBA8), 其余格式返回 false。
        // 内部对 Asset/BulkData 做临时加载, 调用前不是常驻状态的话用完照原样还原。
        // outRgba8 固定是 kMaxThumbnailEdge 见方, 由 padToSquareRGBA8() 居中填充得到。
        bool generateTextureThumbnail(Asset& asset, std::vector<uint8_t>& outRgba8) const;

        // 六个面先还原成等距柱状投影(equirectangular), 再由 padToSquareRGBA8() 补齐成正方形。
        // 像素格式限制、临时加载规则跟 generateTextureThumbnail() 一致。
        bool generateCubeThumbnail(Asset& asset, std::vector<uint8_t>& outRgba8) const;

        void uploadThumbnail(Guid guid, std::span<const uint8_t> rgba8);

        // uploadThumbnail() 直接建的 TextureProxy, 生命周期不归任何 Texture2D 管——按
        // Texture::~Texture() 同样的顺序手动销毁: 先把销毁命令排进渲染线程, 再当场把 handle
        // 还给 ProxyHandleAllocator(此时对应的 GPU 资源实际销毁还没跑, 但 handle 索引可以
        // 立即复用, 跟 Texture 那边的既有约定一致)。
        void destroyThumbnailProxy(TextureProxyHandle handle) noexcept;

        // mGpuThumbnails 超过上限时按 lastUsedFrame 淘汰最旧的一批, 给 SceneView 视口/
        // TextureEditor 预览这些别的 ImGui 纹理注册留出配额, 不会把 ImGui 后端的描述符池
        // 占满(见 ImGuiBackend.cpp 里 DescriptorPoolSize 的注释)。
        void evictLeastRecentlyUsedThumbnails();

    private:
        ImGuiTextureHandle mFolderIconHandle;
        TextureProxyHandle mFolderIconProxyHandle;
        ImGuiTextureHandle mAssetIconHandle;
        TextureProxyHandle mAssetIconProxyHandle;

        DerivedDataCache mThumbnailDiskCache;

        struct GpuThumbnail
        {
            ImGuiTextureHandle handle;
            TextureProxyHandle proxyHandle;
            uint64_t lastUsedFrame = 0;
        };
        std::unordered_map<Guid, GpuThumbnail> mGpuThumbnails;
        int mThumbnailBudgetThisFrame = 0;
        uint64_t mCurrentFrame = 0;

        // 记录"这个 guid 在这个 mtime 下生成失败过"(比如格式暂不支持)——没有这张表的话,
        // 每一帧只要预算够就会重新尝试一次完整生成(现查 Asset、解压 BulkData), 对一个
        // 永远不会成功的资产来说等于每帧都在做无用功。value 是失败当时的
        // computeThumbnailKey(), 资产改动导致 mtime 变化时自然不再匹配, 会重新尝试一次。
        std::unordered_map<Guid, uint128_t> mFailedGenerationKeys;
    };
}
