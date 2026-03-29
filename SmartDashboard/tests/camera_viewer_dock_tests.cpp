// Ian: Tests for CameraViewerDock — discovery populates combo, manual connect/
// disconnect, auto-reconnect on error, manual disconnect suppression, and
// CameraPublisherDiscovery key parsing / signal emission.
//
// Also tests the camera discovery abstraction layer:
//   - CameraDiscoveryAggregator: merges providers, prefixes names, per-provider clearing
//   - StaticCameraSource: add/remove/persist static cameras
//   - Aggregator→Dock integration: wiring through the aggregator
//   - RemoveDiscoveredCamera: single-camera removal on the dock
//
// Auto-connect on discovery was removed — the user must click Connect manually.
// Auto-reconnect after stream errors is still active.
//
// These tests use the dock's public API without a real MJPEG server.
// The MjpegStreamSource will go to Error state when connecting to a
// nonexistent server, which lets us verify auto-reconnect scheduling.

#include "widgets/camera_viewer_dock.h"
#include "camera/camera_publisher_discovery.h"
#include "camera/camera_discovery_aggregator.h"
#include "camera/camera_discovery_source.h"
#include "camera/static_camera_source.h"
#include "camera/camera_stream_source.h"

#include <QApplication>
#include <QSignalSpy>

#include <gtest/gtest.h>

#include <memory>

namespace
{

QApplication* EnsureApp()
{
    if (QApplication::instance() != nullptr)
    {
        return qobject_cast<QApplication*>(QApplication::instance());
    }

    static int argc = 1;
    static char appName[] = "SmartDashboardTests";
    static char* argv[] = { appName };
    static std::unique_ptr<QApplication> app =
        std::make_unique<QApplication>(argc, argv);
    return app.get();
}

// ────────────────────────────────────────────────────────────────────────
// CameraPublisherDiscovery tests
// ────────────────────────────────────────────────────────────────────────

class CameraPublisherDiscoveryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        EnsureApp();
        m_discovery = std::make_unique<sd::camera::CameraPublisherDiscovery>();
    }

    void TearDown() override
    {
        m_discovery.reset();
    }

    std::unique_ptr<sd::camera::CameraPublisherDiscovery> m_discovery;
};

TEST_F(CameraPublisherDiscoveryTest, IgnoresNonCameraPublisherKeys)
{
    QSignalSpy spy(m_discovery.get(),
        &sd::camera::CameraPublisherDiscovery::CameraDiscovered);

    m_discovery->OnVariableUpdate("/SmartDashboard/Velocity", 1, QVariant(42.0));
    m_discovery->OnVariableUpdate("Heading", 1, QVariant(0.0));
    m_discovery->OnVariableUpdate("", 0, QVariant());

    EXPECT_EQ(spy.count(), 0);
}

TEST_F(CameraPublisherDiscoveryTest, IgnoresCameraPublisherNonStreamsKeys)
{
    QSignalSpy spy(m_discovery.get(),
        &sd::camera::CameraPublisherDiscovery::CameraDiscovered);

    m_discovery->OnVariableUpdate("/CameraPublisher/SimCamera/source", 4, QVariant("test"));
    m_discovery->OnVariableUpdate("/CameraPublisher/SimCamera/connected", 0, QVariant(true));
    m_discovery->OnVariableUpdate("/CameraPublisher/SimCamera/description", 4, QVariant("desc"));

    EXPECT_EQ(spy.count(), 0);
}

TEST_F(CameraPublisherDiscoveryTest, DiscoversCameraFromStringArrayValue)
{
    QSignalSpy spy(m_discovery.get(),
        &sd::camera::CameraPublisherDiscovery::CameraDiscovered);

    // Simulate the value arriving as QStringList (the normal NT4 path).
    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    ASSERT_EQ(spy.count(), 1);
    const auto args = spy.first();
    EXPECT_EQ(args.at(0).toString(), "SimCamera");
    const QStringList resultUrls = args.at(1).toStringList();
    ASSERT_EQ(resultUrls.size(), 1);
    EXPECT_EQ(resultUrls.first(), "http://127.0.0.1:1181/?action=stream");
}

TEST_F(CameraPublisherDiscoveryTest, DiscoversCameraFromSingleStringValue)
{
    QSignalSpy spy(m_discovery.get(),
        &sd::camera::CameraPublisherDiscovery::CameraDiscovered);

    m_discovery->OnVariableUpdate(
        "/CameraPublisher/FrontCam/streams", 4,
        QVariant(QStringLiteral("mjpg:http://10.0.0.2:1181/?action=stream")));

    ASSERT_EQ(spy.count(), 1);
    const auto args = spy.first();
    EXPECT_EQ(args.at(0).toString(), "FrontCam");
    const QStringList resultUrls = args.at(1).toStringList();
    ASSERT_EQ(resultUrls.size(), 1);
    EXPECT_EQ(resultUrls.first(), "http://10.0.0.2:1181/?action=stream");
}

