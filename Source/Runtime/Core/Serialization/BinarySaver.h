#pragma once
#include "BinaryArchive.h"

namespace hyp
{
    class BinarySaver : public BinaryArchive
    {
    public:
        BinarySaver(bool isCooking = false);

        void serialize(bool* value, size_t count = 1) override;
        void serialize(char* value, size_t count = 1) override;
        void serialize(wchar_t* value, size_t count = 1) override;
        void serialize(int8_t* value, size_t count = 1) override;
        void serialize(int16_t* value, size_t count = 1) override;
        void serialize(int32_t* value, size_t count = 1) override;
        void serialize(int64_t* value, size_t count = 1) override;
        void serialize(uint8_t* value, size_t count = 1) override;
        void serialize(uint16_t* value, size_t count = 1) override;
        void serialize(uint32_t* value, size_t count = 1) override;
        void serialize(uint64_t* value, size_t count = 1) override;
        void serialize(float* value, size_t count = 1) override;
        void serialize(double* value, size_t count = 1) override;
        void serialize(std::string* value, size_t count = 1) override;
        void serialize(HString* value, size_t count = 1) override;
        void serializeObjectRef(int32_t& ref) override;

        void beginStructure(HString& typeName, uint32_t& version, uint32_t& propCount) override;
        void endStructure() override;
        void beginObject(HString& typeName, uint32_t& version, uint32_t& propCount) override;
        void endObject() override;
        void beginProperty(HString& name, HString& typeName) override;
        void endProperty() override;
        void beginArray(uint32_t& count) override;
        void endArray() override;
        void beginPair() override;
        void endPair() override;

        std::vector<uint8_t> pack() const;

    private:
        void writeData(const void* data, size_t size);

        static void appendBytes(std::vector<uint8_t>& output, const void* data, size_t size);

        static void appendString(std::vector<uint8_t>& output, std::string_view str);

    private:
        std::unordered_map<std::string, uint32_t> mStringIdMap;
        std::unordered_map<HString, uint32_t> mHStringIdMap;
        std::vector<uint64_t> mPropertySizeOffsetStack;
    };
}
