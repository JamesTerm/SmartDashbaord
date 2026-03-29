#pragma once

/// @file camera_discovery_aggregator.h
/// @brief Merges camera entries from multiple ICameraDiscoverySource providers.
///
/// Ian: The aggregator is the single point of contact for the CameraViewerDock.
/// It subscribes to every registered provider's CameraDiscovered / CamerasCleared
/// signals and re-emits a unified stream.  Each provider has an internal tag used
/// to track ownership during scoped clearing.  The tag also controls the display
/// name: a non-empty tag adds a "[tag] " prefix (e.g. "[Static] ShopCam") while
/// an empty tag shows the camera name undecorated (e.g. "SimCamera").
/// When a provider clears, only that provider's cameras are removed from the
/// aggregated set; other providers' cameras remain.

#include "camera/camera_discovery_source.h"

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sd::camera
{
    /// @brief Aggregates cameras from multiple discovery providers.
    ///
    /// Providers are registered via AddSource().  The aggregator does NOT
    /// take ownership of the provider — the caller must ensure the provider
    /// outlives the aggregator (or parent both to the same QObject).
    class CameraDiscoveryAggregator final : public QObject
    {
        Q_OBJECT

    public:
        explicit CameraDiscoveryAggregator(QObject* parent = nullptr);

        /// @brief Register a discovery provider.
        /// @param tag Short label for this provider (e.g. "Static").
        ///            Non-empty tags prefix camera names in the display output
        ///            (e.g. "[Static] ShopCam").  An empty tag means cameras
        ///            from this provider appear undecorated (e.g. "SimCamera").
        ///            Internally the tag is always used for ownership tracking
        ///            during scoped clearing, even when empty.
        /// @param source The provider.  Must outlive the aggregator.
        void AddSource(const QString& tag, ICameraDiscoverySource* source);

        /// @brief Return all aggregated camera names.
        QStringList GetCameraNames() const;

        /// @brief Return stream URLs for an aggregated camera name.
        QStringList GetStreamUrls(const QString& aggregatedName) const;

        /// @brief Clear all providers and the aggregated camera set.
        void ClearAll();

    signals:
        /// @brief Emitted when any provider discovers or updates a camera.
        /// @param name Aggregated camera name (may or may not have a tag prefix).
        /// @param urls Stream URLs.
        void CameraDiscovered(const QString& name, const QStringList& urls);

        /// @brief Emitted when the aggregated camera set is fully cleared.
        /// Only emitted when ClearAll() is called.
        void CamerasCleared();

        /// @brief Emitted when a single camera is removed (provider cleared
        /// that camera, but other providers still have cameras).
        void CameraRemoved(const QString& name);

    private:
        void OnProviderCameraDiscovered(const QString& tag, const QString& name, const QStringList& urls);
        void OnProviderCamerasCleared(const QString& tag);

        static QString MakeAggregatedName(const QString& tag, const QString& name);

        struct ProviderEntry
        {
            QString tag;
            ICameraDiscoverySource* source = nullptr;
        };

        QVector<ProviderEntry> m_providers;

        // Ian: Aggregated camera map.  Key is the display name (e.g. "SimCamera"
        // or "[Static] ShopCam"), value is the URL list.
        QMap<QString, QStringList> m_cameras;

        // Ian: Tracks which provider tag owns each aggregated camera name.
        // Used by OnProviderCamerasCleared() to identify which cameras to remove
        // without relying on name prefix parsing — this decouples display naming
        // from ownership tracking and allows empty-tag providers to work correctly.
        QMap<QString, QString> m_cameraOwner;

        // Ian: Guard flag to prevent double CamerasCleared emission during ClearAll().
        // When true, OnProviderCamerasCleared won't emit CamerasCleared even if the
        // set becomes empty — ClearAll() emits it once at the end instead.
        bool m_clearingAll = false;
    };
}
