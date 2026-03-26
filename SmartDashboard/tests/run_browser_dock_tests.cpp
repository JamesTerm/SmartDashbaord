// Ian: Tests for RunBrowserDock — parsing, tree construction, signal emission,
// and edge-case handling.  JSON test fixtures are written to a temp directory
// per test to avoid external file dependencies.

#include "widgets/run_browser_dock.h"

#include <QApplication>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTreeView>

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
        static std::unique_ptr<QApplication> app =
            std::make_unique<QApplication>(argc, argv);
        return app.get();
    }

    /// Write @p content to a new file called @p fileName inside @p dir.
    /// Returns the full path, or an empty string on failure.
    QString WriteFixture(const QTemporaryDir& dir, const QString& fileName, const QByteArray& content)
    {
        const QString path = dir.filePath(fileName);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            return {};
        }
        f.write(content);
        f.close();
        return path;
    }

    /// A minimal valid capture-session with two signals.
    const QByteArray kMinimalValidJson = R"({
        "metadata": {
            "label": "TestRun",
            "run_id": "run-001",
            "start_time_utc": "2026-03-25T10:00:00Z",
            "duration_sec": 15.5,
            "captured_update_count": 42,
            "tags": { "mode": "teleop", "team": "1234" }
        },
        "signals": [
            { "key": "flush_fence/TotalMs", "type": "double", "sample_count": 100 },
            { "key": "flush_fence/Count", "type": "int", "sample_count": 100 }
        ]
    })";

    /// A capture-session with deeply nested keys.
    const QByteArray kNestedKeysJson = R"({
        "signals": [
            { "key": "a/b/c/d", "type": "double", "sample_count": 5 },
            { "key": "a/b/c/e", "type": "double", "sample_count": 3 },
            { "key": "a/b/f", "type": "bool", "sample_count": 7 },
            { "key": "x", "type": "string", "sample_count": 1 }
        ]
    })";

    /// A capture-session with sample_count omitted but samples array present.
    const QByteArray kSampleCountFallbackJson = R"({
        "signals": [
            { "key": "temp/Value", "type": "double", "samples": [1.0, 2.0, 3.0] }
        ]
    })";

    /// A capture-session where some signals have empty or slash-only keys.
    const QByteArray kEdgeCaseKeysJson = R"({
        "signals": [
            { "key": "", "type": "double", "sample_count": 1 },
            { "key": "/", "type": "double", "sample_count": 1 },
            { "key": "///", "type": "double", "sample_count": 1 },
            { "key": "valid/key", "type": "double", "sample_count": 5 }
        ]
    })";

    /// A capture-session where signals array is empty.
    const QByteArray kEmptySignalsJson = R"({
        "metadata": { "label": "EmptyRun" },
        "signals": []
    })";

    /// A valid JSON object but missing the "signals" key entirely.
    const QByteArray kMissingSignalsKey = R"({
        "metadata": { "label": "NoSignals" }
    })";

    /// Not valid JSON at all.
    const QByteArray kMalformedJson = R"({ this is not json })";

    /// Valid JSON but not an object (it's an array).
    const QByteArray kJsonArrayNotObject = R"([ 1, 2, 3 ])";

    /// An empty file.
    const QByteArray kEmptyFile = "";

    /// Whitespace-only file.
    const QByteArray kWhitespaceFile = "   \n\t  \n  ";

    /// Valid JSON with non-object items in the signals array.
    const QByteArray kNonObjectSignalItems = R"({
        "signals": [ "not-an-object", 42, null, { "key": "good/signal", "type": "double", "sample_count": 10 } ]
    })";

    /// Capture-session with metadata but no label (falls back to filename).
    const QByteArray kNoLabelJson = R"({
        "metadata": { "run_id": "run-nolabel", "duration_sec": 3.14 },
        "signals": [
            { "key": "motor/RPM", "type": "double", "sample_count": 50 }
        ]
    })";
}

// ============================================================================
// Parsing tests (via ParseCaptureSessionFileForTesting)
// ============================================================================

TEST(RunBrowserDockTests, ParseValidFileExtractsMetadata)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "valid.json", kMinimalValidJson);
    ASSERT_FALSE(path.isEmpty());

    sd::widgets::LoadedRun run;
    ASSERT_TRUE(sd::widgets::RunBrowserDock::ParseCaptureSessionFileForTesting(path, run));

    EXPECT_EQ(run.metadata.label, "TestRun");
    EXPECT_EQ(run.metadata.runId, "run-001");
    EXPECT_EQ(run.metadata.startTimeUtc, "2026-03-25T10:00:00Z");
    EXPECT_DOUBLE_EQ(run.metadata.durationSec, 15.5);
    EXPECT_EQ(run.metadata.capturedUpdateCount, 42);
    EXPECT_EQ(run.metadata.tags.size(), 2u);
    EXPECT_EQ(run.metadata.tags["mode"], "teleop");
    EXPECT_EQ(run.metadata.tags["team"], "1234");
}

TEST(RunBrowserDockTests, ParseValidFileExtractsSignals)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "valid.json", kMinimalValidJson);
    ASSERT_FALSE(path.isEmpty());

    sd::widgets::LoadedRun run;
    ASSERT_TRUE(sd::widgets::RunBrowserDock::ParseCaptureSessionFileForTesting(path, run));

    ASSERT_EQ(run.signalInfos.size(), 2u);
    EXPECT_EQ(run.signalInfos[0].key, "flush_fence/TotalMs");
    EXPECT_EQ(run.signalInfos[0].type, "double");
    EXPECT_EQ(run.signalInfos[0].sampleCount, 100);
    EXPECT_EQ(run.signalInfos[1].key, "flush_fence/Count");
    EXPECT_EQ(run.signalInfos[1].type, "int");
}

TEST(RunBrowserDockTests, ParseFallsBackToSamplesArrayCount)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "fallback.json", kSampleCountFallbackJson);

    sd::widgets::LoadedRun run;
    ASSERT_TRUE(sd::widgets::RunBrowserDock::ParseCaptureSessionFileForTesting(path, run));
    ASSERT_EQ(run.signalInfos.size(), 1u);
    EXPECT_EQ(run.signalInfos[0].sampleCount, 3);
}

TEST(RunBrowserDockTests, ParseRejectsEmptySignalsArray)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "empty_signals.json", kEmptySignalsJson);

    sd::widgets::LoadedRun run;
    EXPECT_FALSE(sd::widgets::RunBrowserDock::ParseCaptureSessionFileForTesting(path, run));
}

TEST(RunBrowserDockTests, ParseRejectsMissingSignalsKey)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "no_signals.json", kMissingSignalsKey);

    sd::widgets::LoadedRun run;
    EXPECT_FALSE(sd::widgets::RunBrowserDock::ParseCaptureSessionFileForTesting(path, run));
}

TEST(RunBrowserDockTests, ParseRejectsMalformedJson)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "bad.json", kMalformedJson);

    sd::widgets::LoadedRun run;
    EXPECT_FALSE(sd::widgets::RunBrowserDock::ParseCaptureSessionFileForTesting(path, run));
}

TEST(RunBrowserDockTests, ParseRejectsJsonArray)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "array.json", kJsonArrayNotObject);

    sd::widgets::LoadedRun run;
    EXPECT_FALSE(sd::widgets::RunBrowserDock::ParseCaptureSessionFileForTesting(path, run));
}

TEST(RunBrowserDockTests, ParseRejectsEmptyFile)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "empty.json", kEmptyFile);

    sd::widgets::LoadedRun run;
    EXPECT_FALSE(sd::widgets::RunBrowserDock::ParseCaptureSessionFileForTesting(path, run));
}

TEST(RunBrowserDockTests, ParseRejectsWhitespaceOnlyFile)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "ws.json", kWhitespaceFile);

    sd::widgets::LoadedRun run;
    EXPECT_FALSE(sd::widgets::RunBrowserDock::ParseCaptureSessionFileForTesting(path, run));
}

TEST(RunBrowserDockTests, ParseRejectsNonexistentFile)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::LoadedRun run;
    EXPECT_FALSE(sd::widgets::RunBrowserDock::ParseCaptureSessionFileForTesting(
        "C:/nonexistent/path/to/file.json", run));
}

TEST(RunBrowserDockTests, ParseSkipsNonObjectSignalEntries)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "mixed.json", kNonObjectSignalItems);

    sd::widgets::LoadedRun run;
    ASSERT_TRUE(sd::widgets::RunBrowserDock::ParseCaptureSessionFileForTesting(path, run));
    // Only the one valid object entry should survive.
    ASSERT_EQ(run.signalInfos.size(), 1u);
    EXPECT_EQ(run.signalInfos[0].key, "good/signal");
}

TEST(RunBrowserDockTests, ParseDropsSignalsWithEmptyKey)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "edge_keys.json", kEdgeCaseKeysJson);

    sd::widgets::LoadedRun run;
    ASSERT_TRUE(sd::widgets::RunBrowserDock::ParseCaptureSessionFileForTesting(path, run));

    // Empty key ("") is dropped at parse time.
    // "/" and "///" pass the isEmpty check but consist only of slashes.
    // "valid/key" is valid.
    // The exact count depends on the parser: empty-key signals are dropped,
    // but "/" and "///" are not empty strings, so they pass through.
    // We expect 3 signals in the parsed data (/, ///, valid/key).
    ASSERT_EQ(run.signalInfos.size(), 3u);
    EXPECT_EQ(run.signalInfos[2].key, "valid/key");
}

TEST(RunBrowserDockTests, ParseNormalizesTypeToLowercase)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QByteArray json = R"({
        "signals": [
            { "key": "sig/A", "type": "  DOUBLE  ", "sample_count": 1 }
        ]
    })";
    const QString path = WriteFixture(tmp, "upper.json", json);

    sd::widgets::LoadedRun run;
    ASSERT_TRUE(sd::widgets::RunBrowserDock::ParseCaptureSessionFileForTesting(path, run));
    ASSERT_EQ(run.signalInfos.size(), 1u);
    EXPECT_EQ(run.signalInfos[0].type, "double");
}

