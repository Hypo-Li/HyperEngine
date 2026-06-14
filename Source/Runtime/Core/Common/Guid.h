#pragma once
#include "../Reflection/Reflection.h"

namespace hyp
{
    struct Guid
    {
        uint32_t A = 0;
        uint32_t B = 0;
        uint32_t C = 0;
        uint32_t D = 0;

        inline bool isValid() const
        {
            return A != 0 || B != 0 || C != 0 || D != 0;
        }

        std::string toString() const;

        static Guid newGuid();

        friend bool operator==(const Guid& guid1, const Guid& guid2)
        {
            return guid1.A == guid2.A && guid1.B == guid2.B && guid1.C == guid2.C && guid1.D == guid2.D;
        }

        friend bool operator!=(const Guid& guid1, const Guid& guid2)
        {
            return guid1.A != guid2.A || guid1.B != guid2.B || guid1.C != guid2.C || guid1.D != guid2.D;
        }
    };

    template <> Type* Reflection::createType<Guid>();
}

namespace std
{
    template <> struct hash<hyp::Guid>
    {
        std::size_t operator()(const hyp::Guid& guid) const
        {
            return std::hash<uint32_t>()(guid.A) ^ std::hash<uint32_t>()(guid.B) ^ std::hash<uint32_t>()(guid.C) ^ std::hash<uint32_t>()(guid.D);
        }
    };
}
