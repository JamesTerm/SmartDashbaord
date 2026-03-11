#include "transport/dashboard_transport.h"

#include "sd_direct_publisher.h"
#include "sd_direct_subscriber.h"

#if SD_HAS_LEGACY_NT2
#include "networktables/NetworkTable.h"
#include "tables/ITableListener.h"
#include "tables/IRemoteConnectionListener.h"
#endif

#include <atomic>

#include <QByteArray>
#include <QMetaObject>

namespace sd::transport
{
    namespace
    {
        ConnectionState ToConnectionState(sd::direct::ConnectionState state)
        {
            switch (state)
            {
                case sd::direct::ConnectionState::Connecting:
                    return ConnectionState::Connecting;
                case sd::direct::ConnectionState::Connected:
                    return ConnectionState::Connected;
                case sd::direct::ConnectionState::Stale:
                    return ConnectionState::Stale;
                case sd::direct::ConnectionState::Disconnected:
                default:
                    return ConnectionState::Disconnected;
            }
        }

        class DirectDashboardTransport final : public IDashboardTransport
        {
        public:
            DirectDashboardTransport()
            {
                sd::direct::SubscriberConfig subConfig;
                m_subscriber = sd::direct::CreateDirectSubscriber(subConfig);

                sd::direct::PublisherConfig pubConfig;
                pubConfig.mappingName = L"Local\\SmartDashboard.Direct.Command.Buffer";
                pubConfig.dataEventName = L"Local\\SmartDashboard.Direct.Command.DataAvailable";
                pubConfig.heartbeatEventName = L"Local\\SmartDashboard.Direct.Command.Heartbeat";
                pubConfig.autoFlushThread = false;
                m_commandPublisher = sd::direct::CreateDirectPublisher(pubConfig);
            }

            bool Start(VariableUpdateCallback onVariableUpdate, ConnectionStateCallback onConnectionState) override
            {
                m_onVariableUpdate = std::move(onVariableUpdate);
                m_onConnectionState = std::move(onConnectionState);

                if (!m_subscriber || !m_commandPublisher)
                {
                    return false;
                }

                if (!m_commandPublisher->Start())
                {
                    return false;
                }

                const bool subscriberStarted = m_subscriber->Start(
                    [this](const sd::direct::VariableUpdate& update)
                    {
                        if (!m_onVariableUpdate)
                        {
                            return;
                        }

                        VariableUpdate converted;
                        converted.key = QString::fromStdString(update.key);
                        converted.valueType = static_cast<int>(update.type);
                        converted.seq = update.seq;

                        switch (update.type)
                        {
                            case sd::direct::ValueType::Bool:
                                converted.value = update.value.boolValue;
                                break;
                            case sd::direct::ValueType::Double:
                                converted.value = update.value.doubleValue;
                                break;
                            case sd::direct::ValueType::String:
                                converted.value = QString::fromStdString(update.value.stringValue);
                                break;
                            default:
                                converted.value = QVariant();
                                break;
                        }

                        m_onVariableUpdate(converted);
                    },
                    [this](sd::direct::ConnectionState state)
                    {
                        if (m_onConnectionState)
                        {
                            m_onConnectionState(ToConnectionState(state));
                        }
                    }
                );

                if (!subscriberStarted)
                {
                    m_commandPublisher->Stop();
                    return false;
                }

                return true;
            }

            void Stop() override
            {
                if (m_subscriber)
                {
                    m_subscriber->Stop();
                }

                if (m_commandPublisher)
                {
                    m_commandPublisher->Stop();
                }
            }

            bool PublishBool(const QString& key, bool value) override
            {
                if (!m_commandPublisher)
                {
                    return false;
                }
                m_commandPublisher->PublishBool(key.toStdString(), value);
                return m_commandPublisher->FlushNow();
            }

            bool PublishDouble(const QString& key, double value) override
            {
                if (!m_commandPublisher)
                {
                    return false;
                }
                m_commandPublisher->PublishDouble(key.toStdString(), value);
                return m_commandPublisher->FlushNow();
            }

            bool PublishString(const QString& key, const QString& value) override
            {
                if (!m_commandPublisher)
                {
                    return false;
                }
                m_commandPublisher->PublishString(key.toStdString(), value.toStdString());
                return m_commandPublisher->FlushNow();
            }

        private:
            std::unique_ptr<sd::direct::IDirectSubscriber> m_subscriber;
            std::unique_ptr<sd::direct::IDirectPublisher> m_commandPublisher;
            VariableUpdateCallback m_onVariableUpdate;
            ConnectionStateCallback m_onConnectionState;
        };

        class NetworkTablesDashboardTransportStub final : public IDashboardTransport
        {
        public:
            explicit NetworkTablesDashboardTransportStub(ConnectionConfig config)
                : m_config(std::move(config))
            {
            }

            bool Start(VariableUpdateCallback, ConnectionStateCallback onConnectionState) override
            {
                if (onConnectionState)
                {
                    onConnectionState(ConnectionState::Connecting);
                    onConnectionState(ConnectionState::Disconnected);
                }
                return false;
            }

            void Stop() override
            {
            }

            bool PublishBool(const QString&, bool) override
            {
                return false;
            }

            bool PublishDouble(const QString&, double) override
            {
                return false;
            }

            bool PublishString(const QString&, const QString&) override
            {
                return false;
            }

        private:
            ConnectionConfig m_config;
        };

#if SD_HAS_LEGACY_NT2
        class LegacyNt2TableListener final : public ITableListener
        {
        public:
            explicit LegacyNt2TableListener(VariableUpdateCallback callback)
                : m_callback(std::move(callback))
            {
            }

