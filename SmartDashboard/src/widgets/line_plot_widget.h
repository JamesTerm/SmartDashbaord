#pragma once

#include <QWidget>

#include <chrono>
#include <deque>

class QPaintEvent;

namespace sd::widgets
{
    class LinePlotWidget final : public QWidget
    {
    public:
        explicit LinePlotWidget(QWidget* parent = nullptr);

        void AddSample(double value);
        void ResetGraph();
        void SetBufferSizeSamples(int samples);
        void SetYAxisModeAuto(bool enabled);
        void SetYAxisLimits(double lowerLimit, double upperLimit);
        void SetShowNumberLines(bool enabled);
        void SetShowGridLines(bool enabled);

    protected:
        void paintEvent(QPaintEvent* event) override;

    private:
        struct AxisRange
        {
            double min = 0.0;
            double max = 1.0;
        };

        struct SamplePoint
        {
            double xSeconds = 0.0;
            double yValue = 0.0;
        };

        struct YExtents
        {
            double min = 0.0;
            double max = 1.0;
        };

        AxisRange ComputeXRange() const;
        AxisRange ComputeYRange() const;
        YExtents ComputeRecentYExtents() const;

        std::deque<SamplePoint> m_samples;
        std::chrono::steady_clock::time_point m_startTime;
        bool m_hasStarted = false;
        int m_bufferSizeSamples = 5000;
        bool m_autoYAxis = true;
        bool m_showNumberLines = false;
        bool m_showGridLines = false;
        double m_manualYLowerLimit = 0.0;
        double m_manualYUpperLimit = 1.0;
    };
}