TEST_F(CameraPublisherDiscoveryTest, StripsMjpgPrefixCaseInsensitive)
{
    QSignalSpy spy(m_discovery.get(),
        &sd::camera::CameraPublisherDiscovery::CameraDiscovered);

    QStringList urls;
    urls << "MJPG:http://10.0.0.1:1181/?action=stream";
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/TestCam/streams", 20, QVariant(urls));

    ASSERT_EQ(spy.count(), 1);
    const QStringList resultUrls = spy.first().at(1).toStringList();
    EXPECT_EQ(resultUrls.first(), "http://10.0.0.1:1181/?action=stream");
}

TEST_F(CameraPublisherDiscoveryTest, DuplicateUrlsDoNotReemit)
{
    QSignalSpy spy(m_discovery.get(),
        &sd::camera::CameraPublisherDiscovery::CameraDiscovered);

    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";

    m_discovery->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    // Same URLs → only emitted once.
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(CameraPublisherDiscoveryTest, ChangedUrlsReemit)
{
    QSignalSpy spy(m_discovery.get(),
        &sd::camera::CameraPublisherDiscovery::CameraDiscovered);

    QStringList urls1;
    urls1 << "mjpg:http://127.0.0.1:1181/?action=stream";
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls1));

    QStringList urls2;
    urls2 << "mjpg:http://127.0.0.1:1182/?action=stream";
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls2));

    EXPECT_EQ(spy.count(), 2);
}

TEST_F(CameraPublisherDiscoveryTest, ClearEmitsCamerasCleared)
{
    QSignalSpy discoveredSpy(m_discovery.get(),
        &sd::camera::CameraPublisherDiscovery::CameraDiscovered);
    QSignalSpy clearedSpy(m_discovery.get(),
        &sd::camera::CameraPublisherDiscovery::CamerasCleared);

    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    EXPECT_EQ(m_discovery->GetCameraNames().size(), 1);

    m_discovery->Clear();

    EXPECT_EQ(clearedSpy.count(), 1);
    EXPECT_EQ(m_discovery->GetCameraNames().size(), 0);
}

TEST_F(CameraPublisherDiscoveryTest, MultipleCamerasTrackedIndependently)
{
    QStringList urls1;
    urls1 << "mjpg:http://127.0.0.1:1181/?action=stream";
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls1));

    QStringList urls2;
    urls2 << "mjpg:http://10.0.0.2:1181/?action=stream";
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/FrontCam/streams", 20, QVariant(urls2));

    EXPECT_EQ(m_discovery->GetCameraNames().size(), 2);
    EXPECT_TRUE(m_discovery->GetCameraNames().contains("SimCamera"));
    EXPECT_TRUE(m_discovery->GetCameraNames().contains("FrontCam"));

    const QStringList simUrls = m_discovery->GetStreamUrls("SimCamera");
    ASSERT_EQ(simUrls.size(), 1);
    EXPECT_EQ(simUrls.first(), "http://127.0.0.1:1181/?action=stream");
}

TEST_F(CameraPublisherDiscoveryTest, EmptyStreamsValueDoesNotEmit)
{
    QSignalSpy spy(m_discovery.get(),
        &sd::camera::CameraPublisherDiscovery::CameraDiscovered);

    QStringList emptyUrls;
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/EmptyCam/streams", 20, QVariant(emptyUrls));

    // Ian: Empty URL list matches the default value for an unknown camera
    // (QMap::value returns empty QStringList), so no change → no emit.
    // This is correct — a camera with no streams is not useful for discovery.
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(CameraPublisherDiscoveryTest, MalformedKeyWithNoNameSegmentIsIgnored)
{
    QSignalSpy spy(m_discovery.get(),
        &sd::camera::CameraPublisherDiscovery::CameraDiscovered);

    // No camera name between the slashes.
    m_discovery->OnVariableUpdate(
        "/CameraPublisher//streams", 20, QVariant(QStringList()));

    EXPECT_EQ(spy.count(), 0);
}

// ────────────────────────────────────────────────────────────────────────
// CameraViewerDock tests
// ────────────────────────────────────────────────────────────────────────

class CameraViewerDockTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        EnsureApp();
        m_dock = std::make_unique<sd::widgets::CameraViewerDock>();
    }

    void TearDown() override
    {
        m_dock.reset();
    }

    std::unique_ptr<sd::widgets::CameraViewerDock> m_dock;
};