            void ValueChanged(ITable*, const std::string& key, EntryValue value, bool) override
            {
                if (!m_callback)
                {
                    return;
                }

                VariableUpdate update;
                update.key = QString::fromStdString(key);
                update.seq = 0;

                bool treated = false;
                if (value.ptr != nullptr)
                {
                    const std::string* asString = static_cast<std::string*>(value.ptr);
                    if (asString != nullptr)
                    {
                        update.valueType = static_cast<int>(sd::direct::ValueType::String);
                        update.value = QString::fromStdString(*asString);
                        treated = true;
                    }
                }

                if (!treated)
                {
                    update.valueType = static_cast<int>(sd::direct::ValueType::Double);
                    update.value = value.f;
                }

                m_callback(update);
            }

        private:
            VariableUpdateCallback m_callback;
        };

        class LegacyNt2ConnectionListener final : public IRemoteConnectionListener
        {
        public:
            explicit LegacyNt2ConnectionListener(ConnectionStateCallback callback)
                : m_callback(std::move(callback))
            {
            }

            void Connected(IRemote*) override
            {
                if (m_callback)
                {
                    m_callback(ConnectionState::Connected);
                }
            }

            void Disconnected(IRemote*) override
            {
                if (m_callback)
                {
                    m_callback(ConnectionState::Disconnected);
                }
            }

        private:
            ConnectionStateCallback m_callback;
        };

        class NetworkTablesDashboardTransport final : public IDashboardTransport
        {
        public:
            explicit NetworkTablesDashboardTransport(ConnectionConfig config)
                : m_config(std::move(config))
            {
            }

            bool Start(VariableUpdateCallback onVariableUpdate, ConnectionStateCallback onConnectionState) override
            {
                m_onVariableUpdate = std::move(onVariableUpdate);
                m_onConnectionState = std::move(onConnectionState);

                if (m_started.exchange(true))
                {
                    return true;
                }

                try
                {
                    if (m_onConnectionState)
                    {
                        m_onConnectionState(ConnectionState::Connecting);
                    }

                    NetworkTable::SetClientMode();
                    if (m_config.ntUseTeam && m_config.ntTeam > 0)
                    {
                        NetworkTable::SetTeam(m_config.ntTeam);
                    }
                    else
                    {
                        const QByteArray hostBytes = m_config.ntHost.toUtf8();
                        NetworkTable::SetIPAddress(hostBytes.constData());
                    }

                    m_table = NetworkTable::GetTable("SmartDashboard");
                    if (m_table == nullptr)
                    {
                        if (m_onConnectionState)
                        {
                            m_onConnectionState(ConnectionState::Disconnected);
                        }
                        return false;
                    }

                    m_tableListener = std::make_unique<LegacyNt2TableListener>(m_onVariableUpdate);
                    m_connectionListener = std::make_unique<LegacyNt2ConnectionListener>(m_onConnectionState);
                    m_table->AddTableListener(m_tableListener.get(), true);
                    m_table->AddConnectionListener(m_connectionListener.get(), true);

                    if (m_onConnectionState)
                    {
                        m_onConnectionState(m_table->IsConnected() ? ConnectionState::Connected : ConnectionState::Disconnected);
                    }
                }
                catch (...)
                {
                    m_started.store(false);
                    if (m_onConnectionState)
                    {
                        m_onConnectionState(ConnectionState::Disconnected);
                    }
                    return false;
                }

                return true;
            }

            void Stop() override
            {
                if (!m_started.exchange(false))
                {
                    return;
                }

                if (m_table != nullptr)
                {
                    if (m_tableListener)
                    {
                        m_table->RemoveTableListener(m_tableListener.get());
                    }
                    if (m_connectionListener)
                    {
                        m_table->RemoveConnectionListener(m_connectionListener.get());
                    }
                }

                m_tableListener.reset();
                m_connectionListener.reset();
                m_table = nullptr;

                if (m_onConnectionState)
                {
                    m_onConnectionState(ConnectionState::Disconnected);
                }
            }

            bool PublishBool(const QString& key, bool value) override
            {
                if (m_table == nullptr)
                {
                    return false;
                }
                m_table->PutBoolean(key.toStdString(), value);
                return true;
            }

            bool PublishDouble(const QString& key, double value) override
            {
                if (m_table == nullptr)
                {
                    return false;
                }
                m_table->PutNumber(key.toStdString(), value);
                return true;
            }

            bool PublishString(const QString& key, const QString& value) override
            {
                if (m_table == nullptr)
                {
                    return false;
                }
                m_table->PutString(key.toStdString(), value.toStdString());
                return true;
            }

        private:
            ConnectionConfig m_config;
            VariableUpdateCallback m_onVariableUpdate;
            ConnectionStateCallback m_onConnectionState;
            std::atomic<bool> m_started {false};
            NetworkTable* m_table = nullptr;
            std::unique_ptr<LegacyNt2TableListener> m_tableListener;
            std::unique_ptr<LegacyNt2ConnectionListener> m_connectionListener;
        };
#endif
    }

    QString ToDisplayString(TransportKind kind)
    {
        switch (kind)
        {
            case TransportKind::NetworkTables:
                return "NetworkTables";
            case TransportKind::Direct:
            default:
                return "Direct";
        }
    }

    std::unique_ptr<IDashboardTransport> CreateDashboardTransport(const ConnectionConfig& config)
    {
        if (config.kind == TransportKind::NetworkTables)
        {
#if SD_HAS_LEGACY_NT2
            return std::make_unique<NetworkTablesDashboardTransport>(config);
#else
            return std::make_unique<NetworkTablesDashboardTransportStub>(config);
#endif
        }

        return std::make_unique<DirectDashboardTransport>();
    }
}
