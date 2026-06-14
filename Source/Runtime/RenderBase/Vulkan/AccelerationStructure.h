#pragma once
#include "Buffer.h"
#include "CommandBuffer.h"

namespace hyp::vk
{
    class AccelerationStructure : public Refable
    {
    public:
        AccelerationStructure(
            const Device& device,
            std::vector<VkAccelerationStructureGeometryKHR>&& geometries,
            std::vector<uint32_t>&& primitiveCount,
            VkAccelerationStructureTypeKHR type,
            VkBuildAccelerationStructureFlagsKHR buildFlags
        ) : mDevice(device), mGeometries(std::move(geometries)), mPrimitiveCounts(std::move(primitiveCount)), mType(type), mBuildFlags(buildFlags)
        {
            VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
            buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            buildInfo.type = mType;
            buildInfo.flags = mBuildFlags;
            buildInfo.geometryCount = static_cast<uint32_t>(mGeometries.size());
            buildInfo.pGeometries = mGeometries.data();

            mSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

            vkGetAccelerationStructureBuildSizesKHR(
                mDevice.getHandle(),
                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                &buildInfo,
                mPrimitiveCounts.data(),
                &mSizeInfo
            );

            Buffer::CreateInfo bufferInfo{};
            bufferInfo.size = mSizeInfo.accelerationStructureSize;
            bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            mBuffer = new Buffer(mDevice, bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY);

            VkAccelerationStructureCreateInfoKHR asInfo{};
            asInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            asInfo.buffer = mBuffer->getHandle();
            asInfo.size = mSizeInfo.accelerationStructureSize;
            asInfo.type = mType;

            vkCreateAccelerationStructureKHR(
                mDevice.getHandle(),
                &asInfo,
                nullptr,
                &mAccelerationStructure
            );
        }

        virtual ~AccelerationStructure() noexcept
        {
            vkDestroyAccelerationStructureKHR(mDevice.getHandle(), mAccelerationStructure, nullptr);
        }

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        VkAccelerationStructureKHR getHandle() const noexcept
        {
            return mAccelerationStructure;
        }

        VkAccelerationStructureTypeKHR getType() const noexcept
        {
            return mType;
        }

        const Buffer& getBuffer() const noexcept
        {
            return *mBuffer;
        }

        void beginBuild()
        {
            vk::Buffer::CreateInfo scratchInfo = {};
            scratchInfo.size = mSizeInfo.buildScratchSize;
            scratchInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            mScratchBuffer = new vk::Buffer(mDevice, scratchInfo, VMA_MEMORY_USAGE_GPU_ONLY);
        }

        void build(const vk::CommandBuffer& cmd)
        {
            VkAccelerationStructureBuildGeometryInfoKHR buildInfos[1] = {};
            buildInfos[0].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            buildInfos[0].type = mType;
            buildInfos[0].flags = mBuildFlags;
            buildInfos[0].mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            buildInfos[0].dstAccelerationStructure = mAccelerationStructure;
            buildInfos[0].geometryCount = static_cast<uint32_t>(mGeometries.size());
            buildInfos[0].pGeometries = mGeometries.data();
            buildInfos[0].scratchData.deviceAddress = mScratchBuffer->getDeviceAddress();

            std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeInfos(mGeometries.size());
            for (size_t i = 0; i < buildRangeInfos.size(); ++i)
            {
                buildRangeInfos[i].primitiveCount = mPrimitiveCounts[i];
                buildRangeInfos[i].primitiveOffset = 0;
                buildRangeInfos[i].firstVertex = 0;
                buildRangeInfos[i].transformOffset = 0;
            }

            const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfos[] = { buildRangeInfos.data() };

            cmd.buildAccelerationStructures(buildInfos, pBuildRangeInfos);
        }

        void endBuild()
        {
            mScratchBuffer = nullptr;
        }