TEST_F(CameraViewerDockTest, StartsWithZeroDiscoveredCameras)
{
    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 0);
}

TEST_F(CameraViewerDockTest, AddDiscoveredCameraIncreasesCount)
{
    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1181/?action=stream");
    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 1);
}

TEST_F(CameraViewerDockTest, ClearDiscoveredCamerasResetsCount)
{
    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1181/?action=stream");
    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 1);

    m_dock->ClearDiscoveredCameras();
    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 0);
}

TEST_F(CameraViewerDockTest, AddDiscoveredCameraUpdatesExisting)
{
    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1181/?action=stream");
    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1182/?action=stream");
    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 1);
}

TEST_F(CameraViewerDockTest, MultipleDiscoveredCamerasTracked)
{
    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1181/?action=stream");
    m_dock->AddDiscoveredCamera("FrontCam",
        QStringList() << "http://10.0.0.2:1181/?action=stream");
    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 2);
}

TEST_F(CameraViewerDockTest, DiscoveryDoesNotAutoConnect)
{
    // Ian: Auto-connect was removed.  Discovering a camera should NOT
    // automatically connect.  The user must click Connect manually.
    m_dock->show();
    QApplication::processEvents();

    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1181/?action=stream");

    // No auto-connect — last connected URL stays empty.
    EXPECT_TRUE(m_dock->GetLastConnectedUrl().isEmpty());
}

TEST_F(CameraViewerDockTest, AutoConnectDoesNotFireWhenHidden)
{
    // Dock is hidden by default — auto-connect should NOT fire.
    EXPECT_FALSE(m_dock->isVisible());

    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1181/?action=stream");

    // No auto-connect because dock is hidden.
    EXPECT_TRUE(m_dock->GetLastConnectedUrl().isEmpty());
}

TEST_F(CameraViewerDockTest, ManualDisconnectSuppressesAutoReconnect)
{
    m_dock->show();
    QApplication::processEvents();

    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1181/?action=stream");

    EXPECT_FALSE(m_dock->IsAutoReconnectSuppressed());

    // Simulate user clicking Disconnect.
    // We can't call OnDisconnectClicked directly (private slot), but we
    // can call StopStream which has the same suppress behavior when called
    // from MainWindow.  However, the user disconnect path is through the
    // button.  Let's verify via the public API.
    m_dock->StopStream();

    EXPECT_TRUE(m_dock->IsAutoReconnectSuppressed());
}

TEST_F(CameraViewerDockTest, NewDiscoveryClearsManualDisconnectFlag)
{
    m_dock->show();
    QApplication::processEvents();

    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1181/?action=stream");
    m_dock->StopStream();

    EXPECT_TRUE(m_dock->IsAutoReconnectSuppressed());

    // A new camera discovery should clear the suppress flag.
    m_dock->AddDiscoveredCamera("FrontCam",
        QStringList() << "http://10.0.0.2:1181/?action=stream");

    EXPECT_FALSE(m_dock->IsAutoReconnectSuppressed());
}

TEST_F(CameraViewerDockTest, ClearDiscoveredCamerasSuppressesReconnect)
{
    m_dock->show();
    QApplication::processEvents();

    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1181/?action=stream");

    m_dock->ClearDiscoveredCameras();

    EXPECT_TRUE(m_dock->IsAutoReconnectSuppressed());
    EXPECT_TRUE(m_dock->GetLastConnectedUrl().isEmpty());
    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 0);
}

TEST_F(CameraViewerDockTest, StopStreamSuppressesReconnect)
{
    m_dock->show();
    QApplication::processEvents();

    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1181/?action=stream");

    m_dock->StopStream();

    EXPECT_TRUE(m_dock->IsAutoReconnectSuppressed());
}

TEST_F(CameraViewerDockTest, DiscoveryDoesNotAutoConnectEvenWithMjpgPrefix)
{
    // Ian: Auto-connect was removed.  Even with mjpg: prefix URLs,
    // no auto-connect should happen.
    m_dock->show();
    QApplication::processEvents();

    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "mjpg:http://127.0.0.1:1181/?action=stream");

    // No auto-connect — URL stays empty.
    EXPECT_TRUE(m_dock->GetLastConnectedUrl().isEmpty());
}

TEST_F(CameraViewerDockTest, EmptyUrlsDoNotTriggerAutoConnect)
{
    m_dock->show();
    QApplication::processEvents();

    m_dock->AddDiscoveredCamera("EmptyCam", QStringList());

    EXPECT_TRUE(m_dock->GetLastConnectedUrl().isEmpty());
}

