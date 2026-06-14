#pragma once
#include <type_traits>

namespace hyp
{
    template <typename T>
    concept HasStdHash = requires(T a) { { std::hash<T>{}(a) } -> std::convertible_to<std::size_t>; };

    template <typename T, typename U>
    concept HasEqualityOperator = requires(T a, U b) { { a == b } -> std::convertible_to<bool>; };

    template <typename T>
    class TDataKey
    {
    public:
        TDataKey()
        {
            mData = {};
            updateHash();
        }

        TDataKey(T data)
        {
            mData = std::move(data);
            updateHash();
        }

        TDataKey(const TDataKey& other) = default;

        TDataKey(TDataKey&& other) noexcept = default;

        TDataKey& operator=(const TDataKey& other) = default;

        TDataKey& operator=(TDataKey&& other) noexcept = default;

        template <typename U>
        bool operator==(const TDataKey<U>& other) const
        {
            if (mHash != other.getHash()) return false;
            if constexpr (HasEqualityOperator<T, U>)
            {
                return mData == other.getData();
            }
            else if constexpr (std::is_same_v<T, U> && std::is_trivially_copyable_v<T>)
            {
                return std::memcmp(&mData, &other.getData(), sizeof(T)) == 0;
            }
            else
            {
                static_assert(!std::is_same_v<T, T>, "No equality operator found for TDataKey");
            }
        }

        void setData(T data)
        {
            mData = std::move(data);
            updateHash();
        }

        template <typename LAMBDA>
        void update(LAMBDA lambda)
        {
            lambda(mData);
            updateHash();
        }

        const T& getData() const noexcept
        {
            return mData;
        }

        uint64_t getHash() const noexcept
        {
            return mHash;
        }

    private:
        void updateHash()
        {
            if constexpr (HasStdHash<T>)
            {
                mHash = std::hash<T>{}(mData);
            }
            else if constexpr (std::is_trivially_copyable_v<T>)
            {
                std::string_view view(reinterpret_cast<const char*>(&mData), sizeof(T));
                mHash = std::hash<std::string_view>{}(view);
            }
            else
            {
                static_assert(!std::is_same_v<T, T>, "No hash function found for TDataKey");
            }
        }

    private:
        T mData;
        uint64_t mHash;
    };
}

namespace std
{
    template <typename T>
    struct hash<hyp::TDataKey<T>>
    {
        size_t operator()(const hyp::TDataKey<T>& key) const
        {
            return key.getHash();
        }
    };
}
