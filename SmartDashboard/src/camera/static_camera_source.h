#pragma once

/// @file static_camera_source.h
/// @brief Camera discovery provider for user-configured static URLs.
///
/// Ian: This provider allows teams to manually add camera URLs that persist
/// across sessions via QSettings (Windows Registry).  This is essential for
/// Direct and Native Link transports that don't deliver /CameraPublisher/
/// keys — teams can still use the camera viewer by entering URLs manually.
/// Static cameras survive transport switches and reconnects.

#include "camera/camera_discovery_source.h"

#include <QMap>
#include <QString>
#include <QStringList>

namespace sd::camera
{
    /// @brief Camera discovery provider for manually configured URLs.
    ///
    /// Cameras are added/removed via AddCamera() / RemoveCamera() and
    /// persisted to QSettings under "camera/static/<name>".
    class StaticCameraSource final : public ICameraDiscoverySource
    {
        Q_OBJECT

    public:
        explicit StaticCameraSource(QObject* parent = nullptr);

        /// @brief Add or update a static camera.
        /// @param name User-chosen camera name.
        /// @param url Stream URL (raw HTTP, no mjpg: prefix needed).
        ///
        /// Emits CameraDiscovered if the URL changed or the camera is new.
        /// Persists to QSettings immediately.
        void AddCamera(const QString& name, const QString& url);

        /// @brief Remove a static camera by name.
        /// @return true if the camera existed and was removed.
        ///
        /// Persists the removal to QSettings immediately.
        bool RemoveCamera(const QString& name);

        /// @brief Load all static cameras from QSettings.
        /// Called once at startup after construction.
        void LoadFromSettings();

        // ICameraDiscoverySource overrides.
        void Clear() override;
        QStringList GetCameraNames() const override;
        QStringList GetStreamUrls(const QString& cameraName) const override;

    private:
        void PersistToSettings() const;

        // Ian: Map of camera name -> single URL (wrapped in QStringList for
        // interface compatibility).  Static cameras always have exactly one URL.
        QMap<QString, QStringList> m_cameras;
    };
}
