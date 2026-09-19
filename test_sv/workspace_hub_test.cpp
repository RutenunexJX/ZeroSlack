#include "semanticstateview.h"
#include "workspacehubcontextprovider.h"
#include "workspacehubmodel.h"
#include "workspacehubsession.h"
#include "workspacehubview.h"

#include <QAccessible>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

#include <chrono>
#include <stdexcept>
#include <thread>

class WorkspaceHubTest : public QObject
{
    Q_OBJECT

    static bool writeText(const QString& path, const QString& text)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Text)
            && file.write(text.toUtf8()) == text.toUtf8().size()
            && file.commit();
    }

private slots:
    void defaultBuilderProducesFourStableSections()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString source = QDir(workspace.path()).filePath(
            QStringLiteral("rtl/top.sv"));
        const QString wave = QDir(workspace.path()).filePath(
            QStringLiteral("timing/project.wave.json"));
        const QString regmap = QDir(workspace.path()).filePath(
            QStringLiteral("registers/device.regmap.yaml"));
        QVERIFY(writeText(source,
            QStringLiteral("module top; logic ready; endmodule\n")));
        QVERIFY(writeText(wave,
            QStringLiteral("{\"schemaVersion\":1,\"projectId\":\"p\","
                           "\"name\":\"Timing\",\"scenarios\":[]}\n")));
        QVERIFY(writeText(regmap,
            QStringLiteral("schema_version: 2\nworkspace:\n"
                           "  id: device\n  name: Device\n"
                           "  address_spaces: []\n")));
        QVERIFY(writeText(QDir(workspace.path()).filePath(
            QStringLiteral(".zeroslack/suite-references.json")),
            QStringLiteral(
                "{\"schema\":\"zeroslack.suite-references/v1\","
                "\"resources\":["
                "{\"id\":\"wave\",\"provider\":\"wave\","
                "\"file\":\"timing/project.wave.json\"},"
                "{\"id\":\"reg\",\"provider\":\"regmap\","
                "\"file\":\"registers/device.regmap.yaml\"}]}")));

        WorkspaceHubRequest request;
        request.workspaceRoot = workspace.path();
        request.workspaceId = workspace.path();
        request.documentId = QStringLiteral("doc-top");
        request.documentRevision = 1;
        request.semanticRevision = 2;
        request.filePath = source;
        request.documentText = QStringLiteral(
            "module top; logic ready; endmodule\n");
        request.symbolName = QStringLiteral("ready");
        request.cursorPosition = 18;
        const auto flag = std::make_shared<std::atomic_bool>(false);
        const WorkspaceHubSnapshot snapshot =
            WorkspaceHubSession::buildDefaultSnapshot(request, 7, flag);
        QCOMPARE(snapshot.phase, WorkspaceHubPhase::Ready);
        QCOMPARE(snapshot.generation, quint64(7));
        QCOMPARE(snapshot.sections.size(), 4);
        QCOMPARE(snapshot.sections.at(0).id, QStringLiteral("source"));
        QCOMPARE(snapshot.sections.at(1).id, QStringLiteral("pinloom"));
        QCOMPARE(snapshot.sections.at(2).id, QStringLiteral("wave"));
        QCOMPARE(snapshot.sections.at(3).id, QStringLiteral("regmap"));
        QVERIFY(snapshot.sections.at(0).items.size() == 5);
        QCOMPARE(snapshot.sections.at(2).items.size(), 1);
        QCOMPARE(snapshot.sections.at(3).items.size(), 1);
    }

    void latestGenerationWins()
    {
        WorkspaceHubSession session;
        session.setDebounceIntervalForTesting(0);
        session.setBuilder([](const WorkspaceHubRequest& request,
                              quint64 generation,
                              const WorkspaceHubSession::CancellationFlag& flag) {
            const int iterations = request.symbolName == QStringLiteral("old")
                ? 30 : 1;
            for (int index = 0; index < iterations; ++index) {
                if (flag && flag->load())
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            WorkspaceHubSnapshot snapshot;
            snapshot.generation = generation;
            snapshot.requestKey = request.stableKey();
            snapshot.phase = WorkspaceHubPhase::Ready;
            WorkspaceHubSection section;
            section.id = QStringLiteral("source");
            section.title = QStringLiteral("Source");
            WorkspaceHubItem item;
            item.stableKey = request.symbolName;
            item.providerId = QStringLiteral("source");
            item.title = request.symbolName;
            item.suiteUri = QUrl(QStringLiteral("zeroslack://probe"));
            section.items.append(item);
            snapshot.sections.append(section);
            return snapshot;
        });
        QSignalSpy spy(&session, &WorkspaceHubSession::snapshotChanged);
        WorkspaceHubRequest oldRequest;
        oldRequest.workspaceRoot = QDir::tempPath();
        oldRequest.documentId = QStringLiteral("doc");
        oldRequest.symbolName = QStringLiteral("old");
        session.requestUpdate(oldRequest);
        QTest::qWait(8);
        WorkspaceHubRequest latest = oldRequest;
        latest.symbolName = QStringLiteral("latest");
        latest.cursorPosition = 1;
        session.requestUpdate(latest);
        QTRY_VERIFY_WITH_TIMEOUT(
            session.snapshot().phase == WorkspaceHubPhase::Ready,
            2000);
        QCOMPARE(session.snapshot().requestKey, latest.stableKey());
        QCOMPARE(session.snapshot().sections.at(0).items.at(0).title,
                 QStringLiteral("latest"));
        QVERIFY(spy.count() >= 3);
    }

    void modelAndViewPreserveStableSelectionAndAccessibility()
    {
        WorkspaceHubSnapshot first;
        first.phase = WorkspaceHubPhase::Ready;
        WorkspaceHubSection source;
        source.id = QStringLiteral("source");
        source.title = QStringLiteral("Source");
        WorkspaceHubItem one;
        one.stableKey = QStringLiteral("source:one");
        one.providerId = QStringLiteral("source");
        one.title = QStringLiteral("One");
        one.suiteUri = QUrl(QStringLiteral("zeroslack://one"));
        WorkspaceHubItem two = one;
        two.stableKey = QStringLiteral("source:two");
        two.title = QStringLiteral("Two");
        source.items = {one, two};
        first.sections = {source};

        WorkspaceHubView view;
        view.resize(520, 680);
        view.setSnapshot(first);
        const QModelIndex group = view.model()->index(0, 0);
        const QModelIndex selected = view.model()->index(1, 0, group);
        view.treeView()->setCurrentIndex(selected);
        QCOMPARE(view.saveState().value(
                     QStringLiteral("selectedItem")).toString(),
                 QStringLiteral("source:two"));

        WorkspaceHubSnapshot second = first;
        second.sections[0].items = {two, one};
        view.setSnapshot(second);
        QCOMPARE(view.treeView()->currentIndex().data(
                     WorkspaceHubModel::StableKeyRole).toString(),
                 QStringLiteral("source:two"));
        QVERIFY(!view.accessibleName().isEmpty());
        QVERIFY(!view.treeView()->accessibleName().isEmpty());
        QAccessibleInterface* interface =
            QAccessible::queryAccessibleInterface(view.treeView());
        QVERIFY(interface != nullptr);
        QVERIFY(interface->role() != QAccessible::NoRole);
    }

    void providerStateSurvivesClosedView()
    {
        WorkspaceHubSession session;
        WorkspaceHubContextProvider provider(&session);
        std::unique_ptr<QWidget> widget(provider.createView(
            WorkspaceHubContextProvider::homeResource(
                QStringLiteral("workspace")), nullptr));
        QVERIFY(widget);
        auto* view = qobject_cast<WorkspaceHubView*>(widget.get());
        QVERIFY(view);
        const QVariantMap expected{
            {QStringLiteral("expandedSections"),
             QStringList{QStringLiteral("wave")}},
            {QStringLiteral("selectedItem"), QStringLiteral("wave:item")},
            {QStringLiteral("previewVisible"), false},
            {QStringLiteral("scrollValue"), 12},
        };
        view->restoreState(expected);
        const QVariantMap saved = provider.saveViewState(view);
        QCOMPARE(saved, expected);
        provider.restoreProviderState(saved);
        QCOMPARE(provider.saveProviderState(), saved);
    }

    void switchingWorkspaceDoesNotExposePreviousStaleItems()
    {
        QTemporaryDir workspaceA;
        QTemporaryDir workspaceB;
        QVERIFY(workspaceA.isValid());
        QVERIFY(workspaceB.isValid());
        WorkspaceHubSession session;
        session.setDebounceIntervalForTesting(0);
        session.setBuilder([](const WorkspaceHubRequest& request,
                              quint64 generation,
                              const WorkspaceHubSession::CancellationFlag&) {
            WorkspaceHubSnapshot snapshot;
            snapshot.generation = generation;
            snapshot.requestKey = request.stableKey();
            snapshot.phase = WorkspaceHubPhase::Ready;
            WorkspaceHubSection section;
            section.id = QStringLiteral("wave");
            section.title = QStringLiteral("Wave");
            WorkspaceHubItem item;
            item.stableKey = request.workspaceRoot;
            item.providerId = QStringLiteral("wave");
            item.title = QFileInfo(request.workspaceRoot).fileName();
            item.suiteUri = QUrl(QStringLiteral("wave://project"));
            section.items.append(item);
            snapshot.sections.append(section);
            return snapshot;
        });
        WorkspaceHubRequest first;
        first.workspaceRoot = workspaceA.path();
        first.documentId = QStringLiteral("a");
        session.requestUpdate(first);
        QTRY_COMPARE_WITH_TIMEOUT(session.snapshot().phase,
                                  WorkspaceHubPhase::Ready, 2000);
        QVERIFY(!session.snapshot().sections.isEmpty());

        session.setDebounceIntervalForTesting(1000);
        WorkspaceHubRequest second = first;
        second.workspaceRoot = workspaceB.path();
        second.documentId = QStringLiteral("b");
        session.requestUpdate(second);
        QCOMPARE(session.snapshot().phase, WorkspaceHubPhase::Updating);
        QVERIFY(session.snapshot().sections.isEmpty());
        QVERIFY(!session.snapshot().stale);
        session.clear();
    }

    void builderFailurePublishesErrorState()
    {
        WorkspaceHubSession session;
        session.setDebounceIntervalForTesting(0);
        session.setBuilder([](const WorkspaceHubRequest&,
                              quint64,
                              const WorkspaceHubSession::CancellationFlag&)
                -> WorkspaceHubSnapshot {
            throw std::runtime_error("synthetic hub failure");
        });
        WorkspaceHubRequest request;
        request.workspaceRoot = QDir::tempPath();
        request.documentId = QStringLiteral("failure-document");
        session.requestUpdate(request);
        QTRY_COMPARE_WITH_TIMEOUT(session.snapshot().phase,
                                  WorkspaceHubPhase::Error, 2000);
        QVERIFY(session.snapshot().failureReason.contains(
            QStringLiteral("synthetic hub failure")));
    }

    void allCollapsedAndPreviewHiddenRoundTrip()
    {
        WorkspaceHubSnapshot snapshot;
        snapshot.phase = WorkspaceHubPhase::Ready;
        for (const QString& id : {QStringLiteral("source"),
                                  QStringLiteral("pinloom"),
                                  QStringLiteral("wave"),
                                  QStringLiteral("regmap")}) {
            WorkspaceHubSection section;
            section.id = id;
            section.title = id;
            WorkspaceHubItem item;
            item.stableKey = id + QStringLiteral(":item");
            item.providerId = id;
            item.title = id + QStringLiteral(" item");
            item.suiteUri = QUrl(id + QStringLiteral("://item"));
            section.items.append(item);
            snapshot.sections.append(section);
        }

        WorkspaceHubView first;
        first.setSnapshot(snapshot);
        for (int row = 0; row < first.model()->rowCount(); ++row)
            first.treeView()->setExpanded(first.model()->index(row, 0), false);
        QVariantMap state = first.saveState();
        state.insert(QStringLiteral("previewVisible"), false);

        WorkspaceHubView restored;
        restored.restoreState(state);
        restored.setSnapshot(snapshot);
        for (int row = 0; row < restored.model()->rowCount(); ++row)
            QVERIFY(!restored.treeView()->isExpanded(
                restored.model()->index(row, 0)));
        QCOMPARE(restored.saveState().value(
                     QStringLiteral("previewVisible")).toBool(),
                 false);
    }

    void unavailableItemDoesNotActivateAndPreviewIsPlainText()
    {
        qRegisterMetaType<WorkspaceHubItem>();
        WorkspaceHubSnapshot snapshot;
        snapshot.phase = WorkspaceHubPhase::Ready;
        WorkspaceHubSection section;
        section.id = QStringLiteral("wave");
        section.title = QStringLiteral("Wave");
        WorkspaceHubItem available;
        available.stableKey = QStringLiteral("wave:available");
        available.providerId = QStringLiteral("wave");
        available.title = QStringLiteral("<b>literal title</b>");
        available.summary = QStringLiteral("<script>literal summary</script>");
        available.metadata.insert(
            QStringLiteral("surfacePreview"),
            QStringLiteral("2 scenarios · 18 lanes"));
        available.suiteUri = QUrl(QStringLiteral("wave://project?id=one"));
        WorkspaceHubItem unavailable = available;
        unavailable.stableKey = QStringLiteral("wave:unavailable");
        unavailable.title = QStringLiteral("Unavailable");
        unavailable.state = WorkspaceHubItemState::Unavailable;
        section.items = {available, unavailable};
        snapshot.sections = {section};

        WorkspaceHubView view;
        view.setSnapshot(snapshot);
        view.resize(520, 640);
        view.show();
        QTest::qWait(20);
        const QModelIndex group = view.model()->index(0, 0);
        const QModelIndex availableIndex = view.model()->index(0, 0, group);
        const QModelIndex unavailableIndex = view.model()->index(1, 0, group);
        view.treeView()->setCurrentIndex(availableIndex);
        QSignalSpy activationSpy(&view, &WorkspaceHubView::itemActivated);
        view.treeView()->setFocus();
        QTest::keyClick(view.treeView(), Qt::Key_Return);
        QTRY_COMPARE(activationSpy.count(), 1);
        activationSpy.clear();
        QTest::mouseDClick(view.treeView()->viewport(), Qt::LeftButton,
                          Qt::NoModifier,
                          view.treeView()->visualRect(
                              availableIndex).center());
        QTRY_COMPARE(activationSpy.count(), 1);
        activationSpy.clear();
        view.treeView()->setCurrentIndex(unavailableIndex);
        QTest::keyClick(view.treeView(), Qt::Key_Return);
        QCOMPARE(activationSpy.count(), 0);
        QVERIFY(!view.stateView()->isHidden());

        auto* status = view.findChild<QLabel*>(
            QStringLiteral("workspaceHubStatus"));
        QVERIFY(status);
        QVERIFY(status->accessibleName().contains(status->text()));

        view.treeView()->setCurrentIndex(availableIndex);
        auto* title = view.findChild<QLabel*>(
            QStringLiteral("workspaceHubPreviewTitle"));
        auto* summary = view.findChild<QLabel*>(
            QStringLiteral("workspaceHubPreviewSummary"));
        QVERIFY(title);
        QVERIFY(summary);
        QCOMPARE(title->textFormat(), Qt::PlainText);
        QCOMPARE(summary->textFormat(), Qt::PlainText);
        QCOMPARE(title->text(), QStringLiteral("<b>literal title</b>"));
        QVERIFY(summary->text().contains(
            QStringLiteral("<script>literal summary</script>")));
        QVERIFY(summary->text().contains(
            QStringLiteral("2 scenarios · 18 lanes")));
    }

    void scrollAndScaledLayoutRemainUsable()
    {
        WorkspaceHubSnapshot snapshot;
        snapshot.phase = WorkspaceHubPhase::Ready;
        WorkspaceHubSection section;
        section.id = QStringLiteral("source");
        section.title = QStringLiteral("Source");
        for (int index = 0; index < 48; ++index) {
            WorkspaceHubItem item;
            item.stableKey = QStringLiteral("source:%1").arg(index);
            item.providerId = QStringLiteral("source");
            item.title = QStringLiteral("Source item %1").arg(index);
            item.suiteUri = QUrl(QStringLiteral("zeroslack://source/%1")
                                     .arg(index));
            section.items.append(item);
        }
        snapshot.sections = {section};

        WorkspaceHubView view;
        view.resize(420, 520);
        view.setSnapshot(snapshot);
        view.show();
        QTest::qWait(30);
        QVERIFY(view.treeView()->isVisible());
        QVERIFY(view.treeView()->geometry().width() > 0);
        QVERIFY(view.treeView()->geometry().height() > 0);
        QVERIFY(view.rect().contains(
            view.treeView()->geometry().topLeft()));
        QVERIFY(view.rect().contains(
            view.treeView()->geometry().bottomRight()));
        QVERIFY2(view.minimumSizeHint().width() <= view.width(),
                 qPrintable(QStringLiteral("minimum=%1 actual=%2")
                                .arg(view.minimumSizeHint().width())
                                .arg(view.width())));

        QSignalSpy stateSpy(&view, &WorkspaceHubView::viewStateChanged);
        const int maximum = view.treeView()->verticalScrollBar()->maximum();
        QVERIFY(maximum > 0);
        view.treeView()->verticalScrollBar()->setValue(maximum);
        QTRY_VERIFY(stateSpy.count() > 0);
    }
};

QTEST_MAIN(WorkspaceHubTest)

#include "workspace_hub_test.moc"