// ============================================================================
// Dock widget / tree construction tests
// ============================================================================

TEST(RunBrowserDockTests, DockStartsEmpty)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    EXPECT_EQ(dock.RunCount(), 0);

    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->rowCount(), 0);
}

TEST(RunBrowserDockTests, AddRunFromFileIncreasesRunCount)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    EXPECT_TRUE(dock.AddRunFromFile(path));
    EXPECT_EQ(dock.RunCount(), 1);
}

TEST(RunBrowserDockTests, AddRunFromFileReturnsFalseForInvalidFile)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "bad.json", kMalformedJson);

    sd::widgets::RunBrowserDock dock;
    EXPECT_FALSE(dock.AddRunFromFile(path));
    EXPECT_EQ(dock.RunCount(), 0);
}

TEST(RunBrowserDockTests, AddRunsFromFilesReturnSuccessCount)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString good1 = WriteFixture(tmp, "good1.json", kMinimalValidJson);
    const QString good2 = WriteFixture(tmp, "good2.json", kNestedKeysJson);
    const QString bad = WriteFixture(tmp, "bad.json", kMalformedJson);

    sd::widgets::RunBrowserDock dock;
    const int loaded = dock.AddRunsFromFiles({good1, bad, good2});
    EXPECT_EQ(loaded, 2);
    EXPECT_EQ(dock.RunCount(), 2);
}

TEST(RunBrowserDockTests, ClearAllRunsResetsState)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);
    ASSERT_EQ(dock.RunCount(), 1);

    dock.ClearAllRuns();
    EXPECT_EQ(dock.RunCount(), 0);
    EXPECT_EQ(dock.GetTreeModelForTesting()->rowCount(), 0);
}

TEST(RunBrowserDockTests, TreeHasOneTopLevelNodePerRun)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path1 = WriteFixture(tmp, "run1.json", kMinimalValidJson);
    const QString path2 = WriteFixture(tmp, "run2.json", kNestedKeysJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path1);
    dock.AddRunFromFile(path2);

    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    EXPECT_EQ(model->rowCount(), 2);
}

TEST(RunBrowserDockTests, TreeUsesLabelWhenAvailable)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    ASSERT_EQ(model->rowCount(), 1);
    const QStandardItem* runItem = model->item(0, 0);
    EXPECT_EQ(runItem->text(), "TestRun");
}

TEST(RunBrowserDockTests, TreeFallsBackToFileNameWhenNoLabel)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "my_run_data.json", kNoLabelJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    ASSERT_EQ(model->rowCount(), 1);
    const QStandardItem* runItem = model->item(0, 0);
    EXPECT_EQ(runItem->text(), "my_run_data.json");
}

TEST(RunBrowserDockTests, TreeGroupsSignalsBySlashPrefix)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    // kMinimalValidJson has "flush_fence/TotalMs" and "flush_fence/Count".
    // Tree should be: RunItem -> "flush_fence" folder -> "TotalMs", "Count"
    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    ASSERT_EQ(model->rowCount(), 1);
    const QStandardItem* runItem = model->item(0, 0);

    // One group folder ("flush_fence") under the run.
    ASSERT_EQ(runItem->rowCount(), 1);
    const QStandardItem* groupItem = runItem->child(0, 0);
    EXPECT_EQ(groupItem->text(), "flush_fence");

    // Two leaf signals under the group.
    ASSERT_EQ(groupItem->rowCount(), 2);
    EXPECT_EQ(groupItem->child(0, 0)->text(), "TotalMs");
    EXPECT_EQ(groupItem->child(1, 0)->text(), "Count");
}

TEST(RunBrowserDockTests, TreeHandlesDeeplyNestedKeys)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "nested.json", kNestedKeysJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    // kNestedKeysJson has: a/b/c/d, a/b/c/e, a/b/f, x
    // Expected tree:
    //   run -> "a" -> "b" -> "c" -> "d", "e"
    //                      -> "f"
    //       -> "x"
    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    ASSERT_EQ(model->rowCount(), 1);
    const QStandardItem* runItem = model->item(0, 0);

    // Two top-level children under run: "a" folder and "x" leaf.
    ASSERT_EQ(runItem->rowCount(), 2);
    const QStandardItem* aFolder = runItem->child(0, 0);
    EXPECT_EQ(aFolder->text(), "a");

    const QStandardItem* xLeaf = runItem->child(1, 0);
    EXPECT_EQ(xLeaf->text(), "x");

    // a -> b
    ASSERT_EQ(aFolder->rowCount(), 1);
    const QStandardItem* bFolder = aFolder->child(0, 0);
    EXPECT_EQ(bFolder->text(), "b");

    // b -> c (folder) + f (leaf)
    ASSERT_EQ(bFolder->rowCount(), 2);
    const QStandardItem* cFolder = bFolder->child(0, 0);
    EXPECT_EQ(cFolder->text(), "c");
    const QStandardItem* fLeaf = bFolder->child(1, 0);
    EXPECT_EQ(fLeaf->text(), "f");

    // c -> d, e
    ASSERT_EQ(cFolder->rowCount(), 2);
    EXPECT_EQ(cFolder->child(0, 0)->text(), "d");
    EXPECT_EQ(cFolder->child(1, 0)->text(), "e");
}

TEST(RunBrowserDockTests, TreeOmitsSlashOnlyKeysFromDisplay)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "edges.json", kEdgeCaseKeysJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    // Of the four signals in kEdgeCaseKeysJson:
    //  - "" is dropped at parse time (empty key)
    //  - "/" and "///" pass parse but split to empty parts -> skipped in tree
    //  - "valid/key" -> "valid" folder with "key" leaf
    // So tree should have one run with one visible folder.
    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    ASSERT_EQ(model->rowCount(), 1);
    const QStandardItem* runItem = model->item(0, 0);

    // Only the "valid" group should appear.
    ASSERT_EQ(runItem->rowCount(), 1);
    EXPECT_EQ(runItem->child(0, 0)->text(), "valid");
}

TEST(RunBrowserDockTests, SingleSegmentKeyAppearsDirectlyUnderRun)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QByteArray json = R"({
        "signals": [
            { "key": "TopLevel", "type": "string", "sample_count": 1 }
        ]
    })";
    const QString path = WriteFixture(tmp, "flat.json", json);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    ASSERT_EQ(model->rowCount(), 1);
    const QStandardItem* runItem = model->item(0, 0);

    // "TopLevel" should be a direct child of the run node, no folder.
    ASSERT_EQ(runItem->rowCount(), 1);
    EXPECT_EQ(runItem->child(0, 0)->text(), "TopLevel");
}

// ============================================================================
// Signal emission tests
// ============================================================================

TEST(RunBrowserDockTests, SignalActivatedEmittedOnLeafActivation)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QSignalSpy spy(&dock, &sd::widgets::RunBrowserDock::SignalActivated);
    ASSERT_TRUE(spy.isValid());

    // Find a leaf node in the model and simulate activation.
    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    const QStandardItem* runItem = model->item(0, 0);
    const QStandardItem* groupItem = runItem->child(0, 0);     // "flush_fence"
    const QStandardItem* leafItem = groupItem->child(0, 0);    // "TotalMs"

    // The QTreeView::activated signal drives OnTreeActivated.
    // We call the view's activated signal indirectly by finding the view
    // and emitting against its model index.
    // Since OnTreeActivated is private but connected to the view, we can
    // trigger the view's activated signal.
    QTreeView* view = dock.findChild<QTreeView*>();
    ASSERT_NE(view, nullptr);
    emit view->activated(leafItem->index());

    ASSERT_EQ(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toInt(), 0);   // runIndex
    EXPECT_EQ(args.at(1).toString(), "flush_fence/TotalMs");
}

TEST(RunBrowserDockTests, RunActivatedEmittedOnRunNodeActivation)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QSignalSpy spy(&dock, &sd::widgets::RunBrowserDock::RunActivated);
    ASSERT_TRUE(spy.isValid());

    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    const QStandardItem* runItem = model->item(0, 0);

    QTreeView* view = dock.findChild<QTreeView*>();
    ASSERT_NE(view, nullptr);
    emit view->activated(runItem->index());

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.takeFirst().at(0).toInt(), 0);
}

TEST(RunBrowserDockTests, GroupActivationEmitsNoSignal)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QSignalSpy sigSpy(&dock, &sd::widgets::RunBrowserDock::SignalActivated);
    QSignalSpy runSpy(&dock, &sd::widgets::RunBrowserDock::RunActivated);

    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    const QStandardItem* groupItem = model->item(0, 0)->child(0, 0);   // "flush_fence"

    QTreeView* view = dock.findChild<QTreeView*>();
    ASSERT_NE(view, nullptr);
    emit view->activated(groupItem->index());

    EXPECT_EQ(sigSpy.count(), 0);
    EXPECT_EQ(runSpy.count(), 0);
}

// ============================================================================
// GetRunForTesting accessor
// ============================================================================

TEST(RunBrowserDockTests, GetRunForTestingReturnsLoadedData)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    const sd::widgets::LoadedRun& run = dock.GetRunForTesting(0);
    EXPECT_EQ(run.metadata.label, "TestRun");
    EXPECT_EQ(run.signalInfos.size(), 2u);
}

// ============================================================================
// Multiple runs and reload
// ============================================================================

