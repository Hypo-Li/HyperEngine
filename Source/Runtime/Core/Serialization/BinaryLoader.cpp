#include "BinaryLoader.h"
#include <cassert>
#include <cstring>

namespace hyp
{
    BinaryLoader::BinaryLoader(bool isCooking) :
        BinaryArchive(true, isCooking)
    {
    }

    void BinaryLoader::serialize(bool* value, size_t count)
    {
        readData(value, count * sizeof(bool));
    }

    void BinaryLoader::serialize(char* value, size_t count)
    {
        readData(value, count * sizeof(char));
    }

    void BinaryLoader::serialize(wchar_t* value, size_t count)
    {
        readData(value, count * sizeof(wchar_t));
    }

    void BinaryLoader::serialize(int8_t* value, size_t count)
    {
        readData(value, count * sizeof(int8_t));
    }

    void BinaryLoader::serialize(int16_t* value, size_t count)
    {
        readData(value, count * sizeof(int16_t));
    }

    void BinaryLoader::serialize(int32_t* value, size_t count)
    {
        readData(value, count * sizeof(int32_t));
    }

    void BinaryLoader::serialize(int64_t* value, size_t count)
    {
        readData(value, count * sizeof(int64_t));
    }

    void BinaryLoader::serialize(uint8_t* value, size_t count)
    {
        readData(value, count * sizeof(uint8_t));
    }

    void BinaryLoader::serialize(uint16_t* value, size_t count)
    {
        readData(value, count * sizeof(uint16_t));
    }

    void BinaryLoader::serialize(uint32_t* value, size_t count)
    {
        readData(value, count * sizeof(uint32_t));
    }

    void BinaryLoader::serialize(uint64_t* value, size_t count)
    {
        readData(value, count * sizeof(uint64_t));
    }

    void BinaryLoader::serialize(float* value, size_t count)
    {
        readData(value, count * sizeof(float));
    }

    void BinaryLoader::serialize(double* value, size_t count)
    {
        readData(value, count * sizeof(double));
    }

    void BinaryLoader::serialize(std::string* value, size_t count)
    {
        std::vector<uint32_t> stringIds(count);
        serialize(stringIds.data(), count);
        for (size_t i = 0; i < count; ++i)
        {
            value[i] = mStringTable[stringIds[i]];
        }
    }

    void BinaryLoader::serialize(HString* value, size_t count)
    {
        std::vector<uint32_t> hstringIds(count);
        serialize(hstringIds.data(), count);
        for (size_t i = 0; i < count; ++i)
        {
            value[i] = mHStringTable[hstringIds[i]];
        }
    }

    void BinaryLoader::serializeObjectRef(int32_t& ref)
    {
        if (mObjectBufferIdxStack.empty())
        {
            ref = -1;
        }
        else
        {
            serialize(&ref);
        }
        if (ref < 0)
        {
            mCurrentObjectRef = ref;
        }
    }

    void BinaryLoader::beginStructure(HString& typeName, uint32_t& version, uint32_t& propCount)
    {
        serialize(&typeName);
        serialize(&version);
        serialize(&propCount);
    }

    void BinaryLoader::endStructure()
    {
    }

    void BinaryLoader::beginObject(HString& typeName, uint32_t& version, uint32_t& propCount)
    {
        uint32_t newObjectBufferIdx = static_cast<uint32_t>(-mCurrentObjectRef - 1);
        pushObjectBufferIdx(newObjectBufferIdx);
        // 和 BinarySaver::beginObject 对称: cook 产物没有写这三个字段, 不读
        if (isCooking())
            return;
        serialize(&typeName);
        serialize(&version);
        serialize(&propCount);
    }

    void BinaryLoader::endObject()
    {
        popObjectBufferIdx();
    }

    void BinaryLoader::beginProperty(HString& name, HString& typeName)
    {
        serialize(&name);
        serialize(&typeName);
        uint32_t propSize;
        serialize(&propSize);
        mPropertyEndOffsetStack.push_back(getCurrentObjectBuffer().offset + propSize);
    }

    void BinaryLoader::endProperty()
    {
        getCurrentObjectBuffer().offset = mPropertyEndOffsetStack.back();
        mPropertyEndOffsetStack.pop_back();
    }

    void BinaryLoader::beginArray(uint32_t& count)
    {
        serialize(&count);
    }

    void BinaryLoader::endArray()
    {
    }

    void BinaryLoader::beginPair()
    {
    }

    void BinaryLoader::endPair()
    {
    }

    void BinaryLoader::readData(void* data, size_t size)
    {
        auto& objectBuffer = getCurrentObjectBuffer();
        assert(objectBuffer.offset + size <= objectBuffer.buffer.size());
        std::memcpy(data, objectBuffer.buffer.data() + objectBuffer.offset, size);
        objectBuffer.offset += size;
    }

    void BinaryLoader::readBytes(std::span<const uint8_t> data, size_t& cursor, void* dst, size_t size)
    {
        assert(cursor + size <= data.size());
        std::memcpy(dst, data.data() + cursor, size);
        cursor += size;
    }

    std::string BinaryLoader::readString(std::span<const uint8_t> data, size_t& cursor)
    {
        uint32_t length = 0;
        readBytes(data, cursor, &length, sizeof(length));
        std::string str(length, '\0');
        if (length > 0)
            readBytes(data, cursor, str.data(), length);
        return str;
    }

    void BinaryLoader::unpack(std::span<const uint8_t> data)
    {
        size_t cursor = 0;

        uint32_t stringCount = 0;
        readBytes(data, cursor, &stringCount, sizeof(stringCount));
        mStringTable.resize(stringCount);
        for (uint32_t i = 0; i < stringCount; ++i)
            mStringTable[i] = readString(data, cursor);

        uint32_t hstringCount = 0;
        readBytes(data, cursor, &hstringCount, sizeof(hstringCount));
        mHStringTable.resize(hstringCount);
        for (uint32_t i = 0; i < hstringCount; ++i)
            mHStringTable[i] = HString(readString(data, cursor));

        uint32_t objectCount = 0;
        readBytes(data, cursor, &objectCount, sizeof(objectCount));
        mObjectBuffers.resize(objectCount);
        for (uint32_t i = 0; i < objectCount; ++i)
        {
            uint64_t byteLength = 0;
            readBytes(data, cursor, &byteLength, sizeof(byteLength));
            ObjectBuffer& objectBuffer = mObjectBuffers[i];
            objectBuffer.offset = 0;
            objectBuffer.buffer.resize(byteLength);
            if (byteLength > 0)
                readBytes(data, cursor, objectBuffer.buffer.data(), byteLength);
        }
    }

}
