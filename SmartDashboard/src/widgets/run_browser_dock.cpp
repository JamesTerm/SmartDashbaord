#include "widgets/run_browser_dock.h"

#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMap>
#include <QSet>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace sd::widgets
{
    // Ian: Custom data roles stashed on QStandardItem so we can tell runs from
    // folders from signal leaves without maintaining a parallel lookup map.
    // Values start at Qt::UserRole + 100 to avoid collisions with any future
    // Qt or project roles.
    namespace
    {
        constexpr int kRoleNodeKind  = Qt::UserRole + 100;  // "run", "group", "signal"
        constexpr int kRoleRunIndex  = Qt::UserRole + 101;  // int index into m_runs
        constexpr int kRoleSignalKey = Qt::UserRole + 102;  // full signal key string

        constexpr int kNodeKindRun    = 0;
        constexpr int kNodeKindGroup  = 1;
        constexpr int kNodeKindSignal = 2;
    }

    RunBrowserDock::RunBrowserDock(QWidget* parent)
        : QDockWidget("Run Browser", parent)
    {
        setObjectName("runBrowserDock");
        setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);

        auto* container = new QWidget(this);
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(4);

        // Ian: The Run Browser intentionally has no toolbar buttons.
        // The tree itself (checkboxes, right-click context menus) is the
        // only interaction surface.  A "Clear" button was removed because
        // it confused users — the dock's content is driven entirely by
        // the layout's tile lifecycle and transport connection state.

        m_treeModel = new QStandardItemModel(this);
        m_treeModel->setHorizontalHeaderLabels({"Name", "Details"});

        // Ian: itemChanged fires when the user toggles a checkbox.  We use it
        // to propagate check state (group -> run tri-state rollup) and emit
        // the checked-signals-changed signal to the main window.
        connect(
            m_treeModel,
            &QStandardItemModel::itemChanged,
            this,
            &RunBrowserDock::OnModelItemChanged
        );

        m_treeView = new QTreeView(container);
        m_treeView->setModel(m_treeModel);
        m_treeView->setHeaderHidden(false);
        m_treeView->setRootIsDecorated(true);
        m_treeView->setAlternatingRowColors(true);
        m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_treeView->header()->setStretchLastSection(true);
        m_treeView->header()->setSectionResizeMode(0, QHeaderView::Interactive);
        m_treeView->header()->setDefaultSectionSize(200);
        layout->addWidget(m_treeView, 1);

        connect(
            m_treeView,
            &QTreeView::activated,
            this,
            &RunBrowserDock::OnTreeActivated
        );

        m_summaryLabel = new QLabel("No runs loaded", container);
        m_summaryLabel->setStyleSheet("QLabel { color: #8c8c8c; }");
        layout->addWidget(m_summaryLabel);

        setWidget(container);
    }

    bool RunBrowserDock::AddRunFromFile(const QString& filePath)
    {
        LoadedRun run;
        if (!ParseCaptureSessionFile(filePath, run))
        {
            return false;
        }

        m_runs.push_back(std::move(run));
        RebuildTree();
        UpdateSummaryLabel();
        return true;
    }

    int RunBrowserDock::AddRunsFromFiles(const QStringList& filePaths)
    {
        int loaded = 0;
        for (const QString& path : filePaths)
        {
            LoadedRun run;
            if (ParseCaptureSessionFile(path, run))
            {
                m_runs.push_back(std::move(run));
                ++loaded;
            }
        }

        if (loaded > 0)
        {
            RebuildTree();
            UpdateSummaryLabel();
        }

        return loaded;
    }

    void RunBrowserDock::ClearAllRuns()
    {
        m_runs.clear();
        // Ian: ClearAllRuns is the ONLY method that exits streaming mode.
        // It is called when switching to a replay file (reading mode).
        // ClearDiscoveredKeys deliberately does NOT set m_streamingMode = false
        // because it needs to stay in streaming mode for TileAdded signals
        // that follow a layout-load.  ClearAllRuns is the hard reset.
        m_streamingMode = false;
        m_streamingRootItem = nullptr;  // Owned by model — cleared below.
        m_discoveredKeys.clear();
        m_discoveredKeyTypes.clear();
        m_streamingHiddenKeys.clear();

        RebuildTree();
        UpdateSummaryLabel();

        // Ian: Emit with empty sets so the consumer (MainWindow) knows all
        // signal keys are now unchecked and can show all tiles again.
        emit CheckedSignalsChanged(QSet<QString>(), QMap<QString, QString>());
    }

    int RunBrowserDock::RunCount() const
    {
        return static_cast<int>(m_runs.size());
    }

    QString RunBrowserDock::GetLoadedFilePath() const
    {
        if (m_runs.empty())
        {
            return {};
        }
        return m_runs[0].filePath;
    }

    // ====================================================================
    // Streaming mode
    // ====================================================================

    void RunBrowserDock::SetStreamingRootLabel(const QString& label)
    {
        m_streamingRootLabel = label;

        // Ian: Initialize or re-initialize streaming mode.  Every call clears
        // prior state and recreates the root node from scratch.  This is the
        // simplest way to guarantee the root label is always correct when
        // switching transports — ClearDiscoveredKeys is called first by
        // StartTransport, but even if the caller forgets, this method is
        // self-contained and idempotent.
        m_runs.clear();
        m_treeModel->clear();
        m_treeModel->setHorizontalHeaderLabels({"Name", "Details"});
        m_streamingMode = true;
        m_streamingRootItem = nullptr;  // Cleared by m_treeModel->clear().
        m_discoveredKeys.clear();
        m_discoveredKeyTypes.clear();
        EnsureStreamingRootNode();
    }

    // Ian: OnTileAdded is the layout-mirror entry point.  Each call from
    // MainWindow::TileAdded signal feeds one tile key into the tree.  The
    // two rules that make this simple:
    //   1. Build the tree as tiles appear on the layout — this method creates group/leaf nodes.
    //   2. Everything visible by default — groups start Qt::Checked.
    //
    // Streaming mode must be initialized first via SetStreamingRootLabel().
    // If not in streaming mode, this is a no-op (tiles created during replay
    // mode or before any transport starts are ignored).
    //
    // Subsequent calls are no-ops if the key already exists (e.g. reconnect
    // re-delivers the same key set).
    //
    // We suppress itemChanged during insertion to avoid per-node signal storms,
    // then emit CheckedSignalsChanged once at the end so MainWindow gets a
    // single consolidated update.

    void RunBrowserDock::OnTileAdded(const QString& key, const QString& type)
    {
        if (key.isEmpty())
        {
            return;
        }

        // Ian: Only act when streaming mode has been explicitly initialized
        // by SetStreamingRootLabel().  This prevents tiles created during
        // replay mode or before any transport starts from accidentally
        // switching the dock into streaming mode.
        //
        // Stress test: load a replay file, then File → Open Layout (or
        // drag-drop a layout).  The layout-load calls GetOrCreateTile for
        // every tile, which emits TileAdded.  Without this guard, each
        // TileAdded would create streaming tree nodes on top of the
        // file-driven replay tree — corrupting it.
        if (!m_streamingMode)
        {
            return;
        }

        // No-op for duplicate keys.
        if (m_discoveredKeys.contains(key))
        {
            return;
        }

        m_discoveredKeys.insert(key);
        m_discoveredKeyTypes.insert(key, type);

        // Split key on '/' and build the group/leaf hierarchy.
        const QStringList parts = key.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty())
        {
            return;
        }

        // Ian: All groups and leaves nest under the streaming root node,
        // which is the synthetic transport-named folder (e.g. "Direct").
        // If no root label was set, fall back to the invisible root so
        // the tree still works (just without the wrapper folder).
        m_suppressCheckSignal = true;

        QStandardItem* parent = (m_streamingRootItem != nullptr)
            ? m_streamingRootItem
            : m_treeModel->invisibleRootItem();
        for (int i = 0; i < parts.size() - 1; ++i)
        {
            parent = GetOrCreateGroupItemForStreaming(parent, parts[i]);
        }

        // Leaf: the signal name.
        const QString leafName = parts.back();
        auto* signalItem = new QStandardItem(leafName);
        signalItem->setData(kNodeKindSignal, kRoleNodeKind);
        signalItem->setData(-1, kRoleRunIndex);  // No run index in streaming mode.
        signalItem->setData(key, kRoleSignalKey);

        auto* signalDetailItem = new QStandardItem(type);
        signalDetailItem->setData(kNodeKindSignal, kRoleNodeKind);
        signalDetailItem->setData(-1, kRoleRunIndex);
        signalDetailItem->setData(key, kRoleSignalKey);

        parent->appendRow({signalItem, signalDetailItem});

        // Ian: Lazy hidden-keys application.  If SetHiddenDiscoveredKeys was
        // called before keys arrived (the normal reconnect path), we stored
        // the hidden set in m_streamingHiddenKeys.  Now that this key's group
        // hierarchy exists, walk up from the leaf's parent and uncheck any
        // group where ALL descendant signals are hidden.  A group that already
        // has some visible descendants stays checked.
        if (!m_streamingHiddenKeys.isEmpty())
        {
            // Recursive helper: returns true if ALL signal descendants of @p item
            // are in m_streamingHiddenKeys.
            std::function<bool(const QStandardItem*)> allDescendantsHidden;
            allDescendantsHidden = [&](const QStandardItem* item) -> bool
            {
                bool hasSignalDescendant = false;
                for (int row = 0; row < item->rowCount(); ++row)
                {
                    const QStandardItem* child = item->child(row, 0);
                    if (child == nullptr)
                    {
                        continue;
                    }
                    const int kind = child->data(kRoleNodeKind).toInt();
                    if (kind == kNodeKindSignal)
                    {
                        hasSignalDescendant = true;
                        const QString childKey = child->data(kRoleSignalKey).toString();
                        if (!m_streamingHiddenKeys.contains(childKey))
                        {
                            return false;  // At least one visible signal.
                        }
                    }
                    else if (kind == kNodeKindGroup)
                    {
                        hasSignalDescendant = true;
                        if (!allDescendantsHidden(child))
                        {
                            return false;
                        }
                    }
                }
                return hasSignalDescendant;
            };

            // Walk from the leaf's parent up to the top-level groups.
            // Stop before the streaming root node (kNodeKindRun) — we update
            // its tri-state via UpdateRunCheckState after all groups are set.
            QStandardItem* ancestor = parent;
            while (ancestor != nullptr
                   && ancestor != m_treeModel->invisibleRootItem()
                   && ancestor != m_streamingRootItem)
            {
                const int kind = ancestor->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindGroup)
                {
                    if (allDescendantsHidden(ancestor))
                    {
                        ancestor->setCheckState(Qt::Unchecked);
                    }
                    else
                    {
                        ancestor->setCheckState(Qt::Checked);
                    }
                }
                ancestor = ancestor->parent();
            }

            // Update the streaming root's tri-state to reflect its children.
            if (m_streamingRootItem != nullptr)
            {
                UpdateRunCheckState(m_streamingRootItem);
            }
        }
        else
        {
            // Ian: Even when there are no hidden keys to apply, the root
            // tri-state still needs updating.  After a remove-then-re-add
            // cycle the root may have been set to Qt::Unchecked when its
            // last child was removed.  Re-adding a key creates a new checked
            // group, but without this call the root would stay unchecked
            // and CollectAndEmitCheckedSignals would skip the subtree.
            if (m_streamingRootItem != nullptr)
            {
                UpdateRunCheckState(m_streamingRootItem);
            }
        }

        m_suppressCheckSignal = false;

        UpdateSummaryLabel();
        CollectAndEmitCheckedSignals();
    }

    void RunBrowserDock::ClearDiscoveredKeys()
    {
        // Ian: Only clear streaming state when we are actually in streaming
        // mode.  This method is connected to MainWindow::TilesCleared, which
        // fires on "Clear Widgets" and layout-load.  In reading mode (replay
        // file loaded) the tree is driven entirely by the file — layout
        // operations must not touch it.  OnTileAdded and OnTileRemoved already
        // have this guard; ClearDiscoveredKeys needs it too.
        //
        // Stress test (reading mode): load a replay file, then press
        // "Clear Widgets" or File → Open Layout.  Without this guard the
        // TilesCleared signal would wipe the model, destroying the replay
        // tree the student just loaded.
        if (!m_streamingMode)
        {
            return;
        }

        // Ian: Stay in streaming mode so that TileAdded signals from the
        // subsequent layout-load can repopulate the tree.  If we set
        // m_streamingMode = false here, OnTileAdded would be a no-op for
        // every tile the layout creates, leaving the tree empty and all
        // tiles hidden.
        //
        // Stress test (streaming mode): connect to Direct, then
        // File → Open Layout (Replace).  OnLoadLayoutReplace calls
        // OnClearWidgets (fires TilesCleared → here) then LoadLayoutFromPath
        // (fires TileAdded per tile).  We must stay in streaming mode so
        // those TileAdded signals rebuild the tree.
        m_streamingRootItem = nullptr;  // Owned by model — cleared below.
        m_discoveredKeys.clear();
        m_discoveredKeyTypes.clear();
        m_streamingHiddenKeys.clear();

        m_treeModel->clear();
        m_treeModel->setHorizontalHeaderLabels({"Name", "Details"});

        // Recreate the root node so the tree is ready for incoming TileAdded.
        EnsureStreamingRootNode();

        UpdateSummaryLabel();

        emit CheckedSignalsChanged(QSet<QString>(), QMap<QString, QString>());
    }

    // Ian: OnTileRemoved is called when a tile is removed from the layout
    // (user right-click "Remove").  We remove the corresponding leaf node
    // from the streaming tree and prune any parent groups that become empty.
    // The tree stays a 1:1 mirror of what tiles exist on the layout.

    void RunBrowserDock::OnTileRemoved(const QString& key)
    {
        // Ian: Guard — in reading mode, tile removals on the layout must not
        // alter the file-driven replay tree.
        //
        // Stress test: load a replay file whose signals created tiles on the
        // layout, then right-click a tile and choose "Remove".  Without this
        // guard the removal would try to prune tree nodes that belong to the
        // replay, corrupting the tree structure.
        if (!m_streamingMode || key.isEmpty())
        {
            return;
        }

        if (!m_discoveredKeys.contains(key))
        {
            return;
        }

        m_discoveredKeys.remove(key);
        m_discoveredKeyTypes.remove(key);
        m_streamingHiddenKeys.remove(key);

        RemoveStreamingLeafAndPruneAncestors(key);

        UpdateSummaryLabel();
        CollectAndEmitCheckedSignals();
    }

    // Ian: Walk the streaming tree to find the signal leaf with the given key,
    // remove it, and then walk up removing any parent groups that became empty.
    // The streaming root node is never removed by this — only groups and leaves.

    void RunBrowserDock::RemoveStreamingLeafAndPruneAncestors(const QString& key)
    {
        QStandardItem* rootItem = m_streamingRootItem;
        if (rootItem == nullptr)
        {
            return;
        }

        // Recursive search: find the leaf node with kRoleSignalKey == key.
        std::function<QStandardItem*(QStandardItem*)> findLeaf;
        findLeaf = [&](QStandardItem* parent) -> QStandardItem*
        {
            for (int row = 0; row < parent->rowCount(); ++row)
            {
                QStandardItem* child = parent->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }

                const int kind = child->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindSignal)
                {
                    if (child->data(kRoleSignalKey).toString() == key)
                    {
                        return child;
                    }
                }
                else if (kind == kNodeKindGroup)
                {
                    QStandardItem* found = findLeaf(child);
                    if (found != nullptr)
                    {
                        return found;
                    }
                }
            }
            return nullptr;
        };

        QStandardItem* leaf = findLeaf(rootItem);
        if (leaf == nullptr)
        {
            return;
        }

        // Remove the leaf from its parent, then prune empty ancestors
        // up to (but not including) the streaming root.
        m_suppressCheckSignal = true;

        QStandardItem* parent = leaf->parent();
        if (parent == nullptr)
        {
            parent = m_treeModel->invisibleRootItem();
        }
        parent->removeRow(leaf->row());

        // Walk up: remove empty group parents.
        while (parent != nullptr
               && parent != rootItem
               && parent != m_treeModel->invisibleRootItem()
               && parent->rowCount() == 0)
        {
            QStandardItem* grandparent = parent->parent();
            if (grandparent == nullptr)
            {
                grandparent = m_treeModel->invisibleRootItem();
            }
            grandparent->removeRow(parent->row());
            parent = grandparent;
        }

        // Update the streaming root's tri-state.
        UpdateRunCheckState(rootItem);

        m_suppressCheckSignal = false;
    }

    int RunBrowserDock::DiscoveredKeyCount() const
    {
        return m_discoveredKeys.size();
    }

    bool RunBrowserDock::HasDiscoveredKey(const QString& key) const
    {
        return m_discoveredKeys.contains(key);
    }

    // Ian: GetHiddenDiscoveredKeys returns the inverse of the checked set —
    // all discovered keys whose parent group is NOT checked.  In streaming
    // mode we persist hidden keys rather than checked keys because the default
    // is "everything visible."  This means a reconnect starts with all keys
    // visible, then re-applies the hidden set as keys re-arrive.

    QSet<QString> RunBrowserDock::GetHiddenDiscoveredKeys() const
    {
        if (!m_streamingMode)
        {
            return {};
        }

        // Collect the currently checked (visible) keys.
        const QSet<QString> checkedKeys = GetCheckedSignalKeysForTesting();

        // Hidden = discovered - checked.
        QSet<QString> hidden;
        for (const QString& key : m_discoveredKeys)
        {
            if (!checkedKeys.contains(key))
            {
                hidden.insert(key);
            }
        }
        return hidden;
    }

    // Ian: SetHiddenDiscoveredKeys unchecks groups whose ALL descendant signals
    // are in the hidden set.  Called after a reconnect when keys re-arrive and
    // we want to restore the user's previous opt-out choices.  We walk the tree
    // top-down: for each group node, if every signal leaf under it is hidden,
    // we uncheck the group.  Otherwise the group stays checked (the default).

    void RunBrowserDock::SetHiddenDiscoveredKeys(const QSet<QString>& hiddenKeys)
    {
        // Ian: Always store the hidden set so that OnTileAdded can lazily
        // apply it as new keys arrive.  Even if the tree is currently empty
        // (e.g. called right after ClearDiscoveredKeys before any keys arrive),
        // storing the set here means future OnTileAdded calls will create
        // groups with the correct check state.
        m_streamingHiddenKeys = hiddenKeys;

        if (!m_streamingMode || hiddenKeys.isEmpty())
        {
            return;
        }

        // Recursive helper: returns true if ALL descendant signals are hidden.
        std::function<bool(QStandardItem*)> allHidden;
        allHidden = [&](QStandardItem* item) -> bool
        {
            bool allChildrenHidden = true;
            bool hasSignalDescendant = false;

            for (int row = 0; row < item->rowCount(); ++row)
            {
                QStandardItem* child = item->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }

                const int kind = child->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindSignal)
                {
                    hasSignalDescendant = true;
                    const QString key = child->data(kRoleSignalKey).toString();
                    if (!hiddenKeys.contains(key))
                    {
                        allChildrenHidden = false;
                    }
                }
                else if (kind == kNodeKindGroup)
                {
                    hasSignalDescendant = true;
                    if (!allHidden(child))
                    {
                        allChildrenHidden = false;
                    }
                }
            }

            return hasSignalDescendant && allChildrenHidden;
        };

        m_suppressCheckSignal = true;

        // Ian: Walk the streaming root's children (groups and signal leaves).
        // With the synthetic root node, groups are children of
        // m_streamingRootItem, not top-level model items.
        QStandardItem* rootItem = m_streamingRootItem;
        if (rootItem == nullptr)
        {
            m_suppressCheckSignal = false;
            return;
        }

        for (int row = 0; row < rootItem->rowCount(); ++row)
        {
            QStandardItem* item = rootItem->child(row, 0);
            if (item == nullptr)
            {
                continue;
            }

            const int kind = item->data(kRoleNodeKind).toInt();
            if (kind == kNodeKindGroup)
            {
                if (allHidden(item))
                {
                    item->setCheckState(Qt::Unchecked);
                }
            }
            // Single-segment signals are not checkable — their visibility
            // is governed by the root node's check state.
        }

        // Update the streaming root's tri-state to reflect its children.
        UpdateRunCheckState(rootItem);

        m_suppressCheckSignal = false;
        CollectAndEmitCheckedSignals();
    }

    void RunBrowserDock::EnsureStreamingRootNode()
    {
        if (m_streamingRootItem != nullptr)
        {
            return;
        }

        // Ian: The streaming root is structurally identical to a run node in
        // reading mode — a top-level checkable folder that contains groups.
        // Using kNodeKindRun lets OnModelItemChanged and UpdateRunCheckState
        // handle tri-state push-down/roll-up without any mode branching.
        // It starts Qt::Checked because streaming mode defaults to visible.
        const QString label = m_streamingRootLabel.isEmpty()
            ? QStringLiteral("Live")
            : m_streamingRootLabel;

        auto* rootItem = new QStandardItem(label);
        rootItem->setData(kNodeKindRun, kRoleNodeKind);
        rootItem->setData(-1, kRoleRunIndex);  // No run index in streaming mode.
        rootItem->setCheckable(true);
        rootItem->setCheckState(Qt::Checked);

        auto* rootDetailItem = new QStandardItem();
        rootDetailItem->setData(kNodeKindRun, kRoleNodeKind);
        rootDetailItem->setData(-1, kRoleRunIndex);
        m_treeModel->appendRow({rootItem, rootDetailItem});

        m_streamingRootItem = rootItem;

        // Expand the root so groups are immediately visible.
        if (m_treeView != nullptr)
        {
            m_treeView->expand(m_treeModel->indexFromItem(rootItem));
        }
    }

    bool RunBrowserDock::IsStreamingModeForTesting() const
    {
        return m_streamingMode;
    }

    // Ian: Streaming-mode group creation.  Like GetOrCreateGroupItem but
    // groups start Qt::Checked (visible by default).  This is the key
    // behavioral difference from reading mode where groups start unchecked.

    QStandardItem* RunBrowserDock::GetOrCreateGroupItemForStreaming(QStandardItem* parentItem, const QString& groupName)
    {
        // Search existing children for a matching group folder.
        for (int row = 0; row < parentItem->rowCount(); ++row)
        {
            QStandardItem* child = parentItem->child(row, 0);
            if (child != nullptr
                && child->data(kRoleNodeKind).toInt() == kNodeKindGroup
                && child->text() == groupName)
            {
                return child;
            }
        }

        // Create a new folder item — starts CHECKED in streaming mode.
        auto* groupItem = new QStandardItem(groupName);
        groupItem->setData(kNodeKindGroup, kRoleNodeKind);
        groupItem->setCheckable(true);
        groupItem->setCheckState(Qt::Checked);

        auto* emptyDetail = new QStandardItem();
        emptyDetail->setData(kNodeKindGroup, kRoleNodeKind);
        parentItem->appendRow({groupItem, emptyDetail});
        return groupItem;
    }

    bool RunBrowserDock::ParseCaptureSessionFileForTesting(const QString& filePath, LoadedRun& outRun)
    {
        return ParseCaptureSessionFile(filePath, outRun);
    }

    const LoadedRun& RunBrowserDock::GetRunForTesting(int index) const
    {
        return m_runs[static_cast<size_t>(index)];
    }

    QStandardItemModel* RunBrowserDock::GetTreeModelForTesting() const
    {
        return m_treeModel;
    }

    void RunBrowserDock::OnTreeActivated(const QModelIndex& index)
    {
        if (!index.isValid())
        {
            return;
        }

        // Resolve column-0 index for data lookup (user may click column 1).
        const QModelIndex col0 = index.siblingAtColumn(0);
        const QStandardItem* item = m_treeModel->itemFromIndex(col0);
        if (item == nullptr)
        {
            return;
        }

        const int nodeKind = item->data(kRoleNodeKind).toInt();
        const int runIndex = item->data(kRoleRunIndex).toInt();

        if (nodeKind == kNodeKindSignal)
        {
            const QString signalKey = item->data(kRoleSignalKey).toString();
            emit SignalActivated(runIndex, signalKey);
        }
        else if (nodeKind == kNodeKindRun)
        {
            emit RunActivated(runIndex);
        }
    }

    void RunBrowserDock::UpdateSummaryLabel()
    {
        if (m_summaryLabel == nullptr)
        {
            return;
        }

        if (m_streamingMode)
        {
            const int keyCount = m_discoveredKeys.size();
            if (keyCount == 0)
            {
                m_summaryLabel->setText("No signals discovered");
            }
            else
            {
                m_summaryLabel->setText(
                    QString("%1 signal%2 discovered")
                        .arg(keyCount)
                        .arg(keyCount == 1 ? "" : "s")
                );
            }
            return;
        }

        if (m_runs.empty())
        {
            m_summaryLabel->setText("No runs loaded");
            return;
        }

        int totalSignals = 0;
        for (const LoadedRun& run : m_runs)
        {
            totalSignals += static_cast<int>(run.signalInfos.size());
        }

        m_summaryLabel->setText(
            QString("%1 run%2, %3 signal%4")
                .arg(m_runs.size())
                .arg(m_runs.size() == 1 ? "" : "s")
                .arg(totalSignals)
                .arg(totalSignals == 1 ? "" : "s")
        );
    }

    void RunBrowserDock::RebuildTree()
    {
        m_treeModel->clear();
        m_treeModel->setHorizontalHeaderLabels({"Name", "Details"});

        for (int runIdx = 0; runIdx < static_cast<int>(m_runs.size()); ++runIdx)
        {
            const LoadedRun& run = m_runs[static_cast<size_t>(runIdx)];

            // Top-level: run node.
            QString runLabel = run.metadata.label;
            if (runLabel.isEmpty())
            {
                // Fall back to file name.
                const int lastSlash = std::max(run.filePath.lastIndexOf('/'), run.filePath.lastIndexOf('\\'));
                runLabel = (lastSlash >= 0) ? run.filePath.mid(lastSlash + 1) : run.filePath;
            }

            auto* runItem = new QStandardItem(runLabel);
            runItem->setData(kNodeKindRun, kRoleNodeKind);
            runItem->setData(runIdx, kRoleRunIndex);
            runItem->setToolTip(run.filePath);
            runItem->setCheckable(true);
            runItem->setCheckState(Qt::Unchecked);

            // Build details string from tags.
            QStringList tagParts;
            for (const auto& [tagKey, tagValue] : run.metadata.tags)
            {
                tagParts.append(QString("%1=%2").arg(tagKey, tagValue));
            }
            QString details = QString("%1 signals").arg(run.signalInfos.size());
            if (!tagParts.isEmpty())
            {
                details += QString("  [%1]").arg(tagParts.join(", "));
            }
            if (run.metadata.durationSec > 0.0)
            {
                details += QString("  %1s").arg(run.metadata.durationSec, 0, 'f', 3);
            }

            auto* runDetailItem = new QStandardItem(details);
            runDetailItem->setData(kNodeKindRun, kRoleNodeKind);
            runDetailItem->setData(runIdx, kRoleRunIndex);
            m_treeModel->appendRow({runItem, runDetailItem});

            // Ian: Build the grouped signal tree.  Signal keys use '/' as a
            // separator (e.g. "flush_fence/TotalMs").  We create folder nodes
            // for each prefix component, and leaf nodes for the final metric.
            // This handles arbitrary depth — if a key has more than one '/',
            // nested folders are created.
            for (const RunSignalInfo& sig : run.signalInfos)
            {
                const QStringList parts = sig.key.split('/', Qt::SkipEmptyParts);
                if (parts.isEmpty())
                {
                    continue;
                }

                // Walk/create the folder chain.
                QStandardItem* parent = runItem;
                for (int i = 0; i < parts.size() - 1; ++i)
                {
                    parent = GetOrCreateGroupItem(parent, parts[i]);
                }

                // Leaf: the metric name.
                const QString leafName = parts.back();
                auto* signalItem = new QStandardItem(leafName);
                signalItem->setData(kNodeKindSignal, kRoleNodeKind);
                signalItem->setData(runIdx, kRoleRunIndex);
                signalItem->setData(sig.key, kRoleSignalKey);

                auto* signalDetailItem = new QStandardItem(
                    QString("%1  (%2 samples)").arg(sig.type).arg(sig.sampleCount)
                );
                signalDetailItem->setData(kNodeKindSignal, kRoleNodeKind);
                signalDetailItem->setData(runIdx, kRoleRunIndex);
                signalDetailItem->setData(sig.key, kRoleSignalKey);

                parent->appendRow({signalItem, signalDetailItem});
            }
        }

        // Expand all run-level nodes by default.
        if (m_treeView != nullptr)
        {
            for (int i = 0; i < m_treeModel->rowCount(); ++i)
            {
                m_treeView->expand(m_treeModel->index(i, 0));
            }
        }
    }

    QStandardItem* RunBrowserDock::GetOrCreateGroupItem(QStandardItem* parentItem, const QString& groupName)
    {
        // Search existing children for a matching group folder.
        for (int row = 0; row < parentItem->rowCount(); ++row)
        {
            QStandardItem* child = parentItem->child(row, 0);
            if (child != nullptr
                && child->data(kRoleNodeKind).toInt() == kNodeKindGroup
                && child->text() == groupName)
            {
                return child;
            }
        }

        // Create a new folder item.
        auto* groupItem = new QStandardItem(groupName);
        groupItem->setData(kNodeKindGroup, kRoleNodeKind);
        groupItem->setCheckable(true);
        groupItem->setCheckState(Qt::Unchecked);

        auto* emptyDetail = new QStandardItem();
        emptyDetail->setData(kNodeKindGroup, kRoleNodeKind);
        parentItem->appendRow({groupItem, emptyDetail});
        return groupItem;
    }

    bool RunBrowserDock::ParseCaptureSessionFile(const QString& filePath, LoadedRun& outRun)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            return false;
        }

        const QByteArray raw = file.readAll();
        if (raw.trimmed().isEmpty())
        {
            return false;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            return false;
        }

        const QJsonObject root = doc.object();

        // Require the "signals" array — this is the capture-session schema.
        if (!root.contains("signals") || !root.value("signals").isArray())
        {
            return false;
        }

        outRun.filePath = filePath;

        // Parse metadata (optional).
        if (root.contains("metadata") && root.value("metadata").isObject())
        {
            const QJsonObject meta = root.value("metadata").toObject();
            outRun.metadata.label = meta.value("label").toString();
            outRun.metadata.runId = meta.value("run_id").toString();
            outRun.metadata.startTimeUtc = meta.value("start_time_utc").toString();
            outRun.metadata.durationSec = meta.value("duration_sec").toDouble();
            outRun.metadata.capturedUpdateCount = meta.value("captured_update_count").toInt();

            if (meta.contains("tags") && meta.value("tags").isObject())
            {
                const QJsonObject tags = meta.value("tags").toObject();
                for (auto it = tags.constBegin(); it != tags.constEnd(); ++it)
                {
                    outRun.metadata.tags[it.key()] = it.value().toVariant().toString();
                }
            }
        }

        // Parse signal summaries (we don't load samples into memory here —
        // that's the replay transport's job).
        const QJsonArray signalArray = root.value("signals").toArray();
        outRun.signalInfos.reserve(static_cast<size_t>(signalArray.size()));

        for (const QJsonValue& signalValue : signalArray)
        {
            if (!signalValue.isObject())
            {
                continue;
            }

            const QJsonObject signalObj = signalValue.toObject();
            RunSignalInfo info;
            info.key = signalObj.value("key").toString();
            info.type = signalObj.value("type").toString().trimmed().toLower();
            info.sampleCount = signalObj.value("sample_count").toInt();

            if (info.sampleCount == 0 && signalObj.contains("samples"))
            {
                // Fallback: count the samples array.
                info.sampleCount = signalObj.value("samples").toArray().size();
            }

            if (!info.key.isEmpty())
            {
                outRun.signalInfos.push_back(std::move(info));
            }
        }

        return !outRun.signalInfos.empty();
    }

    // Ian: Checkbox propagation logic.
    //
    // Reading mode: When the user checks/unchecks a *group* folder, we update
    // the parent run node to reflect the aggregate state (tri-state).  When the
    // user checks/unchecks a *run* node, we push the new state down to all its
    // immediate group children.
    //
    // Streaming mode: There are no run nodes — groups sit directly under the
    // root.  A group toggle simply re-collects checked signals and emits.
    //
    // In both cases we then collect the full set of checked signal keys and
    // emit CheckedSignalsChanged.
    //
    // m_suppressCheckSignal prevents recursive re-entry: setting child check
    // states from code fires itemChanged again, so we guard against it.

    void RunBrowserDock::OnModelItemChanged(QStandardItem* item)
    {
        if (m_suppressCheckSignal || item == nullptr)
        {
            return;
        }

        // Only react to column-0 items (checkboxes live there).
        if (item->column() != 0)
        {
            return;
        }

        const int nodeKind = item->data(kRoleNodeKind).toInt();

        if (nodeKind == kNodeKindGroup)
        {
            if (m_streamingMode)
            {
                // Ian: In streaming mode, groups sit under the synthetic root
                // node (kNodeKindRun).  Update the root's tri-state to reflect
                // the change, then re-collect and emit.
                if (m_streamingRootItem != nullptr)
                {
                    m_suppressCheckSignal = true;
                    UpdateRunCheckState(m_streamingRootItem);
                    m_suppressCheckSignal = false;
                }
                CollectAndEmitCheckedSignals();
            }
            else
            {
                // Reading mode: update the parent run's tri-state.
                QStandardItem* parentRun = item->parent();
                if (parentRun != nullptr && parentRun->data(kRoleNodeKind).toInt() == kNodeKindRun)
                {
                    m_suppressCheckSignal = true;
                    UpdateRunCheckState(parentRun);
                    m_suppressCheckSignal = false;
                }
                CollectAndEmitCheckedSignals();
            }
        }
        else if (nodeKind == kNodeKindRun)
        {
            // A run was toggled — push the new binary state to all group children.
            // (Tri-state partial is only computed from children, never set by user click.)
            const Qt::CheckState newState = item->checkState();
            const Qt::CheckState childState = (newState == Qt::PartiallyChecked)
                ? Qt::Checked   // Clicking partial toggles to fully checked.
                : newState;

            m_suppressCheckSignal = true;
            for (int row = 0; row < item->rowCount(); ++row)
            {
                QStandardItem* child = item->child(row, 0);
                if (child != nullptr && child->data(kRoleNodeKind).toInt() == kNodeKindGroup)
                {
                    child->setCheckState(childState);
                }
            }
            // Ian: Force the root to the user's intended state rather than
            // recomputing from children.  In streaming mode single-segment
            // signal keys sit directly under the root and are implicitly
            // "always visible" — UpdateRunCheckState would count them as
            // checked, overriding the user's explicit uncheck to
            // PartiallyChecked (the confusing minus sign).  Setting
            // childState directly respects the user's intent: unchecked
            // means "hide everything", checked means "show everything".
            item->setCheckState(childState);
            m_suppressCheckSignal = false;
            CollectAndEmitCheckedSignals();
        }
    }

    void RunBrowserDock::UpdateRunCheckState(QStandardItem* runItem)
    {
        if (runItem == nullptr)
        {
            return;
        }

        int checkedCount = 0;
        int childCount = 0;

        for (int row = 0; row < runItem->rowCount(); ++row)
        {
            const QStandardItem* child = runItem->child(row, 0);
            if (child == nullptr)
            {
                continue;
            }

            const int kind = child->data(kRoleNodeKind).toInt();
            if (kind == kNodeKindGroup)
            {
                ++childCount;
                if (child->checkState() == Qt::Checked)
                {
                    ++checkedCount;
                }
            }
            else if (kind == kNodeKindSignal)
            {
                // Ian: Single-segment keys (signal leaves directly under the
                // run/root node) have no group checkbox to uncheck — they are
                // implicitly always visible.  Count them as checked children
                // so the root tri-state reflects their presence correctly.
                ++childCount;
                ++checkedCount;
            }
        }

        Qt::CheckState newState = Qt::Unchecked;
        if (childCount > 0)
        {
            if (checkedCount == childCount)
            {
                newState = Qt::Checked;
            }
            else if (checkedCount > 0)
            {
                newState = Qt::PartiallyChecked;
            }
        }

        runItem->setCheckState(newState);
    }

    void RunBrowserDock::CollectAndEmitCheckedSignals()
    {
        QSet<QString> checkedKeys;
        QMap<QString, QString> keyToType;

        // Ian: Shared recursive leaf collector — walks a parent node and adds
        // all signal-leaf descendants to checkedKeys.  In streaming mode the
        // type comes from m_discoveredKeyTypes; in reading mode from m_runs.
        std::function<void(const QStandardItem*, int)> collectLeaves;
        collectLeaves = [&](const QStandardItem* parent, int runIdx)
        {
            for (int row = 0; row < parent->rowCount(); ++row)
            {
                const QStandardItem* child = parent->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }

                const int kind = child->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindSignal)
                {
                    const QString key = child->data(kRoleSignalKey).toString();
                    if (!key.isEmpty())
                    {
                        checkedKeys.insert(key);
                        if (m_streamingMode)
                        {
                            const auto typeIt = m_discoveredKeyTypes.find(key);
                            if (typeIt != m_discoveredKeyTypes.end())
                            {
                                keyToType.insert(key, typeIt.value());
                            }
                        }
                        else if (runIdx >= 0 && runIdx < static_cast<int>(m_runs.size()))
                        {
                            for (const RunSignalInfo& sig : m_runs[static_cast<size_t>(runIdx)].signalInfos)
                            {
                                if (sig.key == key)
                                {
                                    keyToType.insert(key, sig.type);
                                    break;
                                }
                            }
                        }
                    }
                }
                else if (kind == kNodeKindGroup)
                {
                    collectLeaves(child, runIdx);
                }
            }
        };

        if (m_streamingMode)
        {
            // Ian: Streaming mode — groups sit under the synthetic root node
            // (kNodeKindRun).  Walk the root's children: checked groups
            // contribute their leaves, signal leaves (single-segment keys)
            // are always visible since the root is their only gating ancestor.
            QStandardItem* rootItem = m_streamingRootItem;
            if (rootItem == nullptr)
            {
                // No root node yet — nothing to collect.
                emit CheckedSignalsChanged(checkedKeys, keyToType);
                return;
            }

            // If the root itself is unchecked, nothing is visible.
            if (rootItem->checkState() == Qt::Unchecked)
            {
                emit CheckedSignalsChanged(checkedKeys, keyToType);
                return;
            }

            for (int row = 0; row < rootItem->rowCount(); ++row)
            {
                const QStandardItem* item = rootItem->child(row, 0);
                if (item == nullptr)
                {
                    continue;
                }

                const int kind = item->data(kRoleNodeKind).toInt();

                if (kind == kNodeKindGroup && item->checkState() == Qt::Checked)
                {
                    collectLeaves(item, -1);
                }
                else if (kind == kNodeKindSignal)
                {
                    // Single-segment keys: visible when the root is checked.
                    const QString key = item->data(kRoleSignalKey).toString();
                    if (!key.isEmpty())
                    {
                        checkedKeys.insert(key);
                        const auto typeIt = m_discoveredKeyTypes.find(key);
                        if (typeIt != m_discoveredKeyTypes.end())
                        {
                            keyToType.insert(key, typeIt.value());
                        }
                    }
                }
            }
        }
        else
        {
            // Reading mode — walk run nodes, then their group children.
            for (int runRow = 0; runRow < m_treeModel->rowCount(); ++runRow)
            {
                const QStandardItem* runItem = m_treeModel->item(runRow, 0);
                if (runItem == nullptr)
                {
                    continue;
                }

                const int runIdx = runItem->data(kRoleRunIndex).toInt();
                if (runIdx < 0 || runIdx >= static_cast<int>(m_runs.size()))
                {
                    continue;
                }

                for (int groupRow = 0; groupRow < runItem->rowCount(); ++groupRow)
                {
                    const QStandardItem* groupOrLeaf = runItem->child(groupRow, 0);
                    if (groupOrLeaf == nullptr)
                    {
                        continue;
                    }

                    const int childKind = groupOrLeaf->data(kRoleNodeKind).toInt();

                    if (childKind == kNodeKindGroup && groupOrLeaf->checkState() == Qt::Checked)
                    {
                        collectLeaves(groupOrLeaf, runIdx);
                    }
                    else if (childKind == kNodeKindSignal)
                    {
                        // Ian: Signals directly under a run (no group folder) are
                        // included when the run itself is checked.  This handles
                        // single-segment keys like "TopLevel" that have no folder.
                        if (runItem->checkState() == Qt::Checked)
                        {
                            const QString key = groupOrLeaf->data(kRoleSignalKey).toString();
                            if (!key.isEmpty())
                            {
                                checkedKeys.insert(key);
                                for (const RunSignalInfo& sig : m_runs[static_cast<size_t>(runIdx)].signalInfos)
                                {
                                    if (sig.key == key)
                                    {
                                        keyToType.insert(key, sig.type);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        emit CheckedSignalsChanged(checkedKeys, keyToType);
    }

    // Ian: Programmatically check groups whose signals are in the given key set.
    // Used by MainWindow to restore persisted checked state on startup.  We walk
    // every group in the tree and check it if ANY of its descendant signals are
    // in signalKeys.  This fires OnModelItemChanged for each group, which is fine —
    // the suppress guard handles the cascading updates.  We call
    // CollectAndEmitCheckedSignals once at the end to give the consumer the
    // final aggregated state.

    void RunBrowserDock::SetCheckedGroupsBySignalKeys(const QSet<QString>& signalKeys)
    {
        if (signalKeys.isEmpty())
        {
            return;
        }

        // Recursive helper: returns true if any descendant signal is in signalKeys.
        std::function<bool(QStandardItem*)> checkGroup;
        checkGroup = [&](QStandardItem* groupItem) -> bool
        {
            bool hasMatch = false;
            for (int row = 0; row < groupItem->rowCount(); ++row)
            {
                QStandardItem* child = groupItem->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }

                const int kind = child->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindSignal)
                {
                    const QString key = child->data(kRoleSignalKey).toString();
                    if (signalKeys.contains(key))
                    {
                        hasMatch = true;
                    }
                }
                else if (kind == kNodeKindGroup)
                {
                    if (checkGroup(child))
                    {
                        hasMatch = true;
                    }
                }
            }

            if (hasMatch && groupItem->isCheckable())
            {
                groupItem->setCheckState(Qt::Checked);
            }
            return hasMatch;
        };

        m_suppressCheckSignal = true;

        for (int runRow = 0; runRow < m_treeModel->rowCount(); ++runRow)
        {
            QStandardItem* runItem = m_treeModel->item(runRow, 0);
            if (runItem == nullptr)
            {
                continue;
            }

            for (int row = 0; row < runItem->rowCount(); ++row)
            {
                QStandardItem* child = runItem->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }

                const int kind = child->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindGroup)
                {
                    checkGroup(child);
                }
            }

            // Update the run node tri-state after all children are set.
            UpdateRunCheckState(runItem);
        }

        m_suppressCheckSignal = false;
        CollectAndEmitCheckedSignals();
    }

    // Ian: Build a display-text path from the root to the given item, using '/'
    // as separator.  This produces e.g. "MyRun/flush_fence" for a group node.
    // Used to persist/restore expanded state across sessions.

    QString RunBrowserDock::BuildItemPath(const QStandardItem* item)
    {
        QStringList parts;
        const QStandardItem* current = item;
        while (current != nullptr)
        {
            parts.prepend(current->text());
            current = current->parent();
        }
        return parts.join('/');
    }

    void RunBrowserDock::CollectExpandedPaths(const QModelIndex& parent, QStringList& outPaths) const
    {
        if (m_treeView == nullptr)
        {
            return;
        }

        const int rowCount = m_treeModel->rowCount(parent);
        for (int row = 0; row < rowCount; ++row)
        {
            const QModelIndex index = m_treeModel->index(row, 0, parent);
            if (m_treeView->isExpanded(index))
            {
                const QStandardItem* item = m_treeModel->itemFromIndex(index);
                if (item != nullptr)
                {
                    outPaths.append(BuildItemPath(item));
                }
                // Recurse into expanded children.
                CollectExpandedPaths(index, outPaths);
            }
        }
    }

    QStringList RunBrowserDock::GetExpandedPaths() const
    {
        QStringList paths;
        CollectExpandedPaths(QModelIndex(), paths);
        return paths;
    }

    void RunBrowserDock::SetExpandedPaths(const QStringList& paths)
    {
        if (m_treeView == nullptr || paths.isEmpty())
        {
            return;
        }

        const QSet<QString> pathSet(paths.begin(), paths.end());

        // Recursive helper: walk the tree and expand matching nodes.
        std::function<void(const QModelIndex&)> restore;
        restore = [&](const QModelIndex& parent)
        {
            const int rowCount = m_treeModel->rowCount(parent);
            for (int row = 0; row < rowCount; ++row)
            {
                const QModelIndex index = m_treeModel->index(row, 0, parent);
                const QStandardItem* item = m_treeModel->itemFromIndex(index);
                if (item != nullptr && pathSet.contains(BuildItemPath(item)))
                {
                    m_treeView->expand(index);
                    restore(index);
                }
            }
        };

        restore(QModelIndex());
    }

    QSet<QString> RunBrowserDock::GetCheckedSignalKeysForTesting() const
    {
        QSet<QString> result;

        std::function<void(const QStandardItem*, bool)> collect;
        collect = [&](const QStandardItem* parent, bool parentGroupChecked)
        {
            for (int row = 0; row < parent->rowCount(); ++row)
            {
                const QStandardItem* child = parent->child(row, 0);
                if (child == nullptr)
                {
                    continue;
                }

                const int kind = child->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindGroup)
                {
                    const bool groupChecked = (child->checkState() == Qt::Checked);
                    collect(child, groupChecked);
                }
                else if (kind == kNodeKindSignal && parentGroupChecked)
                {
                    const QString key = child->data(kRoleSignalKey).toString();
                    if (!key.isEmpty())
                    {
                        result.insert(key);
                    }
                }
            }
        };

        if (m_streamingMode)
        {
            // Ian: Streaming mode — groups sit under the synthetic root node
            // (kNodeKindRun).  Walk the root's children: checked groups
            // contribute their leaves, signal leaves (single-segment keys)
            // are visible when the root is checked.
            if (m_streamingRootItem == nullptr)
            {
                return result;
            }

            // If root is unchecked, nothing is visible.
            if (m_streamingRootItem->checkState() == Qt::Unchecked)
            {
                return result;
            }

            for (int row = 0; row < m_streamingRootItem->rowCount(); ++row)
            {
                const QStandardItem* item = m_streamingRootItem->child(row, 0);
                if (item == nullptr)
                {
                    continue;
                }

                const int kind = item->data(kRoleNodeKind).toInt();
                if (kind == kNodeKindGroup)
                {
                    const bool groupChecked = (item->checkState() == Qt::Checked);
                    collect(item, groupChecked);
                }
                else if (kind == kNodeKindSignal)
                {
                    // Single-segment keys — visible when the root is checked.
                    const QString key = item->data(kRoleSignalKey).toString();
                    if (!key.isEmpty())
                    {
                        result.insert(key);
                    }
                }
            }
        }
        else
        {
            // Reading mode — walk run nodes.
            for (int runRow = 0; runRow < m_treeModel->rowCount(); ++runRow)
            {
                const QStandardItem* runItem = m_treeModel->item(runRow, 0);
                if (runItem == nullptr)
                {
                    continue;
                }

                // For direct children of run that are signals (no group), use run check state.
                for (int row = 0; row < runItem->rowCount(); ++row)
                {
                    const QStandardItem* child = runItem->child(row, 0);
                    if (child == nullptr)
                    {
                        continue;
                    }
                    const int kind = child->data(kRoleNodeKind).toInt();
                    if (kind == kNodeKindGroup)
                    {
                        const bool groupChecked = (child->checkState() == Qt::Checked);
                        collect(child, groupChecked);
                    }
                    else if (kind == kNodeKindSignal && runItem->checkState() == Qt::Checked)
                    {
                        const QString key = child->data(kRoleSignalKey).toString();
                        if (!key.isEmpty())
                        {
                            result.insert(key);
                        }
                    }
                }
            }
        }

        return result;
    }
}
