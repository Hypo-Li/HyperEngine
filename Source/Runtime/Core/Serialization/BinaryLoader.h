#pragma once
#include "BinaryArchive.h"
#include <span>

namespace hyp
{
    class BinaryLoader : public BinaryArchive
    {
    public:
        BinaryLoader() = default;

        bool isLoading() const override;

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

        void unpack(std::span<const uint8_t> data);

    private:
        void readData(void* data, size_t size);

        static void readBytes(std::span<const uint8_t> data, size_t& cursor, void* dst, size_t size);

        static std::string readString(std::span<const uint8_t> data, size_t& cursor);

    private:
        std::vector<std::string> mStringTable;
        std::vector<HString> mHStringTable;
        int32_t mCurrentObjectRef = 0;
        std::vector<uint64_t> mPropertyEndOffsetStack;
    };
}
