#pragma once
#include "../Special.h"
#include "../../Common/BulkData.h"

namespace hyp
{
    class HypBulkData : public Special
    {
        friend class Reflection;
    public:
        virtual void construct(void* instance) const override
        {
            std::construct_at(static_cast<BulkData*>(instance));
        }

        virtual void destruct(void* instance) const override
        {
            std::destroy_at(static_cast<BulkData*>(instance));
        }

        virtual bool compare(const void* lhs, const void* rhs) const override
        {
            // 内容比较代价可能很大(需要先 load),这里只用"是否为空"做启发式判断,
            // 和 TStdVector::compare 的思路一致——default object 的 BulkData 恒为空,
            // 所以任何真正有内容的 BulkData 都会被判定为"和默认值不同"从而参与 diff/save
            return static_cast<const BulkData*>(lhs)->getSize() == 0 &&
                static_cast<const BulkData*>(rhs)->getSize() == 0;
        }

    private:
        HypBulkData() : Special("BulkData", sizeof(BulkData), Case::BulkData) {}
    };
}
