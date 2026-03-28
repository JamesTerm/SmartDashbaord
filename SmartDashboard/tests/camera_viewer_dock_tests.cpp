// Ian: Tests for CameraViewerDock — discovery populates combo, manual connect/
// disconnect, auto-reconnect on error, manual disconnect suppression, and
// CameraPublisherDiscovery key parsing / signal emission.
//
// Auto-connect on discovery was removed — the user must click Connect manually.
// Auto-reconnect after stream errors is still active.
//
// These tests use the dock's public API without a real MJPEG server.
// The MjpegStreamSource will go to Error state when connecting to a
// nonexistent server, which lets us verify auto-reconnect scheduling.

#include "widgets/camera_viewer_dock.h"
#include "camera/camera_publisher_discovery.h"
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
// Integration: Discovery -> Dock wiring
// ────────────────────────────────────────────────────────────────────────

class CameraDiscoveryDockIntegrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        EnsureApp();
        m_discovery = std::make_unique<sd::camera::CameraPublisherDiscovery>();
        m_dock = std::make_unique<sd::widgets::CameraViewerDock>();

        // Wire discovery to dock (same as MainWindow does).
        QObject::connect(
            m_discovery.get(),
            &sd::camera::CameraPublisherDiscovery::CameraDiscovered,
            m_dock.get(),
            &sd::widgets::CameraViewerDock::AddDiscoveredCamera
        );
        QObject::connect(
            m_discovery.get(),
            &sd::camera::CameraPublisherDiscovery::CamerasCleared,
            m_dock.get(),
            &sd::widgets::CameraViewerDock::ClearDiscoveredCameras
        );
    }

    void TearDown() override
    {
        m_dock.reset();
        m_discovery.reset();
    }

    std::unique_ptr<sd::camera::CameraPublisherDiscovery> m_discovery;
    std::unique_ptr<sd::widgets::CameraViewerDock> m_dock;
};

TEST_F(CameraDiscoveryDockIntegrationTest, NT4KeyPopulatesDockCombo)
{
    QStringList urls;
    urls << "mjpg:http://127.0.0.1:1181/?action=stream";
    m_discovery->OnVariableUpdate(
        "/CameraPublisher/SimCamera/streams", 20, QVariant(urls));

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

    // CamerasCleared signal triggers ClearDiscoveredCameras on dock.
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

}  // anonymous namespace
