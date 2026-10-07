#include "mainwindow.h"
#include "analysisscheduler.h"
#include "activitylogservice.h"
#include "contextworkspacecontroller.h"
#include "panellayoutcontroller.h"
#include "semanticindex.h"
#include "tabmanager.h"
#include "temporaryeditorcontextprovider.h"
#include "temporaryeditorcontextview.h"
#include "liveinsightscontextprovider.h"
#include "workspacemanager.h"
#include "testuistyle.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFile>
#include <QDialog>
#include <QLineEdit>
#include <QListView>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QToolButton>
#include <QTimer>
#include <QTreeWidget>
#include <QDir>
#include <QtTest>
#include <qt_windows.h>

namespace {
bool writeText(const QString& path, const QString& text)
{
    QFile file(path);
    const auto bytes = text.toUtf8();
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
PanelLayoutController* panels(MainWindow& window)
{
    for (auto* child : window.children())
        if (auto* controller = dynamic_cast<PanelLayoutController*>(child)) return controller;
    return nullptr;
}
void isolate(MainWindow& window, const QString& root)
{
    window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
    window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(root + "/recovery"));
    window.tabManager->unsavedDocumentManagerForTesting()->setDecisionProvider(
        [](const auto&, QWidget*) { return UnsavedDocumentBatchDecision::DiscardAll; });
    window.resize(1100, 760);
    window.show();
}
}

class DelayedConsumerGenerationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        wchar_t path[32768]{};
        const DWORD size = GetModuleFileNameW(GetModuleHandleW(L"libzeroslack_core.dll"), path, 32768);
        QVERIFY(size > 0);
        qInfo().noquote() << "Loaded core:" << QString::fromWCharArray(path, int(size));
        QVERIFY(initializeUiStyleForTest());
    }

    void searchActivationChecksSource_data()
    {
        QTest::addColumn<QString>("change");
        for (const char* value : {"current", "dirty", "same_dirty", "indexed_dirty", "disk", "format_only", "deleted"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void searchActivationChecksSource()
    {
        QFETCH(QString, change);
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString initial = fixture.filePath("initial.sv"), target = fixture.filePath("target.sv");
        const QString source = "module target;\n  logic chosen_signal;\nendmodule\n";
        QVERIFY(writeText(initial, "module initial; endmodule\n"));
        QVERIFY(writeText(target, source));
        MainWindow window; isolate(window, fixture.path());
        QVERIFY(window.workspaceManager->openWorkspace(fixture.path()));
        const bool liveTarget = change == "dirty" || change == "same_dirty" || change == "indexed_dirty";
        QVERIFY(window.tabManager->openFileInTab(liveTarget ? target : initial));
        QString indexedSource = source;
        QTRY_VERIFY_WITH_TIMEOUT(!window.workspaceManager->isWorkspaceScanActive()
            && !window.analysisScheduler->isSemanticAnalysisActive()
            && SemanticIndex::getInstance()->getCachedFileContent(target) == source, 10000);
        if (change == "indexed_dirty") {
            indexedSource.prepend("// indexed unsaved prefix\n");
            QTextCursor cursor(window.tabManager->getCurrentEditor()->document());
            cursor.insertText("// indexed unsaved prefix\n");
        }
        QTRY_VERIFY_WITH_TIMEOUT(!window.workspaceManager->isWorkspaceScanActive()
            && !window.analysisScheduler->isSemanticAnalysisActive()
            && SemanticIndex::getInstance()->getCachedFileContent(target) == indexedSource, 10000);
        auto* controller = window.findChild<ContextWorkspaceController*>();
        QVERIFY(controller);
        EditorLocation start; start.filePath = change == "same_dirty" ? target : initial;
        const auto resource = TemporaryEditorContextProvider::resourceForLocation(start, fixture.path());
        QVERIFY(controller->openResource(resource, {ContextSurface::Docked, ContextPersistence::Kept}));
        auto* view = qobject_cast<TemporaryEditorContextView*>(controller->viewForResource(resource.stableKey()));
        QVERIFY(view && view->editor());
        auto* field = view->searchField();
        auto* list = view->findChild<QListView*>("temporaryEditorSearchResults");
        QVERIFY(field && list);
        field->setText("chosen_signal");
        QTRY_COMPARE_WITH_TIMEOUT(list->model()->rowCount(), 1, 5000);
        const int history = view->historyCount();
        QTimer dismissMissingFile;
        connect(&dismissMissingFile, &QTimer::timeout, &window, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
        QSignalSpy rejected(view, &TemporaryEditorContextView::openFailed);
        MyCodeEditor* targetEditor = window.tabManager->getDocumentModel()->editorForFile(target);
        if (change == "dirty" || change == "same_dirty") {
            QVERIFY(targetEditor);
            QTextCursor cursor(targetEditor->document());
            cursor.insertText("// inserted before the candidate source\n");
        } else if (change == "disk") {
            QVERIFY(writeText(target, "// inserted on disk\n" + source));
        } else if (change == "format_only") {
            QString crlf = source; crlf.replace("\n", "\r\n");
            QVERIFY(writeText(target, crlf));
        } else if (change == "deleted") {
            QVERIFY(QFile::remove(target));
            dismissMissingFile.start(10);
        }
        list->setCurrentIndex(list->model()->index(0, 0));
        QTest::keyClick(field, Qt::Key_Return, Qt::NoModifier, 0);
        dismissMissingFile.stop();
        if (change == "current" || change == "indexed_dirty" || change == "format_only") {
            QCOMPARE(rejected.size(), 0);
            QVERIFY(EditorFileIdentity::same(view->currentLocation().filePath, target));
            QCOMPARE(view->editor()->textCursor().blockNumber(), change == "indexed_dirty" ? 2 : 1);
        } else {
            QCOMPARE(rejected.size(), 1);
            QVERIFY(EditorFileIdentity::same(view->currentLocation().filePath, start.filePath));
            QCOMPARE(view->historyCount(), history);
            if (change == "dirty" || change == "same_dirty") targetEditor->undo();
            else QVERIFY(writeText(target, source));
            field->clear(); field->setText("chosen_signal");
            QTRY_COMPARE_WITH_TIMEOUT(list->model()->rowCount(), 1, 5000);
            list->setCurrentIndex(list->model()->index(0, 0));
            QTest::keyClick(field, Qt::Key_Return, Qt::NoModifier, 0);
            QVERIFY(EditorFileIdentity::same(view->currentLocation().filePath, target));
            QCOMPARE(view->editor()->textCursor().blockNumber(), 1);
        }
        QVERIFY(window.close());
    }

    void registeredPanelReportsOpenFailure()
    {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto initial = fixture.filePath("initial.sv"), target = fixture.filePath("target.sv");
        QVERIFY(writeText(initial, "module initial; endmodule\n"));
        QVERIFY(writeText(target, "module target; endmodule\n"));
        MainWindow window; isolate(window, fixture.path());
        QVERIFY(window.workspaceManager->openWorkspace(fixture.path()));
        QVERIFY(window.tabManager->openFileInTab(initial));
        QTRY_VERIFY_WITH_TIMEOUT(!window.workspaceManager->isWorkspaceScanActive()
            && !window.analysisScheduler->isSemanticAnalysisActive()
            && !SemanticIndex::getInstance()->getCachedFileContent(initial).isEmpty(), 10000);
        auto* controller = window.findChild<ContextWorkspaceController*>(); QVERIFY(controller);
        const auto resource = LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Module, fixture.path());
        QVERIFY(controller->openResource(resource, {ContextSurface::Docked, ContextPersistence::Kept}));
        auto* view = qobject_cast<LiveInsightsContextView*>(controller->viewForResource(resource.stableKey()));
        QVERIFY(view);
        LiveInsightsContextView::TargetCandidate candidate;
        candidate.label = "initial"; candidate.fileName = initial; candidate.moduleName = "initial";
        QVERIFY(view->applyTargetCandidate(candidate));
        QTRY_VERIFY_WITH_TIMEOUT(view->surfaceForTest(), 5000);
        QStringList messages;
        view->surfaceForTest()->setStatusHandler([&](const QString& message, int) { messages.append(message); });
        auto* tree = view->findChild<QTreeWidget*>("rtlInsightsTree");
        QVERIFY(tree);
        // A retained action row uses the presenter's role contract. Navigation
        // remains the registered MainWindow route; only status is observed.
        auto* row = new QTreeWidgetItem(tree, {"target"});
        row->setData(0, Qt::UserRole, target);
        row->setData(0, Qt::UserRole + 1, 1);
        row->setData(0, Qt::UserRole + 2, 1);
        QVERIFY(QFile::remove(target));
        QVERIFY(QDir().mkdir(target));
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
        dismiss.start(10);
        QVERIFY(QMetaObject::invokeMethod(tree, "itemDoubleClicked", Qt::DirectConnection,
            Q_ARG(QTreeWidgetItem*, row), Q_ARG(int, 0)));
        dismiss.stop();
        QVERIFY(EditorFileIdentity::same(window.tabManager->getCurrentDocument().fileName, initial));
        bool reported = false;
        for (const auto& message : messages)
            reported |= message.contains("jump failed");
        QVERIFY(reported);
        QVERIFY(QDir().rmdir(target));
        QVERIFY(writeText(target, "module target; endmodule\n"));
        QVERIFY(QMetaObject::invokeMethod(tree, "itemDoubleClicked", Qt::DirectConnection,
            Q_ARG(QTreeWidgetItem*, row), Q_ARG(int, 0)));
        QVERIFY(EditorFileIdentity::same(window.tabManager->getCurrentDocument().fileName, target));
        QVERIFY(window.close());
    }

    void activityFlushInterleavings_data()
    {
        QTest::addColumn<QString>("transition");
        for (const char* value : {"flush", "clear", "hide", "close"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void activityFlushInterleavings()
    {
        QFETCH(QString, transition);
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        auto window = std::make_unique<MainWindow>(); isolate(*window, fixture.path());
        auto* layout = panels(*window); QVERIFY(layout);
        layout->setAnimationsEnabled(false);
        layout->setBottomCollapsed(true);
        auto* button = layout->buttonForPanel("activity"); QVERIFY(button);
        button->click();
        QPointer<QPlainTextEdit> output = window->findChild<QPlainTextEdit*>("activityOutputText");
        QVERIFY(output);
        QTRY_VERIFY(output->isVisible() && !output->visibleRegion().isEmpty());
        QCoreApplication::processEvents();
        auto* service = ActivityLogService::getInstance();
        service->clear();
        const QString first = "R5 pending visible activity", second = "R5 retained after clear";
        service->append("R5", ActivityLogLevel::Warning, first);
        QVERIFY(!output->toPlainText().contains(first));
        QCOMPARE(service->unreadCount(), 1);
        if (transition == "close") {
            QVERIFY(window->close());
            window.reset();
            QVERIFY(output.isNull());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            service->clear();
            return;
        }
        if (transition == "clear") {
            auto* clear = window->findChild<QAbstractButton*>("activityLogClearButton"); QVERIFY(clear);
            clear->click();
            service->append("R5", ActivityLogLevel::Warning, second);
        } else if (transition == "hide") {
            layout->setBottomCollapsed(true);
            QVERIFY(!output->isVisible());
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        if (transition == "hide") {
            QCOMPARE(service->unreadCount(), 1);
            button->click();
            QTRY_VERIFY(output->isVisible());
            QCoreApplication::processEvents();
        }
        const QString expected = transition == "clear" ? second : first;
        QTRY_VERIFY(output->toPlainText().contains(expected));
        QCOMPARE(output->toPlainText().count(expected), 1);
        if (transition == "clear") QVERIFY(!output->toPlainText().contains(first));
        QCOMPARE(service->unreadCount(), 0);
        QCOMPARE(service->events().size(), 1);
        QVERIFY(window->close());
        service->clear();
    }
};

int main(int argc, char** argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir settings;
    if (!settings.isValid()) return 2;
    QCoreApplication::setOrganizationName("ZeroSlack");
    QCoreApplication::setApplicationName("ZeroSlack");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", settings.filePath("sessions.ini").toUtf8());
    DelayedConsumerGenerationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "delayed_consumer_generation_test.moc"
