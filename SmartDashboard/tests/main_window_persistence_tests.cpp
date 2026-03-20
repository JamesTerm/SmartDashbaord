#include "app/main_window.h"

#include "sd_direct_types.h"

#include <QApplication>
#include <QSettings>
#include <QString>
#include <QVariant>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
    struct RememberedControlEntry
    {
        QString key;
        int valueType = 0;
        QVariant value;
    };

    QApplication* EnsureApp()
    {
        if (QApplication::instance() != nullptr)
        {
            return qobject_cast<QApplication*>(QApplication::instance());
        }

        static int argc = 1;
        static char appName[] = "SmartDashboardTests";
        static char* argv[] = { appName };
        static std::unique_ptr<QApplication> app = std::make_unique<QApplication>(argc, argv);
        return app.get();
    }

    std::vector<RememberedControlEntry> ReadRememberedControls()
    {
        std::vector<RememberedControlEntry> entries;

        QSettings settings("SmartDashboard", "SmartDashboardApp");
        const int size = settings.beginReadArray("directRememberedControls");
        entries.reserve(static_cast<std::size_t>(size));
        for (int i = 0; i < size; ++i)
        {
            settings.setArrayIndex(i);

            RememberedControlEntry entry;
            entry.key = settings.value("key").toString();
            entry.valueType = settings.value("valueType").toInt();
            entry.value = settings.value("value");
            entries.push_back(entry);
        }
        settings.endArray();

        return entries;
    }

    void WriteRememberedControls(const std::vector<RememberedControlEntry>& entries)
    {
        QSettings settings("SmartDashboard", "SmartDashboardApp");
        settings.remove("directRememberedControls");
        settings.beginWriteArray("directRememberedControls");
        for (int i = 0; i < static_cast<int>(entries.size()); ++i)
        {
            settings.setArrayIndex(i);
            settings.setValue("key", entries[static_cast<std::size_t>(i)].key);
            settings.setValue("valueType", entries[static_cast<std::size_t>(i)].valueType);
            settings.setValue("value", entries[static_cast<std::size_t>(i)].value);
        }
        settings.endArray();
        settings.sync();
    }

    class ScopedPersistenceSettings final
    {
    public:
        ScopedPersistenceSettings()
        {
            QSettings settings("SmartDashboard", "SmartDashboardApp");
            m_hadTransportKind = settings.contains("connection/transportKind");
            m_transportKind = settings.value("connection/transportKind");
            m_hadTransportId = settings.contains("connection/transportId");
            m_transportId = settings.value("connection/transportId");
            m_rememberedControls = ReadRememberedControls();
        }

        ~ScopedPersistenceSettings()
        {
            QSettings settings("SmartDashboard", "SmartDashboardApp");
            if (m_hadTransportKind)
            {
                settings.setValue("connection/transportKind", m_transportKind);
            }
            else
            {
                settings.remove("connection/transportKind");
            }

            if (m_hadTransportId)
            {
                settings.setValue("connection/transportId", m_transportId);
            }
            else
            {
                settings.remove("connection/transportId");
            }

            settings.sync();
            WriteRememberedControls(m_rememberedControls);
        }

    private:
        bool m_hadTransportKind = false;
        QVariant m_transportKind;
        bool m_hadTransportId = false;
        QVariant m_transportId;
        std::vector<RememberedControlEntry> m_rememberedControls;
    };

    void ClearRememberedControlsSetting()
    {
        QSettings settings("SmartDashboard", "SmartDashboardApp");
        settings.remove("directRememberedControls");
        settings.sync();
    }

    int RememberedControlsSettingSize()
    {
        QSettings settings("SmartDashboard", "SmartDashboardApp");
        const int size = settings.beginReadArray("directRememberedControls");
        settings.endArray();
        return size;
    }

    QVariant RememberedControlSettingValue(const QString& key)
    {
        for (const RememberedControlEntry& entry : ReadRememberedControls())
        {
            if (entry.key == key)
            {
                return entry.value;
            }
        }

        return {};
    }

    void SetStartupTransport(const QString& transportId, sd::transport::TransportKind kind)
    {
        QSettings settings("SmartDashboard", "SmartDashboardApp");
        settings.setValue("connection/transportId", transportId);
        settings.setValue("connection/transportKind", static_cast<int>(kind));
        settings.sync();
    }
}

