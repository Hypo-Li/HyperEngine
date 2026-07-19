#include "BinarySaver.h"
#include <cassert>

namespace hyp
{
    bool BinarySaver::isLoading() const
    {
        return false;
    }

    void BinarySaver::serialize(bool* value, size_t count)
    {
        writeData(value, count * sizeof(bool));
    }

    void BinarySaver::serialize(char* value, size_t count)
    {
        writeData(value, count * sizeof(char));
    }

    void BinarySaver::serialize(wchar_t* value, size_t count)
    {
        writeData(value, count * sizeof(wchar_t));
    }

    void BinarySaver::serialize(int8_t* value, size_t count)
    {
        writeData(value, count * sizeof(int8_t));
    }

    void BinarySaver::serialize(int16_t* value, size_t count)
    {
        writeData(value, count * sizeof(int16_t));
    }

    void BinarySaver::serialize(int32_t* value, size_t count)
    {
        writeData(value, count * sizeof(int32_t));
    }

    void BinarySaver::serialize(int64_t* value, size_t count)
    {
        writeData(value, count * sizeof(int64_t));
    }

    void BinarySaver::serialize(uint8_t* value, size_t count)
    {
        writeData(value, count * sizeof(uint8_t));
    }

    void BinarySaver::serialize(uint16_t* value, size_t count)
    {
        writeData(value, count * sizeof(uint16_t));
    }

    void BinarySaver::serialize(uint32_t* value, size_t count)
    {
        writeData(value, count * sizeof(uint32_t));
    }

    void BinarySaver::serialize(uint64_t* value, size_t count)
    {
        writeData(value, count * sizeof(uint64_t));
    }

    void BinarySaver::serialize(float* value, size_t count)
    {
        writeData(value, count * sizeof(float));
    }

    void BinarySaver::serialize(double* value, size_t count)
    {
        writeData(value, count * sizeof(double));
    }

    void BinarySaver::serialize(std::string* value, size_t count)
    {
        std::vector<uint32_t> stringIds;
        stringIds.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            auto it = mStringIdMap.find(value[i]);
            uint32_t stringId;
            if (it != mStringIdMap.end())
            {
                stringId = it->second;
            }
            else
            {
                stringId = static_cast<uint32_t>(mStringIdMap.size());
                mStringIdMap[value[i]] = stringId;
            }
            stringIds.push_back(stringId);
        }
        serialize(stringIds.data(), count);
    }

    void BinarySaver::serialize(HString* value, size_t count)
    {
        std::vector<uint32_t> hstringIds;
        hstringIds.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            auto it = mHStringIdMap.find(value[i]);
            uint32_t hstringId;
            if (it != mHStringIdMap.end())
            {
                hstringId = it->second;
            }
            else
            {
                hstringId = static_cast<uint32_t>(mHStringIdMap.size());
                mHStringIdMap[value[i]] = hstringId;
            }
            hstringIds.push_back(hstringId);
        }
        serialize(hstringIds.data(), count);
    }

    void BinarySaver::serializeObjectRef(int32_t& ref)
    {
        if (!mObjectBufferIdxStack.empty())
        {
            serialize(&ref);
        }
    }

    void BinarySaver::beginStructure(HString& typeName, uint32_t& version, uint32_t& propCount)
    {
        serialize(&typeName);
        serialize(&version);
        serialize(&propCount);
    }

    void BinarySaver::endStructure()
    {
    }

    void BinarySaver::beginObject(HString& typeName, uint32_t& version, uint32_t& propCount)
    {
        uint32_t newObjectBufferIdx = static_cast<uint32_t>(mObjectBuffers.size());
        mObjectBuffers.emplace_back();
        pushObjectBufferIdx(newObjectBufferIdx);
        serialize(&typeName);
        serialize(&version);
        serialize(&propCount);
    }

    void BinarySaver::endObject()
    {
        popObjectBufferIdx();
    }

    void BinarySaver::beginProperty(HString& name, HString& typeName)
    {
        serialize(&name);
        serialize(&typeName);
        mPropertySizeOffsetStack.push_back(getCurrentObjectBuffer().offset);
        uint32_t propSize = 0;
        serialize(&propSize);
    }

    void BinarySaver::endProperty()
    {
        uint64_t propSizeOffset = mPropertySizeOffsetStack.back();
        mPropertySizeOffsetStack.pop_back();
        auto& objectBuffer = getCurrentObjectBuffer();
        uint64_t currentOffset = objectBuffer.offset;
        uint32_t propSize = static_cast<uint32_t>(currentOffset - propSizeOffset - sizeof(uint32_t));
        objectBuffer.offset = propSizeOffset;
        serialize(&propSize);
        objectBuffer.offset = currentOffset;
    }

    void BinarySaver::beginArray(uint32_t& count)
    {
        serialize(&count);
    }

    void BinarySaver::endArray()
    {
    }

    void BinarySaver::beginPair()
    {
    }

    void BinarySaver::endPair()
    {
    }

    void BinarySaver::writeData(const void* data, size_t size)
    {
        auto& objectBuffer = getCurrentObjectBuffer();
        if (objectBuffer.offset + size > objectBuffer.buffer.size())
            objectBuffer.buffer.resize(objectBuffer.offset + size);
        std::memcpy(objectBuffer.buffer.data() + objectBuffer.offset, data, size);
        objectBuffer.offset += size;
    }

    void BinarySaver::appendBytes(std::vector<uint8_t>& output, const void* data, size_t size)
    {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        output.insert(output.end(), bytes, bytes + size);
    }

    void BinarySaver::appendString(std::vector<uint8_t>& output, std::string_view str)
    {
        uint32_t length = static_cast<uint32_t>(str.size());
        appendBytes(output, &length, sizeof(length));
        appendBytes(output, str.data(), length);
    }

    std::vector<uint8_t> BinarySaver::pack() const
    {
        std::vector<uint8_t> output;

        // string table, ordered by id
        std::vector<std::string> orderedStrings(mStringIdMap.size());
        for (const auto& [str, id] : mStringIdMap)
            orderedStrings[id] = str;

        uint32_t stringCount = static_cast<uint32_t>(orderedStrings.size());
        appendBytes(output, &stringCount, sizeof(stringCount));
        for (const std::string& str : orderedStrings)
            appendString(output, str);

        // hstring table, ordered by id
        std::vector<HString> orderedHStrings(mHStringIdMap.size());
        for (const auto& [str, id] : mHStringIdMap)
            orderedHStrings[id] = str;

        uint32_t hstringCount = static_cast<uint32_t>(orderedHStrings.size());
        appendBytes(output, &hstringCount, sizeof(hstringCount));
        for (const HString& str : orderedHStrings)
            appendString(output, str.toStringView());

        // object buffers, ordered by index (matches SerializeContext export id ordering)
        uint32_t objectCount = static_cast<uint32_t>(mObjectBuffers.size());
        appendBytes(output, &objectCount, sizeof(objectCount));
        for (const ObjectBuffer& objectBuffer : mObjectBuffers)
        {
            uint64_t byteLength = static_cast<uint64_t>(objectBuffer.buffer.size());
            appendBytes(output, &byteLength, sizeof(byteLength));
            appendBytes(output, objectBuffer.buffer.data(), objectBuffer.buffer.size());
        }

        return output;
    }

}
