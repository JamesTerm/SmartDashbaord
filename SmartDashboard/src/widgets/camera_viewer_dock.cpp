/// @file camera_viewer_dock.cpp
/// @brief Dockable panel for viewing MJPEG camera streams.

#include "widgets/camera_viewer_dock.h"
#include "widgets/camera_display_widget.h"
#include "camera/mjpeg_stream_source.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace sd::widgets
{

CameraViewerDock::CameraViewerDock(QWidget* parent)
    : QDockWidget(QStringLiteral("Camera"), parent)
{
    setObjectName(QStringLiteral("cameraViewerDock"));
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFeatures(DockWidgetMovable | DockWidgetFloatable | DockWidgetClosable);

    m_streamSource = new sd::camera::MjpegStreamSource(this);

    // Ian: Auto-reconnect timer — single-shot, fires OnReconnectTimer() after
    // kReconnectDelayMs.  Only started when the stream drops unexpectedly and
    // the user hasn't manually disconnected.
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(kReconnectDelayMs);
    connect(m_reconnectTimer, &QTimer::timeout, this, &CameraViewerDock::OnReconnectTimer);

    SetupUi();

    // Ian: Connect stream source signals to dock slots.
    connect(
        m_streamSource,
        &sd::camera::CameraStreamSource::StateChanged,
        this,
        &CameraViewerDock::OnStreamStateChanged
    );
    connect(
        m_streamSource,
        &sd::camera::CameraStreamSource::FrameReady,
        this,
        &CameraViewerDock::OnFrameReceived
    );

    // Ian: When the dock becomes visible and there's a discovered camera but
    // no active stream, populate the URL field for convenience — but do NOT
    // auto-connect.  The user must click Connect manually.
    connect(
        this,
        &QDockWidget::visibilityChanged,
        this,
        [this](bool visible)
        {
            Q_UNUSED(visible);
            // Ian: Auto-connect removed.  The user clicks Connect explicitly.
        }
    );
}

CameraViewerDock::~CameraViewerDock()
{
    m_reconnectTimer->stop();
    StopStream();
}

void CameraViewerDock::SetupUi()
{
    auto* mainWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(mainWidget);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(2);

    // Toolbar row: camera selector + buttons + reticle.
    auto* toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(4);

    m_cameraCombo = new QComboBox();
    m_cameraCombo->setPlaceholderText(QStringLiteral("Select camera..."));
    m_cameraCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_cameraCombo->setMinimumWidth(100);
    toolbarLayout->addWidget(m_cameraCombo);

    m_connectButton = new QPushButton(QStringLiteral("Connect"));
    m_connectButton->setFixedWidth(70);
    toolbarLayout->addWidget(m_connectButton);

    m_disconnectButton = new QPushButton(QStringLiteral("Disconnect"));
    m_disconnectButton->setFixedWidth(80);
    m_disconnectButton->setEnabled(false);
    toolbarLayout->addWidget(m_disconnectButton);

    m_reticleCheckBox = new QCheckBox(QStringLiteral("Reticle"));
    toolbarLayout->addWidget(m_reticleCheckBox);

    mainLayout->addLayout(toolbarLayout);

    // Ian: URL row — full width on its own line so the URL is actually readable.
    // Previously it was crammed into the toolbar row and too narrow to show anything.
    auto* urlLayout = new QHBoxLayout();
    urlLayout->setSpacing(4);

    auto* urlLabel = new QLabel(QStringLiteral("URL:"));
    urlLayout->addWidget(urlLabel);

    m_urlEdit = new QLineEdit();
    m_urlEdit->setPlaceholderText(QStringLiteral("mjpeg:// stream URL"));
    m_urlEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    urlLayout->addWidget(m_urlEdit);

    mainLayout->addLayout(urlLayout);

    // Status label.
    m_statusLabel = new QLabel(QStringLiteral("Disconnected"));
    m_statusLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    mainLayout->addWidget(m_statusLabel);

    // Display widget (takes all remaining space).
    m_displayWidget = new CameraDisplayWidget(mainWidget);
    mainLayout->addWidget(m_displayWidget, 1);  // stretch factor = 1

    setWidget(mainWidget);

    // Connect toolbar signals.
    connect(m_connectButton, &QPushButton::clicked, this, &CameraViewerDock::OnConnectClicked);
    connect(m_disconnectButton, &QPushButton::clicked, this, &CameraViewerDock::OnDisconnectClicked);
    connect(m_cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CameraViewerDock::OnCameraSelected);

    connect(
        m_reticleCheckBox,
        &QCheckBox::toggled,
        this,
        [this](bool checked)
        {
            if (m_displayWidget != nullptr)
            {
                m_displayWidget->SetReticleVisible(checked);
            }
        }
    );

    // Ian: Allow pressing Enter in the URL field to connect.
    connect(
        m_urlEdit,
        &QLineEdit::returnPressed,
        this,
        &CameraViewerDock::OnConnectClicked
    );
}

void CameraViewerDock::StopStream()
{
    // Ian: StopStream is called by MainWindow during transport shutdown.
    // Suppress auto-reconnect — the transport is going away, so there's
    // nothing to reconnect to.
    m_reconnectTimer->stop();
    m_userDisconnected = true;

    if (m_streamSource != nullptr)
    {
        m_streamSource->Stop();
    }
    if (m_displayWidget != nullptr)
    {
        m_displayWidget->ClearFrame();
        m_displayWidget->SetStatusMessage(QStringLiteral("Disconnected"));
    }
    UpdateButtonStates();
}

void CameraViewerDock::ClearDiscoveredCameras()
{
    // Ian: Called on transport disconnect.  Clear everything and suppress
    // auto-reconnect — we can't reach the camera server anymore.
    m_reconnectTimer->stop();
    m_userDisconnected = true;
    m_lastConnectedUrl.clear();

    m_discoveredCameras.clear();
    if (m_cameraCombo != nullptr)
    {
        m_cameraCombo->clear();
    }
}

void CameraViewerDock::AddDiscoveredCamera(const QString& name, const QStringList& urls)
{
    m_discoveredCameras[name] = urls;
    if (m_cameraCombo != nullptr)
    {
        // Ian: Check if this camera is already in the combo (update case).
        const int existingIdx = m_cameraCombo->findText(name);
        if (existingIdx >= 0)
        {
            // Update the stored URL data for the existing entry.
            m_cameraCombo->setItemData(existingIdx, urls.isEmpty() ? QString() : urls.first());
        }
        else
        {
            m_cameraCombo->addItem(name, urls.isEmpty() ? QString() : urls.first());
        }
    }

    // Ian: Auto-connect removed — the user must click Connect manually.
    // We still clear the manual-disconnect flag on discovery so that
    // auto-reconnect (after stream error) works if the user has previously
    // connected and the stream drops.
    m_userDisconnected = false;
}

int CameraViewerDock::DiscoveredCameraCount() const
{
    return m_discoveredCameras.size();
}

void CameraViewerDock::OnConnectClicked()
{
    // Ian: User explicitly clicked Connect — clear the manual-disconnect flag
    // so auto-reconnect works if the stream drops later.
    m_userDisconnected = false;
    m_reconnectTimer->stop();

    // Ian: Priority: if the URL field has text, use that.
    // Otherwise, use the selected camera's URL from the combo.
    QString url = m_urlEdit->text().trimmed();

    if (url.isEmpty() && m_cameraCombo->currentIndex() >= 0)
    {
        url = m_cameraCombo->currentData().toString();
    }

    if (url.isEmpty())
    {
        m_statusLabel->setText(QStringLiteral("No URL specified"));
        return;
    }

    ConnectToUrl(url);
}

void CameraViewerDock::OnDisconnectClicked()
{
    // Ian: User explicitly clicked Disconnect — suppress auto-reconnect.
    // This follows the same pattern as MainWindow's m_userDisconnected for
    // the transport reconnect timer: manual disconnect = stay disconnected.
    m_reconnectTimer->stop();
    m_userDisconnected = true;

    if (m_streamSource != nullptr)
    {
        m_streamSource->Stop();
    }
    if (m_displayWidget != nullptr)
    {
        m_displayWidget->ClearFrame();
        m_displayWidget->SetStatusMessage(QStringLiteral("Disconnected"));
    }
    UpdateButtonStates();
}

void CameraViewerDock::OnCameraSelected(int index)
{
    if (index < 0 || m_cameraCombo == nullptr)
    {
        return;
    }

    // Ian: When the user selects a camera from the combo, populate the URL
    // field with the first stream URL.  Don't auto-connect — let the user
    // click Connect explicitly (or rely on auto-connect from discovery).
    const QString url = m_cameraCombo->itemData(index).toString();
    if (!url.isEmpty() && m_urlEdit != nullptr)
    {
        m_urlEdit->setText(url);
    }
}

void CameraViewerDock::OnStreamStateChanged(sd::camera::CameraStreamSource::State newState)
{
    switch (newState)
    {
        case sd::camera::CameraStreamSource::State::Disconnected:
            m_statusLabel->setText(QStringLiteral("Disconnected"));
            if (m_displayWidget != nullptr)
            {
                m_displayWidget->SetStatusMessage(QStringLiteral("Disconnected"));
            }
            break;

        case sd::camera::CameraStreamSource::State::Connecting:
            m_statusLabel->setText(QStringLiteral("Connecting..."));
            if (m_displayWidget != nullptr)
            {
                m_displayWidget->SetStatusMessage(QStringLiteral("Connecting..."));
            }
            break;

        case sd::camera::CameraStreamSource::State::Streaming:
            m_statusLabel->setText(QStringLiteral("Streaming"));
            // Ian: Stream is live — stop any pending reconnect timer.
            m_reconnectTimer->stop();
            break;

        case sd::camera::CameraStreamSource::State::Error:
        {
            const QString errorMsg = m_streamSource != nullptr
                ? m_streamSource->GetLastError()
                : QStringLiteral("Unknown error");
            m_statusLabel->setText(QStringLiteral("Error: %1").arg(errorMsg));
            if (m_displayWidget != nullptr)
            {
                m_displayWidget->SetStatusMessage(QStringLiteral("Error: %1").arg(errorMsg));
                m_displayWidget->ClearFrame();
            }

            // Ian: Stream error — schedule auto-reconnect if:
            //   1. User hasn't manually disconnected
            //   2. We have a URL to reconnect to
            // The timer is single-shot so we don't hammer a dead server.
            if (!m_userDisconnected && !m_lastConnectedUrl.isEmpty())
            {
                m_statusLabel->setText(
                    QStringLiteral("Error: %1 (reconnecting...)").arg(errorMsg));
                if (m_displayWidget != nullptr)
                {
                    m_displayWidget->SetStatusMessage(
                        QStringLiteral("Reconnecting in %1s...").arg(kReconnectDelayMs / 1000));
                }
                m_reconnectTimer->start();
            }
            break;
        }
    }

    UpdateButtonStates();
}

void CameraViewerDock::OnFrameReceived(const QImage& frame)
{
    if (m_displayWidget != nullptr)
    {
        m_displayWidget->OnFrameReady(frame);
        m_displayWidget->SetFpsDisplay(
            m_streamSource != nullptr ? m_streamSource->GetFps() : 0.0
        );
    }
}

void CameraViewerDock::OnReconnectTimer()
{
    // Ian: Auto-reconnect fired.  If the user hasn't manually disconnected
    // and we still have a URL, try to reconnect.
    if (m_userDisconnected || m_lastConnectedUrl.isEmpty())
    {
        return;
    }

    if (m_streamSource != nullptr
        && m_streamSource->GetState() != sd::camera::CameraStreamSource::State::Disconnected
        && m_streamSource->GetState() != sd::camera::CameraStreamSource::State::Error)
    {
        // Already connected or connecting — don't double-connect.
        return;
    }

    ConnectToUrl(m_lastConnectedUrl);
}

void CameraViewerDock::UpdateButtonStates()
{
    const bool isStreaming = m_streamSource != nullptr
        && (m_streamSource->GetState() == sd::camera::CameraStreamSource::State::Connecting
            || m_streamSource->GetState() == sd::camera::CameraStreamSource::State::Streaming);

    if (m_connectButton != nullptr)
    {
        m_connectButton->setEnabled(!isStreaming);
    }
    if (m_disconnectButton != nullptr)
    {
        m_disconnectButton->setEnabled(isStreaming);
    }
    if (m_urlEdit != nullptr)
    {
        m_urlEdit->setEnabled(!isStreaming);
    }
    if (m_cameraCombo != nullptr)
    {
        m_cameraCombo->setEnabled(!isStreaming);
    }
}

void CameraViewerDock::ConnectToUrl(const QString& url)
{
    if (m_streamSource == nullptr)
    {
        return;
    }

    // Ian: Strip the "mjpg:" prefix that CameraPublisher uses in stream URLs.
    // The raw HTTP URL is what QNetworkAccessManager needs.
    QString cleanUrl = url;
    if (cleanUrl.startsWith(QStringLiteral("mjpg:"), Qt::CaseInsensitive))
    {
        cleanUrl = cleanUrl.mid(5);
    }

    // Ian: Remember the URL for auto-reconnect.
    m_lastConnectedUrl = cleanUrl;

    if (m_displayWidget != nullptr)
    {
        m_displayWidget->ClearFrame();
    }

    m_streamSource->Start(cleanUrl);
}

void CameraViewerDock::TryAutoConnect()
{
    // Ian: Auto-connect disabled.  The user must click Connect manually.
    // This method is kept as a no-op so that call sites don't need to be
    // wrapped in #ifdefs.  Auto-reconnect after stream errors still works
    // via OnReconnectTimer() if the user has previously connected.
    Q_UNUSED(this);
}

}  // namespace sd::widgets
