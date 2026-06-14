#pragma once
#include <memory>
#include <unordered_map>

#include "../Common/HString.h"

namespace hyp
{
    class Event
    {
    public:
        Event(HString type, void* data) : mType(type), mData(data) {}

        ~Event() = default;

        HString getType() const
        {
            return mType;
        }

        void* getData() const
        {
            return mData;
        }

    private:
        HString mType;
        void* mData;
    };

    class IEventListener
    {
    public:
        virtual void onEvent(const Event& event) = 0;
    };

    template <typename LAMBDA>
    class TEventListener : public IEventListener
    {
    public:
        TEventListener(LAMBDA lambda) : mLambda(std::move(lambda)) {}

        virtual void onEvent(const Event& event) override
        {
            mLambda(event);
        }

    protected:
        LAMBDA mLambda;
    };

    class EventManager
    {
    public:
        void dispatch(HString eventType, void* data)
        {
            auto it = mEventRegistry.find(eventType);
            if (it != mEventRegistry.end())
            {
                Event event(eventType, data);
                for (auto& listener : it->second)
                {
                    listener->onEvent(event);
                }
            }
        }

        template <typename LAMBDA>
        void subscribe(HString eventType, LAMBDA lambda)
        {
            auto& registry = mEventRegistry[eventType];
            registry.emplace_back(std::make_unique<TEventListener<LAMBDA>>(std::move(lambda)));
        }

    private:
        std::unordered_map<HString, std::vector<std::unique_ptr<IEventListener>>> mEventRegistry;
    };
}
