#pragma once

#include "sd_direct_publisher.h"
#include "sd_direct_subscriber.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace sd::direct
{
    struct SmartDashboardClientConfig
    {
        PublisherConfig publisher;
        SubscriberConfig subscriber;
        bool enableSubscriber = true;
    };

    struct SubscriptionToken
    {
        std::string key;
        ValueType type = ValueType::String;
        std::uint64_t id = 0;

        explicit operator bool() const
        {
            return id != 0;
        }
    };

    using BoolChangedCallback = std::function<void(bool)>;
    using DoubleChangedCallback = std::function<void(double)>;
    using StringChangedCallback = std::function<void(const std::string&)>;

    class SmartDashboardClient final
    {
    public:
        explicit SmartDashboardClient(const SmartDashboardClientConfig& config = {});
        ~SmartDashboardClient();

        bool Start();
        void Stop();

        void PutBoolean(std::string_view key, bool value);
        void PutDouble(std::string_view key, double value);
        void PutString(std::string_view key, std::string_view value);
        bool FlushNow();

        bool TryGetBoolean(std::string_view key, bool& outValue) const;
        bool TryGetDouble(std::string_view key, double& outValue) const;
        bool TryGetString(std::string_view key, std::string& outValue) const;

        bool GetBoolean(std::string_view key, bool defaultValue);
        double GetDouble(std::string_view key, double defaultValue);
        std::string GetString(std::string_view key, std::string_view defaultValue);

        SubscriptionToken SubscribeBoolean(std::string key, BoolChangedCallback callback, bool invokeImmediately = true);
        SubscriptionToken SubscribeDouble(std::string key, DoubleChangedCallback callback, bool invokeImmediately = true);
        SubscriptionToken SubscribeString(std::string key, StringChangedCallback callback, bool invokeImmediately = true);
        bool Unsubscribe(const SubscriptionToken& token);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