    private:
        const Device& mDevice;
        std::vector<VkAccelerationStructureGeometryKHR> mGeometries;
        std::vector<uint32_t> mPrimitiveCounts;
        VkAccelerationStructureBuildSizesInfoKHR mSizeInfo;
        VkAccelerationStructureTypeKHR mType;
        VkBuildAccelerationStructureFlagsKHR mBuildFlags;
        VkAccelerationStructureKHR mAccelerationStructure{ VK_NULL_HANDLE };
        TRef<Buffer> mBuffer;
        TRef<Buffer> mScratchBuffer;
    };

    class BLAS : public AccelerationStructure
    {
    public:
        struct TrianglesData
        {
            VkFormat vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            VkDeviceAddress vertexAddress;
            VkDeviceSize vertexStride;
            uint32_t maxVertex;
            VkIndexType indexType = VK_INDEX_TYPE_UINT32;
            uint32_t indexCount;
            VkDeviceAddress indexAddress;
            VkGeometryFlagsKHR flags;
        };
        BLAS(const Device& device, std::span<const TrianglesData> triangleData, VkBuildAccelerationStructureFlagBitsKHR buildFlags) :
            AccelerationStructure(device, makeGeometries(triangleData), makePrimitiveCounts(triangleData), VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, buildFlags)
        {
            
        }

    private:
        static std::vector<VkAccelerationStructureGeometryKHR> makeGeometries(std::span<const TrianglesData> triangleData)
        {
            std::vector<VkAccelerationStructureGeometryKHR> geometries(triangleData.size());
            for (size_t i = 0; i < triangleData.size(); ++i)
            {
                geometries[i].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
                geometries[i].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                auto& triangle = geometries[i].geometry.triangles;
                triangle.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                triangle.vertexFormat = triangleData[i].vertexFormat;
                triangle.vertexData.deviceAddress = triangleData[i].vertexAddress;
                triangle.vertexStride = triangleData[i].vertexStride;
                triangle.maxVertex = triangleData[i].maxVertex;
                triangle.indexType = triangleData[i].indexType;
                triangle.indexData.deviceAddress = triangleData[i].indexAddress;
                triangle.transformData.deviceAddress = 0;
                geometries[i].flags = triangleData[i].flags;
            }
            return geometries;
        }

        static std::vector<uint32_t> makePrimitiveCounts(std::span<const TrianglesData> triangleData)
        {
            std::vector<uint32_t> primitiveCounts(triangleData.size());
            for (size_t i = 0; i < triangleData.size(); ++i)
            {
                primitiveCounts[i] = triangleData[i].indexCount / 3;
            }
            return primitiveCounts;
        }
    };

    class TLAS : public AccelerationStructure
    {
    public:
        struct InstanceData
        {
            VkDeviceAddress instanceAddress;
            uint32_t instanceCount;
            VkGeometryFlagsKHR flags;
        };

        TLAS(const Device& device, std::span<const InstanceData> instanceData, VkBuildAccelerationStructureFlagsKHR buildFlags)
            : AccelerationStructure(device, makeGeometries(instanceData), makePrimitiveCounts(instanceData), VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, buildFlags)
        {

        }

    private:
        static std::vector<VkAccelerationStructureGeometryKHR> makeGeometries(std::span<const InstanceData> instanceData)
        {
            std::vector<VkAccelerationStructureGeometryKHR> geometries(instanceData.size());

            for (size_t i = 0; i < instanceData.size(); ++i)
            {
                geometries[i].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
                geometries[i].geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
                auto& instances = geometries[i].geometry.instances;
                instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
                instances.arrayOfPointers = VK_FALSE;
                instances.data.deviceAddress = instanceData[i].instanceAddress;
                geometries[i].flags = instanceData[i].flags;
            }
            return geometries;
        }

        static std::vector<uint32_t> makePrimitiveCounts(std::span<const InstanceData> instanceData)
        {
            std::vector<uint32_t> primitiveCounts(instanceData.size());
            for (size_t i = 0; i < instanceData.size(); ++i)
            {
                primitiveCounts[i] = instanceData[i].instanceCount;
            }
            return primitiveCounts;
        }
    };

}
