#include "widgets/variable_tile.h"

#include <QApplication>
#include <QProgressBar>

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

    QProgressBar* FindProgressBar(sd::widgets::VariableTile& tile)
    {
        return tile.findChild<QProgressBar*>();
    }
}

TEST(VariableTileTests, ProgressBarZeroCentersBeforeWidgetIsShown)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::VariableTile tile("test.progress", sd::widgets::VariableType::Double);
    tile.SetWidgetType("double.progress");
    tile.SetProgressBarProperties(-1.0, 1.0);

    tile.SetDoubleValue(0.0);

    QProgressBar* progressBar = FindProgressBar(tile);
    ASSERT_NE(progressBar, nullptr);
    EXPECT_EQ(progressBar->value(), 50);
}