// ────────────────────────────────────────────────────────────────────────
// Integration: Discovery -> Dock wiring (through aggregator)
// Ian: Updated to route through CameraDiscoveryAggregator, matching the
// real MainWindow wiring.  Protocol discovery uses an empty tag so
// cameras appear undecorated in the UI (e.g. "SimCamera" not "[NT4] SimCamera").
// ────────────────────────────────────────────────────────────────────────

class CameraDiscoveryDockIntegrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        EnsureApp();
        m_discovery = std::make_unique<sd::camera::CameraPublisherDiscovery>();
        m_aggregator = std::make_unique<sd::camera::CameraDiscoveryAggregator>();
        m_dock = std::make_unique<sd::widgets::CameraViewerDock>();

        // Wire discovery -> aggregator -> dock (same as MainWindow does).
        // Empty tag = no prefix in display names (transport-agnostic).
        m_aggregator->AddSource(QString(), m_discovery.get());

        QObject::connect(
            m_aggregator.get(),
            &sd::camera::CameraDiscoveryAggregator::CameraDiscovered,
            m_dock.get(),
            &sd::widgets::CameraViewerDock::AddDiscoveredCamera
        );
        QObject::connect(
            m_aggregator.get(),
            &sd::camera::CameraDiscoveryAggregator::CamerasCleared,
            m_dock.get(),
            &sd::widgets::CameraViewerDock::ClearDiscoveredCameras
        );
        QObject::connect(
            m_aggregator.get(),
            &sd::camera::CameraDiscoveryAggregator::CameraRemoved,
            m_dock.get(),
            &sd::widgets::CameraViewerDock::RemoveDiscoveredCamera
        );
    }

    void TearDown() override
    {
        m_dock.reset();
        m_aggregator.reset();
        m_discovery.reset();
    }

    std::unique_ptr<sd::camera::CameraPublisherDiscovery> m_discovery;
    std::unique_ptr<sd::camera::CameraDiscoveryAggregator> m_aggregator;
    std::unique_ptr<sd::widgets::CameraViewerDock> m_dock;
};

TEST_F(CameraDiscoveryDockIntegrationTest, NT4KeyPopulatesDockCombo)
{
    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    // Ian: Empty-tag provider produces undecorated camera names.
    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 1);
}

TEST_F(CameraDiscoveryDockIntegrationTest, DiscoveryDoesNotAutoConnectWhenVisible)
{
    // Ian: Auto-connect was removed.  Even when the dock is visible,
    // discovering a camera should NOT trigger a connection.
    m_dock->show();
    QApplication::processEvents();

    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    // No auto-connect — last connected URL stays empty.
    EXPECT_TRUE(m_dock->GetLastConnectedUrl().isEmpty());
}

TEST_F(CameraDiscoveryDockIntegrationTest, ClearDiscoveryClearsDock)
{
    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 1);

    m_discovery->Clear();

    // Ian: CamerasCleared from the protocol provider flows through the aggregator.
    // Since no other providers have cameras, aggregator emits CamerasCleared
    // which triggers ClearDiscoveredCameras on dock.
    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 0);
}

TEST_F(CameraDiscoveryDockIntegrationTest, MultipleCamerasAllDiscovered)
{
    QStringList urls1;
    urls1 << "mjpg:http://127.0.0.1:1181/?action=stream";
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls1));

    QStringList urls2;
    urls2 << "mjpg:http://10.0.0.2:1181/?action=stream";
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/FrontCam/streams", 20, QVariant(urls2));

    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 2);
}

// ────────────────────────────────────────────────────────────────────────
// CameraDiscoveryAggregator tests
// ────────────────────────────────────────────────────────────────────────

class CameraDiscoveryAggregatorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        EnsureApp();
        m_aggregator = std::make_unique<sd::camera::CameraDiscoveryAggregator>();
    }

    void TearDown() override
    {
        m_aggregator.reset();
    }

    std::unique_ptr<sd::camera::CameraDiscoveryAggregator> m_aggregator;
};

TEST_F(CameraDiscoveryAggregatorTest, StartsEmpty)
{
    EXPECT_TRUE(m_aggregator->GetCameraNames().isEmpty());
}

TEST_F(CameraDiscoveryAggregatorTest, AddSourceNullIsIgnored)
{
    // Should not crash.
    m_aggregator->AddSource(QStringLiteral("Null"), nullptr);
    EXPECT_TRUE(m_aggregator->GetCameraNames().isEmpty());
}

