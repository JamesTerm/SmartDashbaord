#pragma once

/// @file camera_viewer_dock.h
/// @brief Dockable panel for viewing MJPEG camera streams.
///
/// Contains a CameraDisplayWidget for video rendering, and a toolbar with
/// camera selector combo, manual URL input, connect/disconnect buttons,
/// and a reticle toggle.
///
/// Ian: Follows the same dock pattern as RunBrowserDock:
///   - Object name for Qt state save/restore
///   - View menu toggle action synced via visibilityChanged
///   - Starts hidden, AllDockWidgetAreas allowed
///   - Created in MainWindow::SetupUi after the Run Browser dock
///
/// Camera stream lifecycle ties to transport lifecycle:
///   - StopTransport() -> StopStream() (stop camera stream)
///   - Disconnect -> ClearDiscoveredCameras() (clear camera selector)
///   This follows the same pattern as RunBrowserDock's ClearDiscoveredKeys.

#include <QDockWidget>
#include <QMap>
#include <QString>
#include <QStringList>

#include "camera/camera_stream_source.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

namespace sd::camera
{
    class MjpegStreamSource;
}

namespace sd::widgets
{
    class CameraDisplayWidget;

    /// @brief Dockable panel for viewing camera streams.
    ///
    /// Ian: Auto-connect behavior — three triggers:
    ///   1. Camera discovered via NT4 and dock is idle → connect immediately
    ///   2. MJPEG stream drops (error/server close) → retry after 2 seconds
    ///   3. Dock becomes visible and a camera is available → connect if idle
    /// The user can always disconnect manually, which suppresses auto-reconnect
    /// until a new camera is discovered or the dock is re-shown.
    class CameraViewerDock final : public QDockWidget
    {
        Q_OBJECT

    public:
        explicit CameraViewerDock(QWidget* parent = nullptr);
        ~CameraViewerDock() override;

        /// @brief Stop the current camera stream (if any).
        ///
        /// Ian: Called by MainWindow when StopTransport() runs.  This ensures
        /// the camera stream is torn down at the same lifecycle point as the
        /// data transport.  Suppresses auto-reconnect.
        void StopStream();

        /// @brief Clear all discovered cameras from the selector combo.
        ///
        /// Ian: Called by MainWindow on disconnect, following the same pattern
        /// as RunBrowserDock::ClearDiscoveredKeys().  Suppresses auto-reconnect.
        void ClearDiscoveredCameras();

        /// @brief Add a discovered camera to the selector combo.
        /// @param name Human-readable camera name (e.g. "USB Camera").
        /// @param urls Available stream URLs for this camera.
        ///
        /// Ian: If the dock is idle (not streaming or manually disconnected),
        /// auto-connects to the first URL.  This is the primary auto-connect
        /// trigger — the camera just shows up when the simulator starts.
        void AddDiscoveredCamera(const QString& name, const QStringList& urls);

        /// @brief Return the number of discovered cameras.
        int DiscoveredCameraCount() const;

        /// @brief Return the current auto-reconnect URL (for testing).
        QString GetLastConnectedUrl() const { return m_lastConnectedUrl; }

        /// @brief Return whether auto-reconnect is suppressed (for testing).
        bool IsAutoReconnectSuppressed() const { return m_userDisconnected; }

    private slots:
        void OnConnectClicked();
        void OnDisconnectClicked();
        void OnCameraSelected(int index);
        void OnStreamStateChanged(sd::camera::CameraStreamSource::State newState);
        void OnFrameReceived(const QImage& frame);
        void OnReconnectTimer();

    private:
        void SetupUi();
        void UpdateButtonStates();
        void ConnectToUrl(const QString& url);
        void TryAutoConnect();

        // Toolbar widgets.
        QComboBox* m_cameraCombo = nullptr;
        QLineEdit* m_urlEdit = nullptr;
        QPushButton* m_connectButton = nullptr;
        QPushButton* m_disconnectButton = nullptr;
        QCheckBox* m_reticleCheckBox = nullptr;
        QLabel* m_statusLabel = nullptr;

        // Display widget.
        CameraDisplayWidget* m_displayWidget = nullptr;

        // Stream source.
        sd::camera::MjpegStreamSource* m_streamSource = nullptr;

        // Discovered cameras: name -> list of URLs.
        QMap<QString, QStringList> m_discoveredCameras;

        // Ian: Auto-connect / auto-reconnect state.
        //
        // m_lastConnectedUrl: the URL we last connected (or attempted) to.
        //   Used for auto-reconnect after transient errors.
        //
        // m_userDisconnected: set true when the user clicks Disconnect manually.
        //   Suppresses auto-reconnect until a new camera is discovered, the dock
        //   is re-shown, or the user clicks Connect.  Follows the same pattern as
        //   MainWindow's m_userDisconnected for the transport reconnect timer.
        //
        // m_reconnectTimer: single-shot 2-second timer for reconnect after error.
        //   Non-repeating to avoid hammering a dead server.  Restarted on each
        //   error/disconnect if auto-reconnect is not suppressed.
        QString m_lastConnectedUrl;
        bool m_userDisconnected = false;
        QTimer* m_reconnectTimer = nullptr;

        static constexpr int kReconnectDelayMs = 2000;
    };
}