TEST(RunBrowserDockTests, MultipleRunsHaveCorrectIndices)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path1 = WriteFixture(tmp, "run1.json", kMinimalValidJson);
    const QString path2 = WriteFixture(tmp, "run2.json", kNestedKeysJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path1);
    dock.AddRunFromFile(path2);

    ASSERT_EQ(dock.RunCount(), 2);

    // First run should have the label metadata.
    EXPECT_EQ(dock.GetRunForTesting(0).metadata.label, "TestRun");
    // Second run has no metadata label.
    EXPECT_TRUE(dock.GetRunForTesting(1).metadata.label.isEmpty());

    // Verify signal emission uses correct run index for second run.
    QSignalSpy spy(&dock, &sd::widgets::RunBrowserDock::SignalActivated);
    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    const QStandardItem* run2Item = model->item(1, 0);

    // Find a leaf in run2 — "x" is a direct child.
    const QStandardItem* xLeaf = nullptr;
    for (int i = 0; i < run2Item->rowCount(); ++i)
    {
        if (run2Item->child(i, 0)->text() == "x")
        {
            xLeaf = run2Item->child(i, 0);
            break;
        }
    }
    ASSERT_NE(xLeaf, nullptr);

    QTreeView* view = dock.findChild<QTreeView*>();
    ASSERT_NE(view, nullptr);
    emit view->activated(xLeaf->index());

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.takeFirst().at(0).toInt(), 1);   // runIndex == 1
}

TEST(RunBrowserDockTests, ClearAndReloadProducesCleanTree)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);
    ASSERT_EQ(dock.RunCount(), 1);

    dock.ClearAllRuns();
    ASSERT_EQ(dock.RunCount(), 0);
    ASSERT_EQ(dock.GetTreeModelForTesting()->rowCount(), 0);

    // Re-add the same file.
    dock.AddRunFromFile(path);
    EXPECT_EQ(dock.RunCount(), 1);
    EXPECT_EQ(dock.GetTreeModelForTesting()->rowCount(), 1);
}

TEST(RunBrowserDockTests, DetailColumnShowsSignalCountAndTags)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    ASSERT_EQ(model->rowCount(), 1);

    // Column 1 of the run row holds the details string.
    const QStandardItem* detailItem = model->item(0, 1);
    ASSERT_NE(detailItem, nullptr);
    const QString details = detailItem->text();

    // Should contain signal count.
    EXPECT_TRUE(details.contains("2 signals")) << details.toStdString();
    // Should contain tags.
    EXPECT_TRUE(details.contains("mode=teleop")) << details.toStdString();
    // Should contain duration.
    EXPECT_TRUE(details.contains("15.500s")) << details.toStdString();
}

// ============================================================================
// Checkbox behavior tests
// ============================================================================

namespace
{
    /// A capture-session with two top-level groups (motor, sensor) for
    /// testing tri-state rollup and independent group toggling.
    const QByteArray kTwoGroupsJson = R"({
        "metadata": { "label": "TwoGroups" },
        "signals": [
            { "key": "motor/RPM", "type": "double", "sample_count": 50 },
            { "key": "motor/Current", "type": "double", "sample_count": 50 },
            { "key": "sensor/Temperature", "type": "bool", "sample_count": 20 },
            { "key": "sensor/Voltage", "type": "double", "sample_count": 30 }
        ]
    })";

    /// A capture-session with a single top-level signal (no group folder).
    const QByteArray kSingleSignalJson = R"({
        "signals": [
            { "key": "TopLevel", "type": "string", "sample_count": 1 }
        ]
    })";

    /// A capture-session with three groups — used to verify tri-state when
    /// only some groups are checked.
    const QByteArray kThreeGroupsJson = R"({
        "metadata": { "label": "ThreeGroups" },
        "signals": [
            { "key": "arm/Position", "type": "double", "sample_count": 10 },
            { "key": "drive/Speed", "type": "double", "sample_count": 10 },
            { "key": "intake/Active", "type": "bool", "sample_count": 10 }
        ]
    })";

    /// Helper: find a column-0 child of @p parent whose text matches @p name.
    QStandardItem* FindChild(QStandardItem* parent, const QString& name)
    {
        for (int row = 0; row < parent->rowCount(); ++row)
        {
            QStandardItem* child = parent->child(row, 0);
            if (child != nullptr && child->text() == name)
            {
                return child;
            }
        }
        return nullptr;
    }
}

TEST(RunBrowserDockTests, CheckGroupPopulatesCheckedKeys)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "two_groups.json", kTwoGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);
    QStandardItem* motorGroup = FindChild(runItem, "motor");
    ASSERT_NE(motorGroup, nullptr);

    // Check the "motor" group.
    motorGroup->setCheckState(Qt::Checked);

    QSet<QString> checked = dock.GetCheckedSignalKeysForTesting();
    EXPECT_TRUE(checked.contains("motor/RPM"));
    EXPECT_TRUE(checked.contains("motor/Current"));
    // sensor group remains unchecked.
    EXPECT_FALSE(checked.contains("sensor/Temperature"));
    EXPECT_FALSE(checked.contains("sensor/Voltage"));
}

TEST(RunBrowserDockTests, UncheckGroupClearsCheckedKeys)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "two_groups.json", kTwoGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);
    QStandardItem* motorGroup = FindChild(runItem, "motor");
    ASSERT_NE(motorGroup, nullptr);

    // Check then uncheck.
    motorGroup->setCheckState(Qt::Checked);
    ASSERT_FALSE(dock.GetCheckedSignalKeysForTesting().isEmpty());

    motorGroup->setCheckState(Qt::Unchecked);
    EXPECT_TRUE(dock.GetCheckedSignalKeysForTesting().isEmpty());
}

TEST(RunBrowserDockTests, RunTriStatePartialWhenSomeGroupsChecked)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "two_groups.json", kTwoGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);
    QStandardItem* motorGroup = FindChild(runItem, "motor");
    QStandardItem* sensorGroup = FindChild(runItem, "sensor");
    ASSERT_NE(motorGroup, nullptr);
    ASSERT_NE(sensorGroup, nullptr);

    // Initially unchecked.
    EXPECT_EQ(runItem->checkState(), Qt::Unchecked);

    // Check only motor → run should be PartiallyChecked.
    motorGroup->setCheckState(Qt::Checked);
    EXPECT_EQ(runItem->checkState(), Qt::PartiallyChecked);
}

TEST(RunBrowserDockTests, RunFullyCheckedWhenAllGroupsChecked)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "two_groups.json", kTwoGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);
    QStandardItem* motorGroup = FindChild(runItem, "motor");
    QStandardItem* sensorGroup = FindChild(runItem, "sensor");
    ASSERT_NE(motorGroup, nullptr);
    ASSERT_NE(sensorGroup, nullptr);

    // Check both → run should be fully Checked.
    motorGroup->setCheckState(Qt::Checked);
    sensorGroup->setCheckState(Qt::Checked);
    EXPECT_EQ(runItem->checkState(), Qt::Checked);
}

TEST(RunBrowserDockTests, RunUncheckedWhenAllGroupsUnchecked)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "two_groups.json", kTwoGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);
    QStandardItem* motorGroup = FindChild(runItem, "motor");
    QStandardItem* sensorGroup = FindChild(runItem, "sensor");

    // Check both, then uncheck both.
    motorGroup->setCheckState(Qt::Checked);
    sensorGroup->setCheckState(Qt::Checked);
    ASSERT_EQ(runItem->checkState(), Qt::Checked);

    motorGroup->setCheckState(Qt::Unchecked);
    sensorGroup->setCheckState(Qt::Unchecked);
    EXPECT_EQ(runItem->checkState(), Qt::Unchecked);
}

TEST(RunBrowserDockTests, RunCheckPushesToAllGroupChildren)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "two_groups.json", kTwoGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);

    // Check the run node — both groups should become checked.
    runItem->setCheckState(Qt::Checked);

    QStandardItem* motorGroup = FindChild(runItem, "motor");
    QStandardItem* sensorGroup = FindChild(runItem, "sensor");
    ASSERT_NE(motorGroup, nullptr);
    ASSERT_NE(sensorGroup, nullptr);

    EXPECT_EQ(motorGroup->checkState(), Qt::Checked);
    EXPECT_EQ(sensorGroup->checkState(), Qt::Checked);

    // All signals should be in the checked set.
    QSet<QString> checked = dock.GetCheckedSignalKeysForTesting();
    EXPECT_EQ(checked.size(), 4);
    EXPECT_TRUE(checked.contains("motor/RPM"));
    EXPECT_TRUE(checked.contains("motor/Current"));
    EXPECT_TRUE(checked.contains("sensor/Temperature"));
    EXPECT_TRUE(checked.contains("sensor/Voltage"));
}

TEST(RunBrowserDockTests, RunUncheckClearsAllGroupChildren)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "two_groups.json", kTwoGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);

    // Check then uncheck the run node.
    runItem->setCheckState(Qt::Checked);
    ASSERT_FALSE(dock.GetCheckedSignalKeysForTesting().isEmpty());

    runItem->setCheckState(Qt::Unchecked);

    QStandardItem* motorGroup = FindChild(runItem, "motor");
    QStandardItem* sensorGroup = FindChild(runItem, "sensor");
    EXPECT_EQ(motorGroup->checkState(), Qt::Unchecked);
    EXPECT_EQ(sensorGroup->checkState(), Qt::Unchecked);
    EXPECT_TRUE(dock.GetCheckedSignalKeysForTesting().isEmpty());
}

TEST(RunBrowserDockTests, CheckedSignalsChangedEmittedOnGroupToggle)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "two_groups.json", kTwoGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QSignalSpy spy(&dock, &sd::widgets::RunBrowserDock::CheckedSignalsChanged);
    ASSERT_TRUE(spy.isValid());

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);
    QStandardItem* motorGroup = FindChild(runItem, "motor");
    ASSERT_NE(motorGroup, nullptr);

    motorGroup->setCheckState(Qt::Checked);

    // At least one emission should have occurred.
    ASSERT_GE(spy.count(), 1);

    // Last emission should contain the motor signals.
    const QList<QVariant> args = spy.last();
    const auto keys = args.at(0).value<QSet<QString>>();
    const auto typeMap = args.at(1).value<QMap<QString, QString>>();

    EXPECT_TRUE(keys.contains("motor/RPM"));
    EXPECT_TRUE(keys.contains("motor/Current"));
    EXPECT_EQ(typeMap.value("motor/RPM"), "double");
    EXPECT_EQ(typeMap.value("motor/Current"), "double");
}