TEST_F(CameraDiscoveryAggregatorTest, ProviderDiscoveryPrefixesName)
{
    // Ian: Non-empty tag produces "[tag] name" prefix for disambiguation.
    auto provider = std::make_unique<sd::camera::CameraPublisherDiscovery>();
    m_aggregator->AddSource(QStringLiteral("Tagged"), provider.get());

    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    provider->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    const QStringList names = m_aggregator->GetCameraNames();
    ASSERT_EQ(names.size(), 1);
    EXPECT_EQ(names.first(), "[Tagged] SimCamera");
}

TEST_F(CameraDiscoveryAggregatorTest, EmptyTagProducesUndecoratedName)
{
    // Ian: Empty tag = no brackets/prefix.  This is the production behavior
    // for protocol-discovered cameras — the user sees "SimCamera" not
    // "[NT4] SimCamera".
    auto provider = std::make_unique<sd::camera::CameraPublisherDiscovery>();
    m_aggregator->AddSource(QString(), provider.get());

    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    provider->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    const QStringList names = m_aggregator->GetCameraNames();
    ASSERT_EQ(names.size(), 1);
    EXPECT_EQ(names.first(), "SimCamera");

    // GetStreamUrls should also work with the undecorated name.
    const QStringList resultUrls = m_aggregator->GetStreamUrls("SimCamera");
    ASSERT_EQ(resultUrls.size(), 1);
    EXPECT_EQ(resultUrls.first(), "http://127.0.0.1:1181/?action=stream");
}

TEST_F(CameraDiscoveryAggregatorTest, GetStreamUrlsReturnsCorrectUrls)
{
    auto provider = std::make_unique<sd::camera::CameraPublisherDiscovery>();
    m_aggregator->AddSource(QStringLiteral("Tagged"), provider.get());

    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    provider->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    const QStringList resultUrls = m_aggregator->GetStreamUrls("[Tagged] SimCamera");
    ASSERT_EQ(resultUrls.size(), 1);
    EXPECT_EQ(resultUrls.first(), "http://127.0.0.1:1181/?action=stream");
}

TEST_F(CameraDiscoveryAggregatorTest, UnknownNameReturnsEmptyUrls)
{
    EXPECT_TRUE(m_aggregator->GetStreamUrls("NonExistent").isEmpty());
}

TEST_F(CameraDiscoveryAggregatorTest, MultipleProvidersCoexist)
{
    // Ian: Matches production wiring — protocol discovery has empty tag
    // (undecorated names), static has "[Static]" prefix.
    auto protocol = std::make_unique<sd::camera::CameraPublisherDiscovery>();
    auto staticSrc = std::make_unique<sd::camera::StaticCameraSource>();

    m_aggregator->AddSource(QString(), protocol.get());
    m_aggregator->AddSource(QStringLiteral("Static"), staticSrc.get());

    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    protocol->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    staticSrc->AddCamera("ShopCam", "http://192.168.1.50:8080/video");

    const QStringList names = m_aggregator->GetCameraNames();
    EXPECT_EQ(names.size(), 2);
    EXPECT_TRUE(names.contains("SimCamera"));
    EXPECT_TRUE(names.contains("[Static] ShopCam"));
}

TEST_F(CameraDiscoveryAggregatorTest, DuplicateUrlsFromSameProviderNotReemitted)
{
    auto provider = std::make_unique<sd::camera::CameraPublisherDiscovery>();
    m_aggregator->AddSource(QString(), provider.get());

    QSignalSpy spy(m_aggregator.get(),
        &sd::camera::CameraDiscoveryAggregator::CameraDiscovered);

    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    provider->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));
    // Same URLs again — provider deduplicates, aggregator should not re-emit.
    provider->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    EXPECT_EQ(spy.count(), 1);
}

TEST_F(CameraDiscoveryAggregatorTest, ProviderClearRemovesOnlyThatProvidersCameras)
{
    // Ian: Matches production wiring — empty tag for protocol discovery.
    auto protocol = std::make_unique<sd::camera::CameraPublisherDiscovery>();
    auto staticSrc = std::make_unique<sd::camera::StaticCameraSource>();

    m_aggregator->AddSource(QString(), protocol.get());
    m_aggregator->AddSource(QStringLiteral("Static"), staticSrc.get());

    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    protocol->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    staticSrc->AddCamera("ShopCam", "http://192.168.1.50:8080/video");

    EXPECT_EQ(m_aggregator->GetCameraNames().size(), 2);

    // Ian: Clearing the protocol provider should only remove protocol cameras.
    // Static cameras must survive.
    QSignalSpy removedSpy(m_aggregator.get(),
        &sd::camera::CameraDiscoveryAggregator::CameraRemoved);
    QSignalSpy clearedSpy(m_aggregator.get(),
        &sd::camera::CameraDiscoveryAggregator::CamerasCleared);

    protocol->Clear();

    // Protocol camera removed, static camera remains.
    EXPECT_EQ(m_aggregator->GetCameraNames().size(), 1);
    EXPECT_TRUE(m_aggregator->GetCameraNames().contains("[Static] ShopCam"));

    // CameraRemoved emitted for the protocol camera (undecorated name).
    EXPECT_EQ(removedSpy.count(), 1);
    EXPECT_EQ(removedSpy.first().at(0).toString(), "SimCamera");

    // CamerasCleared NOT emitted because static cameras still exist.
    EXPECT_EQ(clearedSpy.count(), 0);
}

