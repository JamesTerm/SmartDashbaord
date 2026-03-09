#include "layout/layout_serializer.h"

#include "widgets/variable_tile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QWidget>

namespace sd::layout
{
    QString GetDefaultLayoutPath()
    {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        return base + "/layout.json";
    }

    bool SaveLayout(const QWidget* canvas, const QString& filePath)
    {
        if (canvas == nullptr)
        {
            return false;
        }

        // Snapshot pattern: serialize current widget metadata/geometry to JSON.
        QJsonArray widgets;
        const QObjectList children = canvas->children();
        for (QObject* child : children)
        {
            QWidget* widget = qobject_cast<QWidget*>(child);
            if (widget == nullptr)
            {
                continue;
            }

            if (widget->objectName().isEmpty())
            {
                continue;
            }

            const QRect geometry = widget->geometry();
            QJsonObject entry;
            entry["widgetId"] = widget->objectName();
            entry["variableKey"] = widget->property("variableKey").toString();
            entry["widgetType"] = widget->property("widgetType").toString();

            auto* tile = qobject_cast<const sd::widgets::VariableTile*>(widget);
            if (tile != nullptr)
            {
                switch (tile->GetType())
                {
                    case sd::widgets::VariableType::Bool:
                        entry["boolValue"] = tile->GetBoolValue();
                        break;
                    case sd::widgets::VariableType::Double:
                        entry["doubleValue"] = tile->GetDoubleValue();
                        break;
                    case sd::widgets::VariableType::String:
                        entry["stringValue"] = tile->GetStringValue();
                        break;
                    default:
                        break;
                }
            }

            const QVariant gaugeLower = widget->property("gaugeLowerLimit");
            const QVariant gaugeUpper = widget->property("gaugeUpperLimit");
            const QVariant gaugeTick = widget->property("gaugeTickInterval");
            const QVariant gaugeShow = widget->property("gaugeShowTickMarks");
            const QVariant linePlotBufferSize = widget->property("linePlotBufferSizeSamples");
            const QVariant linePlotAutoY = widget->property("linePlotAutoYAxis");
            const QVariant linePlotYLower = widget->property("linePlotYLowerLimit");
            const QVariant linePlotYUpper = widget->property("linePlotYUpperLimit");
            const QVariant doubleNumericEditable = widget->property("doubleNumericEditable");
            if (gaugeLower.isValid())
            {
                entry["gaugeLowerLimit"] = gaugeLower.toDouble();
            }
            if (gaugeUpper.isValid())
            {
                entry["gaugeUpperLimit"] = gaugeUpper.toDouble();
            }
            if (gaugeTick.isValid())
            {
                entry["gaugeTickInterval"] = gaugeTick.toDouble();
            }
            if (gaugeShow.isValid())
            {
                entry["gaugeShowTickMarks"] = gaugeShow.toBool();
            }
            if (linePlotBufferSize.isValid())
            {
                entry["linePlotBufferSizeSamples"] = linePlotBufferSize.toInt();
            }
            if (linePlotAutoY.isValid())
            {
                entry["linePlotAutoYAxis"] = linePlotAutoY.toBool();
            }
            if (linePlotYLower.isValid())
            {
                entry["linePlotYLowerLimit"] = linePlotYLower.toDouble();
            }
            if (linePlotYUpper.isValid())
            {
                entry["linePlotYUpperLimit"] = linePlotYUpper.toDouble();
            }
            if (doubleNumericEditable.isValid())
            {
                entry["doubleNumericEditable"] = doubleNumericEditable.toBool();
            }

            QJsonObject geo;
            geo["x"] = geometry.x();
            geo["y"] = geometry.y();
            geo["w"] = geometry.width();
            geo["h"] = geometry.height();
            entry["geometry"] = geo;
            widgets.append(entry);
        }

        QJsonObject root;
        root["version"] = 1;
        root["widgets"] = widgets;

        QFile file(filePath);
        QDir().mkpath(QFileInfo(filePath).absolutePath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return false;
        }

        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        return true;
    }

    bool LoadLayoutEntries(const QString& filePath, std::vector<WidgetLayoutEntry>& outEntries)
    {
        QFile file(filePath);
        if (!file.exists())
        {
            return false;
        }

        if (!file.open(QIODevice::ReadOnly))
        {
            return false;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
        {
            return false;
        }

        // Defensive parsing: skip incomplete entries and keep loading valid ones.
        outEntries.clear();
        const QJsonArray widgets = doc.object().value("widgets").toArray();
        for (const QJsonValue& value : widgets)
        {
            const QJsonObject entry = value.toObject();
            const QString variableKey = entry.value("variableKey").toString();
            if (variableKey.isEmpty())
            {
                continue;
            }

            const QJsonObject geo = entry.value("geometry").toObject();
            WidgetLayoutEntry layoutEntry;
            layoutEntry.variableKey = variableKey;
            layoutEntry.widgetType = entry.value("widgetType").toString();
            layoutEntry.geometry = QRect(
                geo.value("x").toInt(),
                geo.value("y").toInt(),
                geo.value("w").toInt(220),
                geo.value("h").toInt(84)
            );
            if (entry.contains("gaugeLowerLimit"))
            {
                layoutEntry.gaugeLowerLimit = entry.value("gaugeLowerLimit").toDouble();
            }
            if (entry.contains("gaugeUpperLimit"))
            {
                layoutEntry.gaugeUpperLimit = entry.value("gaugeUpperLimit").toDouble();
            }
            if (entry.contains("gaugeTickInterval"))
            {
                layoutEntry.gaugeTickInterval = entry.value("gaugeTickInterval").toDouble();
            }
            if (entry.contains("gaugeShowTickMarks"))
            {
                layoutEntry.gaugeShowTickMarks = entry.value("gaugeShowTickMarks").toBool();
            }
            if (entry.contains("linePlotBufferSizeSamples"))
            {
                layoutEntry.linePlotBufferSizeSamples = entry.value("linePlotBufferSizeSamples").toInt();
            }
            if (entry.contains("linePlotAutoYAxis"))
            {
                layoutEntry.linePlotAutoYAxis = entry.value("linePlotAutoYAxis").toBool();
            }
            if (entry.contains("linePlotYLowerLimit"))
            {
                layoutEntry.linePlotYLowerLimit = entry.value("linePlotYLowerLimit").toDouble();
            }
            if (entry.contains("linePlotYUpperLimit"))
            {
                layoutEntry.linePlotYUpperLimit = entry.value("linePlotYUpperLimit").toDouble();
            }
            if (entry.contains("doubleNumericEditable"))
            {
                layoutEntry.doubleNumericEditable = entry.value("doubleNumericEditable").toBool();
            }
            if (entry.contains("boolValue"))
            {
                layoutEntry.boolValue = entry.value("boolValue").toBool();
            }
            if (entry.contains("doubleValue"))
            {
                layoutEntry.doubleValue = entry.value("doubleValue").toDouble();
            }
            if (entry.contains("stringValue"))
            {
                layoutEntry.stringValue = entry.value("stringValue").toString();
            }
            outEntries.push_back(layoutEntry);
        }

        return true;
    }
}