TEST(RunBrowserDockTests, CheckedSignalsChangedEmittedOnRunToggle)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "two_groups.json", kTwoGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QSignalSpy spy(&dock, &sd::widgets::RunBrowserDock::CheckedSignalsChanged);
    ASSERT_TRUE(spy.isValid());

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);

    runItem->setCheckState(Qt::Checked);

    // Signal should fire; last emission should include all 4 signals.
    ASSERT_GE(spy.count(), 1);

    const QList<QVariant> args = spy.last();
    const auto keys = args.at(0).value<QSet<QString>>();
    EXPECT_EQ(keys.size(), 4);
}

TEST(RunBrowserDockTests, CheckedSignalsChangedCarriesTypeMap)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "two_groups.json", kTwoGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QSignalSpy spy(&dock, &sd::widgets::RunBrowserDock::CheckedSignalsChanged);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);
    runItem->setCheckState(Qt::Checked);

    ASSERT_GE(spy.count(), 1);
    const auto typeMap = spy.last().at(1).value<QMap<QString, QString>>();

    EXPECT_EQ(typeMap.value("motor/RPM"), "double");
    EXPECT_EQ(typeMap.value("motor/Current"), "double");
    EXPECT_EQ(typeMap.value("sensor/Temperature"), "bool");
    EXPECT_EQ(typeMap.value("sensor/Voltage"), "double");
}

TEST(RunBrowserDockTests, ClearAllRunsResetsCheckedState)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "two_groups.json", kTwoGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);
    runItem->setCheckState(Qt::Checked);
    ASSERT_FALSE(dock.GetCheckedSignalKeysForTesting().isEmpty());

    dock.ClearAllRuns();

    EXPECT_TRUE(dock.GetCheckedSignalKeysForTesting().isEmpty());
    EXPECT_EQ(dock.GetTreeModelForTesting()->rowCount(), 0);
}

TEST(RunBrowserDockTests, MultipleRunsCheckIndependent)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path1 = WriteFixture(tmp, "run1.json", kTwoGroupsJson);
    const QString path2 = WriteFixture(tmp, "run2.json", kThreeGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path1);
    dock.AddRunFromFile(path2);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    ASSERT_EQ(model->rowCount(), 2);

    // Check a group in run1 only.
    QStandardItem* run1Item = model->item(0, 0);
    QStandardItem* motorGroup = FindChild(run1Item, "motor");
    ASSERT_NE(motorGroup, nullptr);
    motorGroup->setCheckState(Qt::Checked);

    QSet<QString> checked = dock.GetCheckedSignalKeysForTesting();
    EXPECT_TRUE(checked.contains("motor/RPM"));
    EXPECT_TRUE(checked.contains("motor/Current"));
    // Run2 signals should not be checked.
    EXPECT_FALSE(checked.contains("arm/Position"));
    EXPECT_FALSE(checked.contains("drive/Speed"));
    EXPECT_FALSE(checked.contains("intake/Active"));
}

TEST(RunBrowserDockTests, MultipleRunsBothCheckedAggregateKeys)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path1 = WriteFixture(tmp, "run1.json", kTwoGroupsJson);
    const QString path2 = WriteFixture(tmp, "run2.json", kThreeGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path1);
    dock.AddRunFromFile(path2);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* run1Item = model->item(0, 0);
    QStandardItem* run2Item = model->item(1, 0);

    // Check both runs entirely.
    run1Item->setCheckState(Qt::Checked);
    run2Item->setCheckState(Qt::Checked);

    QSet<QString> checked = dock.GetCheckedSignalKeysForTesting();
    // Run1: motor/RPM, motor/Current, sensor/Temperature, sensor/Voltage
    // Run2: arm/Position, drive/Speed, intake/Active
    EXPECT_EQ(checked.size(), 7);
    EXPECT_TRUE(checked.contains("motor/RPM"));
    EXPECT_TRUE(checked.contains("arm/Position"));
    EXPECT_TRUE(checked.contains("intake/Active"));
}

TEST(RunBrowserDockTests, TriStateWithThreeGroups)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "three_groups.json", kThreeGroupsJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);
    QStandardItem* armGroup = FindChild(runItem, "arm");
    QStandardItem* driveGroup = FindChild(runItem, "drive");
    QStandardItem* intakeGroup = FindChild(runItem, "intake");
    ASSERT_NE(armGroup, nullptr);
    ASSERT_NE(driveGroup, nullptr);
    ASSERT_NE(intakeGroup, nullptr);

    // Check 1 of 3 → partial.
    armGroup->setCheckState(Qt::Checked);
    EXPECT_EQ(runItem->checkState(), Qt::PartiallyChecked);

    // Check 2 of 3 → still partial.
    driveGroup->setCheckState(Qt::Checked);
    EXPECT_EQ(runItem->checkState(), Qt::PartiallyChecked);

    // Check 3 of 3 → fully checked.
    intakeGroup->setCheckState(Qt::Checked);
    EXPECT_EQ(runItem->checkState(), Qt::Checked);

    // Uncheck 1 → back to partial.
    armGroup->setCheckState(Qt::Unchecked);
    EXPECT_EQ(runItem->checkState(), Qt::PartiallyChecked);
}

TEST(RunBrowserDockTests, SignalLeavesAreNotCheckable)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);
    QStandardItem* groupItem = runItem->child(0, 0);  // "flush_fence"

    // Group and run should be checkable.
    EXPECT_TRUE(runItem->isCheckable());
    EXPECT_TRUE(groupItem->isCheckable());

    // Signal leaves should not be checkable.
    for (int row = 0; row < groupItem->rowCount(); ++row)
    {
        QStandardItem* leaf = groupItem->child(row, 0);
        ASSERT_NE(leaf, nullptr);
        EXPECT_FALSE(leaf->isCheckable()) << "Signal leaf '" << leaf->text().toStdString() << "' should not be checkable";
    }
}

TEST(RunBrowserDockTests, RunNodeStartsUnchecked)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);
    EXPECT_EQ(runItem->checkState(), Qt::Unchecked);
    EXPECT_TRUE(dock.GetCheckedSignalKeysForTesting().isEmpty());
}

TEST(RunBrowserDockTests, GroupNodeStartsUnchecked)
{
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    dock.AddRunFromFile(path);

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    QStandardItem* runItem = model->item(0, 0);
    QStandardItem* groupItem = runItem->child(0, 0);
    EXPECT_EQ(groupItem->checkState(), Qt::Unchecked);
}

// ============================================================================
// Streaming mode tests
// ============================================================================

namespace
{
    /// Helper: return the streaming root node (the synthetic transport-named
    /// folder) from the tree model.  Returns nullptr if not found.
    QStandardItem* GetStreamingRoot(QStandardItemModel* model)
    {
        if (model == nullptr || model->rowCount() == 0)
        {
            return nullptr;
        }
        // The streaming root is the sole top-level item with kNodeKindRun.
        QStandardItem* item = model->item(0, 0);
        if (item != nullptr && item->data(Qt::UserRole + 100).toInt() == 0)  // kNodeKindRun
        {
            return item;
        }
        return nullptr;
    }

    /// The label used for the streaming root in all tests.
    const QString kTestTransportLabel = QStringLiteral("TestTransport");
}

TEST(RunBrowserDockTests, StreamingOnTileAddedCreatesTreeNodes)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");

    EXPECT_TRUE(dock.IsStreamingModeForTesting());
    EXPECT_EQ(dock.DiscoveredKeyCount(), 1);
    EXPECT_TRUE(dock.HasDiscoveredKey("motor/RPM"));

    // Tree: root ("TestTransport") -> "motor" (group) -> "RPM" (signal)
    QStandardItemModel* model = dock.GetTreeModelForTesting();
    ASSERT_EQ(model->rowCount(), 1);  // One top-level node: the streaming root.

    QStandardItem* root = GetStreamingRoot(model);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->text(), kTestTransportLabel);
    ASSERT_EQ(root->rowCount(), 1);

    const QStandardItem* motorGroup = root->child(0, 0);
    EXPECT_EQ(motorGroup->text(), "motor");
    ASSERT_EQ(motorGroup->rowCount(), 1);
    EXPECT_EQ(motorGroup->child(0, 0)->text(), "RPM");
}

TEST(RunBrowserDockTests, StreamingGroupStartsChecked)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");

    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->rowCount(), 1);
    const QStandardItem* motorGroup = root->child(0, 0);
    EXPECT_EQ(motorGroup->checkState(), Qt::Checked);
}

TEST(RunBrowserDockTests, StreamingAllKeysVisibleByDefault)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("motor/Current", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    QSet<QString> checked = dock.GetCheckedSignalKeysForTesting();
    EXPECT_EQ(checked.size(), 3);
    EXPECT_TRUE(checked.contains("motor/RPM"));
    EXPECT_TRUE(checked.contains("motor/Current"));
    EXPECT_TRUE(checked.contains("sensor/Temperature"));
}

TEST(RunBrowserDockTests, StreamingDuplicateKeyIsNoOp)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("motor/RPM", "double");

    EXPECT_EQ(dock.DiscoveredKeyCount(), 1);
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    // Only one group under root.
    ASSERT_EQ(root->rowCount(), 1);
    // Only one signal leaf under the group.
    EXPECT_EQ(root->child(0, 0)->rowCount(), 1);
}

TEST(RunBrowserDockTests, StreamingEmptyKeyIsIgnored)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.OnTileAdded("", "double");

    EXPECT_EQ(dock.DiscoveredKeyCount(), 0);
    EXPECT_FALSE(dock.IsStreamingModeForTesting());
}