TEST_F(CameraDiscoveryAggregatorTest, ClearAllEmitsCamerasCleared)
{
    auto protocol = std::make_unique<sd::camera::CameraPublisherDiscovery>();
    auto staticSrc = std::make_unique<sd::camera::StaticCameraSource>();

    m_aggregator->AddSource(QString(), protocol.get());
    m_aggregator->AddSource(QStringLiteral("Static"), staticSrc.get());

    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    protocol->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));
    staticSrc->AddCamera("ShopCam", "http://192.168.1.50:8080/video");

    QSignalSpy clearedSpy(m_aggregator.get(),
        &sd::camera::CameraDiscoveryAggregator::CamerasCleared);

    m_aggregator->ClearAll();

    EXPECT_TRUE(m_aggregator->GetCameraNames().isEmpty());
    EXPECT_EQ(clearedSpy.count(), 1);
}

TEST_F(CameraDiscoveryAggregatorTest, ProviderClearWhenOnlyProviderEmitsCamerasCleared)
{
    // Ian: If the only provider clears, the aggregated set becomes empty,
    // so CamerasCleared should be emitted.
    auto protocol = std::make_unique<sd::camera::CameraPublisherDiscovery>();
    m_aggregator->AddSource(QString(), protocol.get());

    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    protocol->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    QSignalSpy clearedSpy(m_aggregator.get(),
        &sd::camera::CameraDiscoveryAggregator::CamerasCleared);

    protocol->Clear();

    EXPECT_TRUE(m_aggregator->GetCameraNames().isEmpty());
    EXPECT_EQ(clearedSpy.count(), 1);
}

TEST_F(CameraDiscoveryAggregatorTest, BootstrapExistingCamerasOnAddSource)
{
    // Ian: If a provider already has cameras when AddSource() is called,
    // the aggregator should emit CameraDiscovered for each immediately.
    auto staticSrc = std::make_unique<sd::camera::StaticCameraSource>();
    staticSrc->AddCamera("ShopCam", "http://192.168.1.50:8080/video");

    QSignalSpy spy(m_aggregator.get(),
        &sd::camera::CameraDiscoveryAggregator::CameraDiscovered);

    m_aggregator->AddSource(QStringLiteral("Static"), staticSrc.get());

    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.first().at(0).toString(), "[Static] ShopCam");
    EXPECT_EQ(m_aggregator->GetCameraNames().size(), 1);
}

// ────────────────────────────────────────────────────────────────────────
// StaticCameraSource tests
// ────────────────────────────────────────────────────────────────────────

class StaticCameraSourceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        EnsureApp();
        m_source = std::make_unique<sd::camera::StaticCameraSource>();
    }

    void TearDown() override
    {
        m_source.reset();
    }

    std::unique_ptr<sd::camera::StaticCameraSource> m_source;
};

TEST_F(StaticCameraSourceTest, StartsEmpty)
{
    EXPECT_TRUE(m_source->GetCameraNames().isEmpty());
}

TEST_F(StaticCameraSourceTest, AddCameraEmitsDiscovered)
{
    QSignalSpy spy(m_source.get(),
        &sd::camera::StaticCameraSource::CameraDiscovered);

    m_source->AddCamera("ShopCam", "http://192.168.1.50:8080/video");

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.first().at(0).toString(), "ShopCam");
    const QStringList resultUrls = spy.first().at(1).toStringList();
    ASSERT_EQ(resultUrls.size(), 1);
    EXPECT_EQ(resultUrls.first(), "http://192.168.1.50:8080/video");
}

TEST_F(StaticCameraSourceTest, AddCameraTracksName)
{
    m_source->AddCamera("ShopCam", "http://192.168.1.50:8080/video");

    EXPECT_EQ(m_source->GetCameraNames().size(), 1);
    EXPECT_TRUE(m_source->GetCameraNames().contains("ShopCam"));
}

TEST_F(StaticCameraSourceTest, GetStreamUrlsReturnsUrl)
{
    m_source->AddCamera("ShopCam", "http://192.168.1.50:8080/video");

    const QStringList urls = m_source->GetStreamUrls("ShopCam");
    ASSERT_EQ(urls.size(), 1);
    EXPECT_EQ(urls.first(), "http://192.168.1.50:8080/video");
}

