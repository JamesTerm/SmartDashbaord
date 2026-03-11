#pragma once

#include <QString>
#include <QVariant>

#include <cstdint>
#include <functional>
#include <memory>

namespace sd::transport
{
    enum class TransportKind
    {
        Direct,
        NetworkTables
    };

    enum class ConnectionState
    {
        Disconnected,
        Connecting,
        Connected,
        Stale
    };

    struct ConnectionConfig
    {
        TransportKind kind = TransportKind::Direct;
        QString ntHost = "127.0.0.1";
        int ntTeam = 0;
        bool ntUseTeam = true;
        QString ntClientName = "SmartDashboardApp";
    };

    struct VariableUpdate
    {
        QString key;
        int valueType = 0;
        QVariant value;
        std::uint64_t seq = 0;
    };

    using VariableUpdateCallback = std::function<void(const VariableUpdate&)>;
    using ConnectionStateCallback = std::function<void(ConnectionState)>;

    class IDashboardTransport
    {
    public:
        virtual ~IDashboardTransport() = default;

        virtual bool Start(VariableUpdateCallback onVariableUpdate, ConnectionStateCallback onConnectionState) = 0;
        virtual void Stop() = 0;

        virtual bool PublishBool(const QString& key, bool value) = 0;
        virtual bool PublishDouble(const QString& key, double value) = 0;
        virtual bool PublishString(const QString& key, const QString& value) = 0;
    };

    QString ToDisplayString(TransportKind kind);

    std::unique_ptr<IDashboardTransport> CreateDashboardTransport(const ConnectionConfig& config);
}