TEST(RunBrowserDockTests, StreamingRootNodeInTree)
{
    // Ian: With the synthetic root node, the sole top-level item is the
    // streaming root (kNodeKindRun) named after the transport.  Groups and
    // signal leaves nest underneath it.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");

    QStandardItemModel* model = dock.GetTreeModelForTesting();
    ASSERT_EQ(model->rowCount(), 1);

    const QStandardItem* topItem = model->item(0, 0);
    // The top item should be the streaming root (kNodeKindRun = 0).
    EXPECT_EQ(topItem->data(Qt::UserRole + 100).toInt(), 0);  // kNodeKindRun
    EXPECT_EQ(topItem->text(), kTestTransportLabel);
    EXPECT_TRUE(topItem->isCheckable());
    EXPECT_EQ(topItem->checkState(), Qt::Checked);
}

TEST(RunBrowserDockTests, StreamingSingleSegmentKeyIsUnderRoot)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("TopLevel", "string");

    EXPECT_EQ(dock.DiscoveredKeyCount(), 1);

    // Single-segment key has no group — sits directly under the streaming root.
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->rowCount(), 1);
    const QStandardItem* item = root->child(0, 0);
    EXPECT_EQ(item->text(), "TopLevel");
    // kNodeKindSignal = 2
    EXPECT_EQ(item->data(Qt::UserRole + 100).toInt(), 2);
}

TEST(RunBrowserDockTests, StreamingSingleSegmentKeyAlwaysVisible)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("TopLevel", "string");

    // Single-segment keys have no group checkbox — visible when root is checked.
    QSet<QString> checked = dock.GetCheckedSignalKeysForTesting();
    EXPECT_TRUE(checked.contains("TopLevel"));
}

TEST(RunBrowserDockTests, StreamingDeeplyNestedKeys)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("a/b/c/d", "double");
    dock.OnTileAdded("a/b/c/e", "double");
    dock.OnTileAdded("a/b/f", "bool");

    EXPECT_EQ(dock.DiscoveredKeyCount(), 3);

    // Tree: root -> "a" -> "b" -> "c" -> "d", "e"
    //                           -> "f"
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->rowCount(), 1);

    const QStandardItem* aGroup = root->child(0, 0);
    EXPECT_EQ(aGroup->text(), "a");
    ASSERT_EQ(aGroup->rowCount(), 1);

    const QStandardItem* bGroup = aGroup->child(0, 0);
    EXPECT_EQ(bGroup->text(), "b");
    ASSERT_EQ(bGroup->rowCount(), 2);

    const QStandardItem* cGroup = bGroup->child(0, 0);
    EXPECT_EQ(cGroup->text(), "c");
    ASSERT_EQ(cGroup->rowCount(), 2);
    EXPECT_EQ(cGroup->child(0, 0)->text(), "d");
    EXPECT_EQ(cGroup->child(1, 0)->text(), "e");

    const QStandardItem* fLeaf = bGroup->child(1, 0);
    EXPECT_EQ(fLeaf->text(), "f");
}

TEST(RunBrowserDockTests, StreamingUncheckGroupHidesSignals)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("motor/Current", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    // All visible initially.
    ASSERT_EQ(dock.GetCheckedSignalKeysForTesting().size(), 3);

    // Uncheck "motor" group.
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* motorGroup = FindChild(root, "motor");
    ASSERT_NE(motorGroup, nullptr);
    motorGroup->setCheckState(Qt::Unchecked);

    QSet<QString> checked = dock.GetCheckedSignalKeysForTesting();
    EXPECT_FALSE(checked.contains("motor/RPM"));
    EXPECT_FALSE(checked.contains("motor/Current"));
    EXPECT_TRUE(checked.contains("sensor/Temperature"));
}

TEST(RunBrowserDockTests, StreamingRecheckGroupShowsSignals)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    // Uncheck then recheck.
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* motorGroup = FindChild(root, "motor");
    ASSERT_NE(motorGroup, nullptr);

    motorGroup->setCheckState(Qt::Unchecked);
    EXPECT_FALSE(dock.GetCheckedSignalKeysForTesting().contains("motor/RPM"));

    motorGroup->setCheckState(Qt::Checked);
    EXPECT_TRUE(dock.GetCheckedSignalKeysForTesting().contains("motor/RPM"));
}

TEST(RunBrowserDockTests, StreamingClearDiscoveredKeysResetsState)
{
    // Ian: ClearDiscoveredKeys clears the tree contents (keys, groups, leaves)
    // but stays in streaming mode with a fresh root node.  This is critical
    // because TilesCleared is immediately followed by TileAdded signals when
    // loading a layout — if we exited streaming mode, those would be no-ops
    // and the tree would be empty with all tiles hidden.
    //
    // Stress test: connect to Direct, then File → Open Layout (Replace).
    // OnLoadLayoutReplace calls OnClearWidgets (→ ClearDiscoveredKeys) then
    // LoadLayoutFromPath (→ TileAdded per tile).
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    ASSERT_TRUE(dock.IsStreamingModeForTesting());
    ASSERT_EQ(dock.DiscoveredKeyCount(), 1);

    dock.ClearDiscoveredKeys();

    // Stays in streaming mode with empty root — ready for TileAdded signals.
    EXPECT_TRUE(dock.IsStreamingModeForTesting());
    EXPECT_EQ(dock.DiscoveredKeyCount(), 0);
    // Root node is recreated (1 row), but no children.
    EXPECT_EQ(dock.GetTreeModelForTesting()->rowCount(), 1);
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->rowCount(), 0);
}

TEST(RunBrowserDockTests, StreamingClearAllRunsAlsoResetsStreamingState)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    ASSERT_TRUE(dock.IsStreamingModeForTesting());

    dock.ClearAllRuns();

    EXPECT_FALSE(dock.IsStreamingModeForTesting());
    EXPECT_EQ(dock.DiscoveredKeyCount(), 0);
}

TEST(RunBrowserDockTests, StreamingCheckedSignalsChangedEmittedOnAddKey)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);

    QSignalSpy spy(&dock, &sd::widgets::RunBrowserDock::CheckedSignalsChanged);
    ASSERT_TRUE(spy.isValid());

    dock.OnTileAdded("motor/RPM", "double");

    ASSERT_GE(spy.count(), 1);
    const auto keys = spy.last().at(0).value<QSet<QString>>();
    const auto typeMap = spy.last().at(1).value<QMap<QString, QString>>();

    EXPECT_TRUE(keys.contains("motor/RPM"));
    EXPECT_EQ(typeMap.value("motor/RPM"), "double");
}

TEST(RunBrowserDockTests, StreamingCheckedSignalsChangedCarriesTypes)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);

    QSignalSpy spy(&dock, &sd::widgets::RunBrowserDock::CheckedSignalsChanged);

    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("sensor/Active", "bool");
    dock.OnTileAdded("info/Name", "string");

    ASSERT_GE(spy.count(), 1);
    const auto typeMap = spy.last().at(1).value<QMap<QString, QString>>();

    EXPECT_EQ(typeMap.value("motor/RPM"), "double");
    EXPECT_EQ(typeMap.value("sensor/Active"), "bool");
    EXPECT_EQ(typeMap.value("info/Name"), "string");
}

TEST(RunBrowserDockTests, StreamingCheckedSignalsChangedEmittedOnGroupUncheck)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("sensor/Temp", "double");

    QSignalSpy spy(&dock, &sd::widgets::RunBrowserDock::CheckedSignalsChanged);
    ASSERT_TRUE(spy.isValid());

    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* motorGroup = FindChild(root, "motor");
    ASSERT_NE(motorGroup, nullptr);
    motorGroup->setCheckState(Qt::Unchecked);

    ASSERT_GE(spy.count(), 1);
    const auto keys = spy.last().at(0).value<QSet<QString>>();
    EXPECT_FALSE(keys.contains("motor/RPM"));
    EXPECT_TRUE(keys.contains("sensor/Temp"));
}

// ============================================================================
// Streaming mode: hidden keys (persistence of opt-outs)
// ============================================================================

TEST(RunBrowserDockTests, StreamingGetHiddenKeysReturnsUnchecked)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("motor/Current", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    // Uncheck "motor" group.
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* motorGroup = FindChild(root, "motor");
    ASSERT_NE(motorGroup, nullptr);
    motorGroup->setCheckState(Qt::Unchecked);

    QSet<QString> hidden = dock.GetHiddenDiscoveredKeys();
    EXPECT_TRUE(hidden.contains("motor/RPM"));
    EXPECT_TRUE(hidden.contains("motor/Current"));
    EXPECT_FALSE(hidden.contains("sensor/Temperature"));
}

TEST(RunBrowserDockTests, StreamingGetHiddenKeysEmptyWhenAllChecked)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    // All groups start checked — hidden set should be empty.
    QSet<QString> hidden = dock.GetHiddenDiscoveredKeys();
    EXPECT_TRUE(hidden.isEmpty());
}

TEST(RunBrowserDockTests, StreamingGetHiddenKeysEmptyWhenNotStreamingMode)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    // Not in streaming mode — should return empty.
    QSet<QString> hidden = dock.GetHiddenDiscoveredKeys();
    EXPECT_TRUE(hidden.isEmpty());
}

TEST(RunBrowserDockTests, StreamingSetHiddenKeysUnchecksGroups)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("motor/Current", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    // All checked initially.
    ASSERT_EQ(dock.GetCheckedSignalKeysForTesting().size(), 3);

    // Apply hidden keys to uncheck motor group.
    QSet<QString> hidden;
    hidden.insert("motor/RPM");
    hidden.insert("motor/Current");
    dock.SetHiddenDiscoveredKeys(hidden);

    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* motorGroup = FindChild(root, "motor");
    QStandardItem* sensorGroup = FindChild(root, "sensor");
    ASSERT_NE(motorGroup, nullptr);
    ASSERT_NE(sensorGroup, nullptr);

    EXPECT_EQ(motorGroup->checkState(), Qt::Unchecked);
    EXPECT_EQ(sensorGroup->checkState(), Qt::Checked);

    QSet<QString> checked = dock.GetCheckedSignalKeysForTesting();
    EXPECT_FALSE(checked.contains("motor/RPM"));
    EXPECT_FALSE(checked.contains("motor/Current"));
    EXPECT_TRUE(checked.contains("sensor/Temperature"));
}