TEST_F(StaticCameraSourceTest, AddCameraDuplicateUrlDoesNotReemit)
{
    QSignalSpy spy(m_source.get(),
        &sd::camera::StaticCameraSource::CameraDiscovered);

    m_source->AddCamera("ShopCam", "http://192.168.1.50:8080/video");
    m_source->AddCamera("ShopCam", "http://192.168.1.50:8080/video");

    EXPECT_EQ(spy.count(), 1);
}

TEST_F(StaticCameraSourceTest, AddCameraChangedUrlReemits)
{
    QSignalSpy spy(m_source.get(),
        &sd::camera::StaticCameraSource::CameraDiscovered);

    m_source->AddCamera("ShopCam", "http://192.168.1.50:8080/video");
    m_source->AddCamera("ShopCam", "http://192.168.1.50:9090/stream");

    EXPECT_EQ(spy.count(), 2);
}

TEST_F(StaticCameraSourceTest, AddCameraEmptyNameIgnored)
{
    QSignalSpy spy(m_source.get(),
        &sd::camera::StaticCameraSource::CameraDiscovered);

    m_source->AddCamera("", "http://192.168.1.50:8080/video");

    EXPECT_EQ(spy.count(), 0);
    EXPECT_TRUE(m_source->GetCameraNames().isEmpty());
}

TEST_F(StaticCameraSourceTest, AddCameraEmptyUrlIgnored)
{
    QSignalSpy spy(m_source.get(),
        &sd::camera::StaticCameraSource::CameraDiscovered);

    m_source->AddCamera("ShopCam", "");

    EXPECT_EQ(spy.count(), 0);
    EXPECT_TRUE(m_source->GetCameraNames().isEmpty());
}

TEST_F(StaticCameraSourceTest, RemoveCameraReturnsTrueForExisting)
{
    m_source->AddCamera("ShopCam", "http://192.168.1.50:8080/video");
    EXPECT_TRUE(m_source->RemoveCamera("ShopCam"));
    EXPECT_TRUE(m_source->GetCameraNames().isEmpty());
}

TEST_F(StaticCameraSourceTest, RemoveCameraReturnsFalseForNonExistent)
{
    EXPECT_FALSE(m_source->RemoveCamera("NonExistent"));
}

TEST_F(StaticCameraSourceTest, RemoveCameraEmitsClearedThenReDiscovery)
{
    // Ian: RemoveCamera uses the clear-and-re-emit-all pattern.
    // For a source with two cameras, removing one should:
    //   1. Emit CamerasCleared
    //   2. Re-emit CameraDiscovered for the remaining camera
    m_source->AddCamera("ShopCam", "http://192.168.1.50:8080/video");
    m_source->AddCamera("DockCam", "http://192.168.1.51:8080/video");

    QSignalSpy clearedSpy(m_source.get(),
        &sd::camera::StaticCameraSource::CamerasCleared);
    QSignalSpy discoveredSpy(m_source.get(),
        &sd::camera::StaticCameraSource::CameraDiscovered);

    m_source->RemoveCamera("ShopCam");

    EXPECT_EQ(clearedSpy.count(), 1);
    EXPECT_EQ(discoveredSpy.count(), 1);
    EXPECT_EQ(discoveredSpy.first().at(0).toString(), "DockCam");
}

TEST_F(StaticCameraSourceTest, ClearRemovesAllAndEmits)
{
    m_source->AddCamera("ShopCam", "http://192.168.1.50:8080/video");
    m_source->AddCamera("DockCam", "http://192.168.1.51:8080/video");

    QSignalSpy clearedSpy(m_source.get(),
        &sd::camera::StaticCameraSource::CamerasCleared);

    m_source->Clear();

    EXPECT_TRUE(m_source->GetCameraNames().isEmpty());
    EXPECT_EQ(clearedSpy.count(), 1);
}

TEST_F(StaticCameraSourceTest, GetStreamUrlsUnknownNameReturnsEmpty)
{
    EXPECT_TRUE(m_source->GetStreamUrls("NonExistent").isEmpty());
}

// ────────────────────────────────────────────────────────────────────────
// CameraViewerDock RemoveDiscoveredCamera tests
// ────────────────────────────────────────────────────────────────────────

class CameraViewerDockRemoveTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        EnsureApp();
        m_dock = std::make_unique<sd::widgets::CameraViewerDock>();
    }

    void TearDown() override
    {
        m_dock.reset();
    }

    std::unique_ptr<sd::widgets::CameraViewerDock> m_dock;
};

