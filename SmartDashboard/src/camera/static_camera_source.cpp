/// @file static_camera_source.cpp
/// @brief Camera discovery provider for user-configured static URLs.

#include "camera/static_camera_source.h"

#include <QSettings>

namespace sd::camera
{

StaticCameraSource::StaticCameraSource(QObject* parent)
    : ICameraDiscoverySource(parent)
{
}

void StaticCameraSource::AddCamera(const QString& name, const QString& url)
{
    if (name.isEmpty() || url.isEmpty())
    {
        return;
    }

    const QStringList urls = { url };

    // Ian: Only emit if URLs actually changed (same dedup pattern as
    // CameraPublisherDiscovery).
    if (m_cameras.value(name) != urls)
    {
        m_cameras[name] = urls;
        PersistToSettings();
        emit CameraDiscovered(name, urls);
    }
}

bool StaticCameraSource::RemoveCamera(const QString& name)
{
    if (m_cameras.remove(name) > 0)
    {
        PersistToSettings();
        // Ian: No individual CameraRemoved signal on the interface — the
        // aggregator handles removal via CamerasCleared + re-bootstrap.
        // For a single removal, clear and re-emit all remaining cameras.
        // This is simple and correct for the small number of static cameras
        // a team would configure.
        emit CamerasCleared();
        for (auto it = m_cameras.constBegin(); it != m_cameras.constEnd(); ++it)
        {
            emit CameraDiscovered(it.key(), it.value());
        }
        return true;
    }
    return false;
}

void StaticCameraSource::LoadFromSettings()
{
    QSettings settings(QStringLiteral("SmartDashboard"), QStringLiteral("SmartDashboardApp"));
    settings.beginGroup(QStringLiteral("camera/static"));

    const QStringList names = settings.childKeys();
    for (const QString& name : names)
    {
        const QString url = settings.value(name).toString().trimmed();
        if (!url.isEmpty())
        {
            m_cameras[name] = { url };
        }
    }

    settings.endGroup();

    // Ian: Emit discovery signals for all loaded cameras so the aggregator
    // picks them up during bootstrap.
    for (auto it = m_cameras.constBegin(); it != m_cameras.constEnd(); ++it)
    {
        emit CameraDiscovered(it.key(), it.value());
    }
}

void StaticCameraSource::Clear()
{
    m_cameras.clear();
    PersistToSettings();
    emit CamerasCleared();
}

QStringList StaticCameraSource::GetCameraNames() const
{
    return m_cameras.keys();
}

QStringList StaticCameraSource::GetStreamUrls(const QString& cameraName) const
{
    return m_cameras.value(cameraName);
}

void StaticCameraSource::PersistToSettings() const
{
    QSettings settings(QStringLiteral("SmartDashboard"), QStringLiteral("SmartDashboardApp"));
    settings.beginGroup(QStringLiteral("camera/static"));

    // Ian: Clear existing keys and rewrite.  With a small number of static
    // cameras this is simpler and more reliable than incremental updates.
    settings.remove(QStringLiteral(""));

    for (auto it = m_cameras.constBegin(); it != m_cameras.constEnd(); ++it)
    {
        if (!it.value().isEmpty())
        {
            settings.setValue(it.key(), it.value().first());
        }
    }

    settings.endGroup();
}

}  // namespace sd::camera
