#include "VisualizationRenderData.h"

#include <RenderBase/GeometryProxy.h>
#include <RenderBase/ProxyManager.h>
#include <RenderBase/DefaultResources.h>
#include <RenderBase/StagingAllocator.h>

#include <Core/Math/Math.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>

namespace hyp
{
    namespace
    {
        constexpr size_t alignUp(size_t value, size_t alignment) noexcept
        {
            return (value + alignment - 1) / alignment * alignment;
        }

        // AlwaysOnTop(ignoreDepth)条目在 shader 里全部被钉在最近的深度值(reversed-Z 下的 1.0),
        // 彼此打平——深度比较是 Greater_Equal, 打平时后画的赢, 所以这里把它们按到相机的距离降序
        // 排(远的在前、近的在后), 保证同类型内最近的最后画、正确盖住较远的。DepthTested 条目走
        // 真实深度值 test+write, 跟绘制顺序无关, 不需要排序, 留在原有相对顺序里即可
        template <typename T, typename GetPosition>
        std::vector<const T*> orderByDepthGroup(const std::vector<T>& source, const double3& cameraWorldPos, GetPosition&& getPosition)
        {
            std::vector<const T*> ordered;
            ordered.reserve(source.size());
            for (const T& item : source)
                ordered.push_back(&item);

            auto alwaysOnTopBegin = std::stable_partition(ordered.begin(), ordered.end(),
                [](const T* item) { return !item->ignoreDepth; });
            std::sort(alwaysOnTopBegin, ordered.end(), [&](const T* a, const T* b)
            {
                double distSqrA = Math::lengthSqr(double3(getPosition(*a)) - cameraWorldPos);
                double distSqrB = Math::lengthSqr(double3(getPosition(*b)) - cameraWorldPos);
                return distSqrA > distSqrB;
            });
            return ordered;
        }

        template <typename Instance, typename Source, typename Convert>
        VisualizationTypeRange appendSection(std::vector<uint8_t>& packed, const std::vector<const Source*>& orderedSource, Convert&& convert)
        {
            if (orderedSource.empty())
                return {};

            size_t alignedOffset = alignUp(packed.size(), sizeof(Instance));
            packed.resize(alignedOffset + orderedSource.size() * sizeof(Instance));
            Instance* dst = reinterpret_cast<Instance*>(packed.data() + alignedOffset);
            for (size_t i = 0; i < orderedSource.size(); ++i)
            {
                dst[i] = convert(*orderedSource[i]);
            }
            return VisualizationTypeRange{ static_cast<uint32_t>(alignedOffset / sizeof(Instance)), static_cast<uint32_t>(orderedSource.size()) };
        }

        // 取整避免字节数逐帧小幅波动时命中 TransientResources 里不同的 buffer key
        constexpr uint64_t kVisualizationBufferSizeGranularity = 16 * 1024;

        struct VisualizationUploadData
        {
            rg::BufferHandle buffer;
            std::shared_ptr<std::vector<uint8_t>> data;
        };

        // 这里手写的 binding=0 对应每种形状 shader 里唯一声明的 storage buffer。iconAtlasView/
        // iconAtlasSampler 只给 icon 用(它的 shader 在 binding=1/2 额外声明了图标 atlas 贴图/
        // 采样器, 是这 9 种形状里唯一有纹理输入的), 其余形状传 nullptr、只写 binding=0 这一项——
        // 纹理/采样器创建后不会再变, 不需要额外进 BindGroupCache 的 needUpdate 追踪, instanceBuffer
        // 每帧都变, 已经足够触发重建
        void bindVisualizationShapeResources(
            RenderBackend& renderBackend,
            BindGroupCache<1>& bindGroupCache,
            const VisualizationShapePipelines::Entry& entry,
            rhi::Buffer* instanceBuffer,
            const rhi::TextureView* iconAtlasView = nullptr,
            rhi::Sampler* iconAtlasSampler = nullptr
        )
        {
            if (!bindGroupCache.needUpdate(instanceBuffer))
                return;

            bindGroupCache.update(
                renderBackend,
                *entry.layout,
                [&](rhi::BindGroup& bindGroup)
                {
                    rhi::BufferBindingInfo bufferInfo{ instanceBuffer, 0, rhi::kWholeBuffer };
                    rhi::BindGroupEntry bufferEntry;
                    bufferEntry.binding = 0;
                    bufferEntry.type = rhi::BindingType::Storage_Buffer;
                    bufferEntry.index = 0;
                    bufferEntry.buffers = { &bufferInfo, 1 };

                    if (!iconAtlasView)
                    {
                        bindGroup.update({ &bufferEntry, 1 });
                        return;
                    }

                    rhi::BindGroupEntry textureEntry;
                    textureEntry.binding = 1;
                    textureEntry.type = rhi::BindingType::Sampled_Texture;
                    textureEntry.index = 0;
                    textureEntry.textures = { &iconAtlasView, 1 };

                    rhi::BindGroupEntry samplerEntry;
                    samplerEntry.binding = 2;
                    samplerEntry.type = rhi::BindingType::Sampler;
                    samplerEntry.index = 0;
                    samplerEntry.samplers = { &iconAtlasSampler, 1 };

                    std::array<rhi::BindGroupEntry, 3> entries{ bufferEntry, textureEntry, samplerEntry };
                    bindGroup.update(entries);
                }
            );
        }
    }

