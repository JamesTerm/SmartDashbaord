#pragma once

/// @file camera_discovery_source.h
/// @brief Abstract interface for camera discovery providers.
///
/// Ian: Follows the same QObject-based abstract interface pattern as
/// CameraStreamSource.  Concrete providers (NT4 /CameraPublisher/ keys,
/// manual/static URLs, mDNS, etc.) implement this interface and feed
/// camera entries into CameraDiscoveryAggregator.  The aggregator merges
/// results from all providers so the dock doesn't care where cameras come
/// from.  Keep this interface transport-agnostic.

#include <QObject>
#include <QString>
#include <QStringList>

namespace sd::camera
{
    /// @brief Abstract base for camera discovery providers.
    ///
    /// Each provider manages its own set of discovered cameras and emits
    /// CameraDiscovered / CamerasCleared signals when the set changes.
    /// The aggregator subscribes to these signals and re-emits the union.
    class ICameraDiscoverySource : public QObject
    {
        Q_OBJECT

    public:
        explicit ICameraDiscoverySource(QObject* parent = nullptr)
            : QObject(parent)
        {
        }

        ~ICameraDiscoverySource() override = default;

        /// @brief Return the names of all cameras currently known to this provider.
        virtual QStringList GetCameraNames() const = 0;

        /// @brief Return stream URLs for a given camera name.
        virtual QStringList GetStreamUrls(const QString& cameraName) const = 0;

        /// @brief Clear all discovered cameras (e.g. on disconnect).
        virtual void Clear() = 0;

    signals:
        /// @brief Emitted when a camera is discovered or its streams change.
        /// @param name Camera name (must be unique within this provider).
        /// @param urls Stream URLs.
        void CameraDiscovered(const QString& name, const QStringList& urls);

        /// @brief Emitted when all cameras from this provider are cleared.
        void CamerasCleared();
    };
}
