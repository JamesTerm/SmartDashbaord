#pragma once

#include "sd_direct_publisher.h"

#include <QObject>

#include <memory>

class QString;

class DirectPublisherAdapter final : public QObject
{
    Q_OBJECT

public:
    explicit DirectPublisherAdapter(QObject* parent = nullptr);
    ~DirectPublisherAdapter() override;

    bool Start();
    void Stop();

    bool PublishBool(const QString& key, bool value);
    bool PublishDouble(const QString& key, double value);
    bool PublishString(const QString& key, const QString& value);

private:
    std::unique_ptr<sd::direct::IDirectPublisher> m_publisher;
};