    VisualizationRenderData VisualizationRenderData::build(rg::RenderGraph& renderGraph, RendererContext& rendererContext, const VisualizationItems& items, const double3& cameraWorldPos)
    {
        VisualizationRenderData result;
        if (items.empty())
            return result;

        auto& renderBackend = rendererContext.getRenderBackend();
        auto& defaultResources = renderBackend.getDefaultResources();
        auto& proxyManager = renderBackend.getProxyManager();

        std::vector<uint8_t> packed;

        auto orderedIcons = orderByDepthGroup(items.icons, cameraWorldPos, [](const VisualizationIcon& icon) { return icon.position; });
        result.mIcons = appendSection<VisualizationIconInstance>(packed, orderedIcons, [&](const VisualizationIcon& icon)
        {
            VisualizationIconInstance instance{};
            instance.position.fromDouble3(icon.position);
            instance.size = icon.size;
            instance.atlasUV = defaultResources.getVisualizationIconAtlasUV(static_cast<uint32_t>(icon.type));
            instance.color = icon.color;
            instance.pickId = icon.pickId;
            instance.ignoreDepth = icon.ignoreDepth ? 1u : 0u;
            return instance;
        });

        auto orderedPoints = orderByDepthGroup(items.points, cameraWorldPos, [](const VisualizationPoint& point) { return point.position; });
        result.mPoints = appendSection<VisualizationPointInstance>(packed, orderedPoints, [](const VisualizationPoint& point)
        {
            VisualizationPointInstance instance{};
            instance.position.fromDouble3(point.position);
            instance.size = point.size;
            instance.color = point.color;
            instance.pickId = point.pickId;
            instance.ignoreDepth = point.ignoreDepth ? 1u : 0u;
            return instance;
        });

        auto orderedLines = orderByDepthGroup(items.lines, cameraWorldPos, [](const VisualizationLine& line) { return (line.start + line.end) * 0.5; });
        result.mLines = appendSection<VisualizationLineInstance>(packed, orderedLines, [](const VisualizationLine& line)
        {
            VisualizationLineInstance instance{};
            instance.start.fromDouble3(line.start);
            instance.end.fromDouble3(line.end);
            instance.color = line.color;
            instance.pickId = line.pickId;
            instance.ignoreDepth = line.ignoreDepth ? 1u : 0u;
            return instance;
        });

        auto orderedBoxes = orderByDepthGroup(items.wireframeBoxes, cameraWorldPos, [](const VisualizationWireframeBox& box) { return box.transform[3]; });
        result.mWireframeBoxes = appendSection<VisualizationWireframeBoxInstance>(packed, orderedBoxes, [](const VisualizationWireframeBox& box)
        {
            VisualizationWireframeBoxInstance instance{};
            instance.transform.fromDouble4x4(box.transform);
            instance.halfExtents = box.halfExtents;
            instance.color = box.color;
            instance.pickId = box.pickId;
            instance.ignoreDepth = box.ignoreDepth ? 1u : 0u;
            return instance;
        });

        auto orderedSpheres = orderByDepthGroup(items.wireframeSpheres, cameraWorldPos, [](const VisualizationWireframeSphere& sphere) { return sphere.transform[3]; });
        result.mWireframeSpheres = appendSection<VisualizationWireframeSphereInstance>(packed, orderedSpheres, [](const VisualizationWireframeSphere& sphere)
        {
            VisualizationWireframeSphereInstance instance{};
            instance.transform.fromDouble4x4(sphere.transform);
            instance.radius = sphere.radius;
            instance.color = sphere.color;
            instance.pickId = sphere.pickId;
            instance.ignoreDepth = sphere.ignoreDepth ? 1u : 0u;
            return instance;
        });

        auto orderedCapsules = orderByDepthGroup(items.wireframeCapsules, cameraWorldPos, [](const VisualizationWireframeCapsule& capsule) { return capsule.transform[3]; });
        result.mWireframeCapsules = appendSection<VisualizationWireframeCapsuleInstance>(packed, orderedCapsules, [](const VisualizationWireframeCapsule& capsule)
        {
            VisualizationWireframeCapsuleInstance instance{};
            instance.transform.fromDouble4x4(capsule.transform);
            instance.radius = capsule.radius;
            instance.halfHeight = capsule.halfHeight;
            instance.color = capsule.color;
            instance.pickId = capsule.pickId;
            instance.ignoreDepth = capsule.ignoreDepth ? 1u : 0u;
            return instance;
        });

        auto orderedCylinders = orderByDepthGroup(items.wireframeCylinders, cameraWorldPos, [](const VisualizationWireframeCylinder& cylinder) { return cylinder.transform[3]; });
        result.mWireframeCylinders = appendSection<VisualizationWireframeCylinderInstance>(packed, orderedCylinders, [](const VisualizationWireframeCylinder& cylinder)
        {
            VisualizationWireframeCylinderInstance instance{};
            instance.transform.fromDouble4x4(cylinder.transform);
            instance.radius = cylinder.radius;
            instance.halfHeight = cylinder.halfHeight;
            instance.color = cylinder.color;
            instance.pickId = cylinder.pickId;
            instance.ignoreDepth = cylinder.ignoreDepth ? 1u : 0u;
            return instance;
        });

        auto orderedLightCones = orderByDepthGroup(items.wireframeLightCones, cameraWorldPos, [](const VisualizationWireframeLightCone& cone) { return cone.transform[3]; });
        result.mWireframeLightCones = appendSection<VisualizationWireframeLightConeInstance>(packed, orderedLightCones, [](const VisualizationWireframeLightCone& cone)
        {
            VisualizationWireframeLightConeInstance instance{};
            instance.transform.fromDouble4x4(cone.transform);
            instance.range = cone.range;
            instance.halfAngle = cone.halfAngle;
            instance.color = cone.color;
            instance.pickId = cone.pickId;
            instance.ignoreDepth = cone.ignoreDepth ? 1u : 0u;
            return instance;
        });

        if (!items.wireframeGeometries.empty())
        {
            struct GeometryGroupBuilder
            {
                GeometryProxy* geometry = nullptr;
                std::vector<const VisualizationWireframeGeometry*> items;
                std::vector<VisualizationWireframeGeometryInstance> instances;
            };

            std::vector<GeometryGroupBuilder> builders;
            std::unordered_map<GeometryProxy*, size_t> builderIndexByProxy;
            for (const VisualizationWireframeGeometry& item : items.wireframeGeometries)
            {
                GeometryProxy* geometry = proxyManager.getProxy(item.geometry);

                auto [it, inserted] = builderIndexByProxy.try_emplace(geometry, builders.size());
                if (inserted)
                {
                    builders.push_back(GeometryGroupBuilder{ geometry, {}, {} });
                }

                builders[it->second].items.push_back(&item);
            }

            // 同一个 geometry group 内部也要按 ignoreDepth 分组排序, 道理跟 orderByDepthGroup()
            // 一样——group 之间(不同 GeometryProxy)各自是独立的一次 draw 调用, 相互顺序不影响
            // 正确性, 只有同一组内部的 AlwaysOnTop 条目需要按距离降序排
            for (GeometryGroupBuilder& group : builders)
            {
                auto alwaysOnTopBegin = std::stable_partition(group.items.begin(), group.items.end(),
                    [](const VisualizationWireframeGeometry* item) { return !item->ignoreDepth; });
                std::sort(alwaysOnTopBegin, group.items.end(), [&](const VisualizationWireframeGeometry* a, const VisualizationWireframeGeometry* b)
                {
                    double distSqrA = Math::lengthSqr(double3(a->transform[3]) - cameraWorldPos);
                    double distSqrB = Math::lengthSqr(double3(b->transform[3]) - cameraWorldPos);
                    return distSqrA > distSqrB;
                });

                group.instances.reserve(group.items.size());
                for (const VisualizationWireframeGeometry* item : group.items)
                {
                    VisualizationWireframeGeometryInstance instance{};
                    instance.transform.fromDouble4x4(item->transform);
                    instance.color = item->color;
                    instance.pickId = item->pickId;
                    instance.ignoreDepth = item->ignoreDepth ? 1u : 0u;
                    group.instances.push_back(instance);
                }
            }

            size_t totalInstanceCount = 0;
            for (const GeometryGroupBuilder& builder : builders)
            {
                totalInstanceCount += builder.instances.size();
            }

            using Instance = VisualizationWireframeGeometryInstance;
            size_t sectionStart = alignUp(packed.size(), sizeof(Instance));
            packed.resize(sectionStart + totalInstanceCount * sizeof(Instance));
            uint32_t elementBase = static_cast<uint32_t>(sectionStart / sizeof(Instance));

            uint32_t cursor = 0;
            result.mGeometryGroups.reserve(builders.size());
            for (const GeometryGroupBuilder& builder : builders)
            {
                std::memcpy(packed.data() + sectionStart + cursor * sizeof(Instance), builder.instances.data(), builder.instances.size() * sizeof(Instance));

                VisualizationGeometryGroup group;
                group.geometry = builder.geometry;
                group.range = VisualizationTypeRange{ elementBase + cursor, static_cast<uint32_t>(builder.instances.size()) };
                result.mGeometryGroups.push_back(group);

                cursor += static_cast<uint32_t>(builder.instances.size());
            }
        }

        uint64_t roundedSize = alignUp(packed.size(), kVisualizationBufferSizeGranularity);

        rhi::BufferDesc desc;
        desc.size = roundedSize;
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;

        auto packedData = std::make_shared<std::vector<uint8_t>>(std::move(packed));

        auto& pass = renderGraph.addTransferPass<VisualizationUploadData>(
            "VisualizationUpload"_hs,
            [&](rg::RenderGraph::Builder& builder, VisualizationUploadData& data)
            {
                rg::BufferHandle buffer = builder.createBuffer("VisualizationInstanceBuffer"_hs, desc);
                data.buffer = builder.accessBuffer(buffer, rhi::BufferUsage::Transfer_Dst, rg::AccessType::Write_Only);
                data.data = packedData;
            },
            [](rg::ExecuteContext& context, VisualizationUploadData& data)
            {
                auto& stagingAllocator = context.getRenderBackend().getStagingAllocator();
                auto allocation = stagingAllocator.forceAllocate(data.data->size());
                std::memcpy(allocation.mappedData, data.data->data(), data.data->size());

                rhi::BufferCopyRegion region;
                region.srcOffset = allocation.offset;
                region.dstOffset = 0;
                region.size = data.data->size();
                context.getCommandList().copyBuffer(allocation.buffer, context.getBuffer(data.buffer), { &region, 1 });
            }
        );

        result.mBuffer = pass.getData().buffer;
        result.mBufferByteSize = roundedSize;

        return result;
    }

