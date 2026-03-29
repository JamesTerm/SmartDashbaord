/// @file camera_discovery_aggregator.cpp
/// @brief Merges camera entries from multiple ICameraDiscoverySource providers.

#include "camera/camera_discovery_aggregator.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace sd::camera
{

CameraDiscoveryAggregator::CameraDiscoveryAggregator(QObject* parent)
    : QObject(parent)
{
}

void CameraDiscoveryAggregator::AddSource(const QString& tag, ICameraDiscoverySource* source)
{
    if (source == nullptr)
    {
        return;
    }

    m_providers.append({ tag, source });

    // Ian: Capture the tag by value in the lambdas so each provider's
    // signals route through the aggregator with the correct prefix.
    connect(
        source,
        &ICameraDiscoverySource::CameraDiscovered,
        this,
        [this, tag](const QString& name, const QStringList& urls)
        {
            OnProviderCameraDiscovered(tag, name, urls);
        }
    );
    connect(
        source,
        &ICameraDiscoverySource::CamerasCleared,
        this,
        [this, tag]()
        {
            OnProviderCamerasCleared(tag);
        }
    );

    // Ian: Bootstrap — if the provider already has cameras (e.g. static source
    // loaded from settings before aggregator is wired), emit them now.
    const QStringList existingNames = source->GetCameraNames();
    for (const QString& name : existingNames)
    {
        OnProviderCameraDiscovered(tag, name, source->GetStreamUrls(name));
    }
}

QStringList CameraDiscoveryAggregator::GetCameraNames() const
{
    return m_cameras.keys();
}

QStringList CameraDiscoveryAggregator::GetStreamUrls(const QString& aggregatedName) const
{
    return m_cameras.value(aggregatedName);
}

void CameraDiscoveryAggregator::ClearAll()
{
    // Ian: Suppress per-provider CamerasCleared emissions during bulk clear.
    // Without this, the last provider's Clear() can trigger CamerasCleared
    // through OnProviderCamerasCleared (when the set becomes empty), and then
    // we'd emit it again here — a double emission.
    m_clearingAll = true;

    for (const auto& entry : m_providers)
    {
        if (entry.source != nullptr)
        {
            entry.source->Clear();
        }
    }

    m_clearingAll = false;
    m_cameras.clear();
    m_cameraOwner.clear();
    emit CamerasCleared();
}

void CameraDiscoveryAggregator::OnProviderCameraDiscovered(
    const QString& tag, const QString& name, const QStringList& urls)
{
    const QString aggregatedName = MakeAggregatedName(tag, name);

#ifdef _WIN32
    // Ian: Debug-only trace — visible in Visual Studio Output window.
    // Production users never see the provider tag, but developers can
    // trace which provider discovered each camera during debugging.
    OutputDebugStringW(
        QStringLiteral("[CameraAggregator] provider=\"%1\" camera=\"%2\" display=\"%3\" urls=%4\n")
            .arg(tag.isEmpty() ? QStringLiteral("(default)") : tag,
                 name,
                 aggregatedName,
                 urls.join(QStringLiteral(", ")))
            .toStdWString()
            .c_str());
#endif

    // Ian: Only emit if the URLs actually changed (same dedup as the old
    // CameraPublisherDiscovery).
    if (m_cameras.value(aggregatedName) != urls)
    {
        m_cameras[aggregatedName] = urls;
        m_cameraOwner[aggregatedName] = tag;
        emit CameraDiscovered(aggregatedName, urls);
    }
}

void CameraDiscoveryAggregator::OnProviderCamerasCleared(const QString& tag)
{
    // Ian: Remove only cameras owned by this provider.  Other providers'
    // cameras survive.  Ownership is tracked in m_cameraOwner rather than
    // inferred from the display name prefix — this allows empty-tag
    // providers (whose cameras have no "[tag] " prefix) to work correctly.
    QStringList toRemove;

    for (auto it = m_cameraOwner.begin(); it != m_cameraOwner.end(); ++it)
    {
        if (it.value() == tag)
        {
            toRemove.append(it.key());
        }
    }

    for (const QString& key : toRemove)
    {
        m_cameras.remove(key);
        m_cameraOwner.remove(key);
        emit CameraRemoved(key);
    }

    // Ian: Only emit CamerasCleared if the aggregated set is now completely empty
    // and we're not in the middle of a ClearAll() bulk operation.
    // This matches the dock's expectation: CamerasCleared means "no cameras at all."
    if (m_cameras.isEmpty() && !m_clearingAll)
    {
        emit CamerasCleared();
    }
}

QString CameraDiscoveryAggregator::MakeAggregatedName(const QString& tag, const QString& name)
{
    // Ian: Empty tag = no prefix — camera appears undecorated in the UI.
    // Non-empty tag = bracketed prefix for disambiguation (e.g. "[Static] ShopCam").
    if (tag.isEmpty())
    {
        return name;
    }
    return QStringLiteral("[%1] %2").arg(tag, name);
}

}  // namespace sd::camera
