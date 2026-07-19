#include "BinaryArchive.h"
#include <cassert>

namespace hyp
{
    BinaryArchive::ObjectBuffer& BinaryArchive::getCurrentObjectBuffer()
    {
        assert(!mObjectBufferIdxStack.empty());
        return mObjectBuffers.at(mObjectBufferIdxStack.back());
    }

    void BinaryArchive::pushObjectBufferIdx(uint32_t objectBufferIdx)
    {
        mObjectBufferIdxStack.push_back(objectBufferIdx);
    }

    void BinaryArchive::popObjectBufferIdx()
    {
        mObjectBufferIdxStack.pop_back();
    }


}