TEST(RunBrowserDockTests, StreamingSetHiddenKeysPartialGroupStaysChecked)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("motor/Current", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    // Only hide one of motor's two signals — group should stay checked.
    QSet<QString> hidden;
    hidden.insert("motor/RPM");
    dock.SetHiddenDiscoveredKeys(hidden);

    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* motorGroup = FindChild(root, "motor");
    ASSERT_NE(motorGroup, nullptr);

    // motor group should stay checked because motor/Current is NOT hidden.
    EXPECT_EQ(motorGroup->checkState(), Qt::Checked);
}

TEST(RunBrowserDockTests, StreamingLazyHiddenKeysOnReconnect)
{
    // Ian: Simulates the reconnect scenario: hidden keys are set BEFORE any
    // keys arrive (tree is empty), then keys arrive via OnTileAdded.
    // Groups whose ALL descendants are hidden should start unchecked.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;

    // Step 1: Start streaming mode with an empty tree.
    dock.ClearDiscoveredKeys();
    dock.SetStreamingRootLabel(kTestTransportLabel);

    // Step 2: Pre-load hidden keys (as MainWindow would from persisted state).
    QSet<QString> hidden;
    hidden.insert("motor/RPM");
    hidden.insert("motor/Current");
    dock.SetHiddenDiscoveredKeys(hidden);

    // Step 3: Keys arrive — simulating transport reconnect.
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("motor/Current", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    // motor group should be unchecked (all its signals are hidden).
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* motorGroup = FindChild(root, "motor");
    QStandardItem* sensorGroup = FindChild(root, "sensor");
    ASSERT_NE(motorGroup, nullptr);
    ASSERT_NE(sensorGroup, nullptr);

    EXPECT_EQ(motorGroup->checkState(), Qt::Unchecked);
    EXPECT_EQ(sensorGroup->checkState(), Qt::Checked);

    // Checked set should only contain sensor keys.
    QSet<QString> checked = dock.GetCheckedSignalKeysForTesting();
    EXPECT_FALSE(checked.contains("motor/RPM"));
    EXPECT_FALSE(checked.contains("motor/Current"));
    EXPECT_TRUE(checked.contains("sensor/Temperature"));
}

TEST(RunBrowserDockTests, StreamingLazyHiddenKeysPartialGroup)
{
    // If only some signals in a group are hidden, the group stays checked
    // because there are visible descendants.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.ClearDiscoveredKeys();
    dock.SetStreamingRootLabel(kTestTransportLabel);

    QSet<QString> hidden;
    hidden.insert("motor/RPM");  // Hide only one of two motor signals.
    dock.SetHiddenDiscoveredKeys(hidden);

    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("motor/Current", "double");

    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* motorGroup = FindChild(root, "motor");
    ASSERT_NE(motorGroup, nullptr);

    // motor group should stay checked — motor/Current is not hidden.
    EXPECT_EQ(motorGroup->checkState(), Qt::Checked);
}

TEST(RunBrowserDockTests, StreamingLazyHiddenKeysNestedGroups)
{
    // Nested groups: if all descendants of a parent are hidden, the parent
    // should be unchecked.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.ClearDiscoveredKeys();
    dock.SetStreamingRootLabel(kTestTransportLabel);

    QSet<QString> hidden;
    hidden.insert("a/b/c");
    hidden.insert("a/b/d");
    dock.SetHiddenDiscoveredKeys(hidden);

    dock.OnTileAdded("a/b/c", "double");
    dock.OnTileAdded("a/b/d", "double");
    dock.OnTileAdded("x/y", "bool");

    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* aGroup = FindChild(root, "a");
    QStandardItem* xGroup = FindChild(root, "x");
    ASSERT_NE(aGroup, nullptr);
    ASSERT_NE(xGroup, nullptr);

    EXPECT_EQ(aGroup->checkState(), Qt::Unchecked);
    EXPECT_EQ(xGroup->checkState(), Qt::Checked);
}

TEST(RunBrowserDockTests, StreamingClearDiscoveredKeysAlsoClearsHiddenKeys)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");

    QSet<QString> hidden;
    hidden.insert("motor/RPM");
    dock.SetHiddenDiscoveredKeys(hidden);

    dock.ClearDiscoveredKeys();

    // After clear, adding the same key should create a checked group (no hidden set).
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");

    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* motorGroup = FindChild(root, "motor");
    ASSERT_NE(motorGroup, nullptr);
    EXPECT_EQ(motorGroup->checkState(), Qt::Checked);
}

TEST(RunBrowserDockTests, StreamingModeSwitchBackToReading)
{
    // Verify that ClearAllRuns after streaming mode allows reading mode to work.
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;

    // Enter streaming mode.
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    ASSERT_TRUE(dock.IsStreamingModeForTesting());

    // Switch to reading mode.
    dock.ClearAllRuns();
    ASSERT_FALSE(dock.IsStreamingModeForTesting());

    // Load a replay file.
    EXPECT_TRUE(dock.AddRunFromFile(path));
    EXPECT_EQ(dock.RunCount(), 1);
    EXPECT_FALSE(dock.IsStreamingModeForTesting());

    // Tree should have a run node (reading mode behavior).
    const QStandardItemModel* model = dock.GetTreeModelForTesting();
    ASSERT_EQ(model->rowCount(), 1);
    EXPECT_EQ(model->item(0, 0)->text(), "TestRun");
}

TEST(RunBrowserDockTests, StreamingCheckedSignalsChangedEmittedOnClear)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");

    QSignalSpy spy(&dock, &sd::widgets::RunBrowserDock::CheckedSignalsChanged);
    ASSERT_TRUE(spy.isValid());

    dock.ClearDiscoveredKeys();

    // Clear should emit with empty sets.
    ASSERT_GE(spy.count(), 1);
    const auto keys = spy.last().at(0).value<QSet<QString>>();
    EXPECT_TRUE(keys.isEmpty());
}

TEST(RunBrowserDockTests, StreamingMultipleGroupsIndependent)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");
    dock.OnTileAdded("intake/Active", "bool");

    // Three groups should be independently checkable, all under the root.
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->rowCount(), 3);

    QStandardItem* motorGroup = FindChild(root, "motor");
    QStandardItem* sensorGroup = FindChild(root, "sensor");
    QStandardItem* intakeGroup = FindChild(root, "intake");
    ASSERT_NE(motorGroup, nullptr);
    ASSERT_NE(sensorGroup, nullptr);
    ASSERT_NE(intakeGroup, nullptr);

    // All start checked.
    EXPECT_EQ(motorGroup->checkState(), Qt::Checked);
    EXPECT_EQ(sensorGroup->checkState(), Qt::Checked);
    EXPECT_EQ(intakeGroup->checkState(), Qt::Checked);

    // Uncheck motor only.
    motorGroup->setCheckState(Qt::Unchecked);

    QSet<QString> checked = dock.GetCheckedSignalKeysForTesting();
    EXPECT_FALSE(checked.contains("motor/RPM"));
    EXPECT_TRUE(checked.contains("sensor/Temperature"));
    EXPECT_TRUE(checked.contains("intake/Active"));
}

TEST(RunBrowserDockTests, StreamingDetailColumnShowsType)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");

    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->rowCount(), 1);

    const QStandardItem* motorGroup = root->child(0, 0);
    ASSERT_EQ(motorGroup->rowCount(), 1);

    // Column 1 of the signal row should show the type.
    const QStandardItem* signalDetail = motorGroup->child(0, 1);
    ASSERT_NE(signalDetail, nullptr);
    EXPECT_EQ(signalDetail->text(), "double");
}

TEST(RunBrowserDockTests, StreamingHasDiscoveredKeyReturnsFalseForUnknown)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");

    EXPECT_FALSE(dock.HasDiscoveredKey("sensor/Temperature"));
    EXPECT_FALSE(dock.HasDiscoveredKey(""));
    EXPECT_FALSE(dock.HasDiscoveredKey("motor"));
}

TEST(RunBrowserDockTests, StreamingGetExpandedPathsAndRestore)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("a/b/c", "double");
    dock.OnTileAdded("x/y", "bool");

    // Expand "a" group.
    QTreeView* view = dock.findChild<QTreeView*>();
    ASSERT_NE(view, nullptr);
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* aGroup = FindChild(root, "a");
    ASSERT_NE(aGroup, nullptr);
    view->expand(aGroup->index());

    QStringList expandedPaths = dock.GetExpandedPaths();
    EXPECT_FALSE(expandedPaths.isEmpty());

    // Collapse all, then restore.
    view->collapseAll();
    EXPECT_TRUE(dock.GetExpandedPaths().isEmpty());

    dock.SetExpandedPaths(expandedPaths);
    EXPECT_FALSE(dock.GetExpandedPaths().isEmpty());
}

TEST(RunBrowserDockTests, StreamingGetLoadedFilePathReturnsEmpty)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");

    // In streaming mode, there's no loaded file.
    EXPECT_TRUE(dock.GetLoadedFilePath().isEmpty());
}

TEST(RunBrowserDockTests, StreamingRootTriStatePartialWhenSomeGroupsUnchecked)
{
    // Ian: The streaming root supports tri-state just like a run node.
    // When some (but not all) groups are unchecked, root should be partial.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->checkState(), Qt::Checked);

    // Uncheck one group — root should become partially checked.
    QStandardItem* motorGroup = FindChild(root, "motor");
    ASSERT_NE(motorGroup, nullptr);
    motorGroup->setCheckState(Qt::Unchecked);

    EXPECT_EQ(root->checkState(), Qt::PartiallyChecked);
}

