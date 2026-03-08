#include "transport/direct_publisher_adapter.h"

DirectPublisherAdapter::DirectPublisherAdapter(QObject* parent)
    : QObject(parent)
{
    sd::direct::PublisherConfig config;
    config.mappingName = L"Local\\SmartDashboard.Direct.Command.Buffer";
    config.dataEventName = L"Local\\SmartDashboard.Direct.Command.DataAvailable";
    config.heartbeatEventName = L"Local\\SmartDashboard.Direct.Command.Heartbeat";
    config.autoFlushThread = false;
    m_publisher = sd::direct::CreateDirectPublisher(config);
}

DirectPublisherAdapter::~DirectPublisherAdapter()
{
    Stop();
}

bool DirectPublisherAdapter::Start()
{
    if (!m_publisher)
    {
        return false;
    }
    return m_publisher->Start();
}

void DirectPublisherAdapter::Stop()
{
    if (m_publisher)
    {
        m_publisher->Stop();
    }
}

bool DirectPublisherAdapter::PublishBool(const QString& key, bool value)
{
    if (!m_publisher)
    {
        return false;
    }
    m_publisher->PublishBool(key.toStdString(), value);
    return m_publisher->FlushNow();
}

bool DirectPublisherAdapter::PublishDouble(const QString& key, double value)
{
    if (!m_publisher)
    {
        return false;
    }
    m_publisher->PublishDouble(key.toStdString(), value);
    return m_publisher->FlushNow();
}

bool DirectPublisherAdapter::PublishString(const QString& key, const QString& value)
{
    if (!m_publisher)
    {
        return false;
    }
    m_publisher->PublishString(key.toStdString(), value.toStdString());
    return m_publisher->FlushNow();
}
