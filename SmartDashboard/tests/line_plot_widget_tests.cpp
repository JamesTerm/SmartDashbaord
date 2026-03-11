#include "widgets/line_plot_widget.h"

#include <QApplication>
#include <QThread>

#include <gtest/gtest.h>

#include <memory>

namespace
{
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

    void PumpSamples(sd::widgets::LinePlotWidget& plot, int sampleCount, int sleepMillis, double startValue)
    {
        for (int i = 0; i < sampleCount; ++i)
        {
            plot.AddSample(startValue + static_cast<double>(i));
            if (sleepMillis > 0)
            {
                QThread::msleep(static_cast<unsigned long>(sleepMillis));
            }
        }
    }

    void VerifyStableWindowForScenario(
        sd::widgets::LinePlotWidget& plot,
        int bufferSize,
        int sampleCount,
        int sleepMillis,
        double startValue)
    {
        plot.ResetGraph();
        plot.SetBufferSizeSamples(bufferSize);
        PumpSamples(plot, sampleCount, sleepMillis, startValue);

        const int expectedCount = std::min(bufferSize, sampleCount);
        EXPECT_EQ(plot.GetSampleCountForTesting(), expectedCount);

        const auto xRange = plot.GetXRangeForTesting();
        const double span = xRange.second - xRange.first;
        const double estimatedPeriod = plot.GetEstimatedSamplePeriodSecondsForTesting();
        const double expectedWindow = std::max(1.0, estimatedPeriod * static_cast<double>(bufferSize));

        EXPECT_GT(span, 0.0);
        EXPECT_NEAR(span, expectedWindow, expectedWindow * 0.03 + 0.02);
    }
}

TEST(LinePlotWidgetTests, BufferSizeSetterMaintainsStabilityAcrossRates)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::LinePlotWidget plot;

    VerifyStableWindowForScenario(plot, 250, 250, 1, 0.0);
    VerifyStableWindowForScenario(plot, 250, 500, 1, 1000.0);
    VerifyStableWindowForScenario(plot, 1000, 1000, 0, 2000.0);
    VerifyStableWindowForScenario(plot, 120, 360, 2, 3000.0);
}
