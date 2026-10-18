#pragma once
#include "../Common/HString.h"

namespace hyp
{
    class Archive
    {
    public:
        Archive(bool isLoading, bool isCooking) :
            mIsLoading(isLoading),
            mIsCooking(isCooking)
        {
        }

        bool isLoading() const noexcept
        {
            return mIsLoading;
        }

        bool isCooking() const noexcept
        {
            return mIsCooking;
        }

        virtual void serialize(bool* value, size_t count = 1) = 0;
        virtual void serialize(char* value, size_t count = 1) = 0;
        virtual void serialize(wchar_t* value, size_t count = 1) = 0;
        virtual void serialize(int8_t* value, size_t count = 1) = 0;
        virtual void serialize(int16_t* value, size_t count = 1) = 0;
        virtual void serialize(int32_t* value, size_t count = 1) = 0;
        virtual void serialize(int64_t* value, size_t count = 1) = 0;
        virtual void serialize(uint8_t* value, size_t count = 1) = 0;
        virtual void serialize(uint16_t* value, size_t count = 1) = 0;
        virtual void serialize(uint32_t* value, size_t count = 1) = 0;
        virtual void serialize(uint64_t* value, size_t count = 1) = 0;
        virtual void serialize(float* value, size_t count = 1) = 0;
        virtual void serialize(double* value, size_t count = 1) = 0;
        virtual void serialize(std::string* value, size_t count = 1) = 0;
        virtual void serialize(HString* value, size_t count = 1) = 0;
        virtual void serializeObjectRef(int32_t& ref) = 0;

        virtual void beginStructure(HString& typeName, uint32_t& version, uint32_t& propCount) = 0;
        virtual void endStructure() = 0;
        virtual void beginObject(HString& typeName, uint32_t& version, uint32_t& propCount) = 0;
        virtual void endObject() = 0;
        virtual void beginProperty(HString& name, HString& typeName) = 0;
        virtual void endProperty() = 0;
        virtual void beginArray(uint32_t& count) = 0;
        virtual void endArray() = 0;
        virtual void beginPair() = 0;
        virtual void endPair() = 0;

    private:
        bool mIsLoading = false;
        bool mIsCooking = false;
    };
}