TEST(RunBrowserDockTests, StreamingRootUncheckPushesToAllGroupChildren)
{
    // Ian: Unchecking the streaming root should push unchecked to all groups.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->checkState(), Qt::Checked);

    // Uncheck the root — all groups should become unchecked.
    root->setCheckState(Qt::Unchecked);

    QStandardItem* motorGroup = FindChild(root, "motor");
    QStandardItem* sensorGroup = FindChild(root, "sensor");
    ASSERT_NE(motorGroup, nullptr);
    ASSERT_NE(sensorGroup, nullptr);

    EXPECT_EQ(motorGroup->checkState(), Qt::Unchecked);
    EXPECT_EQ(sensorGroup->checkState(), Qt::Unchecked);
    EXPECT_TRUE(dock.GetCheckedSignalKeysForTesting().isEmpty());
}

TEST(RunBrowserDockTests, StreamingRootCheckPushesToAllGroupChildren)
{
    // Ian: Checking the streaming root should push checked to all groups.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);

    // Uncheck root first, then re-check.
    root->setCheckState(Qt::Unchecked);
    ASSERT_TRUE(dock.GetCheckedSignalKeysForTesting().isEmpty());

    root->setCheckState(Qt::Checked);

    QStandardItem* motorGroup = FindChild(root, "motor");
    QStandardItem* sensorGroup = FindChild(root, "sensor");
    ASSERT_NE(motorGroup, nullptr);
    ASSERT_NE(sensorGroup, nullptr);

    EXPECT_EQ(motorGroup->checkState(), Qt::Checked);
    EXPECT_EQ(sensorGroup->checkState(), Qt::Checked);
    EXPECT_EQ(dock.GetCheckedSignalKeysForTesting().size(), 2);
}

// ============================================================================
// Streaming mode: OnTileRemoved tests
// ============================================================================

TEST(RunBrowserDockTests, StreamingOnTileRemovedRemovesLeafAndPrunesEmptyGroup)
{
    // Ian: When a tile is removed from the layout, OnTileRemoved should
    // remove the leaf node and prune the now-empty parent group.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    ASSERT_EQ(dock.DiscoveredKeyCount(), 1);

    dock.OnTileRemoved("motor/RPM");

    EXPECT_EQ(dock.DiscoveredKeyCount(), 0);
    EXPECT_FALSE(dock.HasDiscoveredKey("motor/RPM"));

    // The "motor" group should have been pruned since it has no children.
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->rowCount(), 0);
}

TEST(RunBrowserDockTests, StreamingOnTileRemovedLeavesOtherSiblingsIntact)
{
    // Removing one signal from a group with multiple signals should leave
    // the group and its remaining children intact.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("motor/Current", "double");
    ASSERT_EQ(dock.DiscoveredKeyCount(), 2);

    dock.OnTileRemoved("motor/RPM");

    EXPECT_EQ(dock.DiscoveredKeyCount(), 1);
    EXPECT_FALSE(dock.HasDiscoveredKey("motor/RPM"));
    EXPECT_TRUE(dock.HasDiscoveredKey("motor/Current"));

    // The "motor" group should still exist with one child.
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* motorGroup = FindChild(root, "motor");
    ASSERT_NE(motorGroup, nullptr);
    EXPECT_EQ(motorGroup->rowCount(), 1);
    EXPECT_EQ(motorGroup->child(0, 0)->text(), "Current");
}

TEST(RunBrowserDockTests, StreamingOnTileRemovedPrunesNestedEmptyGroups)
{
    // Removing a deeply nested leaf should prune all empty ancestor groups
    // up to (but not including) the streaming root.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("a/b/c/d", "double");
    ASSERT_EQ(dock.DiscoveredKeyCount(), 1);

    dock.OnTileRemoved("a/b/c/d");

    EXPECT_EQ(dock.DiscoveredKeyCount(), 0);

    // All groups (a, b, c) should be pruned.
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->rowCount(), 0);
}

TEST(RunBrowserDockTests, StreamingOnTileRemovedPartialNestedPrune)
{
    // Removing a leaf from a nested group that has a sibling group with
    // other content should only prune the empty branch, not the sibling.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("a/b/c", "double");
    dock.OnTileAdded("a/b/d", "double");
    dock.OnTileAdded("a/e", "bool");
    ASSERT_EQ(dock.DiscoveredKeyCount(), 3);

    // Remove a/b/c — a/b still has "d", so "b" stays.
    dock.OnTileRemoved("a/b/c");

    EXPECT_EQ(dock.DiscoveredKeyCount(), 2);
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* aGroup = FindChild(root, "a");
    ASSERT_NE(aGroup, nullptr);
    EXPECT_EQ(aGroup->rowCount(), 2);  // "b" group + "e" leaf

    QStandardItem* bGroup = FindChild(aGroup, "b");
    ASSERT_NE(bGroup, nullptr);
    EXPECT_EQ(bGroup->rowCount(), 1);
    EXPECT_EQ(bGroup->child(0, 0)->text(), "d");
}

TEST(RunBrowserDockTests, StreamingOnTileRemovedUpdatesRootTriState)
{
    // After removing a tile, the root's tri-state should update.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->checkState(), Qt::Checked);

    // Uncheck motor group, root becomes partial.
    QStandardItem* motorGroup = FindChild(root, "motor");
    ASSERT_NE(motorGroup, nullptr);
    motorGroup->setCheckState(Qt::Unchecked);
    EXPECT_EQ(root->checkState(), Qt::PartiallyChecked);

    // Remove the unchecked motor tile — only sensor remains (checked).
    // Root should go back to fully checked.
    dock.OnTileRemoved("motor/RPM");

    root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->checkState(), Qt::Checked);
}

TEST(RunBrowserDockTests, StreamingOnTileRemovedEmitsCheckedSignalsChanged)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    QSignalSpy spy(&dock, &sd::widgets::RunBrowserDock::CheckedSignalsChanged);
    ASSERT_TRUE(spy.isValid());

    dock.OnTileRemoved("motor/RPM");

    ASSERT_GE(spy.count(), 1);
    const auto keys = spy.last().at(0).value<QSet<QString>>();
    EXPECT_FALSE(keys.contains("motor/RPM"));
    EXPECT_TRUE(keys.contains("sensor/Temperature"));
}

TEST(RunBrowserDockTests, StreamingOnTileRemovedNoOpForUnknownKey)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");

    // Removing an unknown key should be a no-op.
    dock.OnTileRemoved("nonexistent/key");

    EXPECT_EQ(dock.DiscoveredKeyCount(), 1);
    EXPECT_TRUE(dock.HasDiscoveredKey("motor/RPM"));
}

TEST(RunBrowserDockTests, StreamingOnTileRemovedNoOpWhenNotStreamingMode)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    // Not in streaming mode — OnTileRemoved should be a no-op.
    dock.OnTileRemoved("motor/RPM");

    EXPECT_FALSE(dock.IsStreamingModeForTesting());
    EXPECT_EQ(dock.DiscoveredKeyCount(), 0);
}

TEST(RunBrowserDockTests, StreamingOnTileRemovedNoOpForEmptyKey)
{
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");

    dock.OnTileRemoved("");

    EXPECT_EQ(dock.DiscoveredKeyCount(), 1);
}

TEST(RunBrowserDockTests, StreamingOnTileRemovedSingleSegmentKey)
{
    // Single-segment keys (no slash) sit directly under the root.
    // Removing them should work correctly.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("TopLevel", "string");
    dock.OnTileAdded("motor/RPM", "double");
    ASSERT_EQ(dock.DiscoveredKeyCount(), 2);

    dock.OnTileRemoved("TopLevel");

    EXPECT_EQ(dock.DiscoveredKeyCount(), 1);
    EXPECT_FALSE(dock.HasDiscoveredKey("TopLevel"));
    EXPECT_TRUE(dock.HasDiscoveredKey("motor/RPM"));

    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    // Only "motor" group should remain (no "TopLevel" leaf).
    EXPECT_EQ(root->rowCount(), 1);
    EXPECT_EQ(root->child(0, 0)->text(), "motor");
}

TEST(RunBrowserDockTests, StreamingOnTileRemovedClearsHiddenKeyEntry)
{
    // Ian: When a tile is removed, its entry in m_streamingHiddenKeys
    // should also be cleaned up so it doesn't affect future reconnects.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("motor/Current", "double");

    // Hide motor group.
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    QStandardItem* motorGroup = FindChild(root, "motor");
    ASSERT_NE(motorGroup, nullptr);
    motorGroup->setCheckState(Qt::Unchecked);

    // Verify motor/RPM is hidden.
    QSet<QString> hidden = dock.GetHiddenDiscoveredKeys();
    EXPECT_TRUE(hidden.contains("motor/RPM"));

    // Remove motor/RPM — it should no longer appear in hidden keys.
    dock.OnTileRemoved("motor/RPM");

    hidden = dock.GetHiddenDiscoveredKeys();
    EXPECT_FALSE(hidden.contains("motor/RPM"));
}

TEST(RunBrowserDockTests, StreamingOnTileRemovedThenReaddedWorks)
{
    // Verify that after removing a tile, re-adding it works correctly.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    ASSERT_EQ(dock.DiscoveredKeyCount(), 1);

    dock.OnTileRemoved("motor/RPM");
    ASSERT_EQ(dock.DiscoveredKeyCount(), 0);

    // Re-add the same key.
    dock.OnTileAdded("motor/RPM", "double");
    EXPECT_EQ(dock.DiscoveredKeyCount(), 1);
    EXPECT_TRUE(dock.HasDiscoveredKey("motor/RPM"));

    // Should be visible (group starts checked).
    QSet<QString> checked = dock.GetCheckedSignalKeysForTesting();
    EXPECT_TRUE(checked.contains("motor/RPM"));
}

// ============================================================================
// Streaming mode: OnTileAdded no-op guard tests
// ============================================================================

