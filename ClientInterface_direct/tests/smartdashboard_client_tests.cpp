#include "sd_smartdashboard_client.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace sd::direct
{
    namespace
    {
        using namespace std::chrono_literals;

        struct TestChannels
        {
            std::wstring mappingName;
            std::wstring dataEventName;
            std::wstring heartbeatEventName;
        };

        TestChannels MakeSharedTestChannels()
        {
            TestChannels channels;
            channels.mappingName = L"Local\\SmartDashboard.Direct.Buffer";
            channels.dataEventName = L"Local\\SmartDashboard.Direct.DataAvailable";
            channels.heartbeatEventName = L"Local\\SmartDashboard.Direct.Heartbeat";
            return channels;
        }

        bool WaitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout)
        {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (predicate())
                {
                    return true;
                }
                std::this_thread::sleep_for(10ms);
            }
            return predicate();
        }
    }

    TEST(SmartDashboardClientTests, AssertiveGetPublishesDefaultAndCallbackReceivesUpdates)
    {
        const TestChannels channels = MakeSharedTestChannels();

        SmartDashboardClientConfig config;
        config.publisher.mappingName = channels.mappingName;
        config.publisher.dataEventName = channels.dataEventName;
        config.publisher.heartbeatEventName = channels.heartbeatEventName;
        config.publisher.autoFlushThread = false;
        config.subscriber.mappingName = channels.mappingName;
        config.subscriber.dataEventName = channels.dataEventName;
        config.subscriber.heartbeatEventName = channels.heartbeatEventName;

        SmartDashboardClient client(config);
        ASSERT_TRUE(client.Start());

        std::atomic<bool> lastSeen {false};
        std::atomic<int> callbackCount {0};

        const auto token = client.SubscribeBoolean(
            "Demo/Ready",
            [&lastSeen, &callbackCount](bool value)
            {
                lastSeen.store(value);
                callbackCount.fetch_add(1);
            },
            true
        );
        ASSERT_TRUE(static_cast<bool>(token));

        const bool initial = client.GetBoolean("Demo/Ready", false);
        EXPECT_FALSE(initial);

        bool passiveRead = true;
        ASSERT_TRUE(WaitUntil(
            [&client, &passiveRead]()
            {
                return client.TryGetBoolean("Demo/Ready", passiveRead);
            },
            1s
        ));
        EXPECT_FALSE(passiveRead);

        client.PutBoolean("Demo/Ready", true);
        ASSERT_TRUE(client.FlushNow());

        ASSERT_TRUE(WaitUntil(
            [&lastSeen]()
            {
                return lastSeen.load();
            },
            2s
        ));
        EXPECT_GE(callbackCount.load(), 1);

        EXPECT_TRUE(client.Unsubscribe(token));
        client.Stop();
    }
}
