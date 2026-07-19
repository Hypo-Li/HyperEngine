#pragma once
#include "Archive.h"

namespace hyp
{
    class BinaryArchive : public Archive
    {
    protected:
        struct ObjectBuffer
        {
            std::vector<uint8_t> buffer;
            uint64_t offset = 0;
        };
        ObjectBuffer& getCurrentObjectBuffer();

        void pushObjectBufferIdx(uint32_t objectBufferIdx);

        void popObjectBufferIdx();

    protected:
        std::vector<ObjectBuffer> mObjectBuffers;
        std::vector<uint32_t> mObjectBufferIdxStack;
    };
}
