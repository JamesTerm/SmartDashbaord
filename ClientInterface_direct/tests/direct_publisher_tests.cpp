#include "sd_direct_publisher.h"
#include "sd_direct_subscriber.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <mutex>
#include <numbers>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

        TestChannels MakeTestChannels()
        {
            // Teaching mode default: use the same channel names as SmartDashboardApp,
            // so students can run these tests and immediately see UI updates.
            // Set SD_DIRECT_TEST_USE_ISOLATED_CHANNELS=1 to isolate each test run.
            const char* useIsolated = std::getenv("SD_DIRECT_TEST_USE_ISOLATED_CHANNELS");
            if (useIsolated == nullptr || std::string(useIsolated) != "1")
            {
                TestChannels channels;
                channels.mappingName = L"Local\\SmartDashboard.Direct.Buffer";
                channels.dataEventName = L"Local\\SmartDashboard.Direct.DataAvailable";
                channels.heartbeatEventName = L"Local\\SmartDashboard.Direct.Heartbeat";
                return channels;
            }

            static std::atomic<std::uint64_t> counter {0};
            const std::uint64_t id = counter.fetch_add(1) + 1;
            const std::uint64_t tick = static_cast<std::uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count()
            );
            const std::wstring suffix = std::to_wstring(tick) + L"." + std::to_wstring(id);

            TestChannels channels;
            channels.mappingName = L"Local\\SmartDashboard.Direct.Test.Buffer." + suffix;
            channels.dataEventName = L"Local\\SmartDashboard.Direct.Test.Data." + suffix;
            channels.heartbeatEventName = L"Local\\SmartDashboard.Direct.Test.Heartbeat." + suffix;
            return channels;
        }

        bool WaitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout)
        {
            // Poll with a small sleep to avoid busy-waiting while still reacting quickly.
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

        template <typename PublishFn>
        void PublishForDuration(
            IDirectPublisher& publisher,
            std::chrono::milliseconds duration,
            std::chrono::milliseconds step,
            PublishFn&& publish
        )
        {
            // Manual publish loop used by all tests.
            // We flush each iteration so subscriber callbacks receive new data promptly.
            const auto start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start < duration)
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start
                );
                publish(elapsed, duration);
                publisher.FlushNow();
                std::this_thread::sleep_for(step);
            }
        }
    }

    TEST(DirectPublisherTests, StreamsCreativeBoolPattern)
    {
        // Subscriber reads updates (dashboard side), publisher writes updates (client side).
        const TestChannels channels = MakeTestChannels();

        SubscriberConfig subConfig;
        subConfig.mappingName = channels.mappingName;
        subConfig.dataEventName = channels.dataEventName;
        subConfig.heartbeatEventName = channels.heartbeatEventName;

        PublisherConfig pubConfig;
        pubConfig.mappingName = channels.mappingName;
        pubConfig.dataEventName = channels.dataEventName;
        pubConfig.heartbeatEventName = channels.heartbeatEventName;
        // Disable auto flush for deterministic behavior in tests.
        pubConfig.autoFlushThread = false;

        auto subscriber = CreateDirectSubscriber(subConfig);
        auto publisher = CreateDirectPublisher(pubConfig);

        std::vector<bool> observed;
        std::mutex observedMutex;

        ASSERT_TRUE(subscriber->Start(
            [&observed, &observedMutex](const VariableUpdate& update)
            {
                // Filter to the exact key/type this test owns.
                if (update.key != "Test/Bool" || update.type != ValueType::Bool)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(observedMutex);
                observed.push_back(update.value.boolValue);
            },
            [](ConnectionState)
            {
            }
        ));

        ASSERT_TRUE(publisher->Start());

        PublishForDuration(*publisher, 3s, 100ms, [&publisher](std::chrono::milliseconds elapsed, std::chrono::milliseconds)
        {
            // Example bool pattern: TTFFF repeat every 5 slots.
            const auto slot = elapsed.count() / 100;
            const bool value = ((slot % 5) == 0) || ((slot % 5) == 1);
            publisher->PublishBool("Test/Bool", value);
        });

        ASSERT_TRUE(WaitUntil(
            [&observed, &observedMutex]()
            {
                std::lock_guard<std::mutex> lock(observedMutex);
                return observed.size() >= 5;
            },
            2s
        ));

        publisher->Stop();
        subscriber->Stop();

        bool sawTrue = false;
        bool sawFalse = false;
        {
            std::lock_guard<std::mutex> lock(observedMutex);
            for (const bool value : observed)
            {
                sawTrue = sawTrue || value;
                sawFalse = sawFalse || !value;
            }
        }

        EXPECT_TRUE(sawTrue);
        EXPECT_TRUE(sawFalse);
    }

    TEST(DirectPublisherTests, StreamsSineWaveDouble)
    {
        const TestChannels channels = MakeTestChannels();

        SubscriberConfig subConfig;
        subConfig.mappingName = channels.mappingName;
        subConfig.dataEventName = channels.dataEventName;
        subConfig.heartbeatEventName = channels.heartbeatEventName;

        PublisherConfig pubConfig;
        pubConfig.mappingName = channels.mappingName;
        pubConfig.dataEventName = channels.dataEventName;
        pubConfig.heartbeatEventName = channels.heartbeatEventName;
        pubConfig.autoFlushThread = false;

        auto subscriber = CreateDirectSubscriber(subConfig);
        auto publisher = CreateDirectPublisher(pubConfig);

        std::vector<double> observed;
        std::mutex observedMutex;

        ASSERT_TRUE(subscriber->Start(
            [&observed, &observedMutex](const VariableUpdate& update)
            {
                if (update.key != "Test/DoubleSine" || update.type != ValueType::Double)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(observedMutex);
                observed.push_back(update.value.doubleValue);
            },
            [](ConnectionState)
            {
            }
        ));

        ASSERT_TRUE(publisher->Start());

        PublishForDuration(*publisher, 3s, 20ms, [&publisher](std::chrono::milliseconds elapsed, std::chrono::milliseconds duration)
        {
            // Sweep phase from -pi to +pi over the test duration, then publish sin(phase).
            const double progress = std::clamp(
                static_cast<double>(elapsed.count()) / static_cast<double>(duration.count()),
                0.0,
                1.0
            );
            const double phase = -std::numbers::pi + (2.0 * std::numbers::pi * progress);
            publisher->PublishDouble("Test/DoubleSine", std::sin(phase));
        });

        ASSERT_TRUE(WaitUntil(
            [&observed, &observedMutex]()
            {
                std::lock_guard<std::mutex> lock(observedMutex);
                return observed.size() >= 50;
            },
            2s
        ));

        publisher->Stop();
        subscriber->Stop();

        double minValue = std::numeric_limits<double>::max();
        double maxValue = std::numeric_limits<double>::lowest();
        {
            std::lock_guard<std::mutex> lock(observedMutex);
            for (const double value : observed)
            {
                minValue = std::min(minValue, value);
                maxValue = std::max(maxValue, value);
            }
        }

        EXPECT_LE(minValue, -0.8);
        EXPECT_GE(maxValue, 0.8);
    }

    TEST(DirectPublisherTests, StreamsRotatingStatusStrings)
    {
        const TestChannels channels = MakeTestChannels();

        SubscriberConfig subConfig;
        subConfig.mappingName = channels.mappingName;
        subConfig.dataEventName = channels.dataEventName;
        subConfig.heartbeatEventName = channels.heartbeatEventName;

        PublisherConfig pubConfig;
        pubConfig.mappingName = channels.mappingName;
        pubConfig.dataEventName = channels.dataEventName;
        pubConfig.heartbeatEventName = channels.heartbeatEventName;
        pubConfig.autoFlushThread = false;

        auto subscriber = CreateDirectSubscriber(subConfig);
        auto publisher = CreateDirectPublisher(pubConfig);

        std::vector<std::string> observed;
        std::mutex observedMutex;

        ASSERT_TRUE(subscriber->Start(
            [&observed, &observedMutex](const VariableUpdate& update)
            {
                if (update.key != "Test/Status" || update.type != ValueType::String)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(observedMutex);
                observed.push_back(update.value.stringValue);
            },
            [](ConnectionState)
            {
            }
        ));

        ASSERT_TRUE(publisher->Start());

        const std::array<std::string, 4> statuses {
            "Booting",
            "Calibrating",
            "Auto Tracking",
            "Teleop Sprint"
        };
        std::size_t statusIndex = 0;

        PublishForDuration(*publisher, 3s, 120ms, [&publisher, &statuses, &statusIndex](std::chrono::milliseconds, std::chrono::milliseconds)
        {
            // Rotate through sample status text values.
            publisher->PublishString("Test/Status", statuses[statusIndex]);
            statusIndex = (statusIndex + 1) % statuses.size();
        });

        ASSERT_TRUE(WaitUntil(
            [&observed, &observedMutex]()
            {
                std::lock_guard<std::mutex> lock(observedMutex);
                return observed.size() >= 10;
            },
            2s
        ));

        publisher->Stop();
        subscriber->Stop();

        std::set<std::string> distinct;
        {
            std::lock_guard<std::mutex> lock(observedMutex);
            distinct.insert(observed.begin(), observed.end());
        }

        // Direct transport keeps latest values per key between flushes,
        // so under timing variability we assert a realistic minimum.
        EXPECT_GE(distinct.size(), 2U);
    }
}