TEST(RunBrowserDockTests, StreamingOnTileAddedNoOpWhenNotInitialized)
{
    // Ian: OnTileAdded should be a no-op when streaming mode has not been
    // initialized via SetStreamingRootLabel().  This prevents tiles created
    // during replay or before any transport from triggering streaming mode.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    // Do NOT call SetStreamingRootLabel.
    dock.OnTileAdded("motor/RPM", "double");

    EXPECT_FALSE(dock.IsStreamingModeForTesting());
    EXPECT_EQ(dock.DiscoveredKeyCount(), 0);
    EXPECT_EQ(dock.GetTreeModelForTesting()->rowCount(), 0);
}

// ============================================================================
// Streaming mode: ClearDiscoveredKeys as TilesCleared handler
// ============================================================================

TEST(RunBrowserDockTests, StreamingClearDiscoveredKeysResetsEntireStreamingTree)
{
    // Ian: ClearDiscoveredKeys is connected to MainWindow::TilesCleared.
    // It should clear all discovered keys and tree children but stay in
    // streaming mode with a fresh root, ready for new TileAdded signals.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");
    dock.OnTileAdded("intake/Active", "bool");
    ASSERT_EQ(dock.DiscoveredKeyCount(), 3);
    ASSERT_TRUE(dock.IsStreamingModeForTesting());

    QSignalSpy spy(&dock, &sd::widgets::RunBrowserDock::CheckedSignalsChanged);
    ASSERT_TRUE(spy.isValid());

    dock.ClearDiscoveredKeys();

    // Stays in streaming mode with empty root.
    EXPECT_TRUE(dock.IsStreamingModeForTesting());
    EXPECT_EQ(dock.DiscoveredKeyCount(), 0);
    // Root node is recreated (1 row), but no children.
    EXPECT_EQ(dock.GetTreeModelForTesting()->rowCount(), 1);
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->rowCount(), 0);

    // Should have emitted with empty sets.
    ASSERT_GE(spy.count(), 1);
    const auto keys = spy.last().at(0).value<QSet<QString>>();
    EXPECT_TRUE(keys.isEmpty());
}

// ============================================================================
// Reading-mode isolation: layout operations must not affect the file-driven tree
// ============================================================================

TEST(RunBrowserDockTests, ReadingModeClearDiscoveredKeysIsNoOp)
{
    // Ian: ClearDiscoveredKeys is connected to MainWindow::TilesCleared, which
    // fires on "Clear Widgets" and layout-load.  In reading mode the tree is
    // driven entirely by the replay file — ClearDiscoveredKeys must be a no-op
    // so the user's loaded replay survives layout operations.
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    ASSERT_TRUE(dock.AddRunFromFile(path));
    ASSERT_EQ(dock.RunCount(), 1);
    ASSERT_FALSE(dock.IsStreamingModeForTesting());

    const int rowsBefore = dock.GetTreeModelForTesting()->rowCount();
    ASSERT_GT(rowsBefore, 0);

    // Simulate "Clear Widgets" — must not touch reading-mode tree.
    dock.ClearDiscoveredKeys();

    EXPECT_EQ(dock.RunCount(), 1);
    EXPECT_FALSE(dock.IsStreamingModeForTesting());
    EXPECT_EQ(dock.GetTreeModelForTesting()->rowCount(), rowsBefore);
}

TEST(RunBrowserDockTests, ReadingModeOnTileRemovedIsNoOp)
{
    // Ian: OnTileRemoved must be a no-op in reading mode.  A user removing a
    // tile from the layout during replay should not affect the replay tree.
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    ASSERT_TRUE(dock.AddRunFromFile(path));
    ASSERT_EQ(dock.RunCount(), 1);

    const int rowsBefore = dock.GetTreeModelForTesting()->rowCount();

    // Simulate removing a tile whose key matches a signal in the replay file.
    dock.OnTileRemoved("flush_fence/TotalMs");

    EXPECT_EQ(dock.RunCount(), 1);
    EXPECT_EQ(dock.GetTreeModelForTesting()->rowCount(), rowsBefore);
}

TEST(RunBrowserDockTests, ReadingModeOnTileAddedIsNoOp)
{
    // Ian: OnTileAdded must be a no-op in reading mode.  Loading a layout
    // that creates new tiles during replay should not add streaming entries.
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    ASSERT_TRUE(dock.AddRunFromFile(path));
    ASSERT_EQ(dock.RunCount(), 1);

    const int rowsBefore = dock.GetTreeModelForTesting()->rowCount();

    // Simulate layout-load creating tiles — must not create streaming nodes.
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");

    EXPECT_EQ(dock.RunCount(), 1);
    EXPECT_FALSE(dock.IsStreamingModeForTesting());
    EXPECT_EQ(dock.DiscoveredKeyCount(), 0);
    EXPECT_EQ(dock.GetTreeModelForTesting()->rowCount(), rowsBefore);
}

TEST(RunBrowserDockTests, ReadingModeTreeSurvivesFullLayoutClearAndReload)
{
    // Ian: Simulate the full sequence a kid might do: load a replay file,
    // then "Clear Widgets" followed by loading a different layout.  The
    // replay tree must survive all three layout lifecycle signals
    // (TilesCleared, TileAdded x N from new layout).
    ASSERT_NE(EnsureApp(), nullptr);

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = WriteFixture(tmp, "run.json", kMinimalValidJson);

    sd::widgets::RunBrowserDock dock;
    ASSERT_TRUE(dock.AddRunFromFile(path));
    ASSERT_EQ(dock.RunCount(), 1);

    const int rowsBefore = dock.GetTreeModelForTesting()->rowCount();

    // Step 1: "Clear Widgets" — fires TilesCleared.
    dock.ClearDiscoveredKeys();

    // Step 2: Loading a layout creates tiles — fires TileAdded for each.
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("intake/Active", "bool");
    dock.OnTileAdded("flush_fence/TotalMs", "double");

    // Step 3: Removing a tile — fires TileRemoved.
    dock.OnTileRemoved("motor/RPM");

    // All layout operations were no-ops — tree is unchanged.
    EXPECT_EQ(dock.RunCount(), 1);
    EXPECT_FALSE(dock.IsStreamingModeForTesting());
    EXPECT_EQ(dock.DiscoveredKeyCount(), 0);
    EXPECT_EQ(dock.GetTreeModelForTesting()->rowCount(), rowsBefore);
}

// ============================================================================
// Streaming mode: layout-replace rebuilds tree (ClearDiscoveredKeys stays streaming)
// ============================================================================

TEST(RunBrowserDockTests, StreamingClearDiscoveredKeysThenTileAddedRebuildsTree)
{
    // Ian: Exact stress test: connect to Direct (streaming mode), then
    // File → Open Layout (Replace).  OnLoadLayoutReplace calls
    // OnClearWidgets (fires TilesCleared → ClearDiscoveredKeys) then
    // LoadLayoutFromPath (fires TileAdded per tile via GetOrCreateTile).
    //
    // ClearDiscoveredKeys must stay in streaming mode so the subsequent
    // TileAdded signals repopulate the tree.  If it exited streaming mode,
    // OnTileAdded would be a no-op and the tree would stay empty with all
    // tiles hidden.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("old/Signal", "double");
    ASSERT_EQ(dock.DiscoveredKeyCount(), 1);

    // Simulate OnClearWidgets → TilesCleared.
    dock.ClearDiscoveredKeys();
    ASSERT_TRUE(dock.IsStreamingModeForTesting());
    ASSERT_EQ(dock.DiscoveredKeyCount(), 0);

    // Simulate LoadLayoutFromPath → GetOrCreateTile → TileAdded per tile.
    dock.OnTileAdded("motor/RPM", "double");
    dock.OnTileAdded("sensor/Temperature", "bool");
    dock.OnTileAdded("intake/Active", "bool");

    // Tree should be fully rebuilt with the new tiles.
    EXPECT_TRUE(dock.IsStreamingModeForTesting());
    EXPECT_EQ(dock.DiscoveredKeyCount(), 3);
    EXPECT_FALSE(dock.HasDiscoveredKey("old/Signal"));
    EXPECT_TRUE(dock.HasDiscoveredKey("motor/RPM"));
    EXPECT_TRUE(dock.HasDiscoveredKey("sensor/Temperature"));
    EXPECT_TRUE(dock.HasDiscoveredKey("intake/Active"));

    // All new groups should be checked (visible by default).
    QStandardItem* root = GetStreamingRoot(dock.GetTreeModelForTesting());
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->checkState(), Qt::Checked);
    QStandardItem* motorGroup = FindChild(root, "motor");
    ASSERT_NE(motorGroup, nullptr);
    EXPECT_EQ(motorGroup->checkState(), Qt::Checked);
}

TEST(RunBrowserDockTests, StreamingClearDiscoveredKeysEmitsCheckedSignalsForNewTiles)
{
    // Ian: After ClearDiscoveredKeys + TileAdded, the dock should emit
    // CheckedSignalsChanged with the new keys so MainWindow can show them.
    // This is the mechanism that makes tiles visible after layout-replace.
    ASSERT_NE(EnsureApp(), nullptr);

    sd::widgets::RunBrowserDock dock;
    dock.SetStreamingRootLabel(kTestTransportLabel);
    dock.OnTileAdded("old/Signal", "double");

    dock.ClearDiscoveredKeys();

    QSignalSpy spy(&dock, &sd::widgets::RunBrowserDock::CheckedSignalsChanged);
    ASSERT_TRUE(spy.isValid());

    dock.OnTileAdded("motor/RPM", "double");

    // OnTileAdded should emit CheckedSignalsChanged with the new key.
    ASSERT_GE(spy.count(), 1);
    const auto keys = spy.last().at(0).value<QSet<QString>>();
    EXPECT_TRUE(keys.contains("motor/RPM"));
    EXPECT_FALSE(keys.contains("old/Signal"));
}