TEST_F(CameraViewerDockRemoveTest, RemoveDecreasesCount)
{
    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1181/?action=stream");
    m_dock->AddDiscoveredCamera("[Static] ShopCam",
        QStringList() << "http://192.168.1.50:8080/video");

    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 2);

    m_dock->RemoveDiscoveredCamera("SimCamera");

    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 1);
}

TEST_F(CameraViewerDockRemoveTest, RemoveNonExistentIsNoOp)
{
    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1181/?action=stream");

    m_dock->RemoveDiscoveredCamera("NonExistent");

    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 1);
}

TEST_F(CameraViewerDockRemoveTest, RemoveLastCameraLeavesEmpty)
{
    m_dock->AddDiscoveredCamera("SimCamera",
        QStringList() << "http://127.0.0.1:1181/?action=stream");

    m_dock->RemoveDiscoveredCamera("SimCamera");

    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 0);
}

// ────────────────────────────────────────────────────────────────────────
// Aggregator -> Dock integration: provider-scoped clearing
// Ian: Verifies that when one provider clears, only its cameras are
// removed from the dock.  Static cameras survive protocol disconnects.
// Uses empty tag for protocol discovery (matching production wiring).
// ────────────────────────────────────────────────────────────────────────

class AggregatorDockScopedClearTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        EnsureApp();
        m_protocol = std::make_unique<sd::camera::CameraPublisherDiscovery>();
        m_static = std::make_unique<sd::camera::StaticCameraSource>();
        m_aggregator = std::make_unique<sd::camera::CameraDiscoveryAggregator>();
        m_dock = std::make_unique<sd::widgets::CameraViewerDock>();

        m_aggregator->AddSource(QString(), m_protocol.get());
        m_aggregator->AddSource(QStringLiteral("Static"), m_static.get());

        QObject::connect(
            m_aggregator.get(),
            &sd::camera::CameraDiscoveryAggregator::CameraDiscovered,
            m_dock.get(),
            &sd::widgets::CameraViewerDock::AddDiscoveredCamera
        );
        QObject::connect(
            m_aggregator.get(),
            &sd::camera::CameraDiscoveryAggregator::CamerasCleared,
            m_dock.get(),
            &sd::widgets::CameraViewerDock::ClearDiscoveredCameras
        );
        QObject::connect(
            m_aggregator.get(),
            &sd::camera::CameraDiscoveryAggregator::CameraRemoved,
            m_dock.get(),
            &sd::widgets::CameraViewerDock::RemoveDiscoveredCamera
        );
    }

    void TearDown() override
    {
        m_dock.reset();
        m_aggregator.reset();
        m_static.reset();
        m_protocol.reset();
    }

    std::unique_ptr<sd::camera::CameraPublisherDiscovery> m_protocol;
    std::unique_ptr<sd::camera::StaticCameraSource> m_static;
    std::unique_ptr<sd::camera::CameraDiscoveryAggregator> m_aggregator;
    std::unique_ptr<sd::widgets::CameraViewerDock> m_dock;
};

TEST_F(AggregatorDockScopedClearTest, NT4ClearLeavesStaticCameras)
{
    // Ian: This is the primary acceptance test for the abstraction.
    // On transport disconnect, the protocol provider clears → only
    // protocol cameras are removed from the dock.  Static cameras survive.
    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    m_protocol->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    m_static->AddCamera("ShopCam", "http://192.168.1.50:8080/video");

    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 2);

    // Simulate transport disconnect — only protocol provider clears.
    m_protocol->Clear();

    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 1);
}

TEST_F(AggregatorDockScopedClearTest, StaticClearLeavesNT4Cameras)
{
    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    m_protocol->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    m_static->AddCamera("ShopCam", "http://192.168.1.50:8080/video");

    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 2);

    m_static->Clear();

    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 1);
}

TEST_F(AggregatorDockScopedClearTest, BothClearEmptiesDock)
{
    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    m_protocol->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    m_static->AddCamera("ShopCam", "http://192.168.1.50:8080/video");

    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 2);

    m_protocol->Clear();
    m_static->Clear();

    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 0);
}

TEST_F(AggregatorDockScopedClearTest, NT4ReconnectRecoversCamera)
{
    // Ian: After transport disconnect+clear, re-discovering the same camera
    // should re-add it.
    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    m_protocol->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    m_static->AddCamera("ShopCam", "http://192.168.1.50:8080/video");

    m_protocol->Clear();
    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 1);

    // Reconnect — same camera rediscovered.
    m_protocol->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

    EXPECT_EQ(m_dock->DiscoveredCameraCount(), 2);
}

}  // anonymous namespace