TEST(MainWindowPersistenceTests, NativeLinkTelemetryUpdatesDoNotPopulateRememberedControls)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    ClearRememberedControlsSetting();
    SetStartupTransport("native-link", sd::transport::TransportKind::Plugin);

    MainWindow window(nullptr, false);
    window.SetTransportSelectionForTesting("native-link", sd::transport::TransportKind::Plugin);

    window.SimulateVariableUpdateForTesting(
        "TestMove",
        static_cast<int>(sd::direct::ValueType::Double),
        QVariant(5.14),
        1
    );
    window.SimulateVariableUpdateForTesting(
        "Timer",
        static_cast<int>(sd::direct::ValueType::Double),
        QVariant(1797.4630771),
        2
    );
    window.SimulateVariableUpdateForTesting(
        "Test/Auton_Selection/AutoChooser/selected",
        static_cast<int>(sd::direct::ValueType::String),
        QVariant(QString("Just Move Forward")),
        3
    );

    EXPECT_EQ(window.RememberedControlValueCountForTesting(), 0);
    EXPECT_FALSE(window.HasRememberedControlValueForTesting("TestMove"));
    EXPECT_FALSE(window.HasRememberedControlValueForTesting("Timer"));
    EXPECT_FALSE(window.HasRememberedControlValueForTesting("Test/Auton_Selection/AutoChooser"));
    EXPECT_EQ(RememberedControlsSettingSize(), 0);
}

TEST(MainWindowPersistenceTests, DirectTelemetryUpdatesDoNotCreateRememberedControls)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    ClearRememberedControlsSetting();
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.SetTransportSelectionForTesting("direct", sd::transport::TransportKind::Direct);

    window.SimulateVariableUpdateForTesting(
        "TestMove",
        static_cast<int>(sd::direct::ValueType::Double),
        QVariant(3.5),
        1
    );

    EXPECT_EQ(window.RememberedControlValueCountForTesting(), 0);
    EXPECT_FALSE(window.HasRememberedControlValueForTesting("TestMove"));
    EXPECT_EQ(RememberedControlsSettingSize(), 0);
}

TEST(MainWindowPersistenceTests, DirectControlEditsStillPersistRememberedControls)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    ClearRememberedControlsSetting();
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    MainWindow window(nullptr, false);
    window.SetTransportSelectionForTesting("direct", sd::transport::TransportKind::Direct);

    window.SimulateControlDoubleEditForTesting("TestMove", 3.5);

    EXPECT_EQ(window.RememberedControlValueCountForTesting(), 1);
    EXPECT_TRUE(window.HasRememberedControlValueForTesting("TestMove"));
    EXPECT_EQ(RememberedControlsSettingSize(), 1);
    EXPECT_EQ(RememberedControlSettingValue("TestMove").toString(), QString("3.5"));
}

TEST(MainWindowPersistenceTests, DirectSliderEditUpdatesRememberedValueAcrossReopen)
{
    ASSERT_NE(EnsureApp(), nullptr);
    const ScopedPersistenceSettings scopedSettings;
    ClearRememberedControlsSetting();
    SetStartupTransport("direct", sd::transport::TransportKind::Direct);

    {
        MainWindow firstWindow(nullptr, false);
        firstWindow.SetTransportSelectionForTesting("direct", sd::transport::TransportKind::Direct);
        firstWindow.SimulateControlDoubleEditForTesting("TestMove", 7.25);
    }

    EXPECT_EQ(RememberedControlsSettingSize(), 1);
    EXPECT_EQ(RememberedControlSettingValue("TestMove").toString(), QString("7.25"));

    MainWindow reopenedWindow(nullptr, false);
    EXPECT_TRUE(reopenedWindow.HasRememberedControlValueForTesting("TestMove"));
}
