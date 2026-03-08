#include "widgets/tile_control_widget.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSlider>

namespace sd::widgets
{
    TileControlWidget::TileControlWidget(VariableType type, QWidget* parent)
        : QWidget(parent)
        , m_type(type)
    {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        if (m_type == VariableType::Bool)
        {
            m_checkBox = new QCheckBox(this);
            layout->addWidget(m_checkBox);
            connect(m_checkBox, &QCheckBox::toggled, this, [this](bool checked)
            {
                if (!m_settingProgrammatically)
                {
                    emit BoolEdited(checked);
                }
            });
        }
        else if (m_type == VariableType::Double)
        {
            m_slider = new QSlider(Qt::Horizontal, this);
            m_slider->setRange(0, 100);
            layout->addWidget(m_slider);
            connect(m_slider, &QSlider::valueChanged, this, [this](int raw)
            {
                if (!m_settingProgrammatically)
                {
                    const double normalized = static_cast<double>(raw) / 100.0;
                    emit DoubleEdited((normalized * 2.0) - 1.0);
                }
            });
        }
        else if (m_type == VariableType::String)
        {
            m_lineEdit = new QLineEdit(this);
            layout->addWidget(m_lineEdit);
            connect(m_lineEdit, &QLineEdit::editingFinished, this, [this]()
            {
                if (!m_settingProgrammatically)
                {
                    emit StringEdited(m_lineEdit->text());
                }
            });
        }
    }

    void TileControlWidget::SetBoolValue(bool value)
    {
        if (!m_checkBox)
        {
            return;
        }

        m_settingProgrammatically = true;
        m_checkBox->setChecked(value);
        m_settingProgrammatically = false;
    }

    void TileControlWidget::SetDoubleValue(double value)
    {
        if (!m_slider)
        {
            return;
        }

        double clamped = value;
        if (clamped < -1.0)
        {
            clamped = -1.0;
        }
        else if (clamped > 1.0)
        {
            clamped = 1.0;
        }

        m_settingProgrammatically = true;
        m_slider->setValue(static_cast<int>((clamped + 1.0) * 50.0));
        m_settingProgrammatically = false;
    }

    void TileControlWidget::SetStringValue(const QString& value)
    {
        if (!m_lineEdit)
        {
            return;
        }

        m_settingProgrammatically = true;
        m_lineEdit->setText(value);
        m_settingProgrammatically = false;
    }
}
