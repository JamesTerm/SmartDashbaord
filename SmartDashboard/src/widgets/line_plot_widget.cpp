#include "widgets/line_plot_widget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>

#include <algorithm>

namespace sd::widgets
{
    LinePlotWidget::LinePlotWidget(QWidget* parent)
        : QWidget(parent)
    {
        setMinimumHeight(80);
        setAutoFillBackground(true);
    }

    void LinePlotWidget::AddSample(double value)
    {
        const auto now = std::chrono::steady_clock::now();
        if (!m_hasStarted)
        {
            m_startTime = now;
            m_hasStarted = true;
        }

        const std::chrono::duration<double> elapsed = now - m_startTime;
        m_samples.push_back(SamplePoint{ elapsed.count(), value });

        while (static_cast<int>(m_samples.size()) > m_bufferSizeSamples)
        {
            m_samples.pop_front();
        }

        update();
    }

    void LinePlotWidget::ResetGraph()
    {
        m_samples.clear();
        m_hasStarted = false;
        update();
    }

    void LinePlotWidget::SetBufferSizeSamples(int samples)
    {
        if (samples < 2)
        {
            samples = 2;
        }

        m_bufferSizeSamples = samples;
        while (static_cast<int>(m_samples.size()) > m_bufferSizeSamples)
        {
            m_samples.pop_front();
        }

        update();
    }

    void LinePlotWidget::SetYAxisModeAuto(bool enabled)
    {
        m_autoYAxis = enabled;
        update();
    }

    void LinePlotWidget::SetYAxisLimits(double lowerLimit, double upperLimit)
    {
        m_manualYLowerLimit = lowerLimit;
        m_manualYUpperLimit = upperLimit;
        if (m_manualYUpperLimit <= m_manualYLowerLimit)
        {
            m_manualYUpperLimit = m_manualYLowerLimit + 0.001;
        }

        update();
    }

    void LinePlotWidget::paintEvent(QPaintEvent* event)
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRect drawRect = rect().adjusted(6, 6, -6, -6);
        if (drawRect.width() <= 2 || drawRect.height() <= 2)
        {
            return;
        }

        painter.setPen(QPen(QColor("#3b3b3b"), 1));
        painter.drawRect(drawRect);

        if (m_samples.size() < 2)
        {
            return;
        }

        const AxisRange xRange = ComputeXRange();
        const AxisRange yRange = ComputeYRange();
        const double xSpan = xRange.max - xRange.min;
        const double ySpan = yRange.max - yRange.min;
        if (xSpan <= 0.0 || ySpan <= 0.0)
        {
            return;
        }

        QPainterPath path;
        bool first = true;
        for (const SamplePoint& sample : m_samples)
        {
            const double xNormalized = (sample.xSeconds - xRange.min) / xSpan;
            const double yNormalized = (sample.yValue - yRange.min) / ySpan;

            const qreal xPixel = drawRect.left() + (xNormalized * drawRect.width());
            const qreal yPixel = drawRect.bottom() - (yNormalized * drawRect.height());
            if (first)
            {
                path.moveTo(xPixel, yPixel);
                first = false;
            }
            else
            {
                path.lineTo(xPixel, yPixel);
            }
        }

        painter.setPen(QPen(QColor("#33b5e5"), 1.5));
        painter.drawPath(path);
    }

    LinePlotWidget::AxisRange LinePlotWidget::ComputeXRange() const
    {
        if (m_samples.empty())
        {
            return AxisRange{ 0.0, 1.0 };
        }

        const double headTime = m_samples.back().xSeconds;
        if (m_samples.size() < static_cast<size_t>(m_bufferSizeSamples))
        {
            return AxisRange{ 0.0, std::max(1.0, headTime) };
        }

        const double tailTime = m_samples.front().xSeconds;
        const double end = std::max(headTime, tailTime + 0.001);
        return AxisRange{ tailTime, end };
    }

    LinePlotWidget::AxisRange LinePlotWidget::ComputeYRange() const
    {
        if (!m_autoYAxis)
        {
            return AxisRange{ m_manualYLowerLimit, m_manualYUpperLimit };
        }

        if (m_samples.empty())
        {
            return AxisRange{ 0.0, 1.0 };
        }

        // Auto-ranging behavior mirrors FRC style: start normalized at [0,1],
        // then expand min/max only when incoming values exceed current bounds.
        double minY = 0.0;
        double maxY = 1.0;
        for (const SamplePoint& sample : m_samples)
        {
            if (sample.yValue < minY)
            {
                minY = sample.yValue;
            }
            if (sample.yValue > maxY)
            {
                maxY = sample.yValue;
            }
        }

        if (maxY <= minY)
        {
            maxY = minY + 0.001;
        }

        return AxisRange{ minY, maxY };
    }
}