    bool VisualizationRenderData::empty() const noexcept
    {
        return !mBuffer.isValid();
    }

    void VisualizationRenderData::draw(
        rg::ExecuteContext& executeContext,
        RendererContext& rendererContext,
        VisualizationShapeBindGroupCaches& bindGroupCaches,
        const VisualizationShapePipelines& pipelines
    ) const
    {
        if (!mBuffer.isValid())
            return;

        auto& renderBackend = rendererContext.getRenderBackend();
        auto& defaultResources = renderBackend.getDefaultResources();
        auto& cmdList = executeContext.getCommandList();
        rhi::Buffer* instanceBuffer = executeContext.getBuffer(mBuffer);

        auto drawSimple = [&](
            const VisualizationTypeRange& range,
            BindGroupCache<1>& bindGroupCache,
            const VisualizationShapePipelines::Entry& entry,
            auto&& issueDraw,
            const rhi::TextureView* iconAtlasView = nullptr,
            rhi::Sampler* iconAtlasSampler = nullptr)
        {
            if (range.count == 0)
                return;

            bindVisualizationShapeResources(renderBackend, bindGroupCache, entry, instanceBuffer, iconAtlasView, iconAtlasSampler);
            cmdList.bindPipeline(entry.pipeline);
            cmdList.setBindGroup(2, rhi::PipelineBindPoint::Graphics, &entry.pipeline->getLayout(), bindGroupCache.getBindGroup(), {});
            cmdList.setImmediateData(&entry.pipeline->getLayout(), 0, std::span(reinterpret_cast<const uint8_t*>(&range.offset), sizeof(range.offset)));
            issueDraw(range.count);
        };

        // 顶点位置由各自 shader 按 SV_VertexID 现算, 不需要绑定顶点/索引 buffer
        drawSimple(mIcons, bindGroupCaches.icon, pipelines.icon, [&](uint32_t count) { cmdList.draw(4, count, 0, 0); },
            &defaultResources.getVisualizationIconAtlasView(), renderBackend.getSamplerCache().getLinearClampNoMip());
        drawSimple(mPoints, bindGroupCaches.point, pipelines.point, [&](uint32_t count) { cmdList.draw(4, count, 0, 0); });
        drawSimple(mLines, bindGroupCaches.line, pipelines.line, [&](uint32_t count) { cmdList.draw(2, count, 0, 0); });

        auto drawUnitWireframe = [&](
            const VisualizationTypeRange& range,
            BindGroupCache<1>& bindGroupCache,
            const VisualizationShapePipelines::Entry& entry,
            rhi::Buffer& unitBuffer,
            uint64_t unitIndexOffset,
            uint32_t unitIndexCount)
        {
            drawSimple(range, bindGroupCache, entry, [&](uint32_t count)
            {
                std::array<rhi::Buffer*, 1> vertexBuffers{ &unitBuffer };
                std::array<uint64_t, 1> vertexOffsets{ 0 };
                cmdList.bindVertexBuffers(0, vertexBuffers, vertexOffsets);
                cmdList.bindIndexBuffer(&unitBuffer, unitIndexOffset, rhi::IndexType::Uint16);
                cmdList.drawIndexed(unitIndexCount, count, 0, 0, 0);
            });
        };

        drawUnitWireframe(mWireframeBoxes, bindGroupCaches.wireframeBox, pipelines.wireframeBox,
            defaultResources.getVisualizationWireframeBoxBuffer(), defaultResources.getVisualizationWireframeBoxIndexOffset(), defaultResources.getVisualizationWireframeBoxIndexCount());

        drawUnitWireframe(mWireframeSpheres, bindGroupCaches.wireframeSphere, pipelines.wireframeSphere,
            defaultResources.getVisualizationWireframeSphereBuffer(), defaultResources.getVisualizationWireframeSphereIndexOffset(), defaultResources.getVisualizationWireframeSphereIndexCount());

        drawUnitWireframe(mWireframeCapsules, bindGroupCaches.wireframeCapsule, pipelines.wireframeCapsule,
            defaultResources.getVisualizationWireframeCapsuleBuffer(), defaultResources.getVisualizationWireframeCapsuleIndexOffset(), defaultResources.getVisualizationWireframeCapsuleIndexCount());

        drawUnitWireframe(mWireframeCylinders, bindGroupCaches.wireframeCylinder, pipelines.wireframeCylinder,
            defaultResources.getVisualizationWireframeCylinderBuffer(), defaultResources.getVisualizationWireframeCylinderIndexOffset(), defaultResources.getVisualizationWireframeCylinderIndexCount());

        drawUnitWireframe(mWireframeLightCones, bindGroupCaches.wireframeLightCone, pipelines.wireframeLightCone,
            defaultResources.getVisualizationWireframeLightConeBuffer(), defaultResources.getVisualizationWireframeLightConeIndexOffset(), defaultResources.getVisualizationWireframeLightConeIndexCount());

        if (!mGeometryGroups.empty())
        {
            // 三角形 index buffer 不能直接当 Line_List 数据画(3 个一组、2 个一组对不上, 会画出
            // 跨三角形的假边), 也没有另外建一份去重后的边索引 buffer——改成 vertex-pulling: 把
            // GeometryProxy 自己那块顶点+索引数据整个当只读 uint 数组多绑一份(binding=1), shader
            // 里按 SV_VertexID 现算要读哪个三角形的哪条边、边的哪个端点, 不经过固定管线的顶点/
            // 索引抓取阶段。三角形内部共享边会被相邻三角形各自画一次(轻微重叠, 不算错, 只有真正
            // 只属于一个三角形的边界/轮廓边才画一次), 具体推导见 Visualization/WireframeGeometry.slang。
            // 每个 group 引用不同的 GeometryProxy、不同的 geometry buffer, 不能像其余 8 种形状
            // 那样全场景共用一份 bind group 缓存
            const VisualizationShapePipelines::Entry& entry = pipelines.wireframeGeometry;
            if (bindGroupCaches.wireframeGeometry.size() < mGeometryGroups.size())
            {
                bindGroupCaches.wireframeGeometry.resize(mGeometryGroups.size());
            }

            cmdList.bindPipeline(entry.pipeline);

            for (size_t i = 0; i < mGeometryGroups.size(); ++i)
            {
                const VisualizationGeometryGroup& group = mGeometryGroups[i];
                rhi::Buffer& geometryBuffer = group.geometry->getBuffer();
                BindGroupCache<2>& bindGroupCache = bindGroupCaches.wireframeGeometry[i];

                if (bindGroupCache.needUpdate(instanceBuffer, &geometryBuffer))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *entry.layout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            rhi::BufferBindingInfo instanceBufferInfo{ instanceBuffer, 0, rhi::kWholeBuffer };
                            rhi::BindGroupEntry instanceEntry;
                            instanceEntry.binding = 0;
                            instanceEntry.type = rhi::BindingType::Storage_Buffer;
                            instanceEntry.index = 0;
                            instanceEntry.buffers = { &instanceBufferInfo, 1 };

                            rhi::BufferBindingInfo geometryBufferInfo{ &geometryBuffer, 0, rhi::kWholeBuffer };
                            rhi::BindGroupEntry geometryEntry;
                            geometryEntry.binding = 1;
                            geometryEntry.type = rhi::BindingType::Storage_Buffer;
                            geometryEntry.index = 0;
                            geometryEntry.buffers = { &geometryBufferInfo, 1 };

                            std::array<rhi::BindGroupEntry, 2> entries{ instanceEntry, geometryEntry };
                            bindGroup.update(entries);
                        }
                    );
                }

                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Graphics, &entry.pipeline->getLayout(), bindGroupCache.getBindGroup(), {});

                const GeometryProxy::Section& section = group.geometry->getSection(0);
                // Uint16/Uint32 都要支持(碰撞体常用的低模网格大概率是 Uint16, MeshCollider
                // 直接复用渲染那份网格数据, 不能假设一定是 Uint32)——indexBaseUint 只到 section
                // 数据起点为止, 不像之前那版直接加 firstIndex, 是因为 Uint16 时 firstIndex 这个
                // "逻辑索引元素下标"要在 shader 里自己换算成"第几个 uint word、word 里的高/低
                // 16 位", 不能直接当 uint 下标加
                struct WireframeGeometryPushConstants
                {
                    uint32_t instanceOffset;
                    uint32_t positionUintOffset;
                    uint32_t indexBaseUint;
                    uint32_t firstIndex; // 逻辑索引元素下标, 不是 uint/字节偏移
                    int32_t vertexOffset;
                    uint32_t indexIs16Bit;
                };
                WireframeGeometryPushConstants pushConstants{
                    group.range.offset,
                    static_cast<uint32_t>(group.geometry->getPositionByteOffset() / 4),
                    static_cast<uint32_t>(group.geometry->getIndexByteOffset() / 4),
                    section.firstIndex,
                    section.vertexOffset,
                    group.geometry->getIndexType() == rhi::IndexType::Uint16 ? 1u : 0u
                };
                cmdList.setImmediateData(&entry.pipeline->getLayout(), 0, std::span(reinterpret_cast<const uint8_t*>(&pushConstants), sizeof(pushConstants)));

                uint32_t triangleCount = section.indexCount / 3;
                cmdList.draw(triangleCount * 6, group.range.count, 0, 0);
            }
        }
    }
}
