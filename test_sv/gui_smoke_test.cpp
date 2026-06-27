// Offscreen GUI smoke test for the real MainWindow/TabManager/MyCodeEditor path.
// It keeps the assertions coarse on purpose: this target is a repeatable guard that
// the GUI workflow is alive, while detailed semantic behavior stays in the focused
// headless tests.
#include <QApplication>
#include <QAbstractButton>
#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QCompleter>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QGraphicsView>
#include <QImage>
#include <QSettings>
#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSet>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>
#include <QtTest/QTest>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <utility>

#define private public
#include "mainwindow.h"
#include "analysisprogresscoordinator.h"
#include "analysisscheduler.h"
#include "activitylogservice.h"
#include "alternatecommandservice.h"
#include "documentmodel.h"
#include "definitionpreviewservice.h"
#include "editorappearance.h"
#include "editorappearancepanel.h"
#include "editorappearancesettings.h"
#include "editorcoordinator.h"
#include "editorhoverpopup.h"
#include "editorruntime.h"
#include "editorsemanticcontextservice.h"
#include "filecommandcoordinator.h"
#include "foldblockshelfmodel.h"
#include "foldblockshelfpanel.h"
#include "formattersettings.h"
#include "commodecommandregistry.h"
#include "commodecoordinator.h"
#include "commodeservice.h"
#include "globalcontrolcoordinator.h"
#include "globalcontrolpanel.h"
#include "globalcontrolservice.h"
#include "navigationwidget.h"
#include "navigationpanecoordinator.h"
#include "semantic_fixture_records.h"
#include "sourcenavigationservice.h"
#include "symbolhoverservice.h"
#include "modemanager.h"
#include "navigationmanager.h"
#include "navigationservice.h"
#include "navigationcommandcoordinator.h"
#include "problemspanelcoordinator.h"
#include "referencespanelcoordinator.h"
#include "relationshipspanelcoordinator.h"
#include "rtlinsightspanelcoordinator.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "semanticdockcoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "semanticruntimecoordinator.h"
#include "signalkernelgraphpanelcoordinator.h"
#include "smartrelationshipbuilder.h"
#include "tabmanager.h"
#include "tsdocument.h"
#include "wavepreviewpanelcoordinator.h"
#include "workspaceanalysisplanservice.h"
#include "workspaceanalysisrequestqueue.h"
#include "workspacemanager.h"
#undef private

static int g_checks = 0;
static int g_fails = 0;

static bool waitUntil(const std::function<bool()>& predicate, int timeoutMs);

static std::shared_ptr<const SemanticIndexSnapshot> snapshotFromRecords(
    const QList<SemanticSymbolRecord>& records,
    QList<SemanticRelationship> relationships = {},
    QList<SemanticDiagnostic> diagnostics = {},
    QHash<QString, QString> fileContents = {})
{
    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            records,
            std::move(relationships),
            std::move(diagnostics),
            std::move(fileContents)));
}

static void expectBool(const char* what, bool got, bool want)
{
    ++g_checks;
    const bool ok = (got == want);
    if (!ok)
        ++g_fails;
    printf("[%s] %-48s got=%s want=%s\n",
           ok ? "PASS" : "FAIL", what, got ? "true" : "false", want ? "true" : "false");
    fflush(stdout);
}

static QString visibleEditorHoverPopupText(bool* visible = nullptr)
{
    bool found = false;
    QString text;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (!widget || widget->objectName() != QStringLiteral("editorHoverPopup")
            || !widget->isVisible()) {
            continue;
        }
        found = true;
        const QList<QLabel*> labels = widget->findChildren<QLabel*>();
        for (const QLabel* label : labels)
            text += label->text() + QLatin1Char('\n');
    }
    if (visible)
        *visible = found;
    return text;
}

static void hideEditorHoverPopups()
{
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget && widget->objectName() == QStringLiteral("editorHoverPopup"))
            widget->hide();
    }
}

static bool sendEditorWheel(MyCodeEditor* editor,
                            Qt::KeyboardModifiers modifiers,
                            int angleY)
{
    if (!editor || !editor->viewport())
        return false;

    const QPoint point(16, 16);
    const QPoint globalPoint = editor->viewport()->mapToGlobal(point);
    QWheelEvent event(QPointF(point),
                      QPointF(globalPoint),
                      QPoint(),
                      QPoint(0, angleY),
                      Qt::NoButton,
                      modifiers,
                      Qt::NoScrollPhase,
                      false);
    event.ignore();
    QCoreApplication::sendEvent(editor->viewport(), &event);
    return event.isAccepted();
}

static void acceptNextLineEditDialog(const QString& text)
{
    auto action = std::make_shared<std::function<void(int)>>();
    *action = [text, action](int attempts) {
        QWidget* dialog = QApplication::activeModalWidget();
        if (!dialog) {
            if (attempts > 0)
                QTimer::singleShot(10, [action, attempts]() { (*action)(attempts - 1); });
            return;
        }

        QLineEdit* lineEdit = dialog->findChild<QLineEdit*>();
        if (lineEdit) {
            lineEdit->setText(text);
            lineEdit->selectAll();
        }

        QDialogButtonBox* buttons =
            dialog->findChild<QDialogButtonBox*>();
        if (buttons && buttons->button(QDialogButtonBox::Ok)) {
            buttons->button(QDialogButtonBox::Ok)->click();
            return;
        }

        if (QDialog* asDialog = qobject_cast<QDialog*>(dialog))
            asDialog->accept();
    };
    QTimer::singleShot(0, [action]() { (*action)(50); });
}

static void acceptNextMessageBoxYes()
{
    auto action = std::make_shared<std::function<void(int)>>();
    *action = [action](int attempts) {
        QMessageBox* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) {
            if (attempts > 0)
                QTimer::singleShot(10, [action, attempts]() { (*action)(attempts - 1); });
            return;
        }
        if (QAbstractButton* yes = box->button(QMessageBox::Yes))
            yes->click();
    };
    QTimer::singleShot(0, [action]() { (*action)(50); });
}

static void acceptNextMultilineDialog(const QString& text)
{
    auto action = std::make_shared<std::function<void(int)>>();
    *action = [text, action](int attempts) {
        QWidget* dialog = QApplication::activeModalWidget();
        QTextEdit* textEdit = dialog ? dialog->findChild<QTextEdit*>() : nullptr;
        QPlainTextEdit* plainTextEdit =
            dialog ? dialog->findChild<QPlainTextEdit*>() : nullptr;
        if (!dialog || (!textEdit && !plainTextEdit)) {
            if (attempts > 0)
                QTimer::singleShot(10, [action, attempts]() { (*action)(attempts - 1); });
            return;
        }

        if (textEdit) {
            textEdit->setPlainText(text);
            textEdit->selectAll();
        } else if (plainTextEdit) {
            plainTextEdit->setPlainText(text);
            plainTextEdit->selectAll();
        }

        QDialogButtonBox* buttons =
            dialog->findChild<QDialogButtonBox*>();
        if (buttons && buttons->button(QDialogButtonBox::Ok)) {
            buttons->button(QDialogButtonBox::Ok)->click();
            return;
        }
        if (QDialog* asDialog = qobject_cast<QDialog*>(dialog))
            asDialog->accept();
    };
    QTimer::singleShot(0, [action]() { (*action)(50); });
}

static std::unique_ptr<QSettings> makeTemporarySettings(
    const QString& fileName)
{
    return std::make_unique<QSettings>(fileName, QSettings::IniFormat);
}

static void runEditorAppearanceSettingsRegression()
{
    QTemporaryDir settingsDir;
    expectBool("appearance settings temp dir valid",
               settingsDir.isValid(),
               true);
    if (!settingsDir.isValid())
        return;

    const QString settingsFile =
        settingsDir.filePath(QStringLiteral("appearance.ini"));
    EditorAppearanceOptions options = EditorAppearance::defaultOptions();
    options.fontFamily = EditorAppearance::fallbackFontFamily();
    options.fontSizePt = 13;
    options.lineHeight = 1.5;
    options.ligaturesEnabled = true;

    {
        EditorAppearanceSettings settings(
            makeTemporarySettings(settingsFile));
        QSignalSpy spy(&settings,
                       &EditorAppearanceSettings::settingsChanged);
        settings.setOptions(options);
        expectBool("appearance settings emits change",
                   spy.count() == 1,
                   true);
    }

    EditorAppearanceSettings reloaded(
        makeTemporarySettings(settingsFile));
    const EditorAppearanceOptions persisted = reloaded.options();
    expectBool("appearance settings persists font",
               persisted.fontFamily == options.fontFamily,
               true);
    expectBool("appearance settings persists size",
               persisted.fontSizePt == options.fontSizePt,
               true);
    expectBool("appearance settings persists line height",
               qAbs(persisted.lineHeight - options.lineHeight) < 0.001,
               true);
    expectBool("appearance settings persists ligatures",
               persisted.ligaturesEnabled == options.ligaturesEnabled,
               true);

    MyCodeEditor editor;
    editor.setPlainText(QStringLiteral("module appearance_probe;\nendmodule\n"));
    editor.applyAppearanceSettings(options);
    expectBool("editor applies appearance font size",
               editor.font().pointSize() == options.fontSizePt,
               true);
    expectBool("editor applies appearance family",
               editor.font().family() == options.fontFamily,
               true);
    expectBool("editor applies appearance line height",
               editor.document()->firstBlock().blockFormat().lineHeight()
                   == qRound(options.lineHeight * 100.0),
               true);
    expectBool("editor applies appearance ligatures",
               editor.font().featureValue(QFont::Tag("liga")) == 1U,
               true);

    expectBool("appearance filters cjk latin alias",
               EditorAppearance::isCjkFontFamily(QStringLiteral("Microsoft YaHei")),
               true);
    expectBool("appearance filters cjk family name",
               EditorAppearance::isCjkFontFamily(QStringLiteral("微软雅黑")),
               true);
    expectBool("appearance keeps latin monospace family",
               !EditorAppearance::isCjkFontFamily(QStringLiteral("Consolas")),
               true);

    const QStringList recommended =
        EditorAppearance::recommendedFontFamilies();
    const QStringList expectedRecommended{
        QStringLiteral("Cascadia Code"),
        QStringLiteral("Maple Mono"),
        QStringLiteral("Iosevka"),
        QStringLiteral("Monaspace Neon"),
        QStringLiteral("Intel One Mono"),
        QStringLiteral("Geist Mono"),
        QStringLiteral("0xProto"),
    };
    expectBool("appearance keeps selected editor fonts",
               recommended == expectedRecommended,
               true);
    expectBool("appearance loads bundled editor fonts",
               EditorAppearance::ensureApplicationFontsLoaded(),
               true);

    const QString fallback = EditorAppearance::fallbackFontFamily();
    expectBool("appearance fallback is Maple Mono",
               fallback == QStringLiteral("Maple Mono"),
               true);
    expectBool("appearance default font size is 15pt",
               EditorAppearance::defaultOptions().fontSizePt == 15,
               true);
    expectBool("appearance accepts Maple Mono",
               EditorAppearance::resolveFontFamily(QStringLiteral("Maple Mono"))
                   == QStringLiteral("Maple Mono"),
               true);
    expectBool("appearance accepts Iosevka",
               EditorAppearance::resolveFontFamily(QStringLiteral("Iosevka"))
                   == QStringLiteral("Iosevka"),
               true);
    expectBool("appearance accepts Monaspace Neon",
               EditorAppearance::resolveFontFamily(QStringLiteral("Monaspace Neon"))
                   == QStringLiteral("Monaspace Neon"),
               true);
    expectBool("appearance accepts Intel One Mono",
               EditorAppearance::resolveFontFamily(QStringLiteral("Intel One Mono"))
                   == QStringLiteral("Intel One Mono"),
               true);
    expectBool("appearance accepts Geist Mono",
               EditorAppearance::resolveFontFamily(QStringLiteral("Geist Mono"))
                   == QStringLiteral("Geist Mono"),
               true);
    expectBool("appearance accepts 0xProto",
               EditorAppearance::resolveFontFamily(QStringLiteral("0xProto"))
                   == QStringLiteral("0xProto"),
               true);
    expectBool("appearance rejects non selected saved font",
               EditorAppearance::resolveFontFamily(QStringLiteral("Consolas"))
                   == QStringLiteral("Maple Mono"),
               true);
    expectBool("appearance rejects commercial saved font",
               EditorAppearance::resolveFontFamily(QStringLiteral("Berkeley Mono"))
                   == QStringLiteral("Maple Mono"),
               true);

    {
        const QString cjkSettingsFile =
            settingsDir.filePath(QStringLiteral("appearance_cjk.ini"));
        {
            QSettings writer(cjkSettingsFile, QSettings::IniFormat);
            writer.setValue(QStringLiteral("editorAppearance/fontFamily"),
                            QStringLiteral("Microsoft YaHei"));
            writer.setValue(QStringLiteral("editorAppearance/fontSizePt"), 14);
            writer.sync();
        }
        EditorAppearanceSettings cjkReloaded(
            makeTemporarySettings(cjkSettingsFile));
        expectBool("appearance cjk saved font falls back",
                   cjkReloaded.options().fontFamily
                       == EditorAppearance::fallbackFontFamily(),
                   true);
    }

    {
        EditorAppearanceSettings panelSettings(
            makeTemporarySettings(
                settingsDir.filePath(QStringLiteral("appearance_panel.ini"))));
        EditorAppearancePanel panel(&panelSettings);
        QComboBox* combo =
            panel.findChild<QComboBox*>(
                QStringLiteral("editorFontFamilyCombo"));
        QStringList comboFamilies;
        if (combo) {
            for (int i = 0; i < combo->count(); ++i) {
                const QString family = combo->itemText(i);
                if (!family.isEmpty())
                    comboFamilies.append(family);
            }
        }
        expectBool("appearance combo only selected fonts",
                   combo && comboFamilies == expectedRecommended,
                   true);
        expectBool("appearance combo has no cjk fonts",
                   combo && !EditorAppearance::isCjkFontFamily(comboFamilies.value(0)),
                   true);
    }
}

static void runEditorAppearanceCoordinatorRegression()
{
    QTemporaryDir settingsDir;
    expectBool("appearance coordinator temp dir valid",
               settingsDir.isValid(),
               true);
    if (!settingsDir.isValid())
        return;

    QTabWidget tabsWidget;
    TabManager tabs(&tabsWidget);
    ModeManager modes(&tabsWidget);
    EditorCoordinator coordinator(&tabs, &modes);
    EditorAppearanceSettings settings(
        makeTemporarySettings(
            settingsDir.filePath(QStringLiteral("appearance.ini"))));

    coordinator.setAppearanceSettings(&settings);
    coordinator.connectSignals();

    tabs.createNewTab();
    tabs.createNewTab();
    expectBool("appearance coordinator has editors",
               tabs.editorCount() == 2,
               true);

    EditorAppearanceOptions options = EditorAppearance::defaultOptions();
    options.fontFamily = EditorAppearance::fallbackFontFamily();
    options.fontSizePt = 15;
    options.lineHeight = 1.35;
    options.ligaturesEnabled = false;
    settings.setOptions(options);

    bool allOpenEditorsUpdated = true;
    for (int i = 0; i < tabs.editorCount(); ++i) {
        MyCodeEditor* editor = tabs.getEditorAt(i);
        allOpenEditorsUpdated = allOpenEditorsUpdated
            && editor
            && editor->font().pointSize() == options.fontSizePt
            && editor->font().featureValue(QFont::Tag("liga")) == 0U;
    }
    expectBool("appearance coordinator updates open editors",
               allOpenEditorsUpdated,
               true);

    tabs.createNewTab();
    MyCodeEditor* newEditor = tabs.getCurrentEditor();
    expectBool("appearance coordinator applies new editor",
               newEditor && newEditor->font().pointSize() == options.fontSizePt,
               true);
    if (newEditor) {
        QStringList scrollLines;
        for (int i = 0; i < 200; ++i)
            scrollLines.append(QStringLiteral("line %1").arg(i));
        newEditor->setPlainText(scrollLines.join(QLatin1Char('\n')));
        newEditor->resize(420, 180);
        newEditor->show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        expectBool("appearance wheel zoom event accepted",
                   sendEditorWheel(newEditor,
                                   Qt::ControlModifier | Qt::ShiftModifier,
                                   120),
                   true);
        expectBool("appearance wheel zoom increases font size",
                   settings.options().fontSizePt == options.fontSizePt + 1
                       && newEditor->font().pointSize()
                              == options.fontSizePt + 1,
                   true);

        const int fontSizeAfterZoom = settings.options().fontSizePt;
        const int scrollBefore = newEditor->verticalScrollBar()->value();
        expectBool("appearance ctrl wheel fast scroll accepted",
                   sendEditorWheel(newEditor, Qt::ControlModifier, -120),
                   true);
        expectBool("appearance ctrl wheel keeps font size",
                   settings.options().fontSizePt == fontSizeAfterZoom
                       && newEditor->font().pointSize() == fontSizeAfterZoom,
                   true);
        expectBool("appearance ctrl wheel scrolls editor",
                   newEditor->verticalScrollBar()->value() >= scrollBefore,
                   true);
    }
}

static void runFormatterSettingsRegression()
{
    QTemporaryDir settingsDir;
    expectBool("formatter settings temp dir valid",
               settingsDir.isValid(),
               true);
    if (!settingsDir.isValid())
        return;

    const QString settingsFile =
        settingsDir.filePath(QStringLiteral("formatter.ini"));
    {
        FormatterSettings settings(makeTemporarySettings(settingsFile));
        QSignalSpy spy(&settings, &FormatterSettings::settingsChanged);
        QSignalSpy formatOnSaveSpy(
            &settings,
            &FormatterSettings::formatOnSaveChanged);
        expectBool("formatter settings default format-on-save off",
                   !settings.formatOnSaveEnabled(),
                   true);
        settings.setProfile(FormatterProfile::IndentOnly);
        expectBool("formatter settings emits change",
                   spy.count() == 1,
                   true);
        settings.setFormatOnSaveEnabled(true);
        expectBool("formatter settings emits format-on-save change",
                   formatOnSaveSpy.count() == 1,
                   true);
    }

    FormatterSettings reloaded(makeTemporarySettings(settingsFile));
    expectBool("formatter settings persists profile",
               reloaded.profile() == FormatterProfile::IndentOnly,
               true);
    expectBool("formatter settings persists format-on-save",
               reloaded.formatOnSaveEnabled(),
               true);

    {
        QSettings writer(settingsFile, QSettings::IniFormat);
        writer.setValue(QStringLiteral("formatter/profile"),
                        QStringLiteral("unknown"));
        writer.sync();
    }
    FormatterSettings invalidReload(makeTemporarySettings(settingsFile));
    expectBool("formatter settings invalid profile falls back",
               invalidReload.profile() == FormatterProfile::Structured,
               true);
}

static void runFormatterCoordinatorRegression()
{
    QTemporaryDir settingsDir;
    expectBool("formatter coordinator temp dir valid",
               settingsDir.isValid(),
               true);
    if (!settingsDir.isValid())
        return;

    QTabWidget tabsWidget;
    TabManager tabs(&tabsWidget);
    ModeManager modes(&tabsWidget);
    EditorCoordinator coordinator(&tabs, &modes);
    FormatterSettings settings(
        makeTemporarySettings(
            settingsDir.filePath(QStringLiteral("formatter.ini"))));

    coordinator.setFormatterSettings(&settings);
    coordinator.connectSignals();

    tabs.createNewTab();
    tabs.createNewTab();
    expectBool("formatter coordinator has editors",
               tabs.editorCount() == 2,
               true);

    settings.setProfile(FormatterProfile::IndentOnly);
    settings.setFormatOnSaveEnabled(true);
    bool allOpenEditorsUpdated = true;
    for (int i = 0; i < tabs.editorCount(); ++i) {
        MyCodeEditor* editor = tabs.getEditorAt(i);
        allOpenEditorsUpdated = allOpenEditorsUpdated
            && editor
            && editor->formatterProfile() == FormatterProfile::IndentOnly
            && editor->formatOnSaveEnabled();
    }
    expectBool("formatter coordinator updates open editors",
               allOpenEditorsUpdated,
               true);

    MyCodeEditor* currentEditor = tabs.getCurrentEditor();
    if (currentEditor) {
        currentEditor->setFormatterProfile(FormatterProfile::Structured);
        currentEditor->setFormatOnSaveEnabled(false);
    }
    expectBool("formatter coordinator stores editor change",
               settings.profile() == FormatterProfile::Structured,
               true);
    expectBool("formatter coordinator stores format-on-save change",
               !settings.formatOnSaveEnabled(),
               true);

    tabs.createNewTab();
    MyCodeEditor* newEditor = tabs.getCurrentEditor();
    expectBool("formatter coordinator applies new editor",
               newEditor
                   && newEditor->formatterProfile()
                       == FormatterProfile::Structured
                   && !newEditor->formatOnSaveEnabled(),
               true);
}

static void runTabOpenDedupRegression()
{
    QTemporaryDir dir;
    expectBool("tab open dedup temp dir valid", dir.isValid(), true);
    if (!dir.isValid())
        return;

    const QString filePath = dir.filePath(QStringLiteral("defs.svh"));
    QFile file(filePath);
    expectBool("tab open dedup fixture writable",
               file.open(QIODevice::WriteOnly | QIODevice::Text),
               true);
    if (!file.isOpen())
        return;
    file.write("`define WIDTH 8\n");
    file.close();

    QTabWidget tabsWidget;
    TabManager tabs(&tabsWidget);

    expectBool("tab open first file succeeds",
               tabs.openFileInTab(filePath),
               true);
    MyCodeEditor* firstEditor = tabs.getCurrentEditor();
    expectBool("tab open first file creates one tab",
               firstEditor && tabs.editorCount() == 1,
               true);

    const QString nativePath = QDir::toNativeSeparators(filePath);
    expectBool("tab open duplicate file succeeds",
               tabs.openFileInTab(nativePath),
               true);
    expectBool("tab open duplicate activates existing tab",
               tabs.editorCount() == 1
                   && tabs.getCurrentEditor() == firstEditor,
               true);

    tabs.createNewTab();
    expectBool("tab open dedup has scratch tab",
               tabs.editorCount() == 2
                   && tabs.getCurrentEditor() != firstEditor,
               true);
    expectBool("tab open existing from scratch succeeds",
               tabs.openFileInTab(filePath),
               true);
    expectBool("tab open existing from scratch reuses tab",
               tabs.editorCount() == 2
                   && tabs.getCurrentEditor() == firstEditor,
               true);

    QTemporaryDir workspaceA;
    QTemporaryDir workspaceB;
    QTemporaryDir externalDir;
    expectBool("tab workspace scope dirs valid",
               workspaceA.isValid()
                   && workspaceB.isValid()
                   && externalDir.isValid(),
               true);
    if (!workspaceA.isValid()
        || !workspaceB.isValid()
        || !externalDir.isValid()) {
        return;
    }

    const QString fileA =
        QDir(workspaceA.path()).absoluteFilePath(QStringLiteral("a.sv"));
    const QString fileB =
        QDir(workspaceB.path()).absoluteFilePath(QStringLiteral("b.sv"));
    const QString externalFile =
        QDir(externalDir.path()).absoluteFilePath(QStringLiteral("external.sv"));
    auto writeProbeFile = [](const QString& path, const QByteArray& text) {
        QFile probe(path);
        if (!probe.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        probe.write(text);
        return true;
    };
    expectBool("tab workspace scope files writable",
               writeProbeFile(fileA, "module a; endmodule\n")
                   && writeProbeFile(fileB, "module b; endmodule\n")
                   && writeProbeFile(externalFile, "module ext; endmodule\n"),
               true);

    QTabWidget scopedWidget;
    TabManager scopedTabs(&scopedWidget);
    expectBool("tab workspace scope open A",
               scopedTabs.openFileInTab(fileA),
               true);
    MyCodeEditor* editorA = scopedTabs.getCurrentEditor();
    expectBool("tab workspace scope open B",
               scopedTabs.openFileInTab(fileB),
               true);
    MyCodeEditor* editorB = scopedTabs.getCurrentEditor();
    expectBool("tab workspace scope open external",
               scopedTabs.openFileInTab(externalFile),
               true);
    MyCodeEditor* externalEditor = scopedTabs.getCurrentEditor();
    scopedTabs.createNewTab();
    MyCodeEditor* scratchEditor = scopedTabs.getCurrentEditor();

    auto tabVisible = [&scopedWidget](MyCodeEditor* editor) {
        const int index = scopedWidget.indexOf(editor);
        return index >= 0 && scopedWidget.tabBar()->isTabVisible(index);
    };

    scopedTabs.setWorkspaceScope(
        {workspaceA.path(), workspaceB.path()},
        workspaceA.path());
    expectBool("workspace A scope shows A scratch external",
               tabVisible(editorA)
                   && !tabVisible(editorB)
                   && tabVisible(externalEditor)
                   && tabVisible(scratchEditor),
               true);

    scopedTabs.setWorkspaceScope(
        {workspaceA.path(), workspaceB.path()},
        workspaceB.path());
    expectBool("workspace B scope shows B scratch external",
               !tabVisible(editorA)
                   && tabVisible(editorB)
                   && tabVisible(externalEditor)
                   && tabVisible(scratchEditor),
               true);

    scopedTabs.setWorkspaceScope({}, {});
    expectBool("workspace scope cleared shows all tabs",
               tabVisible(editorA)
                   && tabVisible(editorB)
                   && tabVisible(externalEditor)
                   && tabVisible(scratchEditor),
               true);

    QSignalSpy scopedCloseSpy(&scopedTabs, &TabManager::tabClosed);
    expectBool("close workspace tabs succeeds",
               scopedTabs.closeTabsInWorkspace(workspaceB.path()),
               true);
    expectBool("close workspace tabs removes only workspace file",
               scopedWidget.indexOf(editorA) >= 0
                   && scopedWidget.indexOf(editorB) < 0
                   && scopedWidget.indexOf(externalEditor) >= 0
                   && scopedWidget.indexOf(scratchEditor) >= 0
                   && scopedTabs.editorCount() == 3
                   && scopedCloseSpy.size() == 1,
               true);
}

static void runWorkspaceCloseRegression()
{
    QTemporaryDir workspaceA;
    QTemporaryDir workspaceB;
    QTemporaryDir workspaceC;
    expectBool("workspace close temp dirs valid",
               workspaceA.isValid()
                   && workspaceB.isValid()
                   && workspaceC.isValid(),
               true);
    if (!workspaceA.isValid()
        || !workspaceB.isValid()
        || !workspaceC.isValid()) {
        return;
    }

    WorkspaceManager workspace;
    QSignalSpy closedSpy(&workspace, &WorkspaceManager::workspaceClosed);
    expectBool("workspace close open A",
               workspace.openWorkspace(workspaceA.path()),
               true);
    expectBool("workspace close open B",
               workspace.openWorkspace(workspaceB.path()),
               true);
    expectBool("workspace close open C",
               workspace.openWorkspace(workspaceC.path()),
               true);
    expectBool("workspace close switch to B",
               workspace.switchWorkspace(1),
               true);

    expectBool("workspace close removes inactive before active",
               workspace.closeWorkspace(0),
               true);
    expectBool("workspace close keeps active path after inactive close",
               workspace.workspaceEntries().size() == 2
                   && workspace.activeWorkspaceIndex() == 0
                   && workspace.getWorkspacePath()
                       == QDir::cleanPath(QDir::fromNativeSeparators(
                              QFileInfo(workspaceB.path()).absoluteFilePath()))
                   && closedSpy.size() == 0,
               true);

    expectBool("workspace close active switches next workspace",
               workspace.closeWorkspace(0),
               true);
    expectBool("workspace close active selects remaining workspace",
               workspace.workspaceEntries().size() == 1
                   && workspace.activeWorkspaceIndex() == 0
                   && workspace.getWorkspacePath()
                       == QDir::cleanPath(QDir::fromNativeSeparators(
                              QFileInfo(workspaceC.path()).absoluteFilePath()))
                   && closedSpy.size() == 1,
               true);

    expectBool("workspace close final workspace succeeds",
               workspace.closeWorkspace(0),
               true);
    expectBool("workspace close final clears active workspace",
               workspace.workspaceEntries().isEmpty()
                   && workspace.activeWorkspaceIndex() == -1
                   && !workspace.isWorkspaceOpen()
                   && closedSpy.size() == 2,
               true);
}

static void runWorkspaceCachedSwitchRegression()
{
    QTemporaryDir workspaceA;
    QTemporaryDir workspaceB;
    expectBool("workspace cached switch temp dirs valid",
               workspaceA.isValid() && workspaceB.isValid(),
               true);
    if (!workspaceA.isValid() || !workspaceB.isValid())
        return;

    const QString fileA =
        QDir(workspaceA.path()).absoluteFilePath(QStringLiteral("a_top.sv"));
    const QString fileB =
        QDir(workspaceB.path()).absoluteFilePath(QStringLiteral("b_top.sv"));
    {
        QFile out(fileA);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write("module a_top; endmodule\n");
    }
    {
        QFile out(fileB);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write("module b_top; endmodule\n");
    }

    WorkspaceManager workspace;
    QSignalSpy openedSpy(&workspace, &WorkspaceManager::workspaceOpened);
    QSignalSpy activatedSpy(&workspace, &WorkspaceManager::workspaceActivated);
    QSignalSpy filesScannedSpy(&workspace, &WorkspaceManager::filesScanned);
    QSignalSpy scanStartedSpy(&workspace, &WorkspaceManager::workspaceScanStarted);

    expectBool("workspace cached switch open A",
               workspace.openWorkspace(workspaceA.path()),
               true);
    expectBool("workspace cached switch scans A",
               waitUntil([&]() {
                   return workspace.getSystemVerilogFiles().contains(
                       QDir::cleanPath(QDir::fromNativeSeparators(
                           QFileInfo(fileA).absoluteFilePath())));
               }, 2000),
               true);

    expectBool("workspace cached switch open B",
               workspace.openWorkspace(workspaceB.path()),
               true);
    expectBool("workspace cached switch scans B",
               waitUntil([&]() {
                   return workspace.getSystemVerilogFiles().contains(
                       QDir::cleanPath(QDir::fromNativeSeparators(
                           QFileInfo(fileB).absoluteFilePath())));
               }, 2000),
               true);

    const int openedBeforeSwitch = openedSpy.count();
    const int filesScannedBeforeSwitch = filesScannedSpy.count();
    const int scanStartedBeforeSwitch = scanStartedSpy.count();
    const int activatedBeforeSwitch = activatedSpy.count();

    expectBool("workspace cached switch activates A",
               workspace.switchWorkspace(0),
               true);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    const QString normalizedA =
        QDir::cleanPath(QDir::fromNativeSeparators(
            QFileInfo(fileA).absoluteFilePath()));
    expectBool("workspace cached switch restores A files",
               workspace.getSystemVerilogFiles() == QStringList{normalizedA},
               true);
    expectBool("workspace cached switch emits activation only",
               activatedSpy.count() == activatedBeforeSwitch + 1
                   && openedSpy.count() == openedBeforeSwitch
                   && filesScannedSpy.count() == filesScannedBeforeSwitch
                   && scanStartedSpy.count() == scanStartedBeforeSwitch,
               true);

    expectBool("workspace cached switch activates B",
               workspace.switchWorkspace(1),
               true);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    const QString normalizedB =
        QDir::cleanPath(QDir::fromNativeSeparators(
            QFileInfo(fileB).absoluteFilePath()));
    expectBool("workspace cached switch restores B files",
               workspace.getSystemVerilogFiles() == QStringList{normalizedB},
               true);
    expectBool("workspace cached switch still avoids rescan",
               openedSpy.count() == openedBeforeSwitch
                   && filesScannedSpy.count() == filesScannedBeforeSwitch
                   && scanStartedSpy.count() == scanStartedBeforeSwitch,
               true);
}

static void runWorkspaceAliasRenameRegression()
{
    QTemporaryDir workspaceA;
    QTemporaryDir workspaceB;
    expectBool("workspace rename temp dirs valid",
               workspaceA.isValid() && workspaceB.isValid(),
               true);
    if (!workspaceA.isValid() || !workspaceB.isValid())
        return;

    WorkspaceManager workspace;
    QSignalSpy listSpy(&workspace, &WorkspaceManager::workspaceListChanged);
    expectBool("workspace rename open A",
               workspace.openWorkspace(workspaceA.path()),
               true);
    expectBool("workspace rename open B",
               workspace.openWorkspace(workspaceB.path()),
               true);
    const QString pathA = workspace.workspaceEntries().at(0).path;
    const QString pathB = workspace.workspaceEntries().at(1).path;
    const QString originalAliasA = workspace.workspaceEntries().at(0).alias;

    QString errorMessage;
    expectBool("workspace rename active succeeds",
               workspace.renameWorkspaceAlias(
                   1,
                   QStringLiteral("SharedName"),
                   &errorMessage),
               true);
    expectBool("workspace rename active updates current alias",
               workspace.getWorkspaceAlias() == QStringLiteral("SharedName")
                   && workspace.workspaceEntries().at(1).path == pathB,
               true);

    expectBool("workspace rename switch to A",
               workspace.switchWorkspace(0),
               true);
    expectBool("workspace rename rejects empty alias",
               workspace.renameWorkspaceAlias(0, QStringLiteral("   "),
                                             &errorMessage),
               false);
    expectBool("workspace rename rejects duplicate alias",
               workspace.renameWorkspaceAlias(
                   0,
                   QStringLiteral("sharedname"),
                   &errorMessage),
               false);
    expectBool("workspace rename failed attempts keep alias",
               workspace.workspaceEntries().at(0).alias == originalAliasA,
               true);

    expectBool("workspace rename active workspace succeeds",
               workspace.renameWorkspaceAlias(
                   0,
                   QStringLiteral("RTL Core"),
                   &errorMessage),
               true);
    expectBool("workspace rename updates active alias and preserves path",
               workspace.getWorkspaceAlias() == QStringLiteral("RTL Core")
                   && workspace.workspaceEntries().at(0).alias
                          == QStringLiteral("RTL Core")
                   && workspace.workspaceEntries().at(0).path == pathA,
               true);

    bool sawRecentA = false;
    bool sawRecentB = false;
    const QList<WorkspaceManager::WorkspaceEntry> recentEntries =
        workspace.recentWorkspaceEntries();
    for (const WorkspaceManager::WorkspaceEntry& entry : recentEntries) {
        sawRecentA = sawRecentA
            || (entry.path == pathA
                && entry.alias == QStringLiteral("RTL Core"));
        sawRecentB = sawRecentB
            || (entry.path == pathB
                && entry.alias == QStringLiteral("SharedName"));
    }
    expectBool("workspace rename updates recent metadata",
               sawRecentA && sawRecentB,
               true);
    expectBool("workspace rename emits list changes",
               listSpy.size() >= 4,
               true);
}

static void runIncludeCompletionRegression()
{
    auto includeProvider = [](const QString&) {
        return QStringList{
            QStringLiteral("defs.svh"),
            QStringLiteral("rtl/top_defs.svh")
        };
    };

    MyCodeEditor partialEditor;
    partialEditor.setIncludeFileCompletionProvider(includeProvider);
    partialEditor.resize(480, 120);
    partialEditor.show();
    partialEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    partialEditor.insertPlainText(QStringLiteral("`inc "));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("include completion waits for full keyword",
               partialEditor.toPlainText() == QStringLiteral("`inc "),
               true);

    MyCodeEditor plainEditor;
    plainEditor.setIncludeFileCompletionProvider(includeProvider);
    plainEditor.resize(480, 120);
    plainEditor.show();
    plainEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    plainEditor.insertPlainText(QStringLiteral("include "));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("include completion requires backtick keyword",
               plainEditor.toPlainText() == QStringLiteral("include "),
               true);

    MyCodeEditor editor;
    editor.setIncludeFileCompletionProvider(includeProvider);
    editor.resize(560, 160);
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    editor.insertPlainText(QStringLiteral("`include "));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    expectBool("include completion inserts quotes",
               editor.toPlainText() == QStringLiteral("`include \"\""),
               true);
    expectBool("include completion places cursor inside quotes",
               editor.textCursor().position()
                   == QStringLiteral("`include \"").size(),
               true);

    QCompleter* completer = editor.findChild<QCompleter*>();
    bool hasDefsCandidate = false;
    if (completer && completer->model()) {
        for (int row = 0; row < completer->model()->rowCount(); ++row) {
            const QString text =
                completer->model()->data(
                    completer->model()->index(row, 0),
                    Qt::DisplayRole).toString();
            hasDefsCandidate = hasDefsCandidate
                || text.contains(QStringLiteral("defs.svh"));
        }
    }
    expectBool("include completion shows workspace file candidate",
               completer && completer->popup()->isVisible() && hasDefsCandidate,
               true);

    editor.insertPlainText(QStringLiteral("de"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("include completion tab inserts selected file",
               editor.toPlainText() == QStringLiteral("`include \"defs.svh\""),
               true);

    MyCodeEditor newHeaderEditor;
    newHeaderEditor.setIncludeFileCompletionProvider(includeProvider);
    IncludeNewHeaderRequest createdRequest;
    int createCalls = 0;
    newHeaderEditor.setIncludeNewHeaderCreator(
        [&](const IncludeNewHeaderRequest& request) {
            createdRequest = request;
            ++createCalls;
            IncludeNewHeaderResult result;
            result.success = true;
            result.includePath =
                request.fileStem + QLatin1Char('.') + request.extension;
            return result;
        });
    newHeaderEditor.resize(640, 180);
    newHeaderEditor.show();
    newHeaderEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    newHeaderEditor.insertPlainText(QStringLiteral("`include "));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    newHeaderEditor.insertPlainText(QStringLiteral("-n new_defs"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    QCompleter* newHeaderCompleter =
        newHeaderEditor.findChild<QCompleter*>();
    auto selectCompletionContaining =
        [](QCompleter* targetCompleter, const QString& needle) {
            if (!targetCompleter || !targetCompleter->model())
                return QModelIndex();
            for (int row = 0; row < targetCompleter->model()->rowCount(); ++row) {
                const QModelIndex index =
                    targetCompleter->model()->index(row, 0);
                const QString text =
                    targetCompleter->model()->data(
                        index,
                        Qt::DisplayRole).toString();
                if (text.contains(needle)) {
                    targetCompleter->popup()->setCurrentIndex(index);
                    return index;
                }
            }
            return QModelIndex();
        };

    const QString defaultFormatText =
        newHeaderCompleter && newHeaderCompleter->popup()
        ? newHeaderCompleter->model()->data(
            newHeaderCompleter->popup()->currentIndex(),
            Qt::DisplayRole).toString()
        : QString();
    const QModelIndex vhIndex =
        selectCompletionContaining(newHeaderCompleter,
                                   QStringLiteral("vh - Verilog header"));
    expectBool("include new header shows format choices",
               newHeaderCompleter && newHeaderCompleter->popup()->isVisible()
                   && vhIndex.isValid(),
               true);
    expectBool("include new header defaults to first format",
               defaultFormatText.contains(QStringLiteral("vh - Verilog header")),
               true);
    QTest::keyClick(&newHeaderEditor, Qt::Key_Tab);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    expectBool("include new header format moves to template stage",
               newHeaderEditor.toPlainText()
                   == QStringLiteral("`include \"-n new_defs vh \""),
               true);

    const QModelIndex guardIndex =
        newHeaderCompleter && newHeaderCompleter->popup()
        ? newHeaderCompleter->popup()->currentIndex()
        : QModelIndex();
    const QString guardDisplay = guardIndex.isValid()
        ? newHeaderCompleter->model()->data(
            guardIndex,
            Qt::DisplayRole).toString()
        : QString();
    const QString guardPreview = guardIndex.isValid()
        ? newHeaderCompleter->model()->data(
            guardIndex,
            Qt::ToolTipRole).toString()
        : QString();
    expectBool("include new header defaults to first template",
               guardDisplay.contains(QStringLiteral("empty - blank header")),
               true);
    expectBool("include new header shows template preview",
               guardIndex.isValid()
                   && guardDisplay.contains(QStringLiteral("<cursor>"))
                   && guardPreview.contains(QStringLiteral("<cursor>")),
               true);
    QTest::keyClick(&newHeaderEditor, Qt::Key_Tab);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    expectBool("include new header creates requested file",
               createCalls == 1
                   && createdRequest.fileStem == QStringLiteral("new_defs")
                   && createdRequest.extension == QStringLiteral("vh")
                   && createdRequest.templateName == QStringLiteral("empty"),
               true);
    expectBool("include new header inserts created include",
               newHeaderEditor.toPlainText()
                   == QStringLiteral("`include \"new_defs.vh\""),
               true);

    QTemporaryDir workspaceDir;
    expectBool("include new header workspace temp dir valid",
               workspaceDir.isValid(),
               true);
    if (!workspaceDir.isValid())
        return;
    QTabWidget tabsWidget;
    TabManager tabs(&tabsWidget);
    ModeManager modes(&tabsWidget);
    WorkspaceManager workspace;
    EditorCoordinator coordinator(&tabs, &modes);
    coordinator.setWorkflowDependencies(&workspace, nullptr, nullptr, nullptr);
    coordinator.connectSignals();
    expectBool("include new header opens test workspace",
               workspace.openWorkspace(workspaceDir.path()),
               true);
    IncludeNewHeaderRequest request;
    request.fileStem = QStringLiteral("created_defs");
    request.extension = QStringLiteral("svh");
    request.templateName = QStringLiteral("guard");
    request.cursorToken = QStringLiteral("__CURSOR__");
    request.templateBody =
        QStringLiteral("`ifndef CREATED_DEFS_SVH_\n"
                       "`define CREATED_DEFS_SVH_\n\n"
                       "__CURSOR__\n\n"
                       "`endif\n");
    const IncludeNewHeaderResult result =
        coordinator.createIncludeNewHeader(request);
    const QString createdPath =
        QDir(workspaceDir.path()).absoluteFilePath(
            QStringLiteral("created_defs.svh"));
    expectBool("include new header coordinator creates file",
               result.success && QFileInfo::exists(createdPath),
               true);
    expectBool("include new header coordinator opens file",
               tabs.editorCount() == 1
                   && tabs.getCurrentEditor()
                   && QDir::cleanPath(QDir::fromNativeSeparators(
                          tabs.getCurrentEditor()->documentFileName()))
                       == QDir::cleanPath(QDir::fromNativeSeparators(
                          createdPath)),
               true);
    expectBool("include new header coordinator places cursor",
               tabs.getCurrentEditor()
                   && tabs.getCurrentEditor()->textCursor().position()
                       == result.cursorPosition,
               true);
}

static void runEditorBracketRangeRegression()
{
    MyCodeEditor editor;
    editor.resize(320, 120);
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTest::keyClick(&editor, Qt::Key_BracketLeft);
    expectBool("editor inserts bracket pair",
               editor.toPlainText() == QStringLiteral("[]")
                   && editor.textCursor().position() == 1,
               true);
    QTest::keyClicks(&editor, "8");
    QTest::keyClick(&editor, Qt::Key_Tab);
    expectBool("editor expands numeric bracket range",
               editor.toPlainText() == QStringLiteral("[7:0]")
                   && editor.textCursor().position()
                       == editor.toPlainText().size(),
               true);

    MyCodeEditor paramEditor;
    paramEditor.resize(320, 120);
    paramEditor.setPlainText(QStringLiteral("[P_W]"));
    QTextCursor paramCursor = paramEditor.textCursor();
    paramCursor.setPosition(QStringLiteral("[P_W").size());
    paramEditor.setTextCursor(paramCursor);
    paramEditor.show();
    paramEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::keyClick(&paramEditor, Qt::Key_Tab);
    expectBool("editor expands parameter bracket range",
               paramEditor.toPlainText()
                   == QStringLiteral("[P_W - 1:0]"),
               true);

    MyCodeEditor exactEditor;
    exactEditor.resize(320, 120);
    exactEditor.show();
    exactEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::keyClick(&exactEditor, Qt::Key_BracketLeft);
    QTest::keyClicks(&exactEditor, "PW+DW:0");
    QTest::keyClick(&exactEditor, Qt::Key_Tab);
    expectBool("editor preserves exact bracket range",
               exactEditor.toPlainText()
                       == QStringLiteral("[PW+DW:0]")
                   && exactEditor.textCursor().position()
                       == exactEditor.toPlainText().size(),
               true);

    MyCodeEditor stepEditor;
    stepEditor.resize(320, 120);
    stepEditor.setPlainText(QStringLiteral("[7:0]"));
    stepEditor.show();
    stepEditor.setFocus();
    QTextCursor stepCursor = stepEditor.textCursor();
    stepCursor.setPosition(2);
    stepEditor.setTextCursor(stepCursor);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QRect clickRect = stepEditor.cursorRect(stepCursor);
    QTest::mouseClick(stepEditor.viewport(),
                      Qt::LeftButton,
                      Qt::ControlModifier,
                      clickRect.center());
    expectBool("editor ctrl-click leaves range body unselected",
               stepEditor.textCursor().selectedText().isEmpty(),
               true);
    QTest::mouseClick(stepEditor.viewport(),
                      Qt::LeftButton,
                      Qt::AltModifier,
                      clickRect.center());
    expectBool("editor alt-click selects range body",
               stepEditor.textCursor().selectedText()
                   == QStringLiteral("7:0"),
               true);
    QTest::keyClick(&stepEditor, Qt::Key_Up);
    expectBool("editor range up increments left bound",
               stepEditor.toPlainText() == QStringLiteral("[8:0]")
                   && stepEditor.textCursor().selectedText()
                       == QStringLiteral("8:0"),
               true);
    QTest::keyClick(&stepEditor, Qt::Key_Up, Qt::ShiftModifier);
    expectBool("editor shift range up increments right bound",
               stepEditor.toPlainText() == QStringLiteral("[8:1]")
                   && stepEditor.textCursor().selectedText()
                       == QStringLiteral("8:1"),
               true);
    QTest::keyClick(&stepEditor, Qt::Key_Down, Qt::ShiftModifier);
    expectBool("editor shift range down decrements right bound",
               stepEditor.toPlainText() == QStringLiteral("[8:0]")
                   && stepEditor.textCursor().selectedText()
                       == QStringLiteral("8:0"),
               true);

    auto selectRangeBody = [](MyCodeEditor& rangeEditor) {
        QTextCursor cursor = rangeEditor.textCursor();
        cursor.setPosition(1);
        cursor.setPosition(rangeEditor.toPlainText().size() - 1,
                           QTextCursor::KeepAnchor);
        rangeEditor.setTextCursor(cursor);
    };
    auto showRangeEditor = [](MyCodeEditor& rangeEditor,
                              const QString& text) {
        rangeEditor.resize(360, 120);
        rangeEditor.setPlainText(text);
        rangeEditor.show();
        rangeEditor.setFocus();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    };

    MyCodeEditor parameterUpEditor;
    showRangeEditor(parameterUpEditor, QStringLiteral("[P_TEST:0]"));
    selectRangeBody(parameterUpEditor);
    QTest::keyClick(&parameterUpEditor, Qt::Key_Up);
    expectBool("editor parameter range up adds one",
               parameterUpEditor.toPlainText()
                   == QStringLiteral("[P_TEST+1:0]"),
               true);

    MyCodeEditor parameterDownEditor;
    showRangeEditor(parameterDownEditor, QStringLiteral("[P_TEST:0]"));
    selectRangeBody(parameterDownEditor);
    QTest::keyClick(&parameterDownEditor, Qt::Key_Down);
    expectBool("editor parameter range down subtracts one",
               parameterDownEditor.toPlainText()
                   == QStringLiteral("[P_TEST-1:0]"),
               true);

    MyCodeEditor complexDownEditor;
    showRangeEditor(complexDownEditor,
                    QStringLiteral("[P_TEST0*P_TEST1:0]"));
    selectRangeBody(complexDownEditor);
    QTest::keyClick(&complexDownEditor, Qt::Key_Down);
    expectBool("editor complex range down parenthesizes expression",
               complexDownEditor.toPlainText()
                   == QStringLiteral("[(P_TEST0*P_TEST1)-1:0]"),
               true);

    MyCodeEditor numericGuardEditor;
    showRangeEditor(numericGuardEditor, QStringLiteral("[0:0]"));
    selectRangeBody(numericGuardEditor);
    QTest::keyClick(&numericGuardEditor, Qt::Key_Down);
    expectBool("editor numeric range down respects right bound",
               numericGuardEditor.toPlainText() == QStringLiteral("[0:0]"),
               true);
}

static void runEditorSmartSelectionRegression()
{
    MyCodeEditor editor;
    editor.resize(520, 140);
    editor.setPlainText(
        QStringLiteral("assign y = ((test_s.test_m == 1) & (test_data == 1));\n"));
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(editor.toPlainText().indexOf(QStringLiteral("test_m")) + 2);
    editor.setTextCursor(cursor);
    QTest::keyClick(&editor, Qt::Key_W, Qt::ControlModifier);
    expectBool("smart selection starts with symbol",
               editor.textCursor().selectedText() == QStringLiteral("test_m"),
               true);
    QTest::keyClick(&editor, Qt::Key_W, Qt::ControlModifier);
    expectBool("smart selection expands to hierarchical expression",
               editor.textCursor().selectedText()
                   == QStringLiteral("test_s.test_m"),
               true);
    QTest::keyClick(&editor, Qt::Key_W, Qt::ControlModifier);
    expectBool("smart selection expands to inner parentheses",
               editor.textCursor().selectedText()
                   == QStringLiteral("test_s.test_m == 1"),
               true);
    QTest::keyClick(&editor, Qt::Key_W, Qt::ControlModifier);
    expectBool("smart selection expands to outer parentheses",
               editor.textCursor().selectedText()
                   == QStringLiteral(
                       "(test_s.test_m == 1) & (test_data == 1)"),
               true);

    QTextCursor numberCursor = editor.textCursor();
    numberCursor.clearSelection();
    numberCursor.setPosition(editor.toPlainText().lastIndexOf(QLatin1Char('1')));
    editor.setTextCursor(numberCursor);
    QTest::keyClick(&editor, Qt::Key_W, Qt::ControlModifier);
    expectBool("smart selection starts operator number at parentheses",
               editor.textCursor().selectedText()
                   == QStringLiteral("test_data == 1"),
               true);
}

static void runEditorOccurrenceNavigationRegression()
{
    MyCodeEditor editor;
    editor.resize(520, 140);
    editor.setPlainText(
        QStringLiteral("assign foo = foo + bar;\nassign bar = foo;\n"));
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    const QString text = editor.toPlainText();
    const int firstFoo = text.indexOf(QStringLiteral("foo"));
    const int secondFoo = text.indexOf(QStringLiteral("foo"), firstFoo + 1);
    const int thirdFoo = text.indexOf(QStringLiteral("foo"), secondFoo + 1);
    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(firstFoo);
    cursor.setPosition(firstFoo + 3, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);

    QTest::keyClick(&editor, Qt::Key_E, Qt::ControlModifier);
    expectBool("occurrence navigation next selects second occurrence",
               editor.textCursor().selectionStart() == secondFoo
                   && editor.textCursor().selectedText()
                          == QStringLiteral("foo"),
               true);
    QTest::keyClick(&editor, Qt::Key_E, Qt::ControlModifier);
    expectBool("occurrence navigation next selects third occurrence",
               editor.textCursor().selectionStart() == thirdFoo
                   && editor.textCursor().selectedText()
                          == QStringLiteral("foo"),
               true);
    QTest::keyClick(&editor, Qt::Key_E, Qt::ControlModifier);
    expectBool("occurrence navigation next wraps",
               editor.textCursor().selectionStart() == firstFoo
                   && editor.textCursor().selectedText()
                          == QStringLiteral("foo"),
               true);
    QTest::keyClick(&editor, Qt::Key_Q, Qt::ControlModifier);
    expectBool("occurrence navigation previous wraps",
               editor.textCursor().selectionStart() == thirdFoo
                   && editor.textCursor().selectedText()
                          == QStringLiteral("foo"),
               true);
}

static void runEditorSafeRenameRegression()
{
    MyCodeEditor editor;
    editor.resize(520, 160);
    editor.setPlainText(
        QStringLiteral("logic oldName;\n"
                       "assign oldName_next = oldName;\n"
                       "assign sink = oldName;\n"));
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextCursor cursor = editor.textCursor();
    const int firstOldName = editor.toPlainText().indexOf(QStringLiteral("oldName"));
    cursor.setPosition(firstOldName);
    cursor.setPosition(firstOldName + QStringLiteral("oldName").size(),
                       QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);

    acceptNextLineEditDialog(QStringLiteral("newName"));
    QTest::keyClick(&editor, Qt::Key_R, Qt::ControlModifier);
    expectBool("safe rename updates current file occurrences",
               editor.toPlainText()
                   == QStringLiteral("logic newName;\n"
                                     "assign oldName_next = newName;\n"
                                     "assign sink = newName;\n"),
               true);
    expectBool("safe rename preserves word boundaries and selection",
               editor.textCursor().selectedText() == QStringLiteral("newName")
                   && editor.toPlainText().contains(QStringLiteral("oldName_next")),
               true);
}

static void runSafeRenameCoordinatorRegression()
{
    QTemporaryDir dir;
    expectBool("safe rename coordinator temp dir valid", dir.isValid(), true);
    if (!dir.isValid())
        return;

    const QString defFile =
        QDir(dir.path()).absoluteFilePath(QStringLiteral("defs.sv"));
    const QString useFile =
        QDir(dir.path()).absoluteFilePath(QStringLiteral("use.sv"));
    const QString defText =
        QStringLiteral("module top;\n"
                       "  parameter int P_WIDTH = 8;\n"
                       "endmodule\n");
    const QString useText =
        QStringLiteral("module top;\n"
                       "  logic [P_WIDTH-1:0] data;\n"
                       "endmodule\n");
    auto writeTextFile = [](const QString& fileName, const QString& text) {
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        file.write(text.toUtf8());
        return true;
    };
    expectBool("safe rename coordinator write def file",
               writeTextFile(defFile, defText),
               true);
    expectBool("safe rename coordinator write use file",
               writeTextFile(useFile, useText),
               true);

    const int defPos = defText.indexOf(QStringLiteral("P_WIDTH"));
    const int usePos = useText.indexOf(QStringLiteral("P_WIDTH"));
    const SemanticSymbolRecord defModule =
        SemanticFixtureRecordBuilder(QStringLiteral("top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(defFile)
            .withLocalHandle(101)
            .withRange(1, 1, 3, 10)
            .record();
    const SemanticSymbolRecord useModule =
        SemanticFixtureRecordBuilder(QStringLiteral("top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(useFile)
            .withLocalHandle(102)
            .withRange(1, 1, 3, 10)
            .record();
    const SemanticSymbolRecord parameter =
        SemanticFixtureRecordBuilder(QStringLiteral("P_WIDTH"),
                                     SymbolTaxonomy::DeclarationKind::Parameter)
            .withFile(defFile)
            .withLocalHandle(103)
            .withLine(2, defPos - defText.lastIndexOf(QLatin1Char('\n'), defPos))
            .withTextSpan(defPos, QStringLiteral("P_WIDTH").size())
            .inModule(QStringLiteral("top"))
            .record();
    QHash<QString, QString> contents;
    contents.insert(defFile, defText);
    contents.insert(useFile, useText);

    const auto previousSnapshot = SemanticIndex::getInstance()->snapshot();
    SemanticIndex::getInstance()->setSnapshot(
        snapshotFromRecords({defModule, useModule, parameter},
                            {},
                            {},
                            contents));

    QTabWidget tabsWidget;
    TabManager tabs(&tabsWidget);
    ModeManager modes(&tabsWidget, &tabsWidget);
    EditorCoordinator coordinator(&tabs, &modes);
    coordinator.connectSignals();

    expectBool("safe rename coordinator opens use file",
               tabs.openFileInTab(useFile),
               true);
    MyCodeEditor* editor = tabs.getCurrentEditor();
    expectBool("safe rename coordinator has editor", editor != nullptr, true);
    if (editor) {
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(usePos);
        cursor.setPosition(usePos + QStringLiteral("P_WIDTH").size(),
                           QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        editor->setFocus();

        acceptNextLineEditDialog(QStringLiteral("P_DATA"));
        QTest::keyClick(editor, Qt::Key_R, Qt::ControlModifier);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    expectBool("safe rename coordinator updates definition file",
               tabs.getPlainTextFromOpenFile(defFile)
                   == QStringLiteral("module top;\n"
                                     "  parameter int P_DATA = 8;\n"
                                     "endmodule\n"),
               true);
    expectBool("safe rename coordinator updates use file",
               tabs.getPlainTextFromOpenFile(useFile)
                   == QStringLiteral("module top;\n"
                                     "  logic [P_DATA-1:0] data;\n"
                                     "endmodule\n"),
               true);

    if (previousSnapshot)
        SemanticIndex::getInstance()->setSnapshot(previousSnapshot);
    else
        SemanticIndex::getInstance()->clearSnapshot();
}

static void runSafeRenameCreateDefinitionRegression()
{
    QTemporaryDir dir;
    expectBool("safe rename create definition temp dir valid", dir.isValid(), true);
    if (!dir.isValid())
        return;

    const QString fileName =
        QDir(dir.path()).absoluteFilePath(QStringLiteral("create_def.sv"));
    const QString text =
        QStringLiteral("module top;\n"
                       "  typedef enum logic {IDLE} st_e;\n"
                       "  assign sink = cs;\n"
                       "endmodule\n");
    QFile file(fileName);
    expectBool("safe rename create definition write file",
               file.open(QIODevice::WriteOnly | QIODevice::Text),
               true);
    if (!file.isOpen())
        return;
    file.write(text.toUtf8());
    file.close();

    const SemanticSymbolRecord module =
        SemanticFixtureRecordBuilder(QStringLiteral("top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(201)
            .withRange(1, 1, 4, 10)
            .record();
    const SemanticSymbolRecord enumType =
        SemanticFixtureRecordBuilder(QStringLiteral("st_e"),
                                     SymbolTaxonomy::DeclarationKind::Enum)
            .withFile(fileName)
            .withLocalHandle(202)
            .withRange(2, 29, 2, 33)
            .inModule(QStringLiteral("top"))
            .record();
    QHash<QString, QString> contents;
    contents.insert(fileName, text);

    const auto previousSnapshot = SemanticIndex::getInstance()->snapshot();
    SemanticIndex::getInstance()->setSnapshot(
        snapshotFromRecords({module, enumType}, {}, {}, contents));

    QTabWidget tabsWidget;
    TabManager tabs(&tabsWidget);
    ModeManager modes(&tabsWidget, &tabsWidget);
    EditorCoordinator coordinator(&tabs, &modes);
    coordinator.connectSignals();

    expectBool("safe rename create definition opens file",
               tabs.openFileInTab(fileName),
               true);
    MyCodeEditor* editor = tabs.getCurrentEditor();
    expectBool("safe rename create definition has editor", editor != nullptr, true);
    if (editor) {
        const int csPos = text.indexOf(QStringLiteral("cs"));
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(csPos);
        cursor.setPosition(csPos + 2, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        editor->setFocus();

        acceptNextLineEditDialog(QStringLiteral("ns"));
        acceptNextMessageBoxYes();
        acceptNextMultilineDialog(QStringLiteral("st_e ns;"));
        QTest::keyClick(editor, Qt::Key_R, Qt::ControlModifier);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    expectBool("safe rename create definition inserts after type",
               tabs.getPlainTextFromOpenFile(fileName)
                   == QStringLiteral("module top;\n"
                                     "  typedef enum logic {IDLE} st_e;\n"
                                     "  st_e ns;\n"
                                     "  assign sink = ns;\n"
                                     "endmodule\n"),
               true);

    if (previousSnapshot)
        SemanticIndex::getInstance()->setSnapshot(previousSnapshot);
    else
        SemanticIndex::getInstance()->clearSnapshot();
}

static void runEditorColumnEditRegression()
{
    MyCodeEditor editor;
    editor.resize(480, 180);
    editor.setPlainText(QStringLiteral("abc\nabc\nabc\n"));
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextBlock firstBlock = editor.document()->findBlockByNumber(0);
    QTextBlock lastBlock = editor.document()->findBlockByNumber(2);
    QTextCursor startCursor(firstBlock);
    startCursor.setPosition(firstBlock.position() + 1);
    QTextCursor endCursor(lastBlock);
    endCursor.setPosition(lastBlock.position() + 1);
    const QPoint endPoint = editor.cursorRect(endCursor).center();
    const Qt::KeyboardModifiers columnModifiers =
        Qt::ShiftModifier | Qt::AltModifier;

    editor.setTextCursor(startCursor);
    QTest::mouseClick(editor.viewport(),
                      Qt::LeftButton,
                      columnModifiers,
                      endPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTest::keyClicks(&editor, "X");
    expectBool("editor column mode inserts on each selected line",
               editor.toPlainText() == QStringLiteral("aXbc\naXbc\naXbc\n"),
               true);

    QTest::keyClick(&editor, Qt::Key_Backspace);
    expectBool("editor column mode backspace edits each line",
               editor.toPlainText() == QStringLiteral("abc\nabc\nabc\n"),
               true);

    QTest::keyClicks(&editor, "Y");
    expectBool("editor column mode remains active after backspace",
               editor.toPlainText() == QStringLiteral("aYbc\naYbc\naYbc\n"),
               true);

    QTest::keyClick(&editor, Qt::Key_Left);
    QTest::keyClicks(&editor, "Z");
    expectBool("editor column mode plain arrow moves vertical cursor",
               editor.toPlainText()
                   == QStringLiteral("aZYbc\naZYbc\naZYbc\n"),
               true);

    QTest::keyClick(&editor, Qt::Key_Backspace);
    expectBool("editor column mode backspace remains multi-line after move",
               editor.toPlainText() == QStringLiteral("aYbc\naYbc\naYbc\n"),
               true);

    QTest::keyClick(&editor,
                    Qt::Key_Right,
                    columnModifiers);
    QTest::keyClick(&editor, Qt::Key_Right);
    QTest::keyClicks(&editor, "Q");
    expectBool("editor column mode adjusts then moves rectangular selection",
               editor.toPlainText() == QStringLiteral("aYQc\naYQc\naYQc\n"),
               true);

    const QPoint plainClickPoint =
        editor.cursorRect(editor.textCursor()).center();
    QTest::mouseClick(editor.viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      plainClickPoint);
    QTest::keyClicks(&editor, "Z");
    expectBool("editor plain click exits column mode",
               editor.toPlainText().count(QLatin1Char('Z')) == 1,
               true);

    MyCodeEditor virtualEditor;
    virtualEditor.resize(480, 180);
    virtualEditor.setPlainText(QStringLiteral("a\nab\nabc\n"));
    virtualEditor.show();
    virtualEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextBlock virtualFirstBlock =
        virtualEditor.document()->findBlockByNumber(0);
    QTextBlock virtualLastBlock =
        virtualEditor.document()->findBlockByNumber(2);
    QTextCursor virtualStartCursor(virtualFirstBlock);
    virtualStartCursor.setPosition(virtualFirstBlock.position() + 1);
    QTextCursor virtualEndCursor(virtualLastBlock);
    virtualEndCursor.setPosition(virtualLastBlock.position() + 1);
    const QPoint virtualEndPoint =
        virtualEditor.cursorRect(virtualEndCursor).center();

    virtualEditor.setTextCursor(virtualStartCursor);
    QTest::mouseClick(virtualEditor.viewport(),
                      Qt::LeftButton,
                      columnModifiers,
                      virtualEndPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    int broadColumnHighlights = 0;
    for (const QTextEdit::ExtraSelection& selection :
         virtualEditor.extraSelections()) {
        if (selection.format.background().style() != Qt::NoBrush
            && selection.format.background().color()
                   == QColor(37, 99, 235, 80)
            && selection.cursor.hasSelection()) {
            ++broadColumnHighlights;
        }
    }
    expectBool("editor zero-width column mode avoids broad highlight",
               broadColumnHighlights == 0,
               true);

    for (int i = 0; i < 4; ++i)
        QTest::keyClick(&virtualEditor, Qt::Key_Right);
    QTest::keyClicks(&virtualEditor, "X");
    expectBool("editor zero-width column mode pads virtual columns",
               virtualEditor.toPlainText()
                   == QStringLiteral("a    X\nab   X\nabc  X\n"),
               true);

    MyCodeEditor clipboardEditor;
    clipboardEditor.resize(480, 180);
    clipboardEditor.setPlainText(QStringLiteral("abc\nabc\nabc\n"));
    clipboardEditor.show();
    clipboardEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextBlock clipFirstBlock =
        clipboardEditor.document()->findBlockByNumber(0);
    QTextBlock clipLastBlock =
        clipboardEditor.document()->findBlockByNumber(2);
    QTextCursor clipStartCursor(clipFirstBlock);
    clipStartCursor.setPosition(clipFirstBlock.position() + 1);
    QTextCursor clipEndCursor(clipLastBlock);
    clipEndCursor.setPosition(clipLastBlock.position() + 2);
    clipboardEditor.setTextCursor(clipStartCursor);
    QTest::mouseClick(clipboardEditor.viewport(),
                      Qt::LeftButton,
                      columnModifiers,
                      clipboardEditor.cursorRect(clipEndCursor).center());
    QTest::keyClick(&clipboardEditor, Qt::Key_C, Qt::ControlModifier);
    expectBool("editor rectangular copy uses column rows",
               QApplication::clipboard()->text()
                   == QStringLiteral("b\nb\nb"),
               true);
    QTest::keyClick(&clipboardEditor, Qt::Key_X, Qt::ControlModifier);
    expectBool("editor rectangular cut edits each selected line",
               clipboardEditor.toPlainText()
                   == QStringLiteral("ac\nac\nac\n"),
               true);
    QApplication::clipboard()->setText(QStringLiteral("XY"));
    QTest::keyClick(&clipboardEditor, Qt::Key_V, Qt::ControlModifier);
    expectBool("editor rectangular paste repeats one clipboard row",
               clipboardEditor.toPlainText()
                   == QStringLiteral("aXYc\naXYc\naXYc\n"),
               true);
}

static void runEditorLineActionRegression()
{
    MyCodeEditor shortcutCommentEditor;
    shortcutCommentEditor.resize(480, 120);
    shortcutCommentEditor.setPlainText(QStringLiteral("logic a;\n"));
    shortcutCommentEditor.show();
    shortcutCommentEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::keyClick(&shortcutCommentEditor,
                    Qt::Key_Slash,
                    Qt::ControlModifier);
    expectBool("Ctrl+/ comments current line",
               shortcutCommentEditor.toPlainText()
                   == QStringLiteral("// logic a;\n"),
               true);
    QTest::keyClick(&shortcutCommentEditor,
                    Qt::Key_Slash,
                    Qt::ControlModifier | Qt::ShiftModifier);
    expectBool("Ctrl+Shift+/ uncomments current line",
               shortcutCommentEditor.toPlainText()
                   == QStringLiteral("logic a;\n"),
               true);

    MyCodeEditor selectionCommentEditor;
    selectionCommentEditor.resize(480, 150);
    selectionCommentEditor.setPlainText(QStringLiteral("aa\n  bb\ncc\n"));
    selectionCommentEditor.show();
    selectionCommentEditor.setFocus();
    QTextBlock selectFirst =
        selectionCommentEditor.document()->findBlockByNumber(0);
    QTextBlock selectThird =
        selectionCommentEditor.document()->findBlockByNumber(2);
    QTextCursor selectionCommentCursor(selectFirst);
    selectionCommentCursor.setPosition(selectFirst.position());
    selectionCommentCursor.setPosition(selectThird.position(),
                                       QTextCursor::KeepAnchor);
    selectionCommentEditor.setTextCursor(selectionCommentCursor);
    selectionCommentEditor.commentSelectionOrLine();
    expectBool("comment action comments selected touched lines",
               selectionCommentEditor.toPlainText()
                   == QStringLiteral("// aa\n  // bb\ncc\n"),
               true);
    expectBool("comment action excludes selection endpoint line",
               !selectionCommentEditor.toPlainText().contains(
                   QStringLiteral("// cc")),
               true);
    selectionCommentEditor.undo();
    expectBool("comment action undo restores selected lines",
               selectionCommentEditor.toPlainText()
                   == QStringLiteral("aa\n  bb\ncc\n"),
               true);

    selectionCommentEditor.setTextCursor(selectionCommentCursor);
    selectionCommentEditor.commentSelectionOrLine();
    selectionCommentEditor.uncommentSelectionOrLine();
    expectBool("uncomment action restores selected line comments",
               selectionCommentEditor.toPlainText()
                   == QStringLiteral("aa\n  bb\ncc\n"),
               true);

    MyCodeEditor inlineCommentEditor;
    inlineCommentEditor.resize(480, 120);
    inlineCommentEditor.setPlainText(
        QStringLiteral("assign a = b; // keep\n"));
    inlineCommentEditor.show();
    inlineCommentEditor.setFocus();
    inlineCommentEditor.uncommentSelectionOrLine();
    expectBool("uncomment action ignores trailing comments",
               inlineCommentEditor.toPlainText()
                   == QStringLiteral("assign a = b; // keep\n"),
               true);

    MyCodeEditor shortcutIndentEditor;
    shortcutIndentEditor.resize(480, 120);
    shortcutIndentEditor.setPlainText(QStringLiteral("logic a;\n"));
    shortcutIndentEditor.show();
    shortcutIndentEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::keyClick(&shortcutIndentEditor,
                    Qt::Key_BracketRight,
                    Qt::ControlModifier);
    expectBool("Ctrl+] indents current line",
               shortcutIndentEditor.toPlainText()
                   == QStringLiteral("    logic a;\n"),
               true);
    QTest::keyClick(&shortcutIndentEditor,
                    Qt::Key_BracketLeft,
                    Qt::ControlModifier);
    expectBool("Ctrl+[ unindents current line",
               shortcutIndentEditor.toPlainText()
                   == QStringLiteral("logic a;\n"),
               true);

    MyCodeEditor selectionIndentEditor;
    selectionIndentEditor.resize(480, 150);
    selectionIndentEditor.setPlainText(QStringLiteral("aa\n  bb\ncc\n"));
    selectionIndentEditor.show();
    selectionIndentEditor.setFocus();
    QTextBlock indentFirst =
        selectionIndentEditor.document()->findBlockByNumber(0);
    QTextBlock indentThird =
        selectionIndentEditor.document()->findBlockByNumber(2);
    QTextCursor selectionIndentCursor(indentFirst);
    selectionIndentCursor.setPosition(indentFirst.position());
    selectionIndentCursor.setPosition(indentThird.position(),
                                      QTextCursor::KeepAnchor);
    selectionIndentEditor.setTextCursor(selectionIndentCursor);
    selectionIndentEditor.indentSelectionOrLine();
    expectBool("indent action indents selected touched lines",
               selectionIndentEditor.toPlainText()
                   == QStringLiteral("    aa\n      bb\ncc\n"),
               true);
    expectBool("indent action excludes selection endpoint line",
               !selectionIndentEditor.toPlainText().startsWith(
                   QStringLiteral("    aa\n      bb\n    cc")),
               true);
    selectionIndentEditor.undo();
    expectBool("indent action undo restores selected lines",
               selectionIndentEditor.toPlainText()
                   == QStringLiteral("aa\n  bb\ncc\n"),
               true);

    selectionIndentEditor.setTextCursor(selectionIndentCursor);
    selectionIndentEditor.indentSelectionOrLine();
    selectionIndentEditor.unindentSelectionOrLine();
    expectBool("unindent action restores selected indentation",
               selectionIndentEditor.toPlainText()
                   == QStringLiteral("aa\n  bb\ncc\n"),
               true);

    MyCodeEditor tabUnindentEditor;
    tabUnindentEditor.resize(480, 120);
    tabUnindentEditor.setPlainText(QStringLiteral("\tlogic a;\n"));
    tabUnindentEditor.show();
    tabUnindentEditor.setFocus();
    tabUnindentEditor.unindentSelectionOrLine();
    expectBool("unindent action removes one leading tab",
               tabUnindentEditor.toPlainText()
                   == QStringLiteral("logic a;\n"),
               true);

    MyCodeEditor gotoLineEditor;
    gotoLineEditor.resize(480, 150);
    gotoLineEditor.setPlainText(QStringLiteral("line1\nline2\nline3\n"));
    gotoLineEditor.show();
    gotoLineEditor.setFocus();
    expectBool("goto line accepts valid line",
               gotoLineEditor.goToLineNumber(2),
               true);
    QTextCursor gotoCursor = gotoLineEditor.textCursor();
    expectBool("goto line moves to requested line start",
               gotoCursor.blockNumber() == 1
                   && gotoCursor.positionInBlock() == 0,
               true);
    const int validGotoPosition = gotoCursor.position();
    expectBool("goto line rejects out-of-range line",
               !gotoLineEditor.goToLineNumber(20)
                   && gotoLineEditor.textCursor().position()
                          == validGotoPosition,
               true);

    MyCodeEditor replaceNextEditor;
    replaceNextEditor.resize(480, 150);
    replaceNextEditor.setPlainText(QStringLiteral("aa bb aa\n"));
    replaceNextEditor.show();
    replaceNextEditor.setFocus();
    expectBool("replace next replaces first match",
               replaceNextEditor.replaceNextText(
                   QStringLiteral("aa"),
                   QStringLiteral("zz")),
               true);
    expectBool("replace next updates first occurrence",
               replaceNextEditor.toPlainText()
                   == QStringLiteral("zz bb aa\n"),
               true);
    expectBool("replace next advances to later match",
               replaceNextEditor.replaceNextText(
                   QStringLiteral("aa"),
                   QStringLiteral("yy")),
               true);
    expectBool("replace next updates later occurrence",
               replaceNextEditor.toPlainText()
                   == QStringLiteral("zz bb yy\n"),
               true);
    const int cursorBeforeMissingReplace =
        replaceNextEditor.textCursor().position();
    expectBool("replace next rejects missing text",
               !replaceNextEditor.replaceNextText(
                   QStringLiteral("missing"),
                   QStringLiteral("unused"))
                   && replaceNextEditor.textCursor().position()
                          == cursorBeforeMissingReplace,
               true);

    MyCodeEditor replaceSelectionEditor;
    replaceSelectionEditor.resize(480, 150);
    replaceSelectionEditor.setPlainText(QStringLiteral("aa bb aa\n"));
    QTextBlock replaceSelectionBlock =
        replaceSelectionEditor.document()->findBlockByNumber(0);
    QTextCursor replaceSelectionCursor(replaceSelectionBlock);
    replaceSelectionCursor.setPosition(replaceSelectionBlock.position() + 3);
    replaceSelectionCursor.setPosition(replaceSelectionBlock.position() + 5,
                                       QTextCursor::KeepAnchor);
    replaceSelectionEditor.setTextCursor(replaceSelectionCursor);
    expectBool("replace next uses matching current selection",
               replaceSelectionEditor.replaceNextText(
                   QStringLiteral("bb"),
                   QStringLiteral("cc")),
               true);
    expectBool("replace next updates current selection",
               replaceSelectionEditor.toPlainText()
                   == QStringLiteral("aa cc aa\n"),
               true);

    MyCodeEditor replaceAllEditor;
    replaceAllEditor.resize(480, 150);
    replaceAllEditor.setPlainText(QStringLiteral("foo FOO\nfoo\n"));
    const int replaceAllCount =
        replaceAllEditor.replaceAllText(QStringLiteral("foo"),
                                        QStringLiteral("bar"),
                                        false);
    expectBool("replace all is case-insensitive by default",
               replaceAllCount == 3
                   && replaceAllEditor.toPlainText()
                          == QStringLiteral("bar bar\nbar\n"),
               true);
    replaceAllEditor.undo();
    expectBool("replace all undo restores document",
               replaceAllEditor.toPlainText()
                   == QStringLiteral("foo FOO\nfoo\n"),
               true);

    MyCodeEditor duplicateEditor;
    duplicateEditor.resize(480, 180);
    duplicateEditor.setPlainText(QStringLiteral("one\ntwo\nthree\n"));
    duplicateEditor.show();
    duplicateEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextBlock twoBlock = duplicateEditor.document()->findBlockByNumber(1);
    QTextCursor duplicateCursor(twoBlock);
    duplicateCursor.setPosition(twoBlock.position() + 1);
    duplicateEditor.setTextCursor(duplicateCursor);
    QTest::keyClick(&duplicateEditor, Qt::Key_D, Qt::ControlModifier);
    QTextCursor afterDuplicate = duplicateEditor.textCursor();
    expectBool("Ctrl+D duplicates current line below",
               duplicateEditor.toPlainText()
                   == QStringLiteral("one\ntwo\ntwo\nthree\n"),
               true);
    expectBool("Ctrl+D preserves current line cursor column",
               afterDuplicate.blockNumber() == 2
                   && afterDuplicate.position() - afterDuplicate.block().position() == 1,
               true);

    MyCodeEditor finalLineEditor;
    finalLineEditor.resize(480, 120);
    finalLineEditor.setPlainText(QStringLiteral("last"));
    finalLineEditor.show();
    finalLineEditor.setFocus();
    QTextCursor finalCursor(finalLineEditor.document());
    finalCursor.setPosition(2);
    finalLineEditor.setTextCursor(finalCursor);
    QTest::keyClick(&finalLineEditor, Qt::Key_D, Qt::ControlModifier);
    expectBool("Ctrl+D duplicates final line onto new line",
               finalLineEditor.toPlainText() == QStringLiteral("last\nlast"),
               true);
    expectBool("Ctrl+D final line keeps cursor column",
               finalLineEditor.textCursor().blockNumber() == 1
                   && finalLineEditor.textCursor().position()
                          - finalLineEditor.textCursor().block().position()
                          == 2,
               true);

    MyCodeEditor selectionDuplicateEditor;
    selectionDuplicateEditor.resize(480, 120);
    selectionDuplicateEditor.setPlainText(QStringLiteral("abc def\n"));
    selectionDuplicateEditor.show();
    selectionDuplicateEditor.setFocus();
    QTextCursor selectionCursor(selectionDuplicateEditor.document());
    const int defStart =
        selectionDuplicateEditor.toPlainText().indexOf(QStringLiteral("def"));
    selectionCursor.setPosition(defStart);
    selectionCursor.setPosition(defStart + 3, QTextCursor::KeepAnchor);
    selectionDuplicateEditor.setTextCursor(selectionCursor);
    QTest::keyClick(&selectionDuplicateEditor,
                    Qt::Key_D,
                    Qt::ControlModifier);
    expectBool("Ctrl+D duplicates selection after selection",
               selectionDuplicateEditor.toPlainText()
                   == QStringLiteral("abc defdef\n"),
               true);
    expectBool("Ctrl+D selects duplicated selection",
               selectionDuplicateEditor.textCursor().selectedText()
                   == QStringLiteral("def"),
               true);

    MyCodeEditor moveEditor;
    moveEditor.resize(480, 180);
    moveEditor.setPlainText(QStringLiteral("aa\nbb\ncc\n"));
    moveEditor.show();
    moveEditor.setFocus();
    QTextBlock bbBlock = moveEditor.document()->findBlockByNumber(1);
    QTextCursor moveCursor(bbBlock);
    moveCursor.setPosition(bbBlock.position() + 1);
    moveEditor.setTextCursor(moveCursor);
    QTest::keyClick(&moveEditor, Qt::Key_Up, Qt::AltModifier);
    expectBool("Alt+Up moves current logical line up",
               moveEditor.toPlainText() == QStringLiteral("bb\naa\ncc\n"),
               true);
    expectBool("Alt+Up preserves cursor column",
               moveEditor.textCursor().blockNumber() == 0
                   && moveEditor.textCursor().position()
                          - moveEditor.textCursor().block().position()
                          == 1,
               true);
    QTest::keyClick(&moveEditor, Qt::Key_Down, Qt::AltModifier);
    expectBool("Alt+Down moves current logical line down",
               moveEditor.toPlainText() == QStringLiteral("aa\nbb\ncc\n"),
               true);

    MyCodeEditor touchedSelectionEditor;
    touchedSelectionEditor.resize(480, 180);
    touchedSelectionEditor.setPlainText(QStringLiteral("aa\nbb\ncc\n"));
    touchedSelectionEditor.show();
    touchedSelectionEditor.setFocus();
    QTextBlock touchedStart =
        touchedSelectionEditor.document()->findBlockByNumber(1);
    QTextBlock touchedNext =
        touchedSelectionEditor.document()->findBlockByNumber(2);
    QTextCursor touchedCursor(touchedStart);
    touchedCursor.setPosition(touchedStart.position());
    touchedCursor.setPosition(touchedNext.position(), QTextCursor::KeepAnchor);
    touchedSelectionEditor.setTextCursor(touchedCursor);
    QTest::keyClick(&touchedSelectionEditor,
                    Qt::Key_Up,
                    Qt::AltModifier);
    expectBool("Alt+Up excludes selection ending at next line column zero",
               touchedSelectionEditor.toPlainText()
                   == QStringLiteral("bb\naa\ncc\n"),
               true);

    MyCodeEditor columnMoveEditor;
    columnMoveEditor.resize(480, 180);
    columnMoveEditor.setPlainText(QStringLiteral("abc\nabc\nabc\n"));
    columnMoveEditor.show();
    columnMoveEditor.setFocus();
    QString columnStatus;
    QObject::connect(&columnMoveEditor,
                     &MyCodeEditor::editorStatusMessageRequested,
                     &columnMoveEditor,
                     [&](const QString& message) {
                         columnStatus = message;
                     });
    QTextBlock columnStart =
        columnMoveEditor.document()->findBlockByNumber(0);
    QTextBlock columnEnd =
        columnMoveEditor.document()->findBlockByNumber(2);
    QTextCursor columnStartCursor(columnStart);
    columnStartCursor.setPosition(columnStart.position() + 1);
    QTextCursor columnEndCursor(columnEnd);
    columnEndCursor.setPosition(columnEnd.position() + 1);
    const Qt::KeyboardModifiers columnModifiers =
        Qt::ShiftModifier | Qt::AltModifier;
    columnMoveEditor.setTextCursor(columnStartCursor);
    QTest::mouseClick(columnMoveEditor.viewport(),
                      Qt::LeftButton,
                      columnModifiers,
                      columnMoveEditor.cursorRect(columnEndCursor).center());
    QTest::keyClick(&columnMoveEditor, Qt::Key_Down, Qt::AltModifier);
    expectBool("Alt+Down is disabled while column selection is active",
               columnMoveEditor.toPlainText()
                       == QStringLiteral("abc\nabc\nabc\n")
                   && columnStatus.contains(QStringLiteral("Column selection")),
               true);
}

static void runEditorCtrlClickNavigationRegression()
{
    const QString path =
        QDir::current().absoluteFilePath(
            QStringLiteral("test_sv/huge_prj/vendor_ip_ctl.sv"));
    QFile file(path);
    expectBool("VENDOR ctrl-click fixture opens",
               file.open(QIODevice::ReadOnly | QIODevice::Text),
               true);
    if (!file.isOpen())
        return;

    const QString text = QString::fromUtf8(file.readAll());
    const QString symbol =
        QStringLiteral("CC_IB_MCPL_SEG_BUF_RAM_ADDR_WD");
    MyCodeEditor editor;
    editor.setDocumentFileName(path);
    editor.setPlainText(text);
    editor.resize(980, 520);
    editor.show();

    const QTextBlock useBlock =
        editor.document()->findBlockByNumber(1659);
    const QTextBlock definitionBlock =
        editor.document()->findBlockByNumber(339);
    expectBool("VENDOR ctrl-click use line exists",
               useBlock.isValid() && useBlock.text().contains(symbol),
               true);
    expectBool("VENDOR ctrl-click definition line exists",
               definitionBlock.isValid()
                   && definitionBlock.text().contains(symbol),
               true);
    if (!useBlock.isValid() || !definitionBlock.isValid())
        return;

    bool navigationRequested = false;
    QObject::connect(
        &editor,
        &MyCodeEditor::sourceNavigationRequested,
        &editor,
        [&](const EditorSourceNavigationTarget& target,
            const EditorSemanticContext&) {
            if (target.text != symbol)
                return;
            navigationRequested = true;
            const SourceLineNavigationTarget lineTarget =
                SourceNavigationService::getInstance()
                    ->lineNavigationTarget(340,
                                           definitionBlock.text().indexOf(symbol)
                                               + 1);
            editor.applyLineNavigationTarget(lineTarget);
        });

    const int useStart = useBlock.text().indexOf(symbol);
    const int usePosition = useBlock.position() + useStart;
    QTextCursor selected(editor.document());
    selected.setPosition(usePosition);
    selected.setPosition(usePosition + symbol.size(),
                         QTextCursor::KeepAnchor);
    editor.setTextCursor(selected);
    editor.centerCursor();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextCursor clickCursor(editor.document());
    clickCursor.setPosition(usePosition + 6);
    const QPoint clickPoint = editor.cursorRect(clickCursor).center();
    QTest::mousePress(editor.viewport(),
                      Qt::LeftButton,
                      Qt::ControlModifier,
                      clickPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QMouseEvent dragAfterJump(QEvent::MouseMove,
                              editor.cursorRect().center(),
                              editor.viewport()->mapToGlobal(
                                  editor.cursorRect().center()),
                              Qt::NoButton,
                              Qt::LeftButton,
                              Qt::ControlModifier);
    QCoreApplication::sendEvent(editor.viewport(), &dragAfterJump);
    QTest::mouseRelease(editor.viewport(),
                        Qt::LeftButton,
                        Qt::ControlModifier,
                        clickPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    expectBool("VENDOR ctrl-click requested parameter navigation",
               navigationRequested,
               true);
    expectBool("VENDOR ctrl-click lands on parameter definition",
               editor.textCursor().blockNumber() == 339,
               true);
    expectBool("VENDOR ctrl-click does not extend selection",
               !editor.textCursor().hasSelection(),
               true);
}

static void runSignalKernelGraphPopupInteractionRegression()
{
    QMainWindow host;
    host.resize(900, 560);
    SignalKernelGraphPanelCoordinator graph(&host);
    host.addDockWidget(Qt::BottomDockWidgetArea, graph.dock());
    host.show();
    graph.dock()->show();

    SignalKernelGraphReport report;
    report.found = true;
    report.kernelModuleName = QStringLiteral("graph_top");
    report.kernel.id = 10;
    report.kernel.role = SignalKernelGraphNodeRole::Kernel;
    report.kernel.displayName = QStringLiteral("kernel_sig");
    report.kernel.moduleDisplayName = QStringLiteral("graph_top");
    report.kernel.typeDisplayName = QStringLiteral("logic");
    report.kernel.detailDisplayName =
        QStringLiteral("assignment with enough detail to build a popup");
    report.kernel.navigateCodeLink =
        RtlInsightLink::fromFileLine(QStringLiteral("graph_top.sv"), 10, 3);

    SignalKernelGraphNode input;
    input.id = 11;
    input.role = SignalKernelGraphNodeRole::Input;
    input.inputLane = SignalKernelGraphInputLane::Data;
    input.displayName = QStringLiteral("data_in");
    input.moduleDisplayName = QStringLiteral("graph_top");
    input.typeDisplayName = QStringLiteral("logic");
    report.inputs.append(input);

    SignalKernelGraphNode output;
    output.id = 12;
    output.role = SignalKernelGraphNodeRole::Output;
    output.displayName = QStringLiteral("data_out");
    output.moduleDisplayName = QStringLiteral("graph_top");
    output.typeDisplayName = QStringLiteral("logic");
    report.outputs.append(output);
    report.edges.append({input.id, report.kernel.id, QString()});
    report.edges.append({report.kernel.id, output.id, QString()});

    bool graphNavigated = false;
    graph.setNavigationHandler(
        [&](const QString& fileName, int line, int column) {
            graphNavigated =
                fileName == QStringLiteral("graph_top.sv")
                && line == 10
                && column == 3;
        });
    graph.renderReport(report);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QGraphicsView* view = graph.view();
    expectBool("signal kernel graph view exists", view != nullptr, true);
    expectBool("signal kernel graph popup starts hidden",
               graph.hoverPopup && !graph.hoverPopup->isVisible(),
               true);
    if (!view || !graph.hoverPopup)
        return;

    const QPoint kernelPoint = view->mapFromScene(QPointF(0, 0));
    QGraphicsRectItem* kernelRectItem = nullptr;
    for (QGraphicsItem* item : view->items(kernelPoint)) {
        auto* rectItem = dynamic_cast<QGraphicsRectItem*>(item);
        if (!rectItem)
            continue;
        const QRectF rect = rectItem->rect();
        if (rect.width() > 120.0 && rect.width() < 260.0
            && rect.height() > 40.0 && rect.height() < 90.0) {
            kernelRectItem = rectItem;
            break;
        }
    }
    expectBool("signal kernel graph kernel item found",
               kernelRectItem != nullptr,
               true);
    if (!kernelRectItem)
        return;

    QTest::mouseMove(view->viewport(), kernelPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("signal kernel graph hover does not show popup",
               !graph.hoverPopup->isVisible(),
               true);

    QTest::mouseClick(view->viewport(),
                      Qt::RightButton,
                      Qt::NoModifier,
                      kernelPoint);
    expectBool("signal kernel graph right-click shows popup",
               waitUntil([&]() { return graph.hoverPopup->isVisible(); },
                         1000),
               true);

    const QRect nodeViewRect =
        view->mapFromScene(kernelRectItem->sceneBoundingRect())
            .boundingRect();
    const QRect nodeGlobalRect(
        view->viewport()->mapToGlobal(nodeViewRect.topLeft()),
        view->viewport()->mapToGlobal(nodeViewRect.bottomRight()));
    const QRect popupRect(graph.hoverPopup->pos(), graph.hoverPopup->size());
    expectBool("signal kernel graph popup avoids clicked node",
               !popupRect.intersects(nodeGlobalRect.normalized()),
               true);

    const QPoint outputPoint = view->mapFromScene(QPointF(430, 0));
    QTest::mouseClick(view->viewport(),
                      Qt::RightButton,
                      Qt::NoModifier,
                      outputPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    bool outputPopupVisible = false;
    const QString popupText = visibleEditorHoverPopupText(&outputPopupVisible);
    expectBool("signal kernel graph right-click switches popup",
               outputPopupVisible
                   && popupText.contains(QStringLiteral("Output: data_out")),
               true);

    const QPoint blankPoint(4, view->viewport()->height() - 4);
    QTest::mouseClick(view->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      blankPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("signal kernel graph blank left-click closes popup",
               !graph.hoverPopup->isVisible(),
               true);

    QTest::mouseClick(view->viewport(),
                      Qt::RightButton,
                      Qt::NoModifier,
                      kernelPoint);
    expectBool("signal kernel graph right-click reopens popup",
               waitUntil([&]() { return graph.hoverPopup->isVisible(); },
                         1000),
               true);
    QTest::mouseClick(view->viewport(),
                      Qt::RightButton,
                      Qt::NoModifier,
                      blankPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("signal kernel graph blank right-click closes popup",
               !graph.hoverPopup->isVisible(),
               true);

    QTest::mouseDClick(view->viewport(),
                       Qt::LeftButton,
                       Qt::NoModifier,
                       kernelPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("signal kernel graph double-click navigates",
               graphNavigated,
               true);
}

static void runEditorFormatterRegression()
{
    MyCodeEditor editor;
    editor.resize(360, 160);
    editor.setPlainText(QStringLiteral("module top;\n"
                                       "logic a;\n"
                                       "always_comb begin\n"
                                       "a = \"end\";\n"
                                       "end\n"
                                       "endmodule\n"));
    QString statusMessage;
    QObject::connect(&editor,
                     &MyCodeEditor::editorStatusMessageRequested,
                     &editor,
                     [&](const QString& message) {
                         statusMessage = message;
                     });
    editor.formatDocument();
    expectBool("editor formatter indents document",
               editor.toPlainText()
                   == QStringLiteral("module top;\n"
                                     "    logic a;\n"
                                     "    always_comb begin\n"
                                     "        a = \"end\";\n"
                                     "    end\n"
                                     "endmodule\n"),
               true);
    expectBool("editor formatter emits status",
               statusMessage.contains(QStringLiteral("Formatted document")),
               true);
    editor.undo();
    expectBool("editor formatter undo restores text",
               editor.toPlainText()
                   == QStringLiteral("module top;\n"
                                     "logic a;\n"
                                     "always_comb begin\n"
                                     "a = \"end\";\n"
                                     "end\n"
                                     "endmodule\n"),
               true);
    editor.setPlainText(QStringLiteral("module profile_demo;\n"
                                       "logic [7:0] data;\n"
                                       "logic valid;\n"
                                       "endmodule\n"));
    editor.setFormatterProfile(FormatterProfile::IndentOnly);
    editor.formatDocument();
    expectBool("editor formatter indent-only profile skips alignment",
               editor.toPlainText()
                   == QStringLiteral("module profile_demo;\n"
                                     "    logic [7:0] data;\n"
                                     "    logic valid;\n"
                                     "endmodule\n"),
               true);
    expectBool("editor formatter status names profile",
               statusMessage.contains(QStringLiteral("Indent Only")),
               true);
    expectBool("editor formatter stores selected profile",
               editor.formatterProfile() == FormatterProfile::IndentOnly,
               true);

    MyCodeEditor selectionEditor;
    selectionEditor.resize(360, 160);
    const QString selectionInput =
        QStringLiteral("module top;\n"
                       "    logic a; // flag\n"
                       "    logic [7:0] data; // byte\n"
                       "    always_comb begin\n"
                       "        data = '0;\n"
                       "    end\n"
                       "endmodule\n");
    selectionEditor.setPlainText(selectionInput);
    QString selectionStatusMessage;
    QObject::connect(&selectionEditor,
                     &MyCodeEditor::editorStatusMessageRequested,
                     &selectionEditor,
                     [&](const QString& message) {
                         selectionStatusMessage = message;
                     });
    QTextCursor selectionCursor = selectionEditor.textCursor();
    const int selectionStart = selectionInput.indexOf(QStringLiteral("logic a"));
    const int selectionEnd =
        selectionInput.indexOf(QStringLiteral("    always_comb"));
    selectionCursor.setPosition(selectionStart);
    selectionCursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    selectionEditor.setTextCursor(selectionCursor);
    selectionEditor.formatSelection();
    expectBool("editor formatter selection preserves base indent",
               selectionEditor.toPlainText()
                   == QStringLiteral("module top;\n"
                                     "    logic       a;     // flag\n"
                                     "    logic [7:0] data;  // byte\n"
                                     "    always_comb begin\n"
                                     "        data = '0;\n"
                                     "    end\n"
                                     "endmodule\n"),
               true);
    expectBool("editor formatter selection emits status",
               selectionStatusMessage.contains(QStringLiteral("Formatted selection")),
               true);
    selectionEditor.undo();
    expectBool("editor formatter selection undo restores text",
               selectionEditor.toPlainText() == selectionInput,
               true);
}

static void runEditorHoverPreviewRegression(const QString& workspacePath)
{
    const QString rtlTopPath =
        QDir(workspacePath).filePath(
            QStringLiteral("elec_phy_import/top/rtl_top.sv"));
    QFile rtlTopFile(rtlTopPath);
    expectBool("hover fixture opens",
               rtlTopFile.open(QIODevice::ReadOnly | QFile::Text),
               true);
    if (!rtlTopFile.isOpen())
        return;
    const QString rtlTopText = QTextStream(&rtlTopFile).readAll();
    rtlTopFile.close();

    const QStringList rtlLines = rtlTopText.split(QLatin1Char('\n'));
    int moduleLine = -1;
    int signalLine = -1;
    int useLine = -1;
    for (int i = 0; i < rtlLines.size(); ++i) {
        if (moduleLine < 0 && rtlLines.at(i).contains(QStringLiteral("module rtl_top")))
            moduleLine = i + 1;
        if (signalLine < 0 && rtlLines.at(i).contains(QStringLiteral("clk_main")))
            signalLine = i + 1;
        if (useLine < 0 && signalLine > 0 && i + 1 != signalLine
            && rtlLines.at(i).contains(QStringLiteral("clk_main"))
            && !rtlLines.at(i).trimmed().startsWith(QStringLiteral("//"))) {
            useLine = i + 1;
        }
    }
    expectBool("hover fixture has module line", moduleLine > 0, true);
    expectBool("hover fixture has signal line", signalLine > 0, true);
    expectBool("hover fixture has signal use line", useLine > 0, true);
    if (moduleLine <= 0 || signalLine <= 0 || useLine <= 0)
        return;

    const int signalColumn =
        rtlLines.at(signalLine - 1).indexOf(QStringLiteral("clk_main"));
    const int useColumn =
        rtlLines.at(useLine - 1).indexOf(QStringLiteral("clk_main"));
    expectBool("hover fixture has signal column", signalColumn >= 0, true);
    expectBool("hover fixture has signal use column", useColumn >= 0, true);
    if (signalColumn < 0 || useColumn < 0)
        return;

    const SemanticSymbolRecord moduleRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("rtl_top"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(rtlTopPath)
            .withLocalHandle(101)
            .withLine(moduleLine, 8)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    const SemanticSymbolRecord signalRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("clk_main"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(rtlTopPath)
            .withLocalHandle(102)
            .withLine(signalLine, signalColumn + 1)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .inModule(QStringLiteral("rtl_top"))
            .withType(QStringLiteral("logic"))
            .record();

    SemanticIndex hoverIndex;
    hoverIndex.setSnapshot(
        snapshotFromRecords(
            {moduleRecord, signalRecord},
            {},
            {},
            {{rtlTopPath, rtlTopText}}));

    EditorSemanticContext context;
    context.fileName = rtlTopPath;
    context.moduleName = QStringLiteral("rtl_top");
    context.lineText = rtlLines.at(signalLine - 1);
    context.lineUpToCursor = context.lineText.left(signalColumn + 1);
    context.cursorLine = signalLine;
    context.column = signalColumn + 1;
    context.cursorPosition = 0;

    SymbolHoverService hoverService(&hoverIndex);
    const SymbolHoverReport hover = hoverService.hoverForContext(context);
    expectBool("hover returns local signal",
               hover.available && hover.symbolName == QStringLiteral("clk_main"),
               true);
    expectBool("hover returns kind owner location",
               !hover.displayKind.isEmpty()
                   && hover.ownerName == QStringLiteral("rtl_top")
                   && hover.definitionFile == rtlTopPath
                   && hover.definitionLine == signalLine,
               true);

    DefinitionPreviewService previewService(&hoverIndex);
    const DefinitionPreviewReport preview =
        previewService.previewForContext(context);
    bool previewContainsDefinition = false;
    for (const QString& line : preview.codeLines)
        previewContainsDefinition |= line.contains(QStringLiteral("clk_main"));
    expectBool("preview resolves target file line",
               preview.targetResolved
                   && preview.targetFile == rtlTopPath
                   && preview.targetLine == signalLine,
               true);
    expectBool("preview includes definition snippet",
               preview.available && previewContainsDefinition,
               true);
    expectBool("preview highlights target line",
               preview.highlightedLine == signalLine,
               true);

    DocumentModel documents;
    MyCodeEditor dirtyEditor;
    documents.registerEditor(&dirtyEditor, rtlTopPath);
    QString dirtyText = rtlTopText;
    QStringList dirtyLines = dirtyText.split(QLatin1Char('\n'));
    dirtyLines[signalLine - 1].append(QStringLiteral(" // dirty preview marker"));
    dirtyText = dirtyLines.join(QLatin1Char('\n'));
    dirtyEditor.setPlainText(dirtyText);
    documents.refreshEditorState(&dirtyEditor);
    previewService.setDocumentModel(&documents);
    const DefinitionPreviewReport dirtyPreview =
        previewService.previewForContext(context);
    bool dirtyPreviewUsesOpenText = false;
    for (const QString& line : dirtyPreview.codeLines)
        dirtyPreviewUsesOpenText |= line.contains(QStringLiteral("dirty preview marker"));
    expectBool("preview prefers dirty open text",
               dirtyPreviewUsesOpenText,
               true);

    EditorSemanticContext commentContext = context;
    commentContext.lineText = QStringLiteral("// clk_main");
    commentContext.column = 4;
    expectBool("hover ignores comment identifier",
               !hoverService.hoverForContext(commentContext).available,
               true);

    EditorSemanticContext stringContext = context;
    stringContext.lineText = QStringLiteral("assign s = \"clk_main\";");
    stringContext.column = stringContext.lineText.indexOf(QStringLiteral("clk_main"));
    expectBool("hover ignores string identifier",
               !hoverService.hoverForContext(stringContext).available,
               true);

    SemanticIndex missingTextIndex;
    missingTextIndex.setSnapshot(snapshotFromRecords({signalRecord}));
    DefinitionPreviewService missingTextPreview(&missingTextIndex);
    const DefinitionPreviewReport missingPreview =
        missingTextPreview.previewForContext(context);
    expectBool("preview explains unavailable text",
               missingPreview.targetResolved
                   && !missingPreview.available
                   && missingPreview.unavailableReason.contains(
                       QStringLiteral("preview unavailable")),
               true);

    EditorHoverPopup popup;
    bool popupNavigationRequested = false;
    QString popupFile;
    int popupLine = -1;
    int popupColumn = -1;
    popup.setNavigationHandler(
        [&](const QString& fileName, int line, int column) {
            popupNavigationRequested = true;
            popupFile = fileName;
            popupLine = line;
            popupColumn = column;
        });
    DefinitionPreviewReport popupReport = missingPreview;
    popupReport.targetFile = rtlTopPath;
    popupReport.targetLine = signalLine;
    popupReport.targetColumn = signalColumn + 1;
    popup.showPreview(popupReport, QPoint(20, 20), QFont(QStringLiteral("Consolas"), 10));
    QTest::mouseDClick(&popup, Qt::LeftButton, Qt::NoModifier, QPoint(8, 8));
    expectBool("preview popup double-click requests navigation",
               popupNavigationRequested
                   && popupFile == rtlTopPath
                   && popupLine == signalLine
                   && popupColumn == signalColumn + 1,
               true);
    popup.closePopup();

    const auto previousGlobalSnapshot = SemanticIndex::getInstance()->snapshot();
    SemanticIndex::getInstance()->setSnapshot(hoverIndex.snapshot());
    SymbolHoverService::getInstance()->setSemanticIndex(SemanticIndex::getInstance());

    MyCodeEditor hoverEditor;
    hoverEditor.setDocumentFileName(rtlTopPath);
    hoverEditor.setLineWrapMode(QPlainTextEdit::NoWrap);
    hoverEditor.resize(800, 320);
    hoverEditor.setPlainText(rtlTopText);
    QTextBlock signalBlock =
        hoverEditor.document()->findBlockByNumber(signalLine - 1);
    QTextCursor hoverCursor(signalBlock);
    hoverCursor.setPosition(signalBlock.position() + signalColumn);
    hoverEditor.setTextCursor(hoverCursor);
    hoverEditor.show();
    hoverEditor.ensureCursorVisible();
    QApplication::processEvents();

    const QRect signalRect = hoverEditor.cursorRect(hoverCursor);
    const QPoint signalPoint(signalRect.left() + 3, signalRect.center().y());
    QTest::mouseDClick(hoverEditor.viewport(),
                       Qt::LeftButton,
                       Qt::NoModifier,
                       signalPoint);
    QApplication::processEvents();
    QTest::qWait(20);

    bool doubleClickHoverVisible = false;
    const QString doubleClickHoverText =
        visibleEditorHoverPopupText(&doubleClickHoverVisible);
    expectBool("editor double-click shows symbol hover",
               doubleClickHoverVisible
                   && doubleClickHoverText.contains(QStringLiteral("clk_main"))
                   && doubleClickHoverText.contains(QStringLiteral("owner: rtl_top")),
               true);
    QTest::mouseMove(hoverEditor.viewport(), signalPoint + QPoint(2, 0));
    QApplication::processEvents();
    QTest::qWait(20);
    bool movedDoubleClickHoverVisible = false;
    visibleEditorHoverPopupText(&movedDoubleClickHoverVisible);
    expectBool("editor double-click hover survives tiny mouse move",
               movedDoubleClickHoverVisible,
               true);
    hideEditorHoverPopups();

    MyCodeEditor numericEditor;
    numericEditor.setLineWrapMode(QPlainTextEdit::NoWrap);
    numericEditor.resize(500, 160);
    numericEditor.setPlainText(
        QStringLiteral("module radix_hover;\n"
                       "initial a <= 'haaaa;\n"
                       "endmodule\n"));
    numericEditor.show();
    QApplication::processEvents();
    const int numericPosition =
        numericEditor.toPlainText().indexOf(QStringLiteral("haaaa"));
    expectBool("numeric hover fixture has literal",
               numericPosition >= 0,
               true);
    if (numericPosition < 0) {
        hideEditorHoverPopups();
        if (previousGlobalSnapshot)
            SemanticIndex::getInstance()->setSnapshot(previousGlobalSnapshot);
        else
            SemanticIndex::getInstance()->clearSnapshot();
        return;
    }
    QTextCursor numericCursor(numericEditor.document());
    numericCursor.setPosition(numericPosition);
    numericEditor.setTextCursor(numericCursor);
    numericEditor.ensureCursorVisible();
    const QPoint numericPoint = numericEditor.cursorRect(numericCursor).center();
    QTest::mouseDClick(numericEditor.viewport(),
                       Qt::LeftButton,
                       Qt::NoModifier,
                       numericPoint);
    QApplication::processEvents();
    QTest::qWait(20);
    bool numericHoverVisible = false;
    const QString numericHoverText =
        visibleEditorHoverPopupText(&numericHoverVisible);
    expectBool("numeric double-click shows other base conversions",
               numericHoverVisible
                   && numericHoverText.contains(QStringLiteral("(B)"))
                   && numericHoverText.contains(QStringLiteral("(D)"))
                   && !numericHoverText.contains(QStringLiteral("(H)")),
               true);
    QTest::mouseMove(numericEditor.viewport(), numericPoint + QPoint(2, 0));
    QApplication::processEvents();
    QTest::qWait(20);
    bool movedNumericHoverVisible = false;
    visibleEditorHoverPopupText(&movedNumericHoverVisible);
    expectBool("numeric double-click hover survives tiny mouse move",
               movedNumericHoverVisible,
               true);
    hideEditorHoverPopups();

    if (previousGlobalSnapshot)
        SemanticIndex::getInstance()->setSnapshot(previousGlobalSnapshot);
    else
        SemanticIndex::getInstance()->clearSnapshot();

    const EditorSourceNavigationTarget navigationTarget =
        EditorSemanticContextService::getInstance()
            ->editorSourceNavigationTarget(context, 0);
    const EditorSourceNavigationClickState clickState =
        EditorSemanticContextService::getInstance()
            ->sourceNavigationClickState(navigationTarget);
    expectBool("ctrl click navigation remains definition action",
               clickState.action
                   == EditorSourceNavigationClickAction::NavigateToDefinition,
               true);
}

static void runActivityLogServiceRegression()
{
    ActivityLogService* service = ActivityLogService::getInstance();
    service->clear();
    QSignalSpy eventSpy(service, &ActivityLogService::eventAppended);
    QSignalSpy clearSpy(service, &ActivityLogService::cleared);

    service->append(QStringLiteral("Test"),
                    ActivityLogLevel::Info,
                    QStringLiteral("probe"),
                    12,
                    QStringLiteral("abc"));
    expectBool("activity log appends event",
               service->events().size() == 1 && eventSpy.count() == 1,
               true);
    const QString formatted =
        ActivityLogService::formatEvent(service->events().first());
    expectBool("activity log formats source",
               formatted.contains(QStringLiteral("[Test]")),
               true);
    expectBool("activity log formats duration",
               formatted.contains(QStringLiteral("12 ms")),
               true);
    service->clear();
    expectBool("activity log clears events",
               service->events().isEmpty() && clearSpy.count() >= 1,
               true);

    AnalysisProgressCoordinator coordinator(nullptr);
    QSignalSpy workspaceStatusSpy(
        &coordinator,
        &AnalysisProgressCoordinator::statusMessageRequested);
    WorkspaceAnalysisRequestTelemetry queuedTelemetry;
    queuedTelemetry.active = true;
    queuedTelemetry.pending = true;
    queuedTelemetry.activeAgeMs = 25;
    queuedTelemetry.pendingAgeMs = 5;
    queuedTelemetry.pendingUpdateCount = 2;
    coordinator.handleWorkspaceAnalysisRequestQueued(queuedTelemetry);
    WorkspaceAnalysisRequestTelemetry resolvedTelemetry;
    resolvedTelemetry.lastFinishedActiveAgeMs = 40;
    resolvedTelemetry.lastTakenPendingAgeMs = 12;
    resolvedTelemetry.lastTakenPendingUpdateCount = 2;
    coordinator.handleWorkspaceAnalysisRequestResolved(resolvedTelemetry);

    bool sawQueuedTelemetry = false;
    bool sawResolvedTelemetry = false;
    bool sawResolvedRestart = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawQueuedTelemetry = sawQueuedTelemetry
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Workspace request queued"))
                && event.message.contains(QStringLiteral("pending updates 2")));
        sawResolvedTelemetry = sawResolvedTelemetry
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Workspace request resolved"))
                && event.message.contains(QStringLiteral("pending wait 12 ms")));
        sawResolvedRestart = sawResolvedRestart
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("restarting latest request")));
    }
    expectBool("activity log records workspace queued telemetry",
               sawQueuedTelemetry,
               true);
    expectBool("activity log records workspace resolved telemetry",
               sawResolvedTelemetry,
               true);
    expectBool("activity log records workspace restart visibility",
               sawResolvedRestart,
               true);
    expectBool("workspace request telemetry emits status visibility",
               workspaceStatusSpy.count() >= 2
                   && workspaceStatusSpy.at(0).at(0).toString().contains(
                       QStringLiteral("latest request"))
                   && workspaceStatusSpy.at(1).at(0).toString().contains(
                       QStringLiteral("restarting")),
               true);
    service->clear();

    AnalysisProgressCoordinator planCoordinator(nullptr);
    QSignalSpy planStatusSpy(
        &planCoordinator,
        &AnalysisProgressCoordinator::statusMessageRequested);
    WorkspaceAnalysisPlan planSummary;
    planSummary.project.workspaceRoot = QStringLiteral("E:/workspace");
    planSummary.project.systemVerilogFiles = {
        QStringLiteral("E:/workspace/current.sv"),
        QStringLiteral("E:/workspace/dirty.sv"),
        QStringLiteral("E:/workspace/open.sv"),
        QStringLiteral("E:/workspace/background.sv")
    };
    planSummary.openFiles = {
        QStringLiteral("E:/workspace/dirty.sv"),
        QStringLiteral("E:/workspace/open.sv")
    };
    planSummary.protectedFiles = {
        QStringLiteral("E:/workspace/dirty.sv")
    };
    planSummary.currentFilePriorityFiles = {
        QStringLiteral("E:/workspace/current.sv")
    };
    planSummary.dirtyOpenPriorityFiles = {
        QStringLiteral("E:/workspace/dirty.sv")
    };
    planSummary.cleanOpenPriorityFiles = {
        QStringLiteral("E:/workspace/open.sv")
    };
    planSummary.backgroundFiles = {
        QStringLiteral("E:/workspace/background.sv")
    };
    planSummary.bandSummaries = {
        {QStringLiteral("current"), QStringLiteral("current"), 1, true, 1},
        {QStringLiteral("dirty-open"), QStringLiteral("dirty"), 1, true, 2},
        {QStringLiteral("open"), QStringLiteral("open"), 1, true, 3},
        {QStringLiteral("background"), QStringLiteral("background"), 1, false, 0}
    };
    planSummary.priorityFileCount = 3;
    planSummary.backgroundFileCount = 1;
    planSummary.currentFileInWorkspace = true;
    planCoordinator.handleWorkspaceAnalysisPlanPrepared(planSummary);
    bool sawPlanSummary = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawPlanSummary = sawPlanSummary
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Workspace plan prepared"))
                && event.message.contains(QStringLiteral("3 priority"))
                && event.message.contains(QStringLiteral("1 background"))
                && event.message.contains(QStringLiteral("1 protected"))
                && event.message.contains(
                    QStringLiteral("bands current 1, dirty 1, open 1, background 1")));
    }
    expectBool("activity log records workspace plan summary",
               sawPlanSummary,
               true);
    expectBool("workspace plan summary emits status",
               planStatusSpy.count() == 1
                   && planStatusSpy.at(0).at(0).toString().contains(
                       QStringLiteral("3 priority (1/1/1) / 4 files")),
               true);
    planCoordinator.handleWorkspaceSymbolProgress(
        1,
        4,
        QStringLiteral("E:/workspace/current.sv"));
    bool sawPlanBandProgress = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawPlanBandProgress = sawPlanBandProgress
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(
                    QStringLiteral("Symbol analysis progress: 25%"))
                && event.message.contains(QStringLiteral("[current]"))
                && event.message.contains(QStringLiteral("current.sv")));
    }
    expectBool("activity log records workspace plan progress band",
               sawPlanBandProgress,
               true);
    expectBool("workspace plan progress emits band status",
               planStatusSpy.count() == 2
                   && planStatusSpy.at(1).at(0).toString().contains(
                       QStringLiteral("[current]"))
                   && planStatusSpy.at(1).at(0).toString().contains(
                       QStringLiteral("current.sv")),
               true);
    service->clear();

    const QString bandReportCurrentFile =
        QDir::temp().absoluteFilePath(QStringLiteral("zs_activity_current.sv"));
    const QString bandReportBackgroundFile =
        QDir::temp().absoluteFilePath(QStringLiteral("zs_activity_background.sv"));
    QHash<QString, SemanticAnalysisBandMetadata> activityBandMetadata;
    SemanticAnalysisBandMetadata currentBand;
    currentBand.label = QStringLiteral("current");
    currentBand.displayName = QStringLiteral("current");
    currentBand.priority = true;
    currentBand.publicationCheckpoint = 1;
    activityBandMetadata.insert(bandReportCurrentFile, currentBand);
    SemanticAnalysisBandMetadata backgroundBand;
    backgroundBand.label = QStringLiteral("background");
    backgroundBand.displayName = QStringLiteral("background");
    backgroundBand.priority = false;
    activityBandMetadata.insert(bandReportBackgroundFile, backgroundBand);
    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    semanticIndex->setWorkspaceFileAnalysisBands(activityBandMetadata);
    const SemanticSymbolRecord currentBandModule =
        SemanticFixtureRecordBuilder(
            QStringLiteral("activity_current_top"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(bandReportCurrentFile)
            .withLocalHandle(91001)
            .record();
    const SemanticSymbolRecord currentBandSignal =
        SemanticFixtureRecordBuilder(
            QStringLiteral("activity_current_sig"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(bandReportCurrentFile)
            .withLocalHandle(91002)
            .inModule(QStringLiteral("activity_current_top"))
            .record();
    const SemanticSymbolRecord backgroundBandModule =
        SemanticFixtureRecordBuilder(
            QStringLiteral("activity_background_top"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(bandReportBackgroundFile)
            .withLocalHandle(91003)
            .record();
    semanticIndex->updateSymbolRecordsForFile(
        bandReportCurrentFile,
        {currentBandModule, currentBandSignal},
        QStringLiteral("module activity_current_top; logic activity_current_sig; endmodule\n"));
    semanticIndex->updateSymbolRecordsForFile(
        bandReportBackgroundFile,
        {backgroundBandModule},
        QStringLiteral("module activity_background_top; endmodule\n"));
    ProjectSnapshot bandReportProject;
    bandReportProject.workspaceRoot = QDir::tempPath();
    bandReportProject.systemVerilogFiles = {
        bandReportCurrentFile,
        bandReportBackgroundFile
    };
    QSignalSpy bandReportStatusSpy(
        &planCoordinator,
        &AnalysisProgressCoordinator::statusMessageRequested);
    planCoordinator.handleWorkspaceSymbolAnalysisFinished(
        bandReportProject,
        2,
        3);
    bool sawAnalysisBandReport = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawAnalysisBandReport = sawAnalysisBandReport
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Parsed 2 files, 3 symbols"))
                && event.message.contains(
                    QStringLiteral("bands current 2 symbols/1 file"))
                && event.message.contains(
                    QStringLiteral("background 1 symbol/1 file")));
    }
    expectBool("activity log records analysis band report summary",
               sawAnalysisBandReport,
               true);
    expectBool("workspace symbol finish keeps status visibility",
               bandReportStatusSpy.count() == 1
                   && bandReportStatusSpy.at(0).at(0).toString().contains(
                       QStringLiteral("2 files, 3 symbols")),
               true);
    semanticIndex->updateSymbolRecordsForFile(
        bandReportCurrentFile,
        {},
        QString());
    semanticIndex->updateSymbolRecordsForFile(
        bandReportBackgroundFile,
        {},
        QString());
    semanticIndex->clearWorkspaceFileAnalysisBands();
    service->clear();

    const QString diagnosticActivityCurrentFile =
        QDir::temp().absoluteFilePath(QStringLiteral("zs_activity_diag_current.sv"));
    const QString diagnosticActivityBackgroundFile =
        QDir::temp().absoluteFilePath(QStringLiteral("zs_activity_diag_background.sv"));
    SemanticDiagnostic diagnosticActivityWarning;
    diagnosticActivityWarning.fileName = diagnosticActivityCurrentFile;
    diagnosticActivityWarning.line = 3;
    diagnosticActivityWarning.column = 5;
    diagnosticActivityWarning.message = QStringLiteral("current warning");
    diagnosticActivityWarning.severity = SemanticDiagnostic::Warning;
    diagnosticActivityWarning.owner = SemanticDiagnostic::SemanticIndexOwner;
    SemanticDiagnostic diagnosticActivityInfo;
    diagnosticActivityInfo.fileName = diagnosticActivityCurrentFile;
    diagnosticActivityInfo.line = 4;
    diagnosticActivityInfo.column = 7;
    diagnosticActivityInfo.message = QStringLiteral("current info");
    diagnosticActivityInfo.severity = SemanticDiagnostic::Info;
    diagnosticActivityInfo.owner = SemanticDiagnostic::SemanticIndexOwner;
    SemanticDiagnostic diagnosticActivityError;
    diagnosticActivityError.fileName = diagnosticActivityBackgroundFile;
    diagnosticActivityError.line = 8;
    diagnosticActivityError.column = 2;
    diagnosticActivityError.message = QStringLiteral("background error");
    diagnosticActivityError.severity = SemanticDiagnostic::Error;
    diagnosticActivityError.owner = SemanticDiagnostic::SlangCompiler;
    semanticIndex->setSnapshot(snapshotFromRecords(
        {},
        {},
        {diagnosticActivityWarning,
         diagnosticActivityInfo,
         diagnosticActivityError}));
    QHash<QString, SemanticAnalysisBandMetadata> diagnosticActivityBands;
    diagnosticActivityBands.insert(diagnosticActivityCurrentFile, currentBand);
    diagnosticActivityBands.insert(diagnosticActivityBackgroundFile, backgroundBand);
    semanticIndex->setWorkspaceFileAnalysisBands(diagnosticActivityBands);

    QWidget diagnosticActivityParent;
    ProblemsPanelCoordinator diagnosticActivityProblems(&diagnosticActivityParent);
    diagnosticActivityProblems.setCurrentFileProvider([=]() {
        return diagnosticActivityCurrentFile;
    });
    diagnosticActivityProblems.setWorkspaceFilesProvider([=]() {
        return QStringList{
            diagnosticActivityCurrentFile,
            diagnosticActivityBackgroundFile,
        };
    });
    if (diagnosticActivityProblems.scopeCombo()) {
        const int allFilesIndex =
            diagnosticActivityProblems.scopeCombo()->findText(
                QStringLiteral("All Files"));
        if (allFilesIndex >= 0)
            diagnosticActivityProblems.scopeCombo()->setCurrentIndex(allFilesIndex);
    }
    diagnosticActivityProblems.update();
    bool sawBandCountLabels = false;
    if (diagnosticActivityProblems.bandCombo()) {
        const int allBandIndex =
            diagnosticActivityProblems.bandCombo()->findData(QString());
        const int currentIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("current"));
        const int backgroundIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("background"));
        sawBandCountLabels =
            allBandIndex >= 0
            && currentIndex >= 0
            && backgroundIndex >= 0
            && diagnosticActivityProblems.bandCombo()->itemText(allBandIndex)
                == QStringLiteral("All Bands (3)")
            && diagnosticActivityProblems.bandCombo()->itemText(currentIndex)
                == QStringLiteral("Current (2)")
            && diagnosticActivityProblems.bandCombo()->itemText(backgroundIndex)
                == QStringLiteral("Background (1)");
    }
    expectBool("problems band filter shows counts",
               sawBandCountLabels,
               true);
    bool sawBandSeverityTooltips = false;
    if (diagnosticActivityProblems.bandCombo()) {
        const int allBandIndex =
            diagnosticActivityProblems.bandCombo()->findData(QString());
        const int currentIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("current"));
        const int backgroundIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("background"));
        const QString allToolTip =
            allBandIndex >= 0
                ? diagnosticActivityProblems.bandCombo()
                      ->itemData(allBandIndex, Qt::ToolTipRole)
                      .toString()
                : QString();
        const QString currentToolTip =
            currentIndex >= 0
                ? diagnosticActivityProblems.bandCombo()
                      ->itemData(currentIndex, Qt::ToolTipRole)
                      .toString()
                : QString();
        const QString backgroundToolTip =
            backgroundIndex >= 0
                ? diagnosticActivityProblems.bandCombo()
                      ->itemData(backgroundIndex, Qt::ToolTipRole)
                      .toString()
                : QString();
        sawBandSeverityTooltips =
            allToolTip.contains(
                QStringLiteral("current 2 diagnostics"))
            && allToolTip.contains(QStringLiteral("1 warning"))
            && allToolTip.contains(QStringLiteral("1 info"))
            && allToolTip.contains(
                QStringLiteral("background 1 diagnostic"))
            && allToolTip.contains(QStringLiteral("1 error"))
            && currentToolTip
                == QStringLiteral(
                    "current 2 diagnostics (1 warning, 1 info)")
            && backgroundToolTip
                == QStringLiteral("background 1 diagnostic (1 error)");
    }
    expectBool("problems band filter shows severity tooltips",
               sawBandSeverityTooltips,
               true);
    QTreeWidget* diagnosticActivityTree = diagnosticActivityProblems.tree();
    expectBool("problems band column visible",
               diagnosticActivityTree
                   && diagnosticActivityTree->columnCount() == 6
                   && diagnosticActivityTree->headerItem()
                   && diagnosticActivityTree->headerItem()->text(5)
                       == QStringLiteral("Band"),
               true);
    bool sawCurrentBandRow = false;
    bool sawBackgroundBandRow = false;
    if (diagnosticActivityTree) {
        for (int i = 0; i < diagnosticActivityTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* group = diagnosticActivityTree->topLevelItem(i);
            if (!group)
                continue;
            sawCurrentBandRow = sawCurrentBandRow
                || group->text(5) == QStringLiteral("current");
            sawBackgroundBandRow = sawBackgroundBandRow
                || group->text(5) == QStringLiteral("background");
            for (int child = 0; child < group->childCount(); ++child) {
                QTreeWidgetItem* row = group->child(child);
                sawCurrentBandRow = sawCurrentBandRow
                    || (row && row->text(5) == QStringLiteral("current"));
                sawBackgroundBandRow = sawBackgroundBandRow
                    || (row && row->text(5) == QStringLiteral("background"));
            }
        }
    }
    expectBool("problems rows show diagnostic bands",
               sawCurrentBandRow && sawBackgroundBandRow,
               true);
    if (diagnosticActivityProblems.bandCombo()) {
        const int backgroundIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("background"));
        if (backgroundIndex >= 0)
            diagnosticActivityProblems.bandCombo()->setCurrentIndex(backgroundIndex);
    }
    bool sawOnlyBackgroundRows = false;
    if (diagnosticActivityTree) {
        int diagnosticRows = 0;
        bool allBackground = true;
        for (int i = 0; i < diagnosticActivityTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* group = diagnosticActivityTree->topLevelItem(i);
            if (!group)
                continue;
            if (group->childCount() == 0) {
                ++diagnosticRows;
                allBackground = allBackground
                    && group->text(5) == QStringLiteral("background");
                continue;
            }
            for (int child = 0; child < group->childCount(); ++child) {
                QTreeWidgetItem* row = group->child(child);
                if (!row)
                    continue;
                ++diagnosticRows;
                allBackground = allBackground
                    && row->text(5) == QStringLiteral("background");
            }
        }
        sawOnlyBackgroundRows = diagnosticRows == 1 && allBackground;
    }
    expectBool("problems band filter narrows diagnostics",
               sawOnlyBackgroundRows,
               true);
    bool keptUnfilteredBandCounts = false;
    if (diagnosticActivityProblems.bandCombo()) {
        const int currentIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("current"));
        const int backgroundIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("background"));
        keptUnfilteredBandCounts =
            currentIndex >= 0
            && backgroundIndex >= 0
            && diagnosticActivityProblems.bandCombo()->itemText(currentIndex)
                == QStringLiteral("Current (2)")
            && diagnosticActivityProblems.bandCombo()->itemText(backgroundIndex)
                == QStringLiteral("Background (1)");
    }
    expectBool("problems band counts ignore band filter",
               keptUnfilteredBandCounts,
               true);
    bool keptUnfilteredBandTooltips = false;
    if (diagnosticActivityProblems.bandCombo()) {
        const int currentIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("current"));
        const int backgroundIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("background"));
        const QString currentToolTip =
            currentIndex >= 0
                ? diagnosticActivityProblems.bandCombo()
                      ->itemData(currentIndex, Qt::ToolTipRole)
                      .toString()
                : QString();
        const QString backgroundToolTip =
            backgroundIndex >= 0
                ? diagnosticActivityProblems.bandCombo()
                      ->itemData(backgroundIndex, Qt::ToolTipRole)
                      .toString()
                : QString();
        keptUnfilteredBandTooltips =
            currentToolTip
                == QStringLiteral(
                    "current 2 diagnostics (1 warning, 1 info)")
            && backgroundToolTip
                == QStringLiteral("background 1 diagnostic (1 error)");
    }
    expectBool("problems band tooltips ignore band filter",
               keptUnfilteredBandTooltips,
               true);
    bool sawDiagnosticBandActivity = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawDiagnosticBandActivity = sawDiagnosticBandActivity
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(
                    QStringLiteral("Diagnostics visible: 3 diagnostics"))
                && event.message.contains(
                    QStringLiteral("diagnostic bands current 2 diagnostics"))
                && event.message.contains(QStringLiteral("1 warning"))
                && event.message.contains(QStringLiteral("1 info"))
                && event.message.contains(
                    QStringLiteral("background 1 diagnostic"))
                && event.message.contains(QStringLiteral("1 error")));
    }
    expectBool("activity log records diagnostic band summary",
               sawDiagnosticBandActivity,
               true);
    semanticIndex->clearSnapshot();
    semanticIndex->clearWorkspaceFileAnalysisBands();
    service->clear();

    AnalysisProgressCoordinator progressCoordinator(nullptr);
    progressCoordinator.handleWorkspaceSymbolProgress(
        1,
        4,
        QStringLiteral("E:/workspace/a.sv"));
    progressCoordinator.handleWorkspaceSymbolProgress(
        2,
        4,
        QStringLiteral("E:/workspace/b.sv"));
    progressCoordinator.handleWorkspaceSymbolProgress(
        4,
        4,
        QStringLiteral("E:/workspace/d.sv"));
    progressCoordinator.showWorkspaceRelationshipProgress(1, 4);
    progressCoordinator.showWorkspaceRelationshipProgress(3, 4);
    progressCoordinator.showWorkspaceRelationshipProgress(4, 4);
    bool sawSymbol25 = false;
    bool sawSymbol50 = false;
    bool sawSymbol100 = false;
    bool sawRelationship25 = false;
    bool sawRelationship75 = false;
    bool sawRelationship100 = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawSymbol25 = sawSymbol25
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Symbol analysis progress: 25%"))
                && event.message.contains(QStringLiteral("a.sv")));
        sawSymbol50 = sawSymbol50
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Symbol analysis progress: 50%"))
                && event.message.contains(QStringLiteral("b.sv")));
        sawSymbol100 = sawSymbol100
            || event.message.contains(QStringLiteral("Symbol analysis progress: 100%"));
        sawRelationship25 = sawRelationship25
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Relationship analysis progress: 25%")));
        sawRelationship75 = sawRelationship75
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Relationship analysis progress: 75%")));
        sawRelationship100 = sawRelationship100
            || event.message.contains(QStringLiteral("Relationship analysis progress: 100%"));
    }
    expectBool("activity log records symbol progress checkpoints",
               sawSymbol25 && sawSymbol50 && !sawSymbol100,
               true);
    expectBool("activity log records relationship progress checkpoints",
               sawRelationship25 && sawRelationship75 && !sawRelationship100,
               true);
    service->clear();

    QSignalSpy relationshipFinishedStatusSpy(
        &progressCoordinator,
        &AnalysisProgressCoordinator::statusMessageRequested);
    WorkspaceRelationshipAnalysisResult relationshipTelemetry;
    relationshipTelemetry.totalFiles = 4;
    relationshipTelemetry.processedFiles = 3;
    relationshipTelemetry.relationshipCount = 7;
    relationshipTelemetry.elapsedMs = 22;
    progressCoordinator.showRelationshipAnalysisFinished(relationshipTelemetry);
    bool sawRelationshipTelemetry = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawRelationshipTelemetry = sawRelationshipTelemetry
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(
                    QStringLiteral("Relationship analysis complete"))
                && event.message.contains(QStringLiteral("3/4 files"))
                && event.message.contains(QStringLiteral("7 relationships"))
                && event.message.contains(QStringLiteral("22 ms")));
    }
    expectBool("activity log records relationship result telemetry",
               sawRelationshipTelemetry,
               true);
    expectBool("relationship result telemetry emits status",
               relationshipFinishedStatusSpy.count() == 1
                   && relationshipFinishedStatusSpy.at(0).at(0).toString().contains(
                       QStringLiteral("3/4 files"))
                   && relationshipFinishedStatusSpy.at(0).at(0).toString().contains(
                       QStringLiteral("7 relationships")),
               true);
    service->clear();

    QSignalSpy workspaceCancelStatusSpy(
        &progressCoordinator,
        &AnalysisProgressCoordinator::statusMessageRequested);
    progressCoordinator.showWorkspaceRelationshipCancelled();
    bool sawWorkspaceRelationshipCancel = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawWorkspaceRelationshipCancel = sawWorkspaceRelationshipCancel
            || (event.source == QStringLiteral("Analyzer")
                && event.level == ActivityLogLevel::Warning
                && event.message.contains(
                    QStringLiteral("Workspace relationship analysis cancelled")));
    }
    expectBool("activity log records workspace relationship cancellation",
               sawWorkspaceRelationshipCancel,
               true);
    expectBool("workspace relationship cancellation emits status",
               workspaceCancelStatusSpy.count() == 1
                   && workspaceCancelStatusSpy.at(0).at(0).toString().contains(
                       QStringLiteral("Workspace relationship analysis cancelled")),
               true);
    service->clear();

    WorkspaceAnalysisRequestTelemetry symbolCancelTelemetry;
    symbolCancelTelemetry.lastFinishedActiveAgeMs = 44;
    symbolCancelTelemetry.lastTakenPendingAgeMs = 9;
    symbolCancelTelemetry.lastTakenPendingUpdateCount = 3;
    progressCoordinator.handleWorkspaceSymbolAnalysisCancelled(
        symbolCancelTelemetry);
    bool sawWorkspaceSymbolCancel = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawWorkspaceSymbolCancel = sawWorkspaceSymbolCancel
            || (event.source == QStringLiteral("Analyzer")
                && event.level == ActivityLogLevel::Warning
                && event.message.contains(
                    QStringLiteral("Workspace symbol analysis cancelled"))
                && event.message.contains(
                    QStringLiteral("pending updates 3")));
    }
    expectBool("activity log records workspace symbol cancellation",
               sawWorkspaceSymbolCancel,
               true);
    expectBool("workspace symbol cancellation emits status",
               workspaceCancelStatusSpy.count() == 2
                   && workspaceCancelStatusSpy.at(1).at(0).toString().contains(
                       QStringLiteral("Workspace symbol analysis cancelled"))
                   && progressCoordinator.isSymbolAnalysisCancelled(),
               true);
    service->clear();
}

static void runRtlInsightsOnDemandRegression()
{
    ActivityLogService* service = ActivityLogService::getInstance();
    service->clear();

    RtlInsightsPanelCoordinator panel(nullptr);
    panel.updateModuleContext(QStringLiteral("probe.sv"),
                              QStringLiteral("probe_module"),
                              QStringLiteral("probe_signal"));
    expectBool("RTL insights context update is passive",
               service->events().isEmpty(),
               true);
    expectBool("RTL insights action list rendered",
               panel.tree()
                   && panel.tree()->topLevelItemCount() > 0
                   && panel.tree()->topLevelItem(0)->text(0).contains(
                       QStringLiteral("Ready")),
               true);

    panel.showModuleBrief();
    bool sawModuleBriefLog = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawModuleBriefLog = sawModuleBriefLog
            || (event.source == QStringLiteral("RTL Insights")
                && event.message.contains(QStringLiteral("Module Brief")));
    }
    expectBool("RTL insights report logs on demand",
               sawModuleBriefLog,
               true);
    service->clear();
}

static bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (predicate())
            return true;
        QTest::qWait(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

static QString largestFile(const QStringList& files)
{
    QStringList sorted = files;
    std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) {
        return QFileInfo(a).size() > QFileInfo(b).size();
    });
    return sorted.isEmpty() ? QString() : sorted.first();
}

static QTextBlock findBlockContaining(QTextDocument* doc, const QString& needle)
{
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        if (block.text().contains(needle))
            return block;
    }
    return QTextBlock();
}

static QTreeWidgetItem* findItemByText(
    QTreeWidgetItem* item,
    const QString& text,
    int column = 0)
{
    if (!item)
        return nullptr;
    if (item->text(column) == text)
        return item;
    for (int i = 0; i < item->childCount(); ++i) {
        if (QTreeWidgetItem* found = findItemByText(item->child(i), text, column))
            return found;
    }
    return nullptr;
}

static QTreeWidgetItem* findItemByText(
    QTreeWidget* tree,
    const QString& text,
    int column = 0)
{
    if (!tree)
        return nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem* found =
                findItemByText(tree->topLevelItem(i), text, column))
            return found;
    }
    return nullptr;
}

static int navigableItemCount(QTreeWidgetItem* item)
{
    if (!item)
        return 0;
    int count = item->data(0, Qt::UserRole).toString().isEmpty() ? 0 : 1;
    for (int i = 0; i < item->childCount(); ++i)
        count += navigableItemCount(item->child(i));
    return count;
}

static int navigableItemCount(QTreeWidget* tree)
{
    if (!tree)
        return 0;
    int count = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        count += navigableItemCount(tree->topLevelItem(i));
    return count;
}

static bool hasNavigableFile(QTreeWidgetItem* item, const QString& fileName)
{
    if (!item)
        return false;
    const QString itemFileName = item->data(0, Qt::UserRole).toString();
    if (!itemFileName.isEmpty()
        && QFileInfo(itemFileName).absoluteFilePath() == QFileInfo(fileName).absoluteFilePath()) {
        return true;
    }
    for (int i = 0; i < item->childCount(); ++i) {
        if (hasNavigableFile(item->child(i), fileName))
            return true;
    }
    return false;
}

static bool hasNavigableFile(QTreeWidget* tree, const QString& fileName)
{
    if (!tree)
        return false;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (hasNavigableFile(tree->topLevelItem(i), fileName))
            return true;
    }
    return false;
}

static QTreeWidgetItem* firstNavigableItem(QTreeWidgetItem* item)
{
    if (!item)
        return nullptr;
    if (!item->data(0, Qt::UserRole).toString().isEmpty())
        return item;
    for (int i = 0; i < item->childCount(); ++i) {
        if (QTreeWidgetItem* found = firstNavigableItem(item->child(i)))
            return found;
    }
    return nullptr;
}

static QTreeWidgetItem* firstNavigableItem(QTreeWidget* tree)
{
    if (!tree)
        return nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem* found = firstNavigableItem(tree->topLevelItem(i)))
            return found;
    }
    return nullptr;
}

static void collectNavigableItems(QTreeWidgetItem* item, QList<QTreeWidgetItem*>& out)
{
    if (!item)
        return;
    if (!item->data(0, Qt::UserRole).toString().isEmpty())
        out.append(item);
    for (int i = 0; i < item->childCount(); ++i)
        collectNavigableItems(item->child(i), out);
}

static QList<QTreeWidgetItem*> navigableItems(QTreeWidget* tree)
{
    QList<QTreeWidgetItem*> out;
    if (!tree)
        return out;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        collectNavigableItems(tree->topLevelItem(i), out);
    return out;
}

static QTreeWidget* problemsTree(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->problemsPanelCoordinator()
        ? window.semanticDocks->problemsPanelCoordinator()->tree()
        : nullptr;
}

static QComboBox* problemsScopeCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->problemsPanelCoordinator()
        ? window.semanticDocks->problemsPanelCoordinator()->scopeCombo()
        : nullptr;
}

static QComboBox* problemsBandCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->problemsPanelCoordinator()
        ? window.semanticDocks->problemsPanelCoordinator()->bandCombo()
        : nullptr;
}

static QTreeWidget* referencesTree(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->referencesPanelCoordinator()
        ? window.semanticDocks->referencesPanelCoordinator()->tree()
        : nullptr;
}

static QComboBox* referenceScopeCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->referencesPanelCoordinator()
        ? window.semanticDocks->referencesPanelCoordinator()->scopeCombo()
        : nullptr;
}

static QComboBox* referenceTypeCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->referencesPanelCoordinator()
        ? window.semanticDocks->referencesPanelCoordinator()->typeCombo()
        : nullptr;
}

static QTreeWidget* relationshipsTree(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->relationshipsPanelCoordinator()
        ? window.semanticDocks->relationshipsPanelCoordinator()->tree()
        : nullptr;
}

static QTreeWidget* rtlInsightsTree(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->rtlInsightsPanelCoordinator()
        ? window.semanticDocks->rtlInsightsPanelCoordinator()->tree()
        : nullptr;
}

static QTreeWidget* wavePreviewTree(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->wavePreviewPanelCoordinator()
        ? window.semanticDocks->wavePreviewPanelCoordinator()->tree()
        : nullptr;
}

static QWidget* wavePreviewCanvas(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->wavePreviewPanelCoordinator()
        ? window.semanticDocks->wavePreviewPanelCoordinator()->canvas()
        : nullptr;
}

static QLabel* wavePreviewSummaryLabel(MainWindow& window)
{
    QDockWidget* dock =
        window.findChild<QDockWidget*>(QStringLiteral("wavePreviewDock"));
    return dock ? dock->findChild<QLabel*>(QStringLiteral("wavePreviewSummary"))
                : nullptr;
}

static bool renderedWidgetHasColorVariation(QWidget* widget)
{
    if (!widget)
        return false;
    if (widget->width() < 120 || widget->height() < 80)
        widget->resize(420, 150);

    QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget->render(&image);
    const QRgb firstPixel = image.pixel(0, 0);
    int differentPixels = 0;
    for (int y = 0; y < image.height(); y += 5) {
        for (int x = 0; x < image.width(); x += 5) {
            if (image.pixel(x, y) != firstPixel)
                ++differentPixels;
            if (differentPixels > 20)
                return true;
        }
    }
    return false;
}

static QComboBox* relationshipViewCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->relationshipsPanelCoordinator()
        ? window.semanticDocks->relationshipsPanelCoordinator()->viewCombo()
        : nullptr;
}

static QComboBox* relationshipDirectionCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->relationshipsPanelCoordinator()
        ? window.semanticDocks->relationshipsPanelCoordinator()->directionCombo()
        : nullptr;
}

static QComboBox* relationshipTypeCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->relationshipsPanelCoordinator()
        ? window.semanticDocks->relationshipsPanelCoordinator()->typeCombo()
        : nullptr;
}

static QComboBox* relationshipDepthCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->relationshipsPanelCoordinator()
        ? window.semanticDocks->relationshipsPanelCoordinator()->depthCombo()
        : nullptr;
}

static SemanticPanelRefreshCoordinator* semanticPanelRefresh(MainWindow& window)
{
    return window.semanticDocks ? window.semanticDocks->refreshCoordinator() : nullptr;
}

static void drainRelationshipWork(MainWindow& window)
{
    SmartRelationshipBuilder* builder = window.semanticRuntime
        ? window.semanticRuntime->relationshipBuilder()
        : nullptr;
    if (builder)
        builder->cancelAnalysis();
    if (window.analysisScheduler) {
        window.analysisScheduler->cancelAllScheduledRelationshipAnalyses();
        window.analysisScheduler->cancelRelationshipAnalysis();
        window.analysisScheduler->cancelWorkspaceRelationshipAnalysis();
    }
}

static void runReferenceDockRegression(MainWindow& window, const QString& fixturePath)
{
    printf("\n-- reference dock regression --\n");

    const SemanticSymbolRecord referenced =
        SemanticFixtureRecordBuilder(QStringLiteral("target_ref"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(fixturePath)
            .withLocalHandle(9001)
            .withRange(3, 9, 3, 18)
            .withTextSpan(0, 10)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .inModule(QStringLiteral("ref_top"))
            .record();
    const SemanticSymbolRecord referencing =
        SemanticFixtureRecordBuilder(QStringLiteral("source_ref"),
                                     SymbolTaxonomy::DeclarationKind::Process)
            .withFile(fixturePath)
            .withLocalHandle(9002)
            .withRange(8, 3, 8, 20)
            .withTextSpan(0, 10)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Assign)
            .withUsageRole(SymbolTaxonomy::SymbolUsageRole::Process)
            .inModule(QStringLiteral("ref_top"))
            .record();
    const SemanticSymbolRecord externalReferencing =
        SemanticFixtureRecordBuilder(QStringLiteral("external_ref"),
                                     SymbolTaxonomy::DeclarationKind::Process)
            .withFile(fixturePath + QStringLiteral(".refs.sv"))
            .withLocalHandle(9004)
            .withRange(4, 5, 4, 22)
            .withTextSpan(0, 12)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Assign)
            .withUsageRole(SymbolTaxonomy::SymbolUsageRole::Process)
            .inModule(QStringLiteral("ref_external"))
            .record();
    const SemanticSymbolRecord target =
        SemanticFixtureRecordBuilder(QStringLiteral("target_sink"),
                                     SymbolTaxonomy::DeclarationKind::Function)
            .withFile(fixturePath)
            .withLocalHandle(9003)
            .withRange(12, 12, 12, 22)
            .withTextSpan(0, 11)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Function)
            .inModule(QStringLiteral("ref_top"))
            .record();

    const SemanticRelationship incomingRelationship =
        semanticFixtureRelationship(referencing,
                                    referenced,
                                    SymbolRelationshipEngine::REFERENCES);
    const SemanticRelationship externalIncomingRelationship =
        semanticFixtureRelationship(externalReferencing,
                                    referenced,
                                    SymbolRelationshipEngine::READS_FROM);
    const SemanticRelationship outgoingRelationship =
        semanticFixtureRelationship(referenced,
                                    target,
                                    SymbolRelationshipEngine::CALLS);

    const QList<SemanticSymbolRecord> referenceRecords{
        referenced,
        referencing,
        externalReferencing,
        target,
    };
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        fixturePath,
        QList<SemanticSymbolRecord>{referenced, referencing, target},
        QString());
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        externalReferencing.location.fileName,
        QList<SemanticSymbolRecord>{externalReferencing},
        QString());
    SemanticIndex::getInstance()->setSnapshot(
        snapshotFromRecords(
            referenceRecords,
            QList<SemanticRelationship>{incomingRelationship,
                                        externalIncomingRelationship,
                                        outgoingRelationship}));

    semanticPanelRefresh(window)->showReferencesForSymbol(QStringLiteral("target_ref"),
                                                          fixturePath,
                                                          QStringLiteral("ref_top"));

    expectBool("references tree exists", referencesTree(window) != nullptr, true);
    expectBool("reference results rendered",
               navigableItemCount(referencesTree(window)) == 2,
               true);
    expectBool("reference scope filter exists",
               referenceScopeCombo(window) != nullptr,
               true);
    expectBool("reference type filter exists",
               referenceTypeCombo(window) != nullptr,
               true);
    if (referenceScopeCombo(window)) {
        referenceScopeCombo(window)->setCurrentIndex(
            referenceScopeCombo(window)->findText(QStringLiteral("Workspace Files")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("reference workspace scope hides non-workspace files",
                   navigableItemCount(referencesTree(window)) == 0,
                   true);
    }
    if (referenceScopeCombo(window)) {
        referenceScopeCombo(window)->setCurrentIndex(
            referenceScopeCombo(window)->findText(QStringLiteral("Current File")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("reference scope narrows to current file",
                   navigableItemCount(referencesTree(window)) == 1,
                   true);
    }
    if (referencesTree(window) && navigableItemCount(referencesTree(window)) == 1) {
        QTreeWidgetItem* item = firstNavigableItem(referencesTree(window));
        expectBool("reference row uses source symbol",
                   item && item->text(0) == QStringLiteral("source_ref"),
                   true);
        expectBool("reference row stores source line",
                   item && item->data(0, Qt::UserRole + 1).toInt()
                       == referencing.location.startLine,
                   true);
    }
    if (referenceScopeCombo(window) && referenceTypeCombo(window)) {
        referenceScopeCombo(window)->setCurrentIndex(
            referenceScopeCombo(window)->findText(QStringLiteral("All Files")));
        referenceTypeCombo(window)->setCurrentIndex(
            referenceTypeCombo(window)->findText(QStringLiteral("Reads From")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("reference type filter narrows results",
                   navigableItemCount(referencesTree(window)) == 1,
                   true);
        QTreeWidgetItem* item = firstNavigableItem(referencesTree(window));
        expectBool("reference type filter keeps external source",
                   item && item->text(0) == QStringLiteral("external_ref"),
                   true);
        referenceTypeCombo(window)->setCurrentIndex(
            referenceTypeCombo(window)->findText(QStringLiteral("All Types")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    MyCodeEditor shortcutEditor;
    shortcutEditor.setPlainText(
        "module ref_top;\n"
        "  logic target_ref;\n"
        "endmodule\n");
    DocumentModel shortcutDocumentModel;
    shortcutDocumentModel.registerEditor(&shortcutEditor, fixturePath);
    const int targetOffset = shortcutEditor.toPlainText().indexOf(QStringLiteral("target_ref")) + 2;
    QTextCursor shortcutCursor(shortcutEditor.document());
    shortcutCursor.setPosition(targetOffset);
    shortcutEditor.setTextCursor(shortcutCursor);
    int sourceActionCount = 0;
    SourceSymbolAction lastSourceAction = SourceSymbolAction::FindReferences;
    EditorSemanticContext lastSourceActionContext;
    QObject::connect(&shortcutEditor,
                     &MyCodeEditor::sourceSymbolActionRequested,
                     &shortcutEditor,
                     [&](SourceSymbolAction action,
                         const EditorSemanticContext& context) {
                         ++sourceActionCount;
                         lastSourceAction = action;
                         lastSourceActionContext = context;
                     });
    QTest::keyClick(&shortcutEditor, Qt::Key_F12, Qt::ShiftModifier);
    expectBool("find references shortcut emits request",
               sourceActionCount == 1
                   && lastSourceAction == SourceSymbolAction::FindReferences,
               true);
    expectBool("find references shortcut emits symbol",
               lastSourceActionContext.lineText.contains(QStringLiteral("target_ref")),
               true);
    QTest::keyClick(&shortcutEditor, Qt::Key_R,
                    Qt::ControlModifier | Qt::ShiftModifier);
    expectBool("show relationships shortcut emits request",
               sourceActionCount == 2
                   && lastSourceAction == SourceSymbolAction::ShowRelationships,
               true);
    expectBool("show relationships shortcut emits symbol",
               lastSourceActionContext.lineText.contains(QStringLiteral("target_ref"))
                   && lastSourceActionContext.fileName == fixturePath
                   && lastSourceActionContext.moduleName == QStringLiteral("ref_top"),
               true);

    QString emittedAlternateCommand;
    QObject::connect(&shortcutEditor,
                     &MyCodeEditor::alternateCommandRequested,
                     &shortcutEditor,
                     [&](const QString& command) {
                         emittedAlternateCommand = command;
                     });
    shortcutEditor.executeAlternateModeCommand(QStringLiteral("save"));
    expectBool("alternate command emits command request",
               emittedAlternateCommand == QStringLiteral("save"),
               true);

    shortcutEditor.clear();
    FileCommandCoordinator editorCommandCoordinator(nullptr, nullptr);
    editorCommandCoordinator.executeAlternateCommandText(
        &shortcutEditor, QStringLiteral("comment"));
    expectBool("file coordinator executes command text",
               shortcutEditor.toPlainText() == QStringLiteral("// "),
               true);

    editorCommandCoordinator.executeAlternateCommandText(
        &shortcutEditor, QStringLiteral("uncomment"));
    expectBool("file coordinator executes uncomment command text",
               shortcutEditor.toPlainText().isEmpty(),
               true);

    shortcutEditor.clear();
    editorCommandCoordinator.executeAlternateCommand(&shortcutEditor,
                                                    AlternateCommandAction::Comment);
    expectBool("file coordinator executes editor command",
               shortcutEditor.toPlainText() == QStringLiteral("// "),
               true);

    editorCommandCoordinator.executeAlternateCommand(
        &shortcutEditor,
        AlternateCommandAction::Uncomment);
    expectBool("file coordinator executes uncomment editor command",
               shortcutEditor.toPlainText().isEmpty(),
               true);

    shortcutEditor.setPlainText(QStringLiteral("logic a;\n"));
    editorCommandCoordinator.executeAlternateCommandText(
        &shortcutEditor, QStringLiteral("indent"));
    expectBool("file coordinator executes indent command text",
               shortcutEditor.toPlainText()
                   == QStringLiteral("    logic a;\n"),
               true);

    editorCommandCoordinator.executeAlternateCommandText(
        &shortcutEditor, QStringLiteral("unindent"));
    expectBool("file coordinator executes unindent command text",
               shortcutEditor.toPlainText()
                   == QStringLiteral("logic a;\n"),
               true);

    editorCommandCoordinator.executeAlternateCommand(
        &shortcutEditor,
        AlternateCommandAction::Indent);
    expectBool("file coordinator executes indent editor command",
               shortcutEditor.toPlainText()
                   == QStringLiteral("    logic a;\n"),
               true);

    editorCommandCoordinator.executeAlternateCommand(
        &shortcutEditor,
        AlternateCommandAction::Unindent);
    expectBool("file coordinator executes unindent editor command",
               shortcutEditor.toPlainText()
                   == QStringLiteral("logic a;\n"),
               true);

    semanticPanelRefresh(window)->showRelationshipsForSymbol(QStringLiteral("target_ref"),
                                                             fixturePath,
                                                             QStringLiteral("ref_top"));
    expectBool("relationships tree exists", relationshipsTree(window) != nullptr, true);
    expectBool("relationship results rendered",
               navigableItemCount(relationshipsTree(window)) == 3,
               true);
    if (relationshipsTree(window) && navigableItemCount(relationshipsTree(window)) == 3) {
        bool sawIncoming = false;
        bool sawOutgoing = false;
        bool sawExternal = false;
        bool sawExplanation = false;
        const QList<QTreeWidgetItem*> items = navigableItems(relationshipsTree(window));
        for (QTreeWidgetItem* item : items) {
            sawIncoming = sawIncoming
                || (item->text(0) == QStringLiteral("Incoming")
                    && item->text(1) == QStringLiteral("source_ref"));
            sawOutgoing = sawOutgoing
                || (item->text(0) == QStringLiteral("Outgoing")
                    && item->text(1) == QStringLiteral("target_sink"));
            sawExternal = sawExternal
                || (item->text(0) == QStringLiteral("Incoming")
                    && item->text(1) == QStringLiteral("external_ref"));
            sawExplanation = sawExplanation
                || item->text(5) == QStringLiteral("source_ref references target_ref");
        }
        expectBool("incoming relationship row rendered", sawIncoming, true);
        expectBool("outgoing relationship row rendered", sawOutgoing, true);
        expectBool("external relationship row rendered", sawExternal, true);
        expectBool("relationship explanation column rendered", sawExplanation, true);
    }

    expectBool("relationship direction filter exists",
               relationshipDirectionCombo(window) != nullptr,
               true);
    expectBool("relationship type filter exists",
               relationshipTypeCombo(window) != nullptr,
               true);
    expectBool("relationship view filter exists",
               relationshipViewCombo(window) != nullptr,
               true);
    expectBool("relationship depth filter exists",
               relationshipDepthCombo(window) != nullptr,
               true);
    if (relationshipDirectionCombo(window) && relationshipTypeCombo(window)) {
        relationshipDirectionCombo(window)->setCurrentIndex(
            relationshipDirectionCombo(window)->findText(QStringLiteral("Outgoing")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("outgoing filter narrows relationships",
                   navigableItemCount(relationshipsTree(window)) == 1,
                   true);
        if (relationshipsTree(window) && navigableItemCount(relationshipsTree(window)) == 1) {
            QTreeWidgetItem* item = firstNavigableItem(relationshipsTree(window));
            expectBool("outgoing filter keeps target",
                       item && item->text(1) == QStringLiteral("target_sink"),
                       true);
        }

        relationshipDirectionCombo(window)->setCurrentIndex(
            relationshipDirectionCombo(window)->findText(QStringLiteral("All Directions")));
        relationshipTypeCombo(window)->setCurrentIndex(
            relationshipTypeCombo(window)->findText(QStringLiteral("References")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("type filter narrows relationships",
                   navigableItemCount(relationshipsTree(window)) == 1,
                   true);
        if (relationshipsTree(window) && navigableItemCount(relationshipsTree(window)) == 1) {
            QTreeWidgetItem* item = firstNavigableItem(relationshipsTree(window));
            expectBool("type filter keeps incoming source",
                       item && item->text(1) == QStringLiteral("source_ref"),
                       true);
        }
    }
    if (relationshipViewCombo(window) && relationshipTypeCombo(window)
        && relationshipDepthCombo(window)) {
        relationshipTypeCombo(window)->setCurrentIndex(
            relationshipTypeCombo(window)->findText(QStringLiteral("Calls")));
        relationshipDepthCombo(window)->setCurrentIndex(
            relationshipDepthCombo(window)->findText(QStringLiteral("Depth 2")));
        relationshipViewCombo(window)->setCurrentIndex(
            relationshipViewCombo(window)->findText(QStringLiteral("Tree")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("relationship tree mode renders hierarchy",
                   navigableItemCount(relationshipsTree(window)) == 2,
                   true);
        bool sawTreeRoot = false;
        bool sawTreeChild = false;
        const QList<QTreeWidgetItem*> items = navigableItems(relationshipsTree(window));
        for (QTreeWidgetItem* item : items) {
            sawTreeRoot = sawTreeRoot
                || (item->text(0) == QStringLiteral("Root")
                    && item->text(1) == QStringLiteral("target_ref"));
            sawTreeChild = sawTreeChild
                || (item->text(0) == QStringLiteral("Outgoing")
                    && item->text(1) == QStringLiteral("target_sink"));
        }
        expectBool("relationship tree mode keeps root", sawTreeRoot, true);
        expectBool("relationship tree mode keeps child target", sawTreeChild, true);
        expectBool("relationship tree keeps direction filter enabled",
                   relationshipDirectionCombo(window)->isEnabled(),
                   true);
        relationshipDirectionCombo(window)->setCurrentIndex(
            relationshipDirectionCombo(window)->findText(QStringLiteral("Incoming")));
        relationshipTypeCombo(window)->setCurrentIndex(
            relationshipTypeCombo(window)->findText(QStringLiteral("Reads From")));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("relationship tree incoming filter renders hierarchy",
                   navigableItemCount(relationshipsTree(window)) == 2,
                   true);
        bool sawIncomingTreeSource = false;
        const QList<QTreeWidgetItem*> incomingItems = navigableItems(relationshipsTree(window));
        for (QTreeWidgetItem* item : incomingItems) {
            sawIncomingTreeSource = sawIncomingTreeSource
                || (item->text(0) == QStringLiteral("Incoming")
                    && item->text(1) == QStringLiteral("external_ref"));
        }
        expectBool("relationship tree keeps incoming source",
                   sawIncomingTreeSource, true);
        QTreeWidgetItem* rootItem = nullptr;
        for (QTreeWidgetItem* item : incomingItems) {
            if (item->text(0) == QStringLiteral("Root")
                && item->text(1) == QStringLiteral("target_ref")) {
                rootItem = item;
                break;
            }
        }
        expectBool("relationship tree root found for expansion state",
                   rootItem != nullptr, true);
        if (rootItem) {
            rootItem->setExpanded(false);
            semanticPanelRefresh(window)->refreshRelationshipsPanel();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTreeWidgetItem* refreshedRoot = nullptr;
            const QList<QTreeWidgetItem*> refreshedItems =
                navigableItems(relationshipsTree(window));
            for (QTreeWidgetItem* item : refreshedItems) {
                if (item->text(0) == QStringLiteral("Root")
                    && item->text(1) == QStringLiteral("target_ref")) {
                    refreshedRoot = item;
                    break;
                }
            }
            expectBool("relationship tree preserves collapsed root",
                       refreshedRoot && !refreshedRoot->isExpanded(),
                       true);
        }
    }
}

static SemanticSymbolRecord makeGuiSmokeRecord(
    int id,
    const QString& fileName,
    const QString& name,
    SymbolTaxonomy::DeclarationKind declarationKind,
    SymbolTaxonomy::CollectorKind collectorKind,
    int line,
    SymbolTaxonomy::SymbolOwnerScope ownerScope =
        SymbolTaxonomy::SymbolOwnerScope::Unknown,
    const QString& ownerName = QString(),
    const QString& rawTypeText = QString())
{
    SemanticFixtureRecordBuilder builder(name, declarationKind);
    builder.withFile(fileName)
        .withLocalHandle(id)
        .withLine(line)
        .withCollectorKind(collectorKind)
        .withTextSpan(0, name.length());
    if (ownerScope != SymbolTaxonomy::SymbolOwnerScope::Unknown
        || !ownerName.isEmpty()) {
        builder.withOwner(ownerScope, ownerName);
    }
    if (!rawTypeText.isEmpty())
        builder.withType(rawTypeText);
    return builder.record();
}

static void runNavigationDesignCacheWorkspaceActivationRegression()
{
    QTemporaryDir workspaceA;
    QTemporaryDir workspaceB;
    expectBool("navigation design cache temp dirs valid",
               workspaceA.isValid() && workspaceB.isValid(),
               true);
    if (!workspaceA.isValid() || !workspaceB.isValid())
        return;

    const QString fileA =
        QDir(workspaceA.path()).absoluteFilePath(QStringLiteral("a_top.sv"));
    const QString fileB =
        QDir(workspaceB.path()).absoluteFilePath(QStringLiteral("b_top.sv"));
    {
        QFile out(fileA);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write("module a_top; endmodule\n");
    }
    {
        QFile out(fileB);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write("module b_top; endmodule\n");
    }

    const SemanticSymbolRecord aTop =
        makeGuiSmokeRecord(1300,
                           fileA,
                           QStringLiteral("a_top"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           1);
    const SemanticSymbolRecord bTop =
        makeGuiSmokeRecord(1301,
                           fileB,
                           QStringLiteral("b_top"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           1);
    SemanticIndex index;
    index.setSnapshot(snapshotFromRecords({aTop, bTop}));
    NavigationService service(&index);
    NavigationWidget widget;
    WorkspaceManager workspace;
    NavigationManager manager;
    manager.setNavigationService(&service);
    manager.setNavigationWidget(&widget);
    manager.connectToWorkspaceManager(&workspace);

    expectBool("navigation design cache opens A",
               workspace.openWorkspace(workspaceA.path()),
               true);
    expectBool("navigation design cache scans A",
               waitUntil([&]() {
                   return workspace.getSystemVerilogFiles().contains(
                       QDir::cleanPath(QDir::fromNativeSeparators(
                           QFileInfo(fileA).absoluteFilePath())));
               }, 2000),
               true);
    manager.setActiveView(NavigationManager::DesignHierarchyView);
    expectBool("navigation design cache builds A once",
               manager.caches.designHierarchyValid
                   && manager.caches.designHierarchy.topModule
                          == QStringLiteral("a_top"),
               true);
    expectBool("navigation design cache keeps A immediately",
               !manager.updateDesignHierarchyData(false),
               true);

    manager.setActiveView(NavigationManager::FileHierarchyView);
    expectBool("navigation design cache opens B",
               workspace.openWorkspace(workspaceB.path()),
               true);
    expectBool("navigation design cache scans B",
               waitUntil([&]() {
                   return workspace.getSystemVerilogFiles().contains(
                       QDir::cleanPath(QDir::fromNativeSeparators(
                           QFileInfo(fileB).absoluteFilePath())));
               }, 2000),
               true);
    manager.setActiveView(NavigationManager::DesignHierarchyView);
    expectBool("navigation design cache builds B once",
               manager.caches.designHierarchyValid
                   && manager.caches.designHierarchy.topModule
                          == QStringLiteral("b_top"),
               true);

    manager.setActiveView(NavigationManager::FileHierarchyView);
    expectBool("navigation design cache switches back to A",
               workspace.switchWorkspace(0),
               true);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    expectBool("navigation design cache restores A without rebuild",
               manager.caches.designHierarchyValid
                   && manager.caches.designHierarchy.topModule
                          == QStringLiteral("a_top")
                   && !manager.updateDesignHierarchyData(false),
               true);

    manager.setActiveView(NavigationManager::FileHierarchyView);
    expectBool("navigation design cache close A activates B",
               workspace.closeWorkspace(0),
               true);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    expectBool("navigation design cache restores B after close",
               manager.caches.designHierarchyValid
                   && manager.caches.designHierarchy.topModule
                          == QStringLiteral("b_top")
                   && !manager.updateDesignHierarchyData(false),
               true);
}

static void runRtlInsightsPanelRegression(MainWindow& window, const QString& fixturePath)
{
    printf("\n-- RTL insights panel regression --\n");

    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using SymbolOwnerScope = SymbolTaxonomy::SymbolOwnerScope;

    QList<SemanticSymbolRecord> records;
    SemanticSymbolRecord module = makeGuiSmokeRecord(
        9601,
        fixturePath,
        QStringLiteral("insight_top"),
        DeclarationKind::Module,
        CollectorKind::Module,
        1);
    module.location.endLine = 18;
    records.append(module);
    const SemanticSymbolRecord clk = makeGuiSmokeRecord(
        9602,
        fixturePath,
        QStringLiteral("clk"),
        DeclarationKind::Port,
        CollectorKind::PortInput,
        2,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(clk);
    const SemanticSymbolRecord reset = makeGuiSmokeRecord(
        9603,
        fixturePath,
        QStringLiteral("rst_n"),
        DeclarationKind::Port,
        CollectorKind::PortInput,
        3,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(reset);
    const SemanticSymbolRecord stageInstance = makeGuiSmokeRecord(
        9604,
        fixturePath,
        QStringLiteral("u_stage"),
        DeclarationKind::Instance,
        CollectorKind::Inst,
        8,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(stageInstance);

    const SemanticSymbolRecord stateQ = makeGuiSmokeRecord(
        9605,
        fixturePath,
        QStringLiteral("state_q"),
        DeclarationKind::Enum,
        CollectorKind::EnumVariable,
        10,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"),
        QStringLiteral("state_t"));
    records.append(stateQ);
    const SemanticSymbolRecord stateD = makeGuiSmokeRecord(
        9606,
        fixturePath,
        QStringLiteral("state_d"),
        DeclarationKind::Enum,
        CollectorKind::EnumVariable,
        11,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"),
        QStringLiteral("state_t"));
    records.append(stateD);
    const SemanticSymbolRecord idle = makeGuiSmokeRecord(
        9607,
        fixturePath,
        QStringLiteral("IDLE"),
        DeclarationKind::Enum,
        CollectorKind::EnumValue,
        5,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"),
        QStringLiteral("state_t"));
    records.append(idle);
    const SemanticSymbolRecord run = makeGuiSmokeRecord(
        9608,
        fixturePath,
        QStringLiteral("RUN"),
        DeclarationKind::Enum,
        CollectorKind::EnumValue,
        5,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"),
        QStringLiteral("state_t"));
    records.append(run);
    const SemanticSymbolRecord dataQ = makeGuiSmokeRecord(
        9609,
        fixturePath,
        QStringLiteral("data_q"),
        DeclarationKind::Signal,
        CollectorKind::Logic,
        12,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(dataQ);
    const SemanticSymbolRecord nextData = makeGuiSmokeRecord(
        9610,
        fixturePath,
        QStringLiteral("next_data"),
        DeclarationKind::Signal,
        CollectorKind::Logic,
        13,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(nextData);
    const SemanticSymbolRecord consumer = makeGuiSmokeRecord(
        9611,
        fixturePath,
        QStringLiteral("consumer"),
        DeclarationKind::Process,
        CollectorKind::AlwaysFf,
        14,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(consumer);
    const SemanticSymbolRecord stageDataPin = makeGuiSmokeRecord(
        9612,
        fixturePath,
        QStringLiteral("u_stage.data_i"),
        DeclarationKind::Instance,
        CollectorKind::InstPin,
        15,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(stageDataPin);
    const SemanticSymbolRecord scanClk = makeGuiSmokeRecord(
        9613,
        fixturePath,
        QStringLiteral("scan_clk"),
        DeclarationKind::Port,
        CollectorKind::PortInput,
        16,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(scanClk);
    const SemanticSymbolRecord insightPackage = makeGuiSmokeRecord(
        9614,
        fixturePath,
        QStringLiteral("insight_pkg"),
        DeclarationKind::Package,
        CollectorKind::Package,
        18);
    records.append(insightPackage);
    records.append(makeGuiSmokeRecord(
        9617,
        fixturePath,
        QStringLiteral("PKG_DEPTH"),
        DeclarationKind::Parameter,
        CollectorKind::Parameter,
        21,
        SymbolOwnerScope::Package,
        QStringLiteral("insight_pkg")));
    records.append(makeGuiSmokeRecord(
        9615,
        fixturePath,
        QStringLiteral("insight_if"),
        DeclarationKind::Interface,
        CollectorKind::Interface,
        19));
    const SemanticSymbolRecord interfaceBus = makeGuiSmokeRecord(
        9616,
        fixturePath,
        QStringLiteral("if_bus"),
        DeclarationKind::Instance,
        CollectorKind::Inst,
        20,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"),
        QStringLiteral("insight_if"));
    records.append(interfaceBus);

    QList<SemanticRelationship> relationships;
    relationships.append(semanticFixtureRelationship(
        module,
        insightPackage,
        SymbolRelationshipEngine::REFERENCES));
    relationships.append(semanticFixtureRelationship(
        clk,
        module,
        SymbolRelationshipEngine::CLOCKS));
    relationships.append(semanticFixtureRelationship(
        reset,
        module,
        SymbolRelationshipEngine::RESETS));
    relationships.append(semanticFixtureRelationship(
        nextData,
        dataQ,
        SymbolRelationshipEngine::ASSIGNS_TO));
    relationships.append(semanticFixtureRelationship(
        consumer,
        dataQ,
        SymbolRelationshipEngine::READS_FROM));
    relationships.append(semanticFixtureRelationship(
        stageDataPin,
        dataQ,
        SymbolRelationshipEngine::REFERENCES));
    relationships.append(semanticFixtureRelationship(
        interfaceBus,
        dataQ,
        SymbolRelationshipEngine::REFERENCES));
    relationships.append(semanticFixtureRelationship(
        module,
        stageInstance,
        SymbolRelationshipEngine::INSTANTIATES));

    const QString content = QStringLiteral(
        "module insight_top(input logic clk, input logic rst_n);\n"
        "  typedef enum logic {IDLE, RUN} state_t;\n"
        "  state_t state_q;\n"
        "  state_t state_d;\n"
        "  always_comb begin\n"
        "    case (state_q)\n"
        "      IDLE: state_d = RUN;\n"
        "      RUN: state_d = IDLE;\n"
        "    endcase\n"
        "  end\n"
        "  input logic scan_clk;\n"
        "endmodule\n");
    QHash<QString, QString> fileContents;
    fileContents.insert(fixturePath, content);
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        fixturePath,
        records,
        content);
    SemanticDiagnostic insightDiagnostic;
    insightDiagnostic.fileName = fixturePath;
    insightDiagnostic.line = 6;
    insightDiagnostic.column = 5;
    insightDiagnostic.message = QStringLiteral("insight warning");
    insightDiagnostic.severity = SemanticDiagnostic::Warning;
    SemanticIndex::getInstance()->setSnapshot(
        snapshotFromRecords(
            records,
            relationships,
            QList<SemanticDiagnostic>{insightDiagnostic},
            fileContents));
    expectBool("RTL insights panel exists", rtlInsightsTree(window) != nullptr, true);
    if (!window.semanticDocks || !window.semanticDocks->rtlInsightsPanelCoordinator())
        return;

    window.semanticDocks->rtlInsightsPanelCoordinator()->showModuleInsights(
        fixturePath,
        QStringLiteral("insight_top"),
        QStringLiteral("data_q"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("RTL insights defaults to on-demand actions",
               rtlInsightsTree(window)
                   && rtlInsightsTree(window)->topLevelItemCount() > 0
                   && rtlInsightsTree(window)->topLevelItem(0)->text(0).contains(
                       QStringLiteral("Ready")),
               true);

    bool sawPort = false;
    bool sawClock = false;
    bool sawRelationshipEvidence = false;
    bool sawRelationshipFromEndpoint = false;
    bool sawRelationshipToEndpoint = false;
    bool sawContextKind = false;
    bool sawContextType = false;
    bool sawContextSourceRole = false;
    bool sawPackageMemberContext = false;
    bool sawPackageMemberKind = false;
    bool sawPackageMemberType = false;
    bool sawModuleBriefDiagnostic = false;
    bool sawModuleBriefDiagnosticSourceRole = false;
    bool sawClockSignalEndpoint = false;
    bool sawClockModuleEndpoint = false;
    bool sawClockRelationshipType = false;
    bool sawClockCategory = false;
    bool sawClockEvidenceReason = false;
    bool sawClockSourceRole = false;
    bool sawClockDomainMemberType = false;
    bool sawClockDomainMemberSourceRole = false;
    bool sawClockDomainMemberSignal = false;
    bool sawResetDomainMemberType = false;
    bool sawResetDomainMemberSignal = false;
    bool sawUnmappedClock = false;
    bool sawUnmappedClockCategory = false;
    bool sawTransition = false;
    bool sawFsmStateType = false;
    bool sawFsmStateSourceRole = false;
    bool sawFsmStateModule = false;
    bool sawFsmRegisterType = false;
    bool sawFsmRegisterSourceRole = false;
    bool sawFsmNextStateSignal = false;
    bool sawFsmNextStateSourceRole = false;
    bool sawFsmFromStateEndpoint = false;
    bool sawFsmToStateEndpoint = false;
    bool sawFsmTransitionSourceRole = false;
    bool sawSignalJourney = false;
    bool sawSignalJourneyDeclarationSourceRole = false;
    bool sawSignalJourneyFromEndpoint = false;
    bool sawSignalJourneyToEndpoint = false;
    bool sawSignalJourneyFromEndpointType = false;
    bool sawSignalJourneyFromEndpointSourceRole = false;
    bool sawSignalJourneyToEndpointType = false;
    bool sawSignalJourneyToEndpointSourceRole = false;
    bool sawSignalJourneyInterfaceConnection = false;
    bool sawSignalJourneyInterfaceKind = false;
    bool sawSignalJourneyInterfaceBase = false;
    bool sawSignalJourneyInterfacePeerType = false;
    bool sawSignalJourneyInterfaceSourceRole = false;
    auto scanRtlInsightItems = [&]() {
        const QList<QTreeWidgetItem*> items = navigableItems(rtlInsightsTree(window));
        for (QTreeWidgetItem* item : items) {
        sawPort = sawPort
            || (item->text(0) == QStringLiteral("Port")
                && item->text(1) == QStringLiteral("clk"));
        sawClock = sawClock
            || (item->text(0) == QStringLiteral("Clock")
                && item->text(1) == QStringLiteral("clk"));
        sawRelationshipEvidence = sawRelationshipEvidence
            || (item->text(0) == QStringLiteral("Outgoing")
                && item->text(1) == QStringLiteral("u_stage")
                && item->text(2) == QStringLiteral("Outgoing Instantiates"));
        sawRelationshipFromEndpoint = sawRelationshipFromEndpoint
            || (item->text(0) == QStringLiteral("From")
                && item->text(1) == QStringLiteral("insight_top")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawRelationshipToEndpoint = sawRelationshipToEndpoint
            || (item->text(0) == QStringLiteral("To")
                && item->text(1) == QStringLiteral("u_stage")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawContextKind = sawContextKind
            || (item->text(0) == QStringLiteral("Kind")
                && item->text(1) == QStringLiteral("package import")
                && item->text(2) == QStringLiteral("Package"));
        sawContextType = sawContextType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("package")
                && item->text(2) == QStringLiteral("package import"));
        sawContextSourceRole = sawContextSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Package"));
        sawPackageMemberContext = sawPackageMemberContext
            || (item->text(0) == QStringLiteral("Package Member")
                && item->text(1) == QStringLiteral("PKG_DEPTH")
                && item->text(2) == QStringLiteral("package parameter"));
        sawPackageMemberKind = sawPackageMemberKind
            || (item->text(0) == QStringLiteral("Kind")
                && item->text(1) == QStringLiteral("package parameter")
                && item->text(2) == QStringLiteral("Package Member"));
        sawPackageMemberType = sawPackageMemberType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("parameter")
                && item->text(2) == QStringLiteral("package parameter"));
        sawModuleBriefDiagnostic = sawModuleBriefDiagnostic
            || (item->text(0) == QStringLiteral("Warning")
                && item->text(1) == QStringLiteral("insight warning")
                && item->text(2) == QStringLiteral("diagnostic"));
        sawModuleBriefDiagnosticSourceRole =
            sawModuleBriefDiagnosticSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Warning"));
        sawClockSignalEndpoint = sawClockSignalEndpoint
            || (item->text(0) == QStringLiteral("Signal")
                && item->text(1) == QStringLiteral("clk")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockModuleEndpoint = sawClockModuleEndpoint
            || (item->text(0) == QStringLiteral("Module")
                && item->text(1) == QStringLiteral("insight_top")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockRelationshipType = sawClockRelationshipType
            || (item->text(0) == QStringLiteral("Relationship Type")
                && item->text(1) == QStringLiteral("Clock")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockCategory = sawClockCategory
            || (item->text(0) == QStringLiteral("Category")
                && item->text(1) == QStringLiteral("mapped domain")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockEvidenceReason = sawClockEvidenceReason
            || (item->text(0) == QStringLiteral("Reason")
                && item->text(1) == QStringLiteral("relationship")
                && item->text(2) == QStringLiteral("clk clocks insight_top"));
        sawClockSourceRole = sawClockSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockDomainMemberType = sawClockDomainMemberType
            || (item->text(0) == QStringLiteral("Relationship Type")
                && item->text(1) == QStringLiteral("Clock")
                && item->text(2) == QStringLiteral("insight_top"));
        sawClockDomainMemberSourceRole = sawClockDomainMemberSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("insight_top"));
        sawClockDomainMemberSignal = sawClockDomainMemberSignal
            || (item->text(0) == QStringLiteral("Domain Signal")
                && item->text(1) == QStringLiteral("clk")
                && item->text(2) == QStringLiteral("Clock"));
        sawResetDomainMemberType = sawResetDomainMemberType
            || (item->text(0) == QStringLiteral("Relationship Type")
                && item->text(1) == QStringLiteral("Reset")
                && item->text(2) == QStringLiteral("insight_top"));
        sawResetDomainMemberSignal = sawResetDomainMemberSignal
            || (item->text(0) == QStringLiteral("Domain Signal")
                && item->text(1) == QStringLiteral("rst_n")
                && item->text(2) == QStringLiteral("Reset"));
        sawUnmappedClock = sawUnmappedClock
            || (item->text(0) == QStringLiteral("Unmapped Clock")
                && item->text(1) == QStringLiteral("scan_clk")
                && item->text(2).contains(
                    QStringLiteral("no clock domain relationship")));
        sawUnmappedClockCategory = sawUnmappedClockCategory
            || (item->text(0) == QStringLiteral("Category")
                && item->text(1) == QStringLiteral("unmapped timing")
                && item->text(2) == QStringLiteral("Unmapped Clock"));
        sawTransition = sawTransition
            || (item->text(0) == QStringLiteral("IDLE")
                && item->text(1) == QStringLiteral("RUN"));
        sawFsmStateType = sawFsmStateType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("enum value")
                && item->text(2) == QStringLiteral("IDLE"));
        sawFsmStateSourceRole = sawFsmStateSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("IDLE"));
        sawFsmStateModule = sawFsmStateModule
            || (item->text(0) == QStringLiteral("Module")
                && item->text(1) == QStringLiteral("insight_top")
                && item->text(2) == QStringLiteral("IDLE"));
        sawFsmRegisterType = sawFsmRegisterType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("enum")
                && item->text(2) == QStringLiteral("state_q"));
        sawFsmRegisterSourceRole = sawFsmRegisterSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("state_q"));
        sawFsmNextStateSignal = sawFsmNextStateSignal
            || (item->text(0) == QStringLiteral("Next State Signal")
                && item->text(1) == QStringLiteral("state_d")
                && item->text(2) == QStringLiteral("enum"));
        sawFsmNextStateSourceRole = sawFsmNextStateSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("state_d"));
        sawFsmFromStateEndpoint = sawFsmFromStateEndpoint
            || (item->text(0) == QStringLiteral("From State")
                && item->text(1) == QStringLiteral("IDLE")
                && item->text(2) == QStringLiteral("unconditional"));
        sawFsmToStateEndpoint = sawFsmToStateEndpoint
            || (item->text(0) == QStringLiteral("To State")
                && item->text(1) == QStringLiteral("RUN")
                && item->text(2) == QStringLiteral("unconditional"));
        sawFsmTransitionSourceRole = sawFsmTransitionSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2).startsWith(QStringLiteral("line ")));
        sawSignalJourney = sawSignalJourney
            || (item->text(0) == QStringLiteral("Assignments")
                && item->text(1) == QStringLiteral("next_data"));
        sawSignalJourneyDeclarationSourceRole =
            sawSignalJourneyDeclarationSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("data_q"));
        sawSignalJourneyFromEndpoint = sawSignalJourneyFromEndpoint
            || (item->text(0) == QStringLiteral("From")
                && item->text(1) == QStringLiteral("next_data")
                && item->text(2) == QStringLiteral("Assigns To"));
        sawSignalJourneyToEndpoint = sawSignalJourneyToEndpoint
            || (item->text(0) == QStringLiteral("To")
                && item->text(1) == QStringLiteral("data_q")
                && item->text(2) == QStringLiteral("Assigns To"));
        sawSignalJourneyFromEndpointType = sawSignalJourneyFromEndpointType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("logic")
                && item->text(2) == QStringLiteral("next_data"));
        sawSignalJourneyFromEndpointSourceRole =
            sawSignalJourneyFromEndpointSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("next_data"));
        sawSignalJourneyToEndpointType = sawSignalJourneyToEndpointType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("logic")
                && item->text(2) == QStringLiteral("data_q"));
        sawSignalJourneyToEndpointSourceRole =
            sawSignalJourneyToEndpointSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("data_q"));
        sawSignalJourneyInterfaceConnection =
            sawSignalJourneyInterfaceConnection
            || (item->text(0) == QStringLiteral("Interface Connections")
                && item->text(1) == QStringLiteral("if_bus")
                && item->text(2) == QStringLiteral("interface incoming References"));
        sawSignalJourneyInterfaceKind = sawSignalJourneyInterfaceKind
            || (item->text(0) == QStringLiteral("Connection")
                && item->text(1) == QStringLiteral("interface instance")
                && item->text(2) == QStringLiteral("interface incoming References"));
        sawSignalJourneyInterfaceBase = sawSignalJourneyInterfaceBase
            || (item->text(0) == QStringLiteral("Interface")
                && item->text(1) == QStringLiteral("insight_if")
                && item->text(2) == QStringLiteral("interface instance"));
        sawSignalJourneyInterfacePeerType = sawSignalJourneyInterfacePeerType
            || (item->text(0) == QStringLiteral("Peer Type")
                && item->text(1) == QStringLiteral("instance")
                && item->text(2) == QStringLiteral("References"));
        sawSignalJourneyInterfaceSourceRole = sawSignalJourneyInterfaceSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("if_bus"));
        }
    };

    window.semanticDocks->rtlInsightsPanelCoordinator()->showModuleBrief();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    scanRtlInsightItems();
    window.semanticDocks->rtlInsightsPanelCoordinator()->showClockResetDomainMap();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    scanRtlInsightItems();
    window.semanticDocks->rtlInsightsPanelCoordinator()->showFsmGraph();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    scanRtlInsightItems();
    window.semanticDocks->rtlInsightsPanelCoordinator()->showSignalJourney();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    scanRtlInsightItems();

    expectBool("RTL insights renders module port", sawPort, true);
    expectBool("RTL insights renders clock domain", sawClock, true);
    expectBool("RTL insights renders relationship evidence",
               sawRelationshipEvidence,
               true);
    expectBool("RTL insights renders relationship from endpoint",
               sawRelationshipFromEndpoint,
               true);
    expectBool("RTL insights renders relationship to endpoint",
               sawRelationshipToEndpoint,
               true);
    expectBool("RTL insights renders context kind",
               sawContextKind,
               true);
    expectBool("RTL insights renders context type",
               sawContextType,
               true);
    expectBool("RTL insights renders context source role",
               sawContextSourceRole,
               true);
    expectBool("RTL insights renders package member context",
               sawPackageMemberContext,
               true);
    expectBool("RTL insights renders package member kind",
               sawPackageMemberKind,
               true);
    expectBool("RTL insights renders package member type",
               sawPackageMemberType,
               true);
    expectBool("RTL insights renders module brief diagnostic",
               sawModuleBriefDiagnostic,
               true);
    expectBool("RTL insights renders module brief diagnostic source role",
               sawModuleBriefDiagnosticSourceRole,
               true);
    expectBool("RTL insights renders clock signal endpoint",
               sawClockSignalEndpoint,
               true);
    expectBool("RTL insights renders clock module endpoint",
               sawClockModuleEndpoint,
               true);
    expectBool("RTL insights renders clock relationship type",
               sawClockRelationshipType,
               true);
    expectBool("RTL insights renders clock category",
               sawClockCategory,
               true);
    expectBool("RTL insights renders clock evidence reason",
               sawClockEvidenceReason,
               true);
    expectBool("RTL insights renders clock source role",
               sawClockSourceRole,
               true);
    expectBool("RTL insights renders clock domain member type",
               sawClockDomainMemberType,
               true);
    expectBool("RTL insights renders clock domain member source role",
               sawClockDomainMemberSourceRole,
               true);
    expectBool("RTL insights renders clock domain member signal",
               sawClockDomainMemberSignal,
               true);
    expectBool("RTL insights renders reset domain member type",
               sawResetDomainMemberType,
               true);
    expectBool("RTL insights renders reset domain member signal",
               sawResetDomainMemberSignal,
               true);
    expectBool("RTL insights renders unmapped clock", sawUnmappedClock, true);
    expectBool("RTL insights renders unmapped clock category",
               sawUnmappedClockCategory,
               true);
    expectBool("RTL insights renders FSM transition", sawTransition, true);
    expectBool("RTL insights renders FSM state type",
               sawFsmStateType,
               true);
    expectBool("RTL insights renders FSM state source role",
               sawFsmStateSourceRole,
               true);
    expectBool("RTL insights renders FSM state module",
               sawFsmStateModule,
               true);
    expectBool("RTL insights renders FSM register type",
               sawFsmRegisterType,
               true);
    expectBool("RTL insights renders FSM register source role",
               sawFsmRegisterSourceRole,
               true);
    expectBool("RTL insights renders FSM next state signal",
               sawFsmNextStateSignal,
               true);
    expectBool("RTL insights renders FSM next state source role",
               sawFsmNextStateSourceRole,
               true);
    expectBool("RTL insights renders FSM from state endpoint",
               sawFsmFromStateEndpoint,
               true);
    expectBool("RTL insights renders FSM to state endpoint",
               sawFsmToStateEndpoint,
               true);
    expectBool("RTL insights renders FSM transition source role",
               sawFsmTransitionSourceRole,
               true);
    expectBool("RTL insights renders signal journey", sawSignalJourney, true);
    expectBool("RTL insights renders signal journey declaration source role",
               sawSignalJourneyDeclarationSourceRole,
               true);
    expectBool("RTL insights renders signal journey from endpoint",
               sawSignalJourneyFromEndpoint,
               true);
    expectBool("RTL insights renders signal journey to endpoint",
               sawSignalJourneyToEndpoint,
               true);
    expectBool("RTL insights renders signal journey from endpoint type",
               sawSignalJourneyFromEndpointType,
               true);
    expectBool("RTL insights renders signal journey from endpoint source role",
               sawSignalJourneyFromEndpointSourceRole,
               true);
    expectBool("RTL insights renders signal journey to endpoint type",
               sawSignalJourneyToEndpointType,
               true);
    expectBool("RTL insights renders signal journey to endpoint source role",
               sawSignalJourneyToEndpointSourceRole,
               true);
    expectBool("RTL insights renders signal journey interface connection",
               sawSignalJourneyInterfaceConnection,
               true);
    expectBool("RTL insights renders signal journey interface kind",
               sawSignalJourneyInterfaceKind,
               true);
    expectBool("RTL insights renders signal journey interface base",
               sawSignalJourneyInterfaceBase,
               true);
    expectBool("RTL insights renders signal journey interface peer type",
               sawSignalJourneyInterfacePeerType,
               true);
    expectBool("RTL insights renders signal journey interface source role",
               sawSignalJourneyInterfaceSourceRole,
               true);

    window.semanticDocks->rtlInsightsPanelCoordinator()->showModuleInsights(
        fixturePath,
        QStringLiteral("insight_top"),
        QStringLiteral("clk"));
    window.semanticDocks->rtlInsightsPanelCoordinator()->showSignalJourney();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    bool sawTimingJourney = false;
    const QList<QTreeWidgetItem*> timingItems = navigableItems(rtlInsightsTree(window));
    for (QTreeWidgetItem* item : timingItems) {
        sawTimingJourney = sawTimingJourney
            || (item->text(0) == QStringLiteral("Timing Connections")
                && item->text(1) == QStringLiteral("insight_top")
                && item->text(2) == QStringLiteral("timing outgoing Clocks"));
    }
    expectBool("RTL insights renders timing signal journey",
               sawTimingJourney,
               true);
}

static void runRtlInsightsSemanticDiffRegression(MainWindow& window,
                                                 const QString& fixturePath)
{
    printf("\n-- RTL insights semantic diff regression --\n");

    QList<SemanticSymbolRecord> beforeRecords;
    const SemanticSymbolRecord beforeModule = makeGuiSmokeRecord(
        9701,
        fixturePath,
        QStringLiteral("diff_top"),
        SymbolTaxonomy::DeclarationKind::Module,
        SymbolTaxonomy::CollectorKind::Module,
        1);
    beforeRecords.append(beforeModule);
    beforeRecords.append(makeGuiSmokeRecord(
        9702,
        fixturePath,
        QStringLiteral("data"),
        SymbolTaxonomy::DeclarationKind::Port,
        SymbolTaxonomy::CollectorKind::PortInput,
        2,
        SymbolTaxonomy::SymbolOwnerScope::Module,
        QStringLiteral("diff_top")));
    beforeRecords.append(makeGuiSmokeRecord(
        9703,
        fixturePath,
        QStringLiteral("stale_q"),
        SymbolTaxonomy::DeclarationKind::Signal,
        SymbolTaxonomy::CollectorKind::Logic,
        6,
        SymbolTaxonomy::SymbolOwnerScope::Module,
        QStringLiteral("diff_top")));
    const SemanticSymbolRecord beforeInstance = makeGuiSmokeRecord(
        9704,
        fixturePath,
        QStringLiteral("u_old"),
        SymbolTaxonomy::DeclarationKind::Instance,
        SymbolTaxonomy::CollectorKind::Inst,
        10,
        SymbolTaxonomy::SymbolOwnerScope::Module,
        QStringLiteral("diff_top"));
    beforeRecords.append(beforeInstance);

    QList<SemanticSymbolRecord> afterRecords;
    const SemanticSymbolRecord afterModule = makeGuiSmokeRecord(
        9801,
        fixturePath,
        QStringLiteral("diff_top"),
        SymbolTaxonomy::DeclarationKind::Module,
        SymbolTaxonomy::CollectorKind::Module,
        1);
    afterRecords.append(afterModule);
    afterRecords.append(makeGuiSmokeRecord(
        9802,
        fixturePath,
        QStringLiteral("data"),
        SymbolTaxonomy::DeclarationKind::Port,
        SymbolTaxonomy::CollectorKind::PortOutput,
        2,
        SymbolTaxonomy::SymbolOwnerScope::Module,
        QStringLiteral("diff_top")));
    afterRecords.append(makeGuiSmokeRecord(
        9803,
        fixturePath,
        QStringLiteral("state_q"),
        SymbolTaxonomy::DeclarationKind::Signal,
        SymbolTaxonomy::CollectorKind::Logic,
        7,
        SymbolTaxonomy::SymbolOwnerScope::Module,
        QStringLiteral("diff_top")));
    const SemanticSymbolRecord afterInstance = makeGuiSmokeRecord(
        9804,
        fixturePath,
        QStringLiteral("u_new"),
        SymbolTaxonomy::DeclarationKind::Instance,
        SymbolTaxonomy::CollectorKind::Inst,
        10,
        SymbolTaxonomy::SymbolOwnerScope::Module,
        QStringLiteral("diff_top"));
    afterRecords.append(afterInstance);

    const SemanticRelationship beforeRelationship =
        semanticFixtureRelationship(beforeModule,
                                    beforeInstance,
                                    SymbolRelationshipEngine::INSTANTIATES);
    const SemanticRelationship afterRelationship =
        semanticFixtureRelationship(afterModule,
                                    afterInstance,
                                    SymbolRelationshipEngine::INSTANTIATES);

    SemanticDiagnostic beforeDiagnostic;
    beforeDiagnostic.fileName = fixturePath;
    beforeDiagnostic.line = 6;
    beforeDiagnostic.column = 3;
    beforeDiagnostic.message = QStringLiteral("old warning");
    beforeDiagnostic.severity = SemanticDiagnostic::Warning;

    SemanticDiagnostic afterDiagnostic;
    afterDiagnostic.fileName = fixturePath;
    afterDiagnostic.line = 7;
    afterDiagnostic.column = 5;
    afterDiagnostic.message = QStringLiteral("new error");
    afterDiagnostic.severity = SemanticDiagnostic::Error;

    auto beforeSnapshot = snapshotFromRecords(
        beforeRecords,
        QList<SemanticRelationship>{beforeRelationship},
        QList<SemanticDiagnostic>{beforeDiagnostic});
    auto afterSnapshot = snapshotFromRecords(
        afterRecords,
        QList<SemanticRelationship>{afterRelationship},
        QList<SemanticDiagnostic>{afterDiagnostic});

    expectBool("RTL insights panel exists for semantic diff",
               rtlInsightsTree(window) != nullptr,
               true);
    if (!window.semanticDocks || !window.semanticDocks->rtlInsightsPanelCoordinator())
        return;

    window.semanticDocks->rtlInsightsPanelCoordinator()->showSemanticDiff(
        beforeSnapshot,
        afterSnapshot,
        QStringLiteral("diff_top"),
        fixturePath,
        fixturePath);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    bool sawModifiedPort = false;
    bool sawModifiedPortBefore = false;
    bool sawModifiedPortAfter = false;
    bool sawModifiedPortSourceRole = false;
    bool sawAddedSignal = false;
    bool sawRemovedSignal = false;
    bool sawAddedRelationship = false;
    bool sawRemovedRelationship = false;
    bool sawAddedRelationshipFromEndpoint = false;
    bool sawAddedRelationshipToEndpoint = false;
    bool sawAddedRelationshipSourceRole = false;
    bool sawAddedDiagnostic = false;
    bool sawAddedDiagnosticSourceRole = false;
    bool sawRemovedDiagnostic = false;
    const QList<QTreeWidgetItem*> items = navigableItems(rtlInsightsTree(window));
    for (QTreeWidgetItem* item : items) {
        sawModifiedPort = sawModifiedPort
            || (item->text(0) == QStringLiteral("Modified Ports")
                && item->text(1) == QStringLiteral("data")
                && item->text(2).contains(QStringLiteral("input -> output"))
                && item->text(2).contains(QStringLiteral("scope diff_top")));
        sawModifiedPortBefore = sawModifiedPortBefore
            || (item->text(0) == QStringLiteral("Before")
                && item->text(1) == QStringLiteral("input")
                && item->text(2) == QStringLiteral("scope diff_top"));
        sawModifiedPortAfter = sawModifiedPortAfter
            || (item->text(0) == QStringLiteral("After")
                && item->text(1) == QStringLiteral("output")
                && item->text(2) == QStringLiteral("scope diff_top"));
        sawModifiedPortSourceRole = sawModifiedPortSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("port"));
        sawAddedSignal = sawAddedSignal
            || (item->text(0) == QStringLiteral("Added Signals")
                && item->text(1) == QStringLiteral("state_q")
                && item->text(2).contains(QStringLiteral("scope diff_top")));
        sawRemovedSignal = sawRemovedSignal
            || (item->text(0) == QStringLiteral("Removed Signals")
                && item->text(1) == QStringLiteral("stale_q")
                && item->text(2).contains(QStringLiteral("scope diff_top")));
        sawAddedRelationship = sawAddedRelationship
            || (item->text(0) == QStringLiteral("Added")
                && item->text(1) == QStringLiteral("Instantiates")
                && item->text(2) == QStringLiteral("diff_top -> u_new"));
        sawAddedRelationshipFromEndpoint = sawAddedRelationshipFromEndpoint
            || (item->text(0) == QStringLiteral("From")
                && item->text(1) == QStringLiteral("diff_top")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawAddedRelationshipToEndpoint = sawAddedRelationshipToEndpoint
            || (item->text(0) == QStringLiteral("To")
                && item->text(1) == QStringLiteral("u_new")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawAddedRelationshipSourceRole = sawAddedRelationshipSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawRemovedRelationship = sawRemovedRelationship
            || (item->text(0) == QStringLiteral("Removed")
                && item->text(1) == QStringLiteral("Instantiates")
                && item->text(2) == QStringLiteral("diff_top -> u_old"));
        sawAddedDiagnostic = sawAddedDiagnostic
            || (item->text(0) == QStringLiteral("Added")
                && item->text(1) == QStringLiteral("new error")
                && item->text(2) == QStringLiteral("Error, design source"));
        sawAddedDiagnosticSourceRole = sawAddedDiagnosticSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Error"));
        sawRemovedDiagnostic = sawRemovedDiagnostic
            || (item->text(0) == QStringLiteral("Removed")
                && item->text(1) == QStringLiteral("old warning")
                && item->text(2) == QStringLiteral("Warning, design source"));
    }

    expectBool("RTL insights renders modified diff port", sawModifiedPort, true);
    expectBool("RTL insights renders modified diff before",
               sawModifiedPortBefore,
               true);
    expectBool("RTL insights renders modified diff after",
               sawModifiedPortAfter,
               true);
    expectBool("RTL insights renders modified diff source role",
               sawModifiedPortSourceRole,
               true);
    expectBool("RTL insights renders added diff signal", sawAddedSignal, true);
    expectBool("RTL insights renders removed diff signal", sawRemovedSignal, true);
    expectBool("RTL insights renders added diff relationship",
               sawAddedRelationship,
               true);
    expectBool("RTL insights renders added diff from endpoint",
               sawAddedRelationshipFromEndpoint,
               true);
    expectBool("RTL insights renders added diff to endpoint",
               sawAddedRelationshipToEndpoint,
               true);
    expectBool("RTL insights renders added diff relationship source role",
               sawAddedRelationshipSourceRole,
               true);
    expectBool("RTL insights renders removed diff relationship",
               sawRemovedRelationship,
               true);
    expectBool("RTL insights renders added diff diagnostic", sawAddedDiagnostic, true);
    expectBool("RTL insights renders added diff diagnostic source role",
               sawAddedDiagnosticSourceRole,
               true);
    expectBool("RTL insights renders removed diff diagnostic", sawRemovedDiagnostic, true);
}

static void runTreeSitterFoldingProviderRegression()
{
    printf("\n-- tree-sitter folding provider regression --\n");

    TSDocument syntaxDocument;
    syntaxDocument.setText(QStringLiteral(
        "module fold_top;\n"
        "  initial begin\n"
        "    case (sel)\n"
        "      1'b0: a = b;\n"
        "      default: a = c;\n"
        "    endcase\n"
        "  end\n"
        "endmodule\n"));
    const QList<TSFoldRange> syntaxRanges = syntaxDocument.foldingRanges();
    bool hasModuleFold = false;
    bool hasNestedFold = false;
    for (const TSFoldRange& range : syntaxRanges) {
        hasModuleFold = hasModuleFold
            || (range.kind == TSFoldRangeKind::Syntax
                && range.startLine == 0
                && range.endLine >= 7);
        hasNestedFold = hasNestedFold
            || (range.kind == TSFoldRangeKind::Syntax
                && range.startLine > 0
                && range.endLine > range.startLine);
    }
    expectBool("folding provider finds module range", hasModuleFold, true);
    expectBool("folding provider finds nested syntax range", hasNestedFold, true);

    TSDocument customDocument;
    customDocument.setText(QStringLiteral(
        "module fold_top;\n"
        "// fold clock   reset path\n"
        "logic clk;\n"
        "logic rst_n;\n"
        "// endfold\n"
        "endmodule\n"));
    const QList<TSFoldRange> customRanges = customDocument.foldingRanges();
    bool hasCustomFold = false;
    for (const TSFoldRange& range : customRanges) {
        hasCustomFold = hasCustomFold
            || (range.kind == TSFoldRangeKind::Custom
                && range.startLine == 1
                && range.endLine == 4
                && range.label == QStringLiteral("clock   reset path"));
    }
    expectBool("custom fold marker creates range", hasCustomFold, true);

    TSDocument malformedDocument;
    malformedDocument.setText(QStringLiteral(
        "// endfold\n"
        "module fold_top;\n"
        "// fold never closed\n"
        "endmodule\n"));
    const QList<TSFoldRange> malformedRanges = malformedDocument.foldingRanges();
    bool hasMalformedCustom = false;
    for (const TSFoldRange& range : malformedRanges)
        hasMalformedCustom = hasMalformedCustom || range.kind == TSFoldRangeKind::Custom;
    expectBool("malformed custom fold markers do not create range",
               hasMalformedCustom,
               false);

    MyCodeEditor editor;
    editor.setPlainText(QStringLiteral(
        "module fold_top;\n"
        "  logic a;\n"
        "endmodule\n"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("editor folding has module start",
               editor.state->folding.hasFoldAtLine(0),
               true);
    const bool folded = editor.state->folding.toggleFoldAtLine(&editor, 0);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("editor folding collapses block",
               folded && !editor.document()->findBlockByNumber(1).isVisible(),
               true);
    const bool unfolded = editor.state->folding.toggleFoldAtLine(&editor, 0);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("editor folding expands block",
               unfolded && editor.document()->findBlockByNumber(1).isVisible(),
               true);

    MyCodeEditor gutterEditor;
    gutterEditor.resize(320, 160);
    gutterEditor.setPlainText(QStringLiteral(
        "module fold_top;\n"
        "  logic a;\n"
        "endmodule\n"));
    gutterEditor.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QMouseEvent foldClick(QEvent::MouseButtonPress,
                          QPointF(6, 4),
                          QPointF(6, 4),
                          QPointF(6, 4),
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
    const bool gutterCollapsed =
        gutterEditor.state->handleGutterMousePress(&gutterEditor, &foldClick)
        && !gutterEditor.document()->findBlockByNumber(1).isVisible();
    expectBool("editor gutter click collapses fold", gutterCollapsed, true);
    QMouseEvent unfoldClick(QEvent::MouseButtonPress,
                            QPointF(6, 4),
                            QPointF(6, 4),
                            QPointF(6, 4),
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    const bool gutterExpanded =
        gutterEditor.state->handleGutterMousePress(&gutterEditor, &unfoldClick)
        && gutterEditor.document()->findBlockByNumber(1).isVisible();
    expectBool("editor gutter click expands fold", gutterExpanded, true);

    MyCodeEditor commandEditor;
    QSignalSpy commandStatusSpy(&commandEditor,
                                &MyCodeEditor::editorStatusMessageRequested);
    commandEditor.startFoldRegionMarkMode();
    expectBool("fold region action enters mark mode",
               commandEditor.foldRegionMarkModeActive(),
               true);
    expectBool("fold region action emits start-line mode status",
               commandStatusSpy.count() > 0
                   && commandStatusSpy.last().at(0).toString().contains(
                       QStringLiteral("click start line")),
               true);
    QMouseEvent markStartClick(QEvent::MouseButtonPress,
                               QPointF(6, 4),
                               QPointF(6, 4),
                               QPointF(6, 4),
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
    const bool startLineSelected =
        commandEditor.state->handleGutterMousePress(&commandEditor, &markStartClick);
    expectBool("fold region gutter start selects next stage",
               startLineSelected
                   && commandStatusSpy.count() > 0
                   && commandStatusSpy.last().at(0).toString().contains(
                       QStringLiteral("click end line")),
               true);
    QKeyEvent blockedText(QEvent::KeyPress,
                          Qt::Key_A,
                          Qt::NoModifier,
                          QStringLiteral("a"));
    QApplication::sendEvent(&commandEditor, &blockedText);
    expectBool("fold region mode blocks text input",
               commandEditor.toPlainText().isEmpty(),
               true);
    QKeyEvent cancelFold(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&commandEditor, &cancelFold);
    expectBool("Esc cancels fold region mark mode",
               !commandEditor.foldRegionMarkModeActive(),
               true);

    MyCodeEditor markerEditor;
    const QString markerOriginal = QStringLiteral(
        "module fold_top;\n"
        "  logic clk;\n"
        "  logic rst_n;\n"
        "endmodule\n");
    markerEditor.setPlainText(markerOriginal);
    const bool markersInserted =
        markerEditor.insertCustomFoldMarkersForTest(
            1,
            2,
            QStringLiteral("clock   reset path"));
    const QString markerText = markerEditor.toPlainText();
    expectBool("fold marker insertion adds alias",
               markersInserted
                   && markerText.contains(QStringLiteral("// fold clock   reset path"))
                   && markerText.contains(QStringLiteral("// endfold")),
               true);
    markerEditor.undo();
    expectBool("fold marker insertion is one undo block",
               markerEditor.toPlainText() == markerOriginal,
               true);

    FoldBlockShelfModel shelfModel;
    FoldShelfItem shelfItem;
    shelfItem.alias = QStringLiteral("clock reset");
    shelfItem.text = QStringLiteral("// fold clock reset\nlogic clk;\n// endfold\n");
    shelfItem.sourceFile = QStringLiteral("C:/fixture/fold_top.sv");
    shelfItem.sourceStartLine = 2;
    shelfItem.sourceEndLine = 4;
    shelfItem.originKind = FoldShelfOriginKind::Moved;
    const QString shelfId = shelfModel.addItem(shelfItem);
    expectBool("fold shelf model stores item",
               !shelfId.isEmpty()
                   && shelfModel.items().size() == 1
                   && shelfModel.item(shelfId).lineCount == 3,
               true);
    expectBool("fold shelf model consumes item",
               shelfModel.consumeItem(shelfId)
                   && shelfModel.item(shelfId).consumed,
               true);
    expectBool("fold shelf model removes item",
               shelfModel.removeItem(shelfId)
                   && shelfModel.items().isEmpty(),
               true);
    const QByteArray encodedShelfItem = encodeFoldShelfItem(shelfItem);
    const FoldShelfItem decodedShelfItem = decodeFoldShelfItem(encodedShelfItem);
    expectBool("fold shelf item mime round-trips",
               decodedShelfItem.alias == shelfItem.alias
                   && decodedShelfItem.text == shelfItem.text
                   && decodedShelfItem.sourceFile == shelfItem.sourceFile
                   && decodedShelfItem.originKind == FoldShelfOriginKind::Moved,
               true);

    MyCodeEditor shelfEditor;
    shelfEditor.setDocumentFileName(QStringLiteral("C:/fixture/fold_top.sv"));
    shelfEditor.setPlainText(QStringLiteral(
        "module fold_top;\n"
        "// fold reusable block\n"
        "  logic clk;\n"
        "  logic rst_n;\n"
        "// endfold\n"
        "endmodule\n"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const FoldShelfItem extractedShelfItem =
        shelfEditor.foldShelfItemAtLineForTest(2, FoldShelfOriginKind::Moved);
    expectBool("fold shelf extracts custom fold block",
               extractedShelfItem.alias == QStringLiteral("reusable block")
                   && extractedShelfItem.text.contains(QStringLiteral("logic clk"))
                   && extractedShelfItem.sourceStartLine == 1
                   && extractedShelfItem.sourceEndLine == 4
                   && extractedShelfItem.originKind == FoldShelfOriginKind::Moved,
               true);
    const bool deletedFoldBlock = shelfEditor.deleteCustomFoldAtLineForTest(2);
    expectBool("fold shelf move deletes source block",
               deletedFoldBlock
                   && !shelfEditor.toPlainText().contains(QStringLiteral("logic clk")),
               true);
    shelfEditor.undo();
    expectBool("fold shelf move delete is one undo block",
               shelfEditor.toPlainText().contains(QStringLiteral("logic clk")),
               true);

    MyCodeEditor insertShelfEditor;
    insertShelfEditor.setPlainText(QStringLiteral("module fold_top;\nendmodule\n"));
    const bool insertedShelfItem =
        insertShelfEditor.insertFoldShelfItemAtLineForTest(extractedShelfItem, 1);
    expectBool("fold shelf inserts item at line boundary",
               insertedShelfItem
                   && insertShelfEditor.toPlainText().contains(QStringLiteral("// fold reusable block"))
                   && insertShelfEditor.toPlainText().contains(QStringLiteral("// endfold")),
               true);
    insertShelfEditor.undo();
    expectBool("fold shelf insert is one undo block",
               insertShelfEditor.toPlainText() == QStringLiteral("module fold_top;\nendmodule\n"),
               true);

    MyCodeEditor shelfCommandEditor;
    QSignalSpy shelfStatusSpy(&shelfCommandEditor,
                              &MyCodeEditor::editorStatusMessageRequested);
    shelfCommandEditor.startFoldShelfMode();
    expectBool("fold shelf action enters shelf mode",
               shelfCommandEditor.foldShelfModeActive(),
               true);
    expectBool("fold shelf action emits shelf mode status",
               shelfStatusSpy.count() > 0
                   && shelfStatusSpy.last().at(0).toString().contains(
                       QStringLiteral("Fold Shelf")),
               true);
    QKeyEvent blockedShelfText(QEvent::KeyPress,
                               Qt::Key_X,
                               Qt::NoModifier,
                               QStringLiteral("x"));
    QApplication::sendEvent(&shelfCommandEditor, &blockedShelfText);
    expectBool("fold shelf mode blocks text input",
               shelfCommandEditor.toPlainText().isEmpty()
                   && shelfCommandEditor.foldShelfModeActive(),
               true);
    QKeyEvent cancelShelf(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&shelfCommandEditor, &cancelShelf);
    expectBool("Esc cancels fold shelf mode",
               !shelfCommandEditor.foldShelfModeActive(),
               true);
}

static void runNavigationHierarchyModelRegression()
{
    printf("\n-- navigation hierarchy model regression --\n");

    NavigationWidget widget;
    expectBool("navigation only exposes files and design tabs",
               widget.tabWidget && widget.tabWidget->count() == 2
                   && widget.tabWidget->tabText(0) == QStringLiteral("Files")
                   && widget.tabWidget->tabText(1) == QStringLiteral("Design"),
               true);

    const QString fileTabPath = QStringLiteral("C:/fixture/relationship_top.sv");
    widget.updateFileHierarchy({fileTabPath});
    QTreeWidgetItem* fileItem = widget.findFileItemByPath(fileTabPath);
    expectBool("files tab renders file item", fileItem != nullptr, true);

    QSignalSpy fileClicks(&widget, &NavigationWidget::fileDoubleClicked);
    if (fileItem)
        widget.onFileTreeDoubleClicked(fileItem, 0);
    expectBool("files tab double-click emits file",
               fileClicks.count() == 1
                   && fileClicks.takeFirst().at(0).toString() == fileTabPath,
               true);

    const QString designTopFile = QStringLiteral("C:/fixture/design_top.sv");
    const QString designStageFile = QStringLiteral("C:/fixture/design_stage.sv");
    const QString designExtraFile = QStringLiteral("C:/fixture/design_extra.sv");
    const SemanticSymbolRecord designTop =
        makeGuiSmokeRecord(1240,
                           designTopFile,
                           QStringLiteral("design_top"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           3);
    const SemanticSymbolRecord designStage =
        makeGuiSmokeRecord(1241,
                           designStageFile,
                           QStringLiteral("design_stage"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           1);
    const SemanticSymbolRecord designInstance =
        makeGuiSmokeRecord(1242,
                           designTopFile,
                           QStringLiteral("u_stage"),
                           SymbolTaxonomy::DeclarationKind::Instance,
                           SymbolTaxonomy::CollectorKind::Inst,
                           8,
                           SymbolTaxonomy::SymbolOwnerScope::Module,
                           QStringLiteral("design_top"),
                           QStringLiteral("design_stage"));

    const SemanticRelationship instantiatesStage =
        semanticFixtureRelationship(designTop,
                                    designInstance,
                                    SymbolRelationshipEngine::INSTANTIATES);
    SemanticIndex designIndex;
    designIndex.setSnapshot(snapshotFromRecords(
        {designTop, designStage, designInstance},
        {instantiatesStage}));
    HierarchyService designHierarchyService(&designIndex);
    const DesignHierarchyReport designReport =
        designHierarchyService.getDesignHierarchyReport(QStringLiteral("design_top"));
    expectBool("design hierarchy infers top from instantiation graph",
               designHierarchyService.inferDesignTopModule()
                   == QStringLiteral("design_top"),
               true);
    expectBool("design hierarchy report has top and instance",
               designReport.nodes.size() == 2,
               true);
    expectBool("design hierarchy report keeps selected top",
               designReport.topModule == QStringLiteral("design_top"),
               true);
    expectBool("design hierarchy report has no unresolved modules",
               designReport.unresolvedModules.isEmpty(),
               true);
    expectBool("modules defined in file lists design top",
               designHierarchyService.modulesDefinedInFile(designTopFile)
                   .contains(QStringLiteral("design_top")),
               true);

    const QString ws1TopFile = QStringLiteral("C:/fixture/ws1/top.sv");
    const QString ws1LeafFile = QStringLiteral("C:/fixture/ws1/shared_leaf.sv");
    const QString ws2TopFile = QStringLiteral("C:/fixture/ws2/top.sv");
    const QString ws2LeafFile = QStringLiteral("C:/fixture/ws2/shared_leaf.sv");
    const SemanticSymbolRecord ws1Top =
        makeGuiSmokeRecord(1250,
                           ws1TopFile,
                           QStringLiteral("ws1_top"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           1);
    const SemanticSymbolRecord ws1Leaf =
        makeGuiSmokeRecord(1251,
                           ws1LeafFile,
                           QStringLiteral("shared_leaf"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           1);
    const SemanticSymbolRecord ws2Top =
        makeGuiSmokeRecord(1252,
                           ws2TopFile,
                           QStringLiteral("ws2_top"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           1);
    const SemanticSymbolRecord ws2Leaf =
        makeGuiSmokeRecord(1253,
                           ws2LeafFile,
                           QStringLiteral("shared_leaf"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           50);
    const SemanticSymbolRecord ws2Instance =
        makeGuiSmokeRecord(1254,
                           ws2TopFile,
                           QStringLiteral("u_shared_leaf"),
                           SymbolTaxonomy::DeclarationKind::Instance,
                           SymbolTaxonomy::CollectorKind::Inst,
                           5,
                           SymbolTaxonomy::SymbolOwnerScope::Module,
                           QStringLiteral("ws2_top"),
                           QStringLiteral("shared_leaf"));
    const SemanticRelationship scopedInstantiates =
        semanticFixtureRelationship(ws2Top,
                                    ws2Instance,
                                    SymbolRelationshipEngine::INSTANTIATES);
    SemanticIndex scopedDesignIndex;
    scopedDesignIndex.setSnapshot(snapshotFromRecords(
        {ws1Top, ws1Leaf, ws2Top, ws2Leaf, ws2Instance},
        {scopedInstantiates}));
    HierarchyService scopedHierarchyService(&scopedDesignIndex);
    QSet<QString> ws2Scope;
    ws2Scope.insert(QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(ws2TopFile).absoluteFilePath())));
    ws2Scope.insert(QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(ws2LeafFile).absoluteFilePath())));
    const QStringList scopedTops =
        scopedHierarchyService.inferDesignTopModules(ws2Scope);
    const DesignHierarchyReport scopedReport =
        scopedHierarchyService.getDesignHierarchyReport(scopedTops,
                                                        QStringLiteral("ws2_top"),
                                                        ws2Scope);
    expectBool("scoped design top excludes other workspace",
               scopedTops.contains(QStringLiteral("ws2_top"))
                   && !scopedTops.contains(QStringLiteral("ws1_top")),
               true);
    expectBool("scoped design hierarchy uses scoped duplicate module",
               scopedReport.nodes.size() == 2
                   && scopedReport.nodes.last().moduleType == QStringLiteral("shared_leaf")
                   && scopedReport.nodes.last().definitionFile
                          == QDir::cleanPath(QDir::fromNativeSeparators(
                              QFileInfo(ws2LeafFile).absoluteFilePath())),
               true);

    const QString normalizedTopFile =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(designTopFile).absoluteFilePath()));
    const QString normalizedStageFile =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(designStageFile).absoluteFilePath()));
    expectBool("design hierarchy participating files include top",
               designReport.participatingFiles.contains(normalizedTopFile),
               true);
    expectBool("design hierarchy participating files include child definition",
               designReport.participatingFiles.contains(normalizedStageFile),
               true);

    widget.setActiveTab(NavigationWidget::DesignTab);
    widget.updateFileHierarchy({designTopFile, designStageFile, designExtraFile});
    widget.updateDesignHierarchy(designReport);

    QTreeWidgetItem* designItem = nullptr;
    QLabel* designStatusLabel = nullptr;
    for (QLabel* label : widget.findChildren<QLabel*>()) {
        if (label->text().contains(QStringLiteral("Design Top: design_top"))) {
            designStatusLabel = label;
            break;
        }
    }
    for (QTreeWidget* tree : widget.findChildren<QTreeWidget*>()) {
        designItem = findItemByText(tree, QStringLiteral("u_stage"), 0);
        if (designItem
            && designItem->text(1) != QStringLiteral("design_stage")) {
            designItem = nullptr;
        }
        if (designItem)
            break;
    }
    expectBool("design hierarchy status shows top",
               designStatusLabel != nullptr,
               true);
    expectBool("design hierarchy renders instance text",
               designItem != nullptr,
               true);

    bool designDoubleClicked = false;
    DesignHierarchyNode clickedDesignNode;
    QObject::connect(&widget, &NavigationWidget::designNodeDoubleClicked,
                     &widget, [&](const DesignHierarchyNode& node) {
                         designDoubleClicked = true;
                         clickedDesignNode = node;
                     });
    if (designItem)
        widget.onDesignTreeDoubleClicked(designItem, 0);
    expectBool("design hierarchy double-click emits node",
               designDoubleClicked,
               true);
    expectBool("design hierarchy double-click preserves instance site",
               clickedDesignNode.instanceName == QStringLiteral("u_stage")
                   && clickedDesignNode.instanceLine == 8,
               true);

    QTreeWidgetItem* topFileItem = widget.findFileItemByPath(designTopFile);
    QTreeWidgetItem* extraFileItem = widget.findFileItemByPath(designExtraFile);
    expectBool("design participating file remains visible",
               topFileItem != nullptr
                   && extraFileItem != nullptr
                   && topFileItem->foreground(0).color().alpha()
                          > extraFileItem->foreground(0).color().alpha(),
               true);
    expectBool("design non-participating file is dimmed",
               extraFileItem != nullptr
                   && extraFileItem->foreground(0).color().alpha() < 255,
               true);

    widget.clearDesignHierarchy();
    topFileItem = widget.findFileItemByPath(designTopFile);
    extraFileItem = widget.findFileItemByPath(designExtraFile);
    expectBool("clear design restores file opacity",
               topFileItem != nullptr
                   && extraFileItem != nullptr
                   && topFileItem->foreground(0).color().alpha()
                          == extraFileItem->foreground(0).color().alpha(),
               true);
}

static void sendWidgetKey(QWidget* widget,
                          int key,
                          const QString& text = QString())
{
    if (!widget)
        return;
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
    QApplication::sendEvent(widget, &event);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

static void runComModeRegression(MainWindow& window)
{
    printf("\n-- com mode regression --\n");

    QTemporaryDir tempDir;
    expectBool("com mode temp workspace valid", tempDir.isValid(), true);
    const QString root = QDir::cleanPath(tempDir.path());
    QDir(root).mkpath(QStringLiteral("rtl"));
    const QString topFile =
        QDir::cleanPath(QDir(root).filePath(QStringLiteral("rtl/top.sv")));
    const QString childFile =
        QDir::cleanPath(QDir(root).filePath(QStringLiteral("rtl/child.sv")));
    const QString packageFile =
        QDir::cleanPath(QDir(root).filePath(QStringLiteral("rtl/cfg_pkg.sv")));
    const QString outsideFile =
        QDir::cleanPath(QDir(root).filePath(QStringLiteral("../outside.sv")));

    ProjectModel project;
    project.setWorkspaceState(root, {topFile, childFile, packageFile});

    const SemanticSymbolRecord topModule =
        SemanticFixtureRecordBuilder(QStringLiteral("top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(topFile)
            .withLocalHandle(87001)
            .withRange(10, 1, 15, 10)
            .record();
    const SemanticSymbolRecord childModule =
        SemanticFixtureRecordBuilder(QStringLiteral("child"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(childFile)
            .withLocalHandle(87002)
            .withRange(30, 1, 35, 10)
            .record();
    const SemanticSymbolRecord outsideModule =
        SemanticFixtureRecordBuilder(QStringLiteral("outside"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(outsideFile)
            .withLocalHandle(87003)
            .withRange(1, 1, 4, 10)
            .record();
    const SemanticSymbolRecord topParameter =
        SemanticFixtureRecordBuilder(QStringLiteral("TOP_P"),
                                     SymbolTaxonomy::DeclarationKind::Parameter)
            .withFile(topFile)
            .withLocalHandle(87004)
            .withRange(11, 15, 11, 20)
            .inModule(QStringLiteral("top"))
            .record();
    const SemanticSymbolRecord topLocalparam =
        SemanticFixtureRecordBuilder(QStringLiteral("TOP_LP"),
                                     SymbolTaxonomy::DeclarationKind::Localparam)
            .withFile(topFile)
            .withLocalHandle(87005)
            .withRange(12, 15, 12, 21)
            .inModule(QStringLiteral("top"))
            .record();
    const SemanticSymbolRecord topLogicSignal =
        SemanticFixtureRecordBuilder(QStringLiteral("data_bus"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(topFile)
            .withLocalHandle(87009)
            .withRange(13, 17, 13, 25)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .inModule(QStringLiteral("top"))
            .withType(QStringLiteral("logic [7:0]"))
            .record();
    const SemanticSymbolRecord topWireSignal =
        SemanticFixtureRecordBuilder(QStringLiteral("ready_w"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(topFile)
            .withLocalHandle(87010)
            .withRange(14, 14, 14, 21)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Wire)
            .inModule(QStringLiteral("top"))
            .record();
    const SemanticSymbolRecord childRegSignal =
        SemanticFixtureRecordBuilder(QStringLiteral("child_state"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(childFile)
            .withLocalHandle(87011)
            .withRange(31, 10, 31, 21)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Reg)
            .inModule(QStringLiteral("child"))
            .withType(QStringLiteral("reg [3:0]"))
            .record();
    const SemanticSymbolRecord topPortRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("clk"),
                                     SymbolTaxonomy::DeclarationKind::Port)
            .withFile(topFile)
            .withLocalHandle(87012)
            .withRange(11, 15, 11, 18)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .inModule(QStringLiteral("top"))
            .record();
    const SemanticSymbolRecord packageRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("cfg_pkg"),
                                     SymbolTaxonomy::DeclarationKind::Package)
            .withFile(packageFile)
            .withLocalHandle(87006)
            .withRange(5, 1, 10, 11)
            .record();
    const SemanticSymbolRecord packageParameter =
        SemanticFixtureRecordBuilder(QStringLiteral("PKG_P"),
                                     SymbolTaxonomy::DeclarationKind::Parameter)
            .withFile(packageFile)
            .withLocalHandle(87007)
            .withRange(6, 15, 6, 20)
            .inPackage(QStringLiteral("cfg_pkg"))
            .record();
    const SemanticSymbolRecord outsidePackage =
        SemanticFixtureRecordBuilder(QStringLiteral("outside_pkg"),
                                     SymbolTaxonomy::DeclarationKind::Package)
            .withFile(outsideFile)
            .withLocalHandle(87008)
            .withRange(10, 1, 14, 11)
            .record();
    QHash<QString, QString> contents;
    contents.insert(topFile,
                    QStringLiteral("// 1\n"
                                   "// 2\n"
                                   "// 3\n"
                                   "// 4\n"
                                   "// 5\n"
                                   "// 6\n"
                                   "// 7\n"
                                   "// 8\n"
                                   "// 9\n"
                                   "module top;\n"
                                   "  input logic clk;\n"
                                   "  parameter TOP_P = 1;\n"
                                   "  logic [7:0] data_bus;\n"
                                   "  wire ready_w;\n"
                                   "endmodule\n"));
    contents.insert(childFile,
                    QStringLiteral("module child;\n"
                                   "endmodule\n"));
    contents.insert(packageFile,
                    QStringLiteral("package cfg_pkg;\n"
                                   "  parameter PKG_P = 1;\n"
                                   "endpackage\n"));

    const auto snapshot =
        snapshotFromRecords({topModule,
                             childModule,
                             outsideModule,
                             topParameter,
                             topLocalparam,
                             topLogicSignal,
                             topWireSignal,
                             childRegSignal,
                             topPortRecord,
                             packageRecord,
                             packageParameter,
                             outsidePackage},
                            {},
                            {},
                            contents);
    ComModeService service;
    ComModePickerQuery moduleQuery;
    moduleQuery.snapshot = snapshot;
    moduleQuery.project = project.snapshot();
    const QList<ComModePickerItem> moduleItems =
        service.moduleItems(moduleQuery);
    bool sawTopModule = false;
    bool sawChildModule = false;
    bool sawOutsideModule = false;
    bool topPathIsRelative = false;
    for (const ComModePickerItem& item : moduleItems) {
        if (item.name == QStringLiteral("top")) {
            sawTopModule = true;
            topPathIsRelative = item.displayPath.endsWith(
                QDir::toNativeSeparators(QStringLiteral("rtl/top.sv")));
        }
        if (item.name == QStringLiteral("child"))
            sawChildModule = true;
        if (item.name == QStringLiteral("outside"))
            sawOutsideModule = true;
    }
    expectBool("com gm lists workspace modules from snapshot",
               sawTopModule && sawChildModule && !sawOutsideModule,
               true);
    expectBool("com gm displays relative module path",
               topPathIsRelative,
               true);

    moduleQuery.filter = QStringLiteral("chd");
    const QList<ComModePickerItem> filteredModuleItems =
        service.moduleItems(moduleQuery);
    expectBool("com gm supports fuzzy module matching",
               !filteredModuleItems.isEmpty()
                   && filteredModuleItems.first().name
                          == QStringLiteral("child"),
               true);

    ComModePickerQuery packageQuery;
    packageQuery.snapshot = snapshot;
    packageQuery.project = project.snapshot();
    const QList<ComModePickerItem> packageItems =
        service.packageItems(packageQuery);
    bool sawPackage = false;
    bool sawOutsidePackage = false;
    bool packagePathIsRelative = false;
    for (const ComModePickerItem& item : packageItems) {
        if (item.name == QStringLiteral("cfg_pkg")) {
            sawPackage = true;
            packagePathIsRelative = item.displayPath.endsWith(
                QDir::toNativeSeparators(QStringLiteral("rtl/cfg_pkg.sv")));
        }
        if (item.name == QStringLiteral("outside_pkg"))
            sawOutsidePackage = true;
    }
    expectBool("com gpk lists workspace packages from snapshot",
               sawPackage && !sawOutsidePackage,
               true);
    expectBool("com gpk displays relative package path",
               packagePathIsRelative,
               true);

    packageQuery.filter = QStringLiteral("cfg");
    const QList<ComModePickerItem> filteredPackageItems =
        service.packageItems(packageQuery);
    expectBool("com gpk supports fuzzy package matching",
               !filteredPackageItems.isEmpty()
                   && filteredPackageItems.first().name
                          == QStringLiteral("cfg_pkg"),
               true);

    ComModeScopedPickerQuery parameterQuery;
    parameterQuery.snapshot = snapshot;
    parameterQuery.project = project.snapshot();
    parameterQuery.fileName = topFile;
    parameterQuery.currentModuleName = QStringLiteral("top");
    parameterQuery.currentLine = 12;
    const ComModeScopedPickerResult moduleParameterResult =
        service.parameterItems(parameterQuery);
    bool sawTopParameter = false;
    bool sawTopLocalparam = false;
    bool sawPackageParameterInModule = false;
    bool moduleParameterHasScope = false;
    for (const ComModePickerItem& item : moduleParameterResult.items) {
        if (item.name == QStringLiteral("TOP_P")) {
            sawTopParameter = true;
            moduleParameterHasScope =
                item.scopeName == QStringLiteral("top");
        }
        if (item.name == QStringLiteral("TOP_LP"))
            sawTopLocalparam = true;
        if (item.name == QStringLiteral("PKG_P"))
            sawPackageParameterInModule = true;
    }
    expectBool("com gpa lists current module parameters",
               moduleParameterResult.hasScope
                   && sawTopParameter
                   && sawTopLocalparam
                   && !sawPackageParameterInModule,
               true);
    expectBool("com gpa parameter item shows scope",
               moduleParameterHasScope,
               true);

    parameterQuery.filter = QStringLiteral("lp");
    const ComModeScopedPickerResult filteredParameterResult =
        service.parameterItems(parameterQuery);
    expectBool("com gpa supports fuzzy parameter matching",
               filteredParameterResult.hasScope
                   && !filteredParameterResult.items.isEmpty()
                   && filteredParameterResult.items.first().name
                          == QStringLiteral("TOP_LP"),
               true);

    parameterQuery.filter.clear();
    parameterQuery.fileName = packageFile;
    parameterQuery.currentModuleName.clear();
    parameterQuery.currentLine = 6;
    const ComModeScopedPickerResult packageParameterResult =
        service.parameterItems(parameterQuery);
    bool sawPackageParameter = false;
    for (const ComModePickerItem& item : packageParameterResult.items) {
        if (item.name == QStringLiteral("PKG_P")
            && item.scopeName == QStringLiteral("cfg_pkg"))
            sawPackageParameter = true;
    }
    expectBool("com gpa lists current package parameters",
               packageParameterResult.hasScope && sawPackageParameter,
               true);

    parameterQuery.fileName = childFile;
    parameterQuery.currentLine = 20;
    const ComModeScopedPickerResult missingParameterScope =
        service.parameterItems(parameterQuery);
    expectBool("com gpa reports missing parameter scope",
               !missingParameterScope.hasScope
                   && missingParameterScope.message
                          == QStringLiteral("No current parameter scope"),
               true);

    ComModeScopedPickerQuery signalQuery;
    signalQuery.snapshot = snapshot;
    signalQuery.project = project.snapshot();
    signalQuery.fileName = topFile;
    signalQuery.currentModuleName = QStringLiteral("top");
    signalQuery.currentLine = 13;
    const ComModeScopedPickerResult signalResult =
        service.signalItems(signalQuery);
    bool sawDataBus = false;
    bool sawReadyWire = false;
    bool sawChildState = false;
    bool sawPortAsSignal = false;
    bool dataBusHasDetail = false;
    for (const ComModePickerItem& item : signalResult.items) {
        if (item.name == QStringLiteral("data_bus")) {
            sawDataBus = true;
            dataBusHasDetail =
                item.kind == ComModePickerItemKind::Signal
                && item.kindLabel == QStringLiteral("logic")
                && item.detailLabel == QStringLiteral("[7:0]");
        }
        if (item.name == QStringLiteral("ready_w")) {
            sawReadyWire =
                item.kindLabel == QStringLiteral("wire")
                && item.detailLabel == QStringLiteral("scalar");
        }
        if (item.name == QStringLiteral("child_state"))
            sawChildState = true;
        if (item.name == QStringLiteral("clk"))
            sawPortAsSignal = true;
    }
    expectBool("com gsd lists current module signals",
               signalResult.hasScope
                   && sawDataBus
                   && sawReadyWire
                   && !sawChildState
                   && !sawPortAsSignal,
               true);
    expectBool("com gsd signal item shows kind and width",
               dataBusHasDetail,
               true);

    signalQuery.filter = QStringLiteral("db");
    const ComModeScopedPickerResult filteredSignalResult =
        service.signalItems(signalQuery);
    expectBool("com gsd supports fuzzy signal matching",
               filteredSignalResult.hasScope
                   && !filteredSignalResult.items.isEmpty()
                   && filteredSignalResult.items.first().name
                          == QStringLiteral("data_bus"),
               true);

    signalQuery.fileName = packageFile;
    signalQuery.currentModuleName.clear();
    signalQuery.currentLine = 6;
    const ComModeScopedPickerResult missingSignalScope =
        service.signalItems(signalQuery);
    expectBool("com gsd reports missing current module",
               !missingSignalScope.hasScope
                   && missingSignalScope.message
                          == QStringLiteral("No current module"),
               true);

    signalQuery.fileName = childFile;
    signalQuery.currentModuleName = QStringLiteral("child");
    signalQuery.currentLine = 31;
    signalQuery.filter = QStringLiteral("no_match_signal");
    const ComModeScopedPickerResult emptySignalResult =
        service.signalItems(signalQuery);
    expectBool("com gsd keeps scope for empty signal results",
               emptySignalResult.hasScope && emptySignalResult.items.isEmpty(),
               true);

    ComModeRelativeLineQuery lineQuery;
    lineQuery.snapshot = snapshot;
    lineQuery.fileName = topFile;
    lineQuery.currentModuleName = QStringLiteral("top");
    lineQuery.currentLine = 12;
    lineQuery.requestedModuleLine = 3;
    ComModeRelativeLineResult lineResult =
        service.relativeLineTarget(lineQuery);
    expectBool("com g<num> resolves module-relative line",
               lineResult.ok
                   && lineResult.filePath == topFile
                   && lineResult.line == 12,
               true);

    lineQuery.requestedModuleLine = 1;
    lineResult = service.relativeLineTarget(lineQuery);
    expectBool("com g1 resolves module declaration line",
               lineResult.ok
                   && lineResult.line == 10
                   && lineResult.column == 1,
               true);

    lineQuery.requestedModuleLine = 6;
    lineResult = service.relativeLineTarget(lineQuery);
    expectBool("com g<num> rejects out of range module line",
               !lineResult.ok
                   && lineResult.message
                          == QStringLiteral("Module has only 5 lines"),
               true);

    lineQuery.requestedModuleLine = 0;
    lineResult = service.relativeLineTarget(lineQuery);
    expectBool("com g0 is invalid",
               !lineResult.ok
                   && lineResult.message
                          == QStringLiteral("Line number must be >= 1"),
               true);

    lineQuery.currentModuleName.clear();
    lineQuery.requestedModuleLine = 1;
    lineResult = service.relativeLineTarget(lineQuery);
    expectBool("com g<num> reports missing current module",
               !lineResult.ok
                   && lineResult.message == QStringLiteral("No current module"),
               true);

    QString registryError;
    expectBool("COM registry metadata is valid",
               comModeCommandRegistryIsValid(&registryError),
               true);
    const QStringList executableCommands = {
        QStringLiteral("gm"),
        QStringLiteral("gpk"),
        QStringLiteral("gpa"),
        QStringLiteral("gpo"),
        QStringLiteral("gpi"),
        QStringLiteral("gsd"),
        QStringLiteral("gsi"),
        QStringLiteral("gii"),
        QStringLiteral("gac"),
        QStringLiteral("gef"),
    };
    bool allExecutableCommandsRegistered = true;
    for (const QString& command : executableCommands) {
        const ComModeCommandMetadata* metadata =
            findComModeCommandMetadata(command);
        allExecutableCommandsRegistered =
            allExecutableCommandsRegistered
            && metadata
            && metadata->executable
            && metadata->inputKind == ComModeCommandInputKind::Fixed
            && executableComModeCommand(command) == command;
    }
    expectBool("COM registry lists existing executable commands",
               allExecutableCommandsRegistered,
               true);
    const QStringList prefixCommands = {
        QStringLiteral("gp"),
        QStringLiteral("gs"),
        QStringLiteral("gi"),
        QStringLiteral("ga"),
        QStringLiteral("ge"),
    };
    bool allPrefixesRegistered = true;
    for (const QString& command : prefixCommands) {
        const ComModeCommandMetadata* metadata =
            findComModeCommandMetadata(command);
        allPrefixesRegistered =
            allPrefixesRegistered
            && metadata
            && !metadata->executable
            && metadata->inputKind == ComModeCommandInputKind::Fixed
            && executableComModeCommand(command).isEmpty()
            && isComModeBufferPrefix(command);
    }
    expectBool("COM registry lists non-executable prefixes",
               allPrefixesRegistered,
               true);
    const ComModeCommandMetadata* relativeLineMetadata =
        findComModeCommandMetadata(QStringLiteral("g<num>"));
    expectBool("COM registry lists module-relative line command",
               relativeLineMetadata
                   && relativeLineMetadata->executable
                   && relativeLineMetadata->inputKind
                          == ComModeCommandInputKind::ModuleRelativeLine
                   && isComModeLineBuffer(QStringLiteral("g20"))
                   && isComModeBufferPrefix(QStringLiteral("g20")),
               true);
    expectBool("COM registry exposes executable command hint",
               comModeCommandHint(QStringLiteral("gm"))
                   .contains(QStringLiteral("module picker")),
               true);
    expectBool("COM registry exposes prefix child hints",
               comModeCommandHint(QStringLiteral("gp"))
                       .contains(QStringLiteral("gpa"))
                   && comModeCommandHint(QStringLiteral("gp"))
                          .contains(QStringLiteral("gpi"))
                   && comModeCommandHint(QStringLiteral("gp"))
                          .contains(QStringLiteral("gpk"))
                   && comModeCommandHint(QStringLiteral("gp"))
                          .contains(QStringLiteral("gpo")),
               true);
    expectBool("COM registry exposes relative line hint",
               comModeCommandHint(QStringLiteral("g20"))
                   .contains(QStringLiteral("Go module line")),
               true);
    QString registryValidationError;
    const QList<ComModeCommandMetadata> duplicateRegistry = {
        {QStringLiteral("gm"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("navigation"),
         QStringLiteral("Go module"),
         QStringLiteral("Open the module picker.")},
        {QStringLiteral("gm"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("navigation"),
         QStringLiteral("Go module duplicate"),
         QStringLiteral("Duplicate command.")},
    };
    expectBool("COM registry validation reports duplicate commands",
               !validateComModeCommandRegistry(duplicateRegistry,
                                               &registryValidationError)
                   && registryValidationError
                          .contains(QStringLiteral("duplicate COM command"))
                   && registryValidationError.contains(QStringLiteral("gm")),
               true);
    const QList<ComModeCommandMetadata> prefixConflictRegistry = {
        {QStringLiteral("g"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("navigation"),
         QStringLiteral("Go"),
         QStringLiteral("Executable short command.")},
        {QStringLiteral("gm"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("navigation"),
         QStringLiteral("Go module"),
         QStringLiteral("Open the module picker.")},
    };
    expectBool("COM registry validation reports executable prefix conflicts",
               !validateComModeCommandRegistry(prefixConflictRegistry,
                                               &registryValidationError)
                   && registryValidationError
                          .contains(QStringLiteral("prefix conflict"))
                   && registryValidationError.contains(QStringLiteral("g"))
                   && registryValidationError.contains(QStringLiteral("gm")),
               true);
    const QList<ComModeCommandMetadata> malformedPrefixRegistry = {
        {QStringLiteral("gp"),
         false,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("package-parameter-port"),
         QStringLiteral("P-family prefix"),
         QStringLiteral("Prefix without a child.")},
    };
    expectBool("COM registry validation reports malformed prefixes",
               !validateComModeCommandRegistry(malformedPrefixRegistry,
                                               &registryValidationError)
                   && registryValidationError
                          .contains(QStringLiteral("no registered child"))
                   && registryValidationError.contains(QStringLiteral("gp")),
               true);
    expectBool("COM failure message names incomplete prefixes",
               comModeCommandFailureMessage(QStringLiteral("gp"))
                       .contains(QStringLiteral("Incomplete COM command"))
                   && comModeCommandFailureMessage(QStringLiteral("gp"))
                          .contains(QStringLiteral("gpk")),
               true);
    expectBool("COM failure message names unknown buffers",
               comModeCommandFailureMessage(QStringLiteral("gx"))
                   .contains(QStringLiteral("gx")),
               true);

    MyCodeEditor insertEditor;
    insertEditor.resize(360, 120);
    insertEditor.show();
    insertEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    sendWidgetKey(&insertEditor, Qt::Key_QuoteLeft, QStringLiteral("`"));
    expectBool("insert mode backtick inserts text",
               insertEditor.toPlainText() == QStringLiteral("`")
                   && !insertEditor.comModeActive(),
               true);
    insertEditor.close();

    MyCodeEditor modeEditor;
    modeEditor.setPlainText(QStringLiteral("module top;\nendmodule\n"));
    modeEditor.resize(360, 120);
    modeEditor.show();
    modeEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    sendWidgetKey(&modeEditor, Qt::Key_Escape);
    expectBool("Esc still enters COM mode from editor focus",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer().isEmpty(),
               true);
    sendWidgetKey(&modeEditor, Qt::Key_G, QStringLiteral("g"));
    sendWidgetKey(&modeEditor, Qt::Key_2, QStringLiteral("2"));
    sendWidgetKey(&modeEditor, Qt::Key_0, QStringLiteral("0"));
    expectBool("COM shows g<num> buffer",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer() == QStringLiteral("g20"),
               true);
    sendWidgetKey(&modeEditor, Qt::Key_Escape);
    expectBool("Esc clears COM buffer without exiting",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer().isEmpty(),
               true);
    QSignalSpy comStatusSpy(&modeEditor,
                            &MyCodeEditor::editorStatusMessageRequested);
    sendWidgetKey(&modeEditor, Qt::Key_G, QStringLiteral("g"));
    sendWidgetKey(&modeEditor, Qt::Key_X, QStringLiteral("x"));
    expectBool("COM unknown input reports centralized failure message",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer().isEmpty()
                   && comStatusSpy.count() == 1
                   && comStatusSpy.takeFirst().at(0).toString()
                          == comModeCommandFailureMessage(
                              QStringLiteral("gx")),
               true);
    QSignalSpy comCommandSpy(&modeEditor,
                             &MyCodeEditor::comCommandRequested);
    sendWidgetKey(&modeEditor, Qt::Key_G, QStringLiteral("g"));
    sendWidgetKey(&modeEditor, Qt::Key_P, QStringLiteral("p"));
    expectBool("COM gp stays a non-executable prefix",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer() == QStringLiteral("gp")
                   && comCommandSpy.count() == 0,
               true);
    sendWidgetKey(&modeEditor, Qt::Key_K, QStringLiteral("k"));
    expectBool("COM gpk executes package picker command",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer().isEmpty()
                   && comCommandSpy.count() == 1
                   && comCommandSpy.takeFirst().at(0).toString()
                          == QStringLiteral("gpk"),
               true);
    sendWidgetKey(&modeEditor, Qt::Key_G, QStringLiteral("g"));
    sendWidgetKey(&modeEditor, Qt::Key_P, QStringLiteral("p"));
    sendWidgetKey(&modeEditor, Qt::Key_A, QStringLiteral("a"));
    expectBool("COM gpa executes parameter picker command",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer().isEmpty()
                   && comCommandSpy.count() == 1
                   && comCommandSpy.takeFirst().at(0).toString()
                          == QStringLiteral("gpa"),
               true);
    sendWidgetKey(&modeEditor, Qt::Key_G, QStringLiteral("g"));
    sendWidgetKey(&modeEditor, Qt::Key_P, QStringLiteral("p"));
    sendWidgetKey(&modeEditor, Qt::Key_O, QStringLiteral("o"));
    expectBool("COM gpo executes port append command",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer().isEmpty()
                   && comCommandSpy.count() == 1
                   && comCommandSpy.takeFirst().at(0).toString()
                          == QStringLiteral("gpo"),
               true);
    sendWidgetKey(&modeEditor, Qt::Key_G, QStringLiteral("g"));
    sendWidgetKey(&modeEditor, Qt::Key_P, QStringLiteral("p"));
    sendWidgetKey(&modeEditor, Qt::Key_I, QStringLiteral("i"));
    expectBool("COM gpi executes parameter insert command",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer().isEmpty()
                   && comCommandSpy.count() == 1
                   && comCommandSpy.takeFirst().at(0).toString()
                          == QStringLiteral("gpi"),
               true);
    sendWidgetKey(&modeEditor, Qt::Key_G, QStringLiteral("g"));
    sendWidgetKey(&modeEditor, Qt::Key_S, QStringLiteral("s"));
    expectBool("COM gs stays a non-executable prefix",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer() == QStringLiteral("gs")
                   && comCommandSpy.count() == 0,
               true);
    sendWidgetKey(&modeEditor, Qt::Key_D, QStringLiteral("d"));
    expectBool("COM gsd executes signal declaration picker command",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer().isEmpty()
                   && comCommandSpy.count() == 1
                   && comCommandSpy.takeFirst().at(0).toString()
                          == QStringLiteral("gsd"),
               true);
    sendWidgetKey(&modeEditor, Qt::Key_G, QStringLiteral("g"));
    sendWidgetKey(&modeEditor, Qt::Key_S, QStringLiteral("s"));
    expectBool("COM gs stays a prefix before signal insert",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer() == QStringLiteral("gs")
                   && comCommandSpy.count() == 0,
               true);
    sendWidgetKey(&modeEditor, Qt::Key_I, QStringLiteral("i"));
    expectBool("COM gsi executes signal insert command",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer().isEmpty()
                   && comCommandSpy.count() == 1
                   && comCommandSpy.takeFirst().at(0).toString()
                          == QStringLiteral("gsi"),
               true);
    sendWidgetKey(&modeEditor, Qt::Key_G, QStringLiteral("g"));
    sendWidgetKey(&modeEditor, Qt::Key_I, QStringLiteral("i"));
    expectBool("COM gi stays a non-executable prefix",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer() == QStringLiteral("gi")
                   && comCommandSpy.count() == 0,
               true);
    sendWidgetKey(&modeEditor, Qt::Key_I, QStringLiteral("i"));
    expectBool("COM gii executes instance insert command",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer().isEmpty()
                   && comCommandSpy.count() == 1
                   && comCommandSpy.takeFirst().at(0).toString()
                          == QStringLiteral("gii"),
               true);
    sendWidgetKey(&modeEditor, Qt::Key_G, QStringLiteral("g"));
    sendWidgetKey(&modeEditor, Qt::Key_A, QStringLiteral("a"));
    expectBool("COM ga stays a non-executable prefix",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer() == QStringLiteral("ga")
                   && comCommandSpy.count() == 0,
               true);
    sendWidgetKey(&modeEditor, Qt::Key_C, QStringLiteral("c"));
    expectBool("COM gac executes assign insert command",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer().isEmpty()
                   && comCommandSpy.count() == 1
                   && comCommandSpy.takeFirst().at(0).toString()
                          == QStringLiteral("gac"),
               true);
    sendWidgetKey(&modeEditor, Qt::Key_G, QStringLiteral("g"));
    sendWidgetKey(&modeEditor, Qt::Key_E, QStringLiteral("e"));
    expectBool("COM ge stays a non-executable prefix",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer() == QStringLiteral("ge")
                   && comCommandSpy.count() == 0,
               true);
    sendWidgetKey(&modeEditor, Qt::Key_F, QStringLiteral("f"));
    expectBool("COM gef executes module end insert command",
               modeEditor.comModeActive()
                   && modeEditor.comModeBuffer().isEmpty()
                   && comCommandSpy.count() == 1
                   && comCommandSpy.takeFirst().at(0).toString()
                          == QStringLiteral("gef"),
               true);
    sendWidgetKey(&modeEditor, Qt::Key_QuoteLeft, QStringLiteral("`"));
    expectBool("backtick exits COM mode",
               !modeEditor.comModeActive(),
               true);
    modeEditor.close();

    MyCodeEditor* activeEditor =
        window.tabManager ? window.tabManager->getCurrentEditor() : nullptr;
    if (activeEditor && window.comModeCoordinator) {
        if (QCompleter* completer = activeEditor->findChild<QCompleter*>())
            completer->popup()->hide();
        activeEditor->cancelFoldRegionMarkMode();
        activeEditor->cancelFoldShelfMode();
        if (activeEditor->comModeActive())
            activeEditor->exitComMode();
        QTextCursor cursor = activeEditor->textCursor();
        cursor.clearSelection();
        activeEditor->setTextCursor(cursor);
        activeEditor->setFocus();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        sendWidgetKey(activeEditor, Qt::Key_Escape);
        QLabel* strip = window.comModeCoordinator->commandStripWidget();
        expectBool("COM mode shows app command strip",
                   activeEditor->comModeActive()
                       && strip
                       && strip->isVisible()
                       && strip->text() == QStringLiteral("COM"),
                   true);

        sendWidgetKey(activeEditor, Qt::Key_G, QStringLiteral("g"));
        sendWidgetKey(activeEditor, Qt::Key_P, QStringLiteral("p"));
        expectBool("COM strip shows prefix child hints",
                   strip
                       && strip->text().contains(QStringLiteral("COM  gp"))
                       && strip->text().contains(QStringLiteral("gpa"))
                       && strip->text().contains(QStringLiteral("gpo")),
                   true);
        sendWidgetKey(activeEditor, Qt::Key_Escape);
        expectBool("COM prefix hint clears with Esc",
                   activeEditor->comModeActive()
                       && strip
                       && strip->text() == QStringLiteral("COM"),
                   true);

        sendWidgetKey(activeEditor, Qt::Key_G, QStringLiteral("g"));
        sendWidgetKey(activeEditor, Qt::Key_M, QStringLiteral("m"));
        ComModuleSelectorPanel* selector =
            window.comModeCoordinator->moduleSelectorPanel();
        expectBool("gm opens module selector",
                   selector && selector->isVisible(),
                   true);
        sendWidgetKey(selector, Qt::Key_Escape);
        expectBool("gm selector Esc returns to COM mode",
                   selector
                       && !selector->isVisible()
                       && activeEditor->comModeActive(),
                   true);

        sendWidgetKey(activeEditor, Qt::Key_G, QStringLiteral("g"));
        sendWidgetKey(activeEditor, Qt::Key_P, QStringLiteral("p"));
        sendWidgetKey(activeEditor, Qt::Key_K, QStringLiteral("k"));
        expectBool("gpk opens package selector",
                   selector && selector->isVisible(),
                   true);
        sendWidgetKey(selector && selector->searchEdit
                          ? static_cast<QWidget*>(selector->searchEdit)
                          : static_cast<QWidget*>(selector),
                      Qt::Key_QuoteLeft,
                      QStringLiteral("`"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("picker backtick exits COM mode",
                   selector
                       && !selector->isVisible()
                       && !activeEditor->comModeActive(),
                   true);

        sendWidgetKey(activeEditor, Qt::Key_Escape);
        expectBool("COM re-enters after picker backtick exit",
                   activeEditor->comModeActive(),
                   true);
        sendWidgetKey(activeEditor, Qt::Key_QuoteLeft, QStringLiteral("`"));
        expectBool("COM strip hides after backtick exit",
                   !activeEditor->comModeActive()
                       && strip
                       && !strip->isVisible(),
                   true);

        QLineEdit offEditorFocus(&window);
        offEditorFocus.setObjectName(QStringLiteral("comModeOffEditorFocus"));
        offEditorFocus.resize(120, 24);
        offEditorFocus.show();
        offEditorFocus.setFocus(Qt::ShortcutFocusReason);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        sendWidgetKey(&offEditorFocus, Qt::Key_Escape);
        QWidget* focusedAfterOffEditorEsc = QApplication::focusWidget();
        const bool focusReturnedToEditor =
            focusedAfterOffEditorEsc == activeEditor
            || activeEditor->isAncestorOf(focusedAfterOffEditorEsc);
        expectBool("Esc enters COM mode from non-editor focus",
                   activeEditor->comModeActive()
                       && focusReturnedToEditor
                       && strip
                       && strip->isVisible()
                       && strip->text() == QStringLiteral("COM"),
                   true);
        sendWidgetKey(activeEditor, Qt::Key_QuoteLeft, QStringLiteral("`"));
        offEditorFocus.close();
    }
}

static void runGlobalControlRegression(MainWindow& window,
                                       NavigationWidget* navWidget)
{
    printf("\n-- global control regression --\n");

    GlobalControlService service;
    const QList<GlobalControlItem> rootMatches =
        service.query(QString(),
                      window.workspaceManager->getProjectModel(),
                      SemanticIndex::getInstance());
    QSet<QString> rootDomainIds;
    QSet<QString> rootCommandIds;
    for (const GlobalControlItem& item : rootMatches) {
        if (item.kind == GlobalControlItemKind::Domain)
            rootDomainIds.insert(item.id);
        if (item.kind == GlobalControlItemKind::Command)
            rootCommandIds.insert(item.id);
    }
    expectBool("global control root shows command domains",
               rootDomainIds == QSet<QString>({
                   QStringLiteral("ow"),
                   QStringLiteral("fd"),
               }),
               true);
    expectBool("global control root hides direct commands",
               rootCommandIds.isEmpty(),
               true);

    const QList<GlobalControlItem> workspaceMatches =
        service.query(QStringLiteral("ow"),
                      window.workspaceManager->getProjectModel(),
                      SemanticIndex::getInstance());
    bool foundOpenOneWorkspaceAction = false;
    bool foundOpenTwoWorkspacesAction = false;
    bool foundDeprecatedWorkspaceAction = false;
    for (const GlobalControlItem& item : workspaceMatches) {
        if (item.id == QStringLiteral("ow 1")
            && item.kind == GlobalControlItemKind::Command)
            foundOpenOneWorkspaceAction = true;
        if (item.id == QStringLiteral("ow 2")
            && item.kind == GlobalControlItemKind::Command)
            foundOpenTwoWorkspacesAction = true;
        if (item.id == QStringLiteral("ow")
            || item.id == QStringLiteral("ow r"))
            foundDeprecatedWorkspaceAction = true;
    }
    expectBool("global control ow domain shows ow 1",
               foundOpenOneWorkspaceAction,
               true);
    expectBool("global control ow domain shows ow 2",
               foundOpenTwoWorkspacesAction,
               true);
    expectBool("global control ow domain hides deprecated commands",
               foundDeprecatedWorkspaceAction,
               false);

    const QList<GlobalControlItem> workspaceCountMatches =
        service.query(QStringLiteral("ow 3"),
                      window.workspaceManager->getProjectModel(),
                      SemanticIndex::getInstance());
    bool foundOpenThreeWorkspacesAction = false;
    for (const GlobalControlItem& item : workspaceCountMatches) {
        if (item.id == QStringLiteral("ow 3")
            && item.kind == GlobalControlItemKind::Command
            && item.subtitle.contains(QStringLiteral("Open 3 workspaces"))) {
            foundOpenThreeWorkspacesAction = true;
        }
    }
    expectBool("global control ow count command is parameterized",
               foundOpenThreeWorkspacesAction,
               true);

    const QList<GlobalControlItem> recentWorkspaceMatches =
        service.query(QStringLiteral("ow r"),
                      window.workspaceManager->getProjectModel(),
                      SemanticIndex::getInstance());
    bool foundRecentByExactCommand = false;
    bool foundWorkspaceCountHint = false;
    for (const GlobalControlItem& item : recentWorkspaceMatches) {
        if (item.id == QStringLiteral("ow r"))
            foundRecentByExactCommand = true;
        if (item.kind == GlobalControlItemKind::Domain
            && item.title == QStringLiteral("ow <num>"))
            foundWorkspaceCountHint = true;
    }
    expectBool("global control UI hides old ow r command",
               foundRecentByExactCommand,
               false);
    expectBool("global control ow r query gives count hint",
               foundWorkspaceCountHint,
               true);

    const QList<GlobalControlItem> foldActionMatches =
        service.query(QStringLiteral("fd"),
                      window.workspaceManager->getProjectModel(),
                      SemanticIndex::getInstance());
    bool foundFoldRegionAction = false;
    bool foundFoldShelfAction = false;
    bool foldActionsExplainBehavior = false;
    bool foundDeprecatedFoldAction = false;
    for (const GlobalControlItem& item : foldActionMatches) {
        if (item.id == QStringLiteral("fd r")) {
            foundFoldRegionAction = item.title == QStringLiteral("fd r")
                && item.subtitle.contains(QStringLiteral("Fold Region"));
        }
        if (item.id == QStringLiteral("fd s")) {
            foundFoldShelfAction = item.title == QStringLiteral("fd s")
                && item.subtitle.contains(QStringLiteral("Fold Shelf"));
        }
        if (item.id == QStringLiteral("fd")
            || item.id == QStringLiteral("fds"))
            foundDeprecatedFoldAction = true;
    }
    foldActionsExplainBehavior = foundFoldRegionAction && foundFoldShelfAction;
    expectBool("global control fd domain finds fold subcommands",
               foundFoldRegionAction && foundFoldShelfAction,
               true);
    expectBool("global control fold actions include explanations",
               foldActionsExplainBehavior,
               true);
    expectBool("global control fd domain hides deprecated commands",
               foundDeprecatedFoldAction,
               false);

    const QList<GlobalControlItem> fileMatches =
        service.query(QStringLiteral("SVH_interface"),
                      window.workspaceManager->getProjectModel(),
                      SemanticIndex::getInstance());
    bool foundFixtureFile = false;
    for (const GlobalControlItem& item : fileMatches) {
        if (item.kind == GlobalControlItemKind::File
            && item.title == QStringLiteral("SVH_interface.sv")) {
            foundFixtureFile = true;
        }
    }
    expectBool("global control omits workspace files",
               foundFixtureFile,
               false);

    bool dispatched = false;
    GlobalControlCoordinator dispatcherProbe(&window);
    dispatcherProbe.setActionHandler([&](const GlobalControlItem& item) {
        dispatched = item.id == QStringLiteral("ow 1");
    });
    dispatcherProbe.dispatch(
        GlobalControlItem{GlobalControlItemKind::Command,
                          QStringLiteral("ow 1"),
                          QStringLiteral("ow 1"),
                          QStringLiteral("Open 1 workspace")});
    expectBool("global control dispatches command",
               dispatched,
               true);

    window.globalControlCoordinator->dispatch(
        GlobalControlItem{GlobalControlItemKind::Command,
                          QStringLiteral("ow r"),
                          QStringLiteral("ow r"),
                          QStringLiteral("Recent Workspaces")});
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    QDialog* recentDialog =
        window.findChild<QDialog*>(QStringLiteral("recentWorkspacesDialog"));
    QTreeWidget* recentTree = recentDialog
        ? recentDialog->findChild<QTreeWidget*>(
              QStringLiteral("recentWorkspacesTree"))
        : nullptr;
    const QString activeAlias =
        window.workspaceManager ? window.workspaceManager->getWorkspaceAlias()
                                : QString();
    const QString activePath =
        window.workspaceManager ? window.workspaceManager->getWorkspacePath()
                                : QString();
    bool recentWindowShowsActiveWorkspace = false;
    if (recentTree) {
        for (int i = 0; i < recentTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = recentTree->topLevelItem(i);
            recentWindowShowsActiveWorkspace =
                recentWindowShowsActiveWorkspace
                || (item
                    && item->text(0) == activeAlias
                    && item->data(0, Qt::UserRole).toString() == activePath);
        }
    }
    expectBool("ow r opens recent workspace window",
               recentDialog && recentDialog->isVisible() && recentTree,
               true);
    expectBool("ow r window lists alias and path",
               recentWindowShowsActiveWorkspace,
               true);
    if (recentDialog)
        recentDialog->close();

    QWidget* focusTarget = navWidget ? static_cast<QWidget*>(navWidget) : &window;
    focusTarget->setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QKeyEvent ctrlSpace(QEvent::KeyPress,
                        Qt::Key_Space,
                        Qt::ControlModifier);
    qApp->notify(focusTarget, &ctrlSpace);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    expectBool("Ctrl+Space opens global control outside editor",
               window.globalControlCoordinator
                   && window.globalControlCoordinator->panel
                   && window.globalControlCoordinator->panel->isVisible(),
               true);

    if (window.globalControlCoordinator
        && window.globalControlCoordinator->panel) {
        bool panelShowsWorkspaceDomain = false;
        bool panelShowsFoldDomain = false;
        bool panelShowsRootCommand = false;
        for (const GlobalControlItem& item :
             window.globalControlCoordinator->panel->currentItems) {
            if (item.kind == GlobalControlItemKind::Domain
                && item.id == QStringLiteral("ow"))
                panelShowsWorkspaceDomain = true;
            if (item.kind == GlobalControlItemKind::Domain
                && item.id == QStringLiteral("fd"))
                panelShowsFoldDomain = true;
            if (item.kind == GlobalControlItemKind::Command)
                panelShowsRootCommand = true;
        }
        expectBool("global control panel root shows only domains",
                   panelShowsWorkspaceDomain && panelShowsFoldDomain
                       && !panelShowsRootCommand,
                   true);

        window.globalControlCoordinator->panel->searchEdit->setText(
            QStringLiteral("fd"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        bool panelShowsFoldRegion = false;
        bool panelShowsFoldShelf = false;
        for (const GlobalControlItem& item :
             window.globalControlCoordinator->panel->currentItems) {
            if (item.id == QStringLiteral("fd r")
                && item.subtitle.contains(QStringLiteral("Fold Region"))) {
                panelShowsFoldRegion = true;
            }
            if (item.id == QStringLiteral("fd s")
                && item.subtitle.contains(QStringLiteral("Fold Shelf"))) {
                panelShowsFoldShelf = true;
            }
        }
        expectBool("global control fd query shows fd r and fd s",
                   panelShowsFoldRegion && panelShowsFoldShelf,
                   true);
        window.globalControlCoordinator->panel->hide();
    }

    if (window.modeManager)
        window.modeManager->setMode(ModeManager::NormalMode);
    QKeyEvent shiftPressOne(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);
    QKeyEvent shiftReleaseOne(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
    QKeyEvent shiftPressTwo(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);
    QKeyEvent shiftReleaseTwo(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
    qApp->notify(&window, &shiftPressOne);
    qApp->notify(&window, &shiftReleaseOne);
    qApp->notify(&window, &shiftPressTwo);
    qApp->notify(&window, &shiftReleaseTwo);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    expectBool("double shift no longer changes app mode",
               window.modeManager
                   && window.modeManager->getCurrentMode()
                       == ModeManager::NormalMode,
               true);

    MyCodeEditor* activeEditor = window.tabManager
        ? window.tabManager->getCurrentEditor()
        : nullptr;
    if (activeEditor)
        activeEditor->cancelFoldShelfMode();
    window.globalControlCoordinator->dispatch(
        GlobalControlItem{GlobalControlItemKind::Command,
                          QStringLiteral("fd r"),
                          QStringLiteral("fd r"),
                          QStringLiteral("Fold Region")});
    expectBool("global control fd r starts fold region mode",
               activeEditor && activeEditor->foldRegionMarkModeActive(),
               true);
    if (activeEditor)
        activeEditor->cancelFoldRegionMarkMode();

    window.globalControlCoordinator->dispatch(
        GlobalControlItem{GlobalControlItemKind::Command,
                          QStringLiteral("fd s"),
                          QStringLiteral("fd s"),
                          QStringLiteral("Fold Shelf")});
    QDockWidget* foldShelfDock =
        window.findChild<QDockWidget*>(QStringLiteral("FoldShelfDock"));
    expectBool("global control fd s starts fold shelf mode",
               activeEditor
                   && activeEditor->foldShelfModeActive()
                   && foldShelfDock
                   && foldShelfDock->isVisible(),
               true);
    if (activeEditor)
        activeEditor->cancelFoldShelfMode();
    if (foldShelfDock)
        foldShelfDock->hide();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    runActivityLogServiceRegression();
    runRtlInsightsOnDemandRegression();
    runEditorAppearanceSettingsRegression();
    runFormatterSettingsRegression();
    runEditorBracketRangeRegression();
    runEditorSmartSelectionRegression();
    runEditorOccurrenceNavigationRegression();
    runEditorSafeRenameRegression();
    runSafeRenameCoordinatorRegression();
    runSafeRenameCreateDefinitionRegression();
    runEditorColumnEditRegression();
    runEditorLineActionRegression();
    runEditorCtrlClickNavigationRegression();
    runSignalKernelGraphPopupInteractionRegression();
    runEditorFormatterRegression();
    runEditorAppearanceCoordinatorRegression();
    runFormatterCoordinatorRegression();
    runTabOpenDedupRegression();
    runWorkspaceCloseRegression();
    runWorkspaceCachedSwitchRegression();
    runWorkspaceAliasRenameRegression();
    runIncludeCompletionRegression();
    runTreeSitterFoldingProviderRegression();
    runNavigationDesignCacheWorkspaceActivationRegression();
    runNavigationHierarchyModelRegression();

    const QString workspacePath = (argc > 1)
        ? QString::fromLocal8Bit(argv[1])
        : QDir::current().absoluteFilePath(QStringLiteral("test_sv/new"));
    const QString symbolFixturePath = (argc > 2)
        ? QString::fromLocal8Bit(argv[2])
        : QDir::current().absoluteFilePath(QStringLiteral("test_sv/test_symbols.sv"));
    const QString normalizedSymbolFixturePath =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(symbolFixturePath).absoluteFilePath()));

    expectBool("workspace fixture exists", QFileInfo(workspacePath).isDir(), true);
    expectBool("symbol fixture exists", QFileInfo(symbolFixturePath).isFile(), true);
    runEditorHoverPreviewRegression(workspacePath);

    MainWindow window;
    bool workspaceSymbolsDone = false;
    bool workspaceFilesScanned = false;
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::workspaceSymbolAnalysisFinished,
                     &window,
                     [&](const ProjectSnapshot&, int filesAnalyzed, int totalSymbols) {
                         Q_UNUSED(filesAnalyzed)
                         Q_UNUSED(totalSymbols)
                         workspaceSymbolsDone = true;
                     });
    QObject::connect(window.workspaceManager.get(),
                     &WorkspaceManager::filesScanned,
                     &window,
                     [&](const QStringList&) {
                         workspaceFilesScanned = true;
                     });

    window.resize(1100, 760);
    window.show();
    expectBool("main window visible", waitUntil([&]() { return window.isVisible(); }, 2000), true);
    expectBool("workspace tab bar has close action",
               window.workspaceTabBar
                   && window.workspaceTabBar->tabsClosable()
                   && window.workspaceTabBar->contextMenuPolicy()
                          == Qt::CustomContextMenu,
               true);
    expectBool("activity output panel exists",
               window.findChild<QPlainTextEdit*>(
                   QStringLiteral("activityOutputText")) != nullptr,
               true);
    QDockWidget* foldShelfDock =
        window.findChild<QDockWidget*>(QStringLiteral("FoldShelfDock"));
    expectBool("fold shelf dock exists",
               foldShelfDock != nullptr,
               true);
    expectBool("fold shelf dock starts hidden",
               foldShelfDock && !foldShelfDock->isVisible(),
               true);
    QMenu* viewMenu = window.findChild<QMenu*>(QStringLiteral("viewMenu"));
    expectBool("view menu exists", viewMenu != nullptr, true);
    QToolButton* panelsStatusButton =
        window.findChild<QToolButton*>(QStringLiteral("panelsStatusButton"));
    expectBool("panels status button exists",
               panelsStatusButton && panelsStatusButton->menu() == viewMenu,
               true);
    QAction* viewFoldShelfAction =
        window.findChild<QAction*>(QStringLiteral("viewFoldShelfAction"));
    expectBool("view menu has fold shelf action",
               viewFoldShelfAction != nullptr,
               true);
    if (viewFoldShelfAction) {
        viewFoldShelfAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("view menu reopens fold shelf",
               foldShelfDock && foldShelfDock->isVisible(),
               true);
    if (foldShelfDock)
        foldShelfDock->hide();

    QDockWidget* activityDock =
        window.findChild<QDockWidget*>(QStringLiteral("activityDock"));
    QAction* viewActivityAction =
        window.findChild<QAction*>(QStringLiteral("viewActivityAction"));
    expectBool("view menu has activity action",
               activityDock && viewActivityAction,
               true);
    if (activityDock)
        activityDock->hide();
    if (viewActivityAction) {
        viewActivityAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("view menu reopens activity panel",
               activityDock && activityDock->isVisible(),
               true);

    QDockWidget* signalKernelGraphDock =
        window.findChild<QDockWidget*>(QStringLiteral("signalKernelGraphDock"));
    QAction* viewSignalKernelGraphAction =
        window.findChild<QAction*>(
            QStringLiteral("viewSignalKernelGraphAction"));
    expectBool("signal kernel graph dock exists",
               signalKernelGraphDock != nullptr,
               true);
    expectBool("signal kernel graph dock starts hidden",
               signalKernelGraphDock && !signalKernelGraphDock->isVisible(),
               true);
    expectBool("view menu has signal kernel graph action",
               signalKernelGraphDock && viewSignalKernelGraphAction,
               true);
    if (viewSignalKernelGraphAction) {
        viewSignalKernelGraphAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("view menu reopens signal kernel graph",
               signalKernelGraphDock && signalKernelGraphDock->isVisible(),
               true);
    if (signalKernelGraphDock)
        signalKernelGraphDock->hide();

    QDockWidget* wavePreviewDock =
        window.findChild<QDockWidget*>(QStringLiteral("wavePreviewDock"));
    QAction* viewWavePreviewAction =
        window.findChild<QAction*>(QStringLiteral("viewWavePreviewAction"));
    expectBool("wave preview dock exists",
               wavePreviewDock != nullptr,
               true);
    expectBool("wave preview dock starts hidden",
               wavePreviewDock && !wavePreviewDock->isVisible(),
               true);
    expectBool("view menu has wave preview action",
               wavePreviewDock && viewWavePreviewAction,
               true);
    if (viewWavePreviewAction) {
        viewWavePreviewAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("view menu reopens wave preview",
               wavePreviewDock && wavePreviewDock->isVisible(),
               true);
    if (wavePreviewDock)
        wavePreviewDock->hide();

    QAction* resetPanelLayoutAction =
        window.findChild<QAction*>(QStringLiteral("resetPanelLayoutAction"));
    expectBool("view menu has reset layout action",
               resetPanelLayoutAction != nullptr,
               true);
    if (window.navigationPane && window.navigationPane->dock())
        window.navigationPane->dock()->hide();
    if (activityDock)
        activityDock->hide();
    if (signalKernelGraphDock)
        signalKernelGraphDock->hide();
    if (wavePreviewDock)
        wavePreviewDock->hide();
    if (resetPanelLayoutAction) {
        resetPanelLayoutAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("reset panel layout reopens navigation",
               window.navigationPane
                   && window.navigationPane->dock()
                   && window.navigationPane->dock()->isVisible(),
               true);
    expectBool("reset panel layout reopens activity",
               activityDock && activityDock->isVisible(),
               true);
    expectBool("reset panel layout reopens signal kernel graph",
               signalKernelGraphDock && signalKernelGraphDock->isVisible(),
               true);
    expectBool("reset panel layout reopens wave preview",
               wavePreviewDock && wavePreviewDock->isVisible(),
               true);

    QAction* newFileAction = window.findChild<QAction*>(QStringLiteral("new_file"));
    const int editorCountBeforeNewAction = window.tabManager->editorCount();
    if (newFileAction) {
        newFileAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("file action routes through coordinator",
               newFileAction
                   && window.tabManager->editorCount()
                       == editorCountBeforeNewAction + 1,
               true);
    MyCodeEditor* waveEditor = window.tabManager->getCurrentEditor();
    QTemporaryDir wavePreviewNavDir;
    expectBool("wave preview nav temp dir valid",
               wavePreviewNavDir.isValid(),
               true);
    const QString wavePreviewNavPath =
        wavePreviewNavDir.isValid()
            ? QDir::toNativeSeparators(
                  wavePreviewNavDir.filePath(QStringLiteral("wave_ui.sv")))
            : QString();
    if (waveEditor && window.tabManager->getDocumentModel()
        && !wavePreviewNavPath.isEmpty()) {
        window.tabManager->getDocumentModel()->setDocumentFileName(
            waveEditor,
            wavePreviewNavPath);
    }
    if (waveEditor) {
        waveEditor->setPlainText(
            QStringLiteral("module wave_ui;\n"
                           "logic clk;\n"
                           "input logic [7:0] data;\n"
                           "logic [7:0] q;\n"
                           "logic [7:0] loop_q;\n"
                           "logic [7:0] out;\n"
                           "assign out = q + data;\n"
                           "always_ff @(posedge clk) begin\n"
                           "    if (data[0]) q <= data;\n"
                           "end\n"
                           "always_comb begin\n"
                           "    for (int i = 0; i < 2; i++) begin\n"
                           "        loop_q = data;\n"
                           "    end\n"
                           "end\n"
                           "endmodule\n"));
    }
    if (wavePreviewDock && !wavePreviewDock->isVisible()
        && viewWavePreviewAction) {
        viewWavePreviewAction->trigger();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTreeWidget* waveTree = wavePreviewTree(window);
    bool sawWaveQ = false;
    bool sawWaveOut = false;
    bool sawWaveClockReset = false;
    bool sawWaveGuard = false;
    bool sawWaveLoopGuard = false;
    bool sawWaveContext = false;
    bool sawWaveLaneGuardSummary = false;
    bool sawWaveLaneSummary = false;
    bool sawWaveActivityMix = false;
    bool sawWaveformTrace = false;
    QTreeWidgetItem* waveQEventItem = nullptr;
    QTreeWidgetItem* waveQLaneItem = nullptr;
    if (waveTree) {
        for (int i = 0; i < waveTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* laneItem = waveTree->topLevelItem(i);
            const QString name = laneItem->text(0);
            sawWaveQ = sawWaveQ || name == QStringLiteral("q");
            sawWaveOut = sawWaveOut || name == QStringLiteral("out");
            sawWaveClockReset = sawWaveClockReset
                || name == QStringLiteral("Clock/Reset Groups");
            if (name == QStringLiteral("Activity Mix")) {
                sawWaveActivityMix = laneItem->text(1).contains(
                                         QStringLiteral("3 events"))
                    && laneItem->text(4)
                        == QStringLiteral("assign 1/comb 1/seq 1")
                    && laneItem->toolTip(0).contains(
                        QStringLiteral("activity: assign 1/comb 1/seq 1"));
            }
            if (name == QStringLiteral("Waveform Trace"))
                sawWaveformTrace = true;
            if (name == QStringLiteral("q")) {
                waveQLaneItem = laneItem;
                sawWaveLaneSummary = sawWaveLaneSummary
                    || (laneItem->text(1).contains(QStringLiteral("1 event"))
                        && laneItem->text(1).contains(QStringLiteral("1 src"))
                        && laneItem->text(1).contains(QStringLiteral("max t+1"))
                        && laneItem->text(1).contains(QStringLiteral("1 block"))
                        && laneItem->text(1).contains(QStringLiteral("seq 1"))
                        && laneItem->text(4) == QStringLiteral("seq 1"));
                sawWaveContext = sawWaveContext
                    || laneItem->text(5) == QStringLiteral("internal logic [7:0]");
                sawWaveLaneGuardSummary = sawWaveLaneGuardSummary
                    || (laneItem->text(3) == QStringLiteral("if data[0]")
                        && laneItem->toolTip(0).contains(
                            QStringLiteral("guards: if data[0]")));
                for (int child = 0; child < laneItem->childCount(); ++child) {
                    sawWaveClockReset = sawWaveClockReset
                        || laneItem->child(child)->text(2)
                            == QStringLiteral("clk posedge clk");
                    sawWaveGuard = sawWaveGuard
                        || laneItem->child(child)->text(3)
                            == QStringLiteral("if data[0]");
                    sawWaveContext = sawWaveContext
                        || laneItem->child(child)->text(5)
                            == QStringLiteral("internal logic [7:0]");
                    if (laneItem->child(child)->text(0).contains(
                            QStringLiteral("q = data"))) {
                        waveQEventItem = laneItem->child(child);
                    }
                }
            }
            if (name == QStringLiteral("loop_q")) {
                for (int child = 0; child < laneItem->childCount(); ++child) {
                    sawWaveLoopGuard = sawWaveLoopGuard
                        || laneItem->child(child)->text(3)
                            == QStringLiteral("for int i = 0; i < 2; i++");
                }
            }
        }
    }
    expectBool("wave preview renders active editor lanes",
               waveTree && sawWaveQ && sawWaveOut,
               true);
    expectBool("wave preview renders clock/reset groups",
               waveTree && sawWaveClockReset,
               true);
    expectBool("wave preview renders guard labels",
               waveTree && sawWaveGuard,
               true);
    expectBool("wave preview renders lane guard summaries",
               waveTree && sawWaveLaneGuardSummary,
               true);
    expectBool("wave preview renders loop guard labels",
               waveTree && sawWaveLoopGuard,
               true);
    expectBool("wave preview renders declaration context",
               waveTree && sawWaveContext,
               true);
    expectBool("wave preview renders lane summary",
               waveTree && sawWaveLaneSummary,
               true);
    expectBool("wave preview renders activity mix summary",
               waveTree && sawWaveActivityMix,
               true);
    expectBool("wave preview renders local waveform trace",
               waveTree && sawWaveformTrace,
               true);
    const QString waveQLaneTooltip =
        waveQLaneItem ? waveQLaneItem->toolTip(0) : QString();
    expectBool("wave preview lane tooltip has summary",
               waveQLaneTooltip.contains(QStringLiteral("summary: 1 event"))
                   && waveQLaneTooltip.contains(QStringLiteral("activity: seq 1")),
               true);
    const QString waveQTooltip =
        waveQEventItem ? waveQEventItem->toolTip(0) : QString();
    expectBool("wave preview event tooltip has source target details",
               waveQTooltip.contains(QStringLiteral("target: q"))
                   && waveQTooltip.contains(QStringLiteral("expression: data"))
                   && waveQTooltip.contains(QStringLiteral("sources: data"))
                   && waveQTooltip.contains(QStringLiteral("timing: t+1 cycle"))
                   && waveQTooltip.contains(QStringLiteral("clock/reset: clk posedge clk"))
                   && waveQTooltip.contains(QStringLiteral("target context: internal logic [7:0]"))
                   && waveQTooltip.contains(QStringLiteral("source context: data: input logic [7:0]"))
                   && waveQTooltip.contains(QStringLiteral("location:")),
               true);
    QLabel* waveSummary = wavePreviewSummaryLabel(window);
    expectBool("wave preview report summary shows activity mix",
               waveSummary
                   && waveSummary->text().contains(
                       QStringLiteral("activity assign 1/comb 1/seq 1")),
               true);
    QWidget* waveCanvas = wavePreviewCanvas(window);
    expectBool("wave preview canvas exists",
               waveCanvas != nullptr,
               true);
    expectBool("wave preview canvas renders sketch",
               renderedWidgetHasColorVariation(waveCanvas),
               true);
    if (waveCanvas) {
        const int labelWidth =
            std::min(130, std::max(84, waveCanvas->width() / 4));
        const QPoint traceLanePoint(10 + qMin(32, labelWidth - 12),
                                    28 + 16);
        QTest::mouseMove(waveCanvas, traceLanePoint);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::mouseClick(waveCanvas,
                          Qt::LeftButton,
                          Qt::NoModifier,
                          traceLanePoint);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("wave preview canvas hover has waveform details",
               waveCanvas
                   && waveCanvas->toolTip().contains(
                       QStringLiteral("waveform signal:")),
               true);
    expectBool("wave preview canvas click selects waveform summary",
               waveSummary
                   && waveSummary->text().contains(
                       QStringLiteral("Selected waveform"))
                   && waveSummary->text().contains(
                       QStringLiteral("values")),
               true);
    if (waveEditor) {
        const QString waveSelectionText = waveEditor->toPlainText();
        const int combStart =
            waveSelectionText.indexOf(QStringLiteral("always_comb"));
        const int combEnd =
            combStart >= 0
                ? waveSelectionText.indexOf(QStringLiteral("endmodule"),
                                            combStart)
                : -1;
        if (combStart >= 0 && combEnd > combStart) {
            QTextCursor scopeCursor(waveEditor->document());
            scopeCursor.setPosition(combStart);
            scopeCursor.setPosition(combEnd, QTextCursor::KeepAnchor);
            waveEditor->setTextCursor(scopeCursor);
        }
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    bool sawScopedLoopQ = false;
    bool sawScopedQ = false;
    bool sawScopedOut = false;
    if (waveTree) {
        for (int i = 0; i < waveTree->topLevelItemCount(); ++i) {
            const QString name = waveTree->topLevelItem(i)->text(0);
            sawScopedLoopQ = sawScopedLoopQ
                || name == QStringLiteral("loop_q");
            sawScopedQ = sawScopedQ || name == QStringLiteral("q");
            sawScopedOut = sawScopedOut || name == QStringLiteral("out");
        }
    }
    expectBool("wave preview selected scope filters lanes",
               waveTree
                   && sawScopedLoopQ
                   && !sawScopedQ
                   && !sawScopedOut
                   && waveSummary
                   && waveSummary->text().contains(
                       QStringLiteral("selected lines"))
                   && waveSummary->text().contains(
                       QStringLiteral("local waveform")),
               true);
    if (waveEditor) {
        QTextCursor clearScopeCursor(waveEditor->document());
        clearScopeCursor.movePosition(QTextCursor::Start);
        waveEditor->setTextCursor(clearScopeCursor);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    bool sawRestoredQ = false;
    bool sawRestoredOut = false;
    if (waveTree) {
        for (int i = 0; i < waveTree->topLevelItemCount(); ++i) {
            const QString name = waveTree->topLevelItem(i)->text(0);
            sawRestoredQ = sawRestoredQ || name == QStringLiteral("q");
            sawRestoredOut = sawRestoredOut || name == QStringLiteral("out");
        }
    }
    expectBool("wave preview clearing selection restores full file",
               waveTree && sawRestoredQ && sawRestoredOut,
               true);
    if (waveEditor) {
        waveEditor->setPlainText(
            QStringLiteral("module wave_warning_ui;\n"
                           "logic clk;\n"
                           "logic a;\n"
                           "logic q;\n"
                           "assign q = a;\n"
                           "always_ff @(posedge clk) q <= a;\n"
                           "always_comb q = a;\n"
                           "endmodule\n"));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    bool sawWaveWarnings = false;
    bool sawWaveMixedWarning = false;
    bool sawWaveMultiBlockWarning = false;
    bool sawWaveWarningLane = false;
    if (waveTree) {
        for (int i = 0; i < waveTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* root = waveTree->topLevelItem(i);
            if (root && root->text(0) == QStringLiteral("q")) {
                sawWaveWarningLane =
                    root->text(1).contains(QStringLiteral("2 warnings"))
                    && root->toolTip(0).contains(
                        QStringLiteral("warnings: signal q mixes assign/comb/seq activity"))
                    && root->toolTip(0).contains(
                        QStringLiteral("assigned from 2 procedural blocks"));
            }
            if (!root || root->text(0) != QStringLiteral("Warnings"))
                continue;
            sawWaveWarnings =
                root->text(1) == QStringLiteral("2 warnings")
                && root->toolTip(0).contains(
                    QStringLiteral("signal q mixes assign/comb/seq activity"));
            for (int child = 0; child < root->childCount(); ++child) {
                QTreeWidgetItem* row = root->child(child);
                sawWaveMixedWarning = sawWaveMixedWarning
                    || (row && row->text(0).contains(
                            QStringLiteral(
                                "Wave Preview does not resolve writer priority")));
                sawWaveMultiBlockWarning = sawWaveMultiBlockWarning
                    || (row && row->text(0).contains(
                            QStringLiteral(
                                "assigned from 2 procedural blocks")));
            }
        }
    }
    expectBool("wave preview renders lane warnings",
               waveTree
                   && sawWaveWarnings
                   && sawWaveMixedWarning
                   && sawWaveMultiBlockWarning
                   && sawWaveWarningLane,
               true);
    expectBool("wave preview summary shows warnings",
               waveSummary
                   && waveSummary->text().contains(QStringLiteral("2 warnings")),
               true);
    if (waveEditor) {
        waveEditor->setPlainText(
            QStringLiteral("module wave_ui;\n"
                           "logic clk;\n"
                           "logic [7:0] q;\n"
                           "logic [7:0] z;\n"
                           "assign z = q;\n"
                           "always_ff @(posedge clk) begin\n"
                           "    q <= z;\n"
                           "end\n"
                           "endmodule\n"));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    bool sawWaveZ = false;
    if (waveTree) {
        for (int i = 0; i < waveTree->topLevelItemCount(); ++i)
            sawWaveZ = sawWaveZ
                || waveTree->topLevelItem(i)->text(0) == QStringLiteral("z");
    }
    expectBool("wave preview refreshes dirty editor text",
               waveTree && sawWaveZ,
               true);
    if (waveEditor) {
        QString largeWaveText;
        largeWaveText.reserve(150 * 1024);
        largeWaveText += QStringLiteral("module wave_large;\n"
                                        "logic clk;\n"
                                        "logic [7:0] source;\n"
                                        "logic [7:0] huge_delayed;\n");
        for (int i = 0; i < 5000; ++i)
            largeWaveText += QStringLiteral("logic [7:0] filler_%1;\n").arg(i);
        largeWaveText += QStringLiteral("always_ff @(posedge clk) begin\n"
                                        "    huge_delayed <= source;\n"
                                        "end\n"
                                        "endmodule\n");
        waveEditor->setPlainText(largeWaveText);
    }
    expectBool("wave preview queues large dirty refresh",
               waveTree && findItemByText(waveTree,
                                           QStringLiteral("huge_delayed"))
                               == nullptr,
               true);
    QTest::qWait(260);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("wave preview flushes latest large dirty refresh",
               waveTree && findItemByText(waveTree,
                                           QStringLiteral("huge_delayed"))
                               != nullptr,
               true);
    QLabel* editorModeChip =
        window.findChild<QLabel*>(QStringLiteral("editorModeChip"));
    expectBool("editor mode chip exists",
               editorModeChip != nullptr && !editorModeChip->isVisible(),
               true);
    MyCodeEditor* modeChipEditor = window.tabManager->getCurrentEditor();
    if (modeChipEditor)
        modeChipEditor->startFoldShelfMode();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("fold shelf mode shows status chip",
               editorModeChip
                   && editorModeChip->isVisible()
                   && editorModeChip->text().contains(QStringLiteral("Fold Shelf")),
               true);
    expectBool("fold shelf mode highlights shelf panel",
               window.foldShelfPanel && window.foldShelfPanel->shelfModeActive(),
               true);
    if (modeChipEditor)
        modeChipEditor->cancelFoldShelfMode();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("fold shelf mode hides status chip on cancel",
               editorModeChip && !editorModeChip->isVisible(),
               true);
    QTemporaryDir saveDir;
    expectBool("save temp dir valid", saveDir.isValid(), true);
    MyCodeEditor* saveEditor = window.tabManager->getCurrentEditor();
    expectBool("save editor exists", saveEditor != nullptr, true);
    if (saveDir.isValid() && saveEditor) {
        const QString savePath = saveDir.filePath(QStringLiteral("saved_tab.sv"));
        QFile seedFile(savePath);
        expectBool("save target seed opens",
                   seedFile.open(QIODevice::WriteOnly | QFile::Text),
                   true);
        seedFile.close();

        const QString savedText =
            QStringLiteral("module saved_tab;\nendmodule\n");
        DocumentModel* saveDocuments = window.tabManager->getDocumentModel();
        if (saveDocuments)
            saveDocuments->setDocumentFileName(saveEditor, savePath);
        saveEditor->setPlainText(savedText);
        expectBool("document model caches save editor text",
                   saveDocuments
                       && saveDocuments->documentTextForEditor(saveEditor) == savedText,
                   true);
        QSignalSpy fileSavedSpy(window.tabManager.get(), &TabManager::fileSaved);
        QSignalSpy documentSavedSpy(
            saveDocuments,
            &DocumentModel::documentSaved);
        expectBool("tab manager saves current tab",
                   window.tabManager->saveCurrentTab(),
                   true);
        QFile savedFile(savePath);
        expectBool("saved file reopens",
                   savedFile.open(QIODevice::ReadOnly | QFile::Text),
                   true);
        const QString savedFileText = QTextStream(&savedFile).readAll();
        savedFile.close();
        expectBool("tab manager writes editor text",
                   savedFileText == savedText,
                   true);
        expectBool("tab manager marks document saved",
                   saveDocuments
                       && saveDocuments->documentForEditor(saveEditor).saved,
                   true);
        expectBool("tab manager emits fileSaved",
                   fileSavedSpy.count() == 1,
                   true);
        expectBool("document model emits one saved snapshot",
                   documentSavedSpy.count() == 1,
                   true);
        const DocumentSnapshot savedDoc =
            window.tabManager->getDocumentModel()
                ? window.tabManager->getDocumentModel()->documentForEditor(saveEditor)
                : DocumentSnapshot();
        expectBool("document model marks saved tab clean",
                   savedDoc.saved && !savedDoc.dirty,
                   true);
        expectBool("document model records saved version",
                   savedDoc.savedTextVersion == savedDoc.textVersion,
                   true);

        const QString shortcutSavedText =
            QStringLiteral("module shortcut_saved_tab;\nendmodule\n");
        saveEditor->setPlainText(shortcutSavedText);
        saveEditor->setFocus();
        QTest::keyClick(saveEditor, Qt::Key_S, Qt::ControlModifier);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QFile shortcutSavedFile(savePath);
        expectBool("ctrl-s save file reopens",
                   shortcutSavedFile.open(QIODevice::ReadOnly | QFile::Text),
                   true);
        const QString shortcutSavedFileText =
            QTextStream(&shortcutSavedFile).readAll();
        shortcutSavedFile.close();
        expectBool("ctrl-s saves current editor with focus",
                   shortcutSavedFileText == shortcutSavedText,
                   true);
        expectBool("ctrl-s emits fileSaved",
                   fileSavedSpy.count() == 2,
                   true);
        expectBool("ctrl-s emits documentSaved",
                   documentSavedSpy.count() == 2,
                   true);

        const QString formatOnSaveText =
            QStringLiteral("module format_save;\n"
                           "logic [7:0] data;\n"
                           "logic valid;\n"
                           "endmodule\n");
        const QString formattedOnSaveText =
            QStringLiteral("module format_save;\n"
                           "    logic [7:0] data;\n"
                           "    logic       valid;\n"
                           "endmodule\n");
        if (window.formatterSettings) {
            window.formatterSettings->setProfile(
                FormatterProfile::Structured);
            window.formatterSettings->setFormatOnSaveEnabled(true);
        }
        saveEditor->setPlainText(formatOnSaveText);
        expectBool("format-on-save enabled on save editor",
                   saveEditor->formatOnSaveEnabled(),
                   true);
        expectBool("tab manager format-on-save saves current tab",
                   window.tabManager->saveCurrentTab(),
                   true);
        QFile formattedFile(savePath);
        expectBool("format-on-save file reopens",
                   formattedFile.open(QIODevice::ReadOnly | QFile::Text),
                   true);
        const QString formattedFileText =
            QTextStream(&formattedFile).readAll();
        formattedFile.close();
        expectBool("format-on-save writes formatted text",
                   formattedFileText == formattedOnSaveText,
                   true);
        expectBool("format-on-save updates editor text",
                   saveEditor->toPlainText() == formattedOnSaveText,
                   true);
        expectBool("format-on-save updates document model text",
                   saveDocuments
                       && saveDocuments->documentTextForEditor(saveEditor)
                           == formattedOnSaveText,
                   true);
    }

    const bool workspaceOpened = window.workspaceManager->openWorkspace(workspacePath);
    expectBool("open workspace", workspaceOpened, true);
    expectBool("workspace file scan completes",
               waitUntil([&]() { return workspaceFilesScanned; }, 10000),
               true);
    bool sawWorkspaceActivity = false;
    for (const ActivityLogEvent& event : ActivityLogService::getInstance()->events()) {
        sawWorkspaceActivity = sawWorkspaceActivity
            || (event.source == QStringLiteral("Workspace")
                && (event.message.startsWith(QStringLiteral("Activated "))
                    || event.message.startsWith(QStringLiteral("Scanned "))));
    }
    expectBool("workspace open logs activity",
               sawWorkspaceActivity,
               true);
    const QStringList svFiles = window.workspaceManager->getSystemVerilogFiles();
    expectBool("workspace has SystemVerilog files", !svFiles.isEmpty(), true);
    const ProjectSnapshot project = window.workspaceManager->projectSnapshot();
    const QString normalizedWorkspacePath =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(workspacePath).absoluteFilePath()));
    expectBool("project model tracks workspace root",
               project.workspaceRoot == normalizedWorkspacePath,
               true);
    expectBool("project model tracks SV files",
               project.systemVerilogFiles.size() == svFiles.size(),
               true);
    expectBool("project model has default include root",
               project.includeDirs.contains(normalizedWorkspacePath),
               true);
    const QString resolvedWorkspaceInclude =
        window.workspaceManager->resolveIncludePath(QFileInfo(svFiles.first()).fileName());
    expectBool("workspace manager resolves include by basename",
               QFileInfo(resolvedWorkspaceInclude).fileName() == QFileInfo(svFiles.first()).fileName(),
               true);
    const QString resolvedCurrentFileInclude =
        window.workspaceManager->resolveIncludePath(QFileInfo(svFiles.first()).fileName(),
                                                    svFiles.first());
    expectBool("workspace manager resolves include from current file",
               QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(resolvedCurrentFileInclude).absoluteFilePath()))
                   == QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(svFiles.first()).absoluteFilePath())),
               true);
    const QString includeSourcePath =
        QDir(workspacePath).absoluteFilePath(QStringLiteral("_svh.svh"));
    const QString includeTargetPath =
        QDir(workspacePath).absoluteFilePath(QStringLiteral("SVH_interface.sv"));
    expectBool("include source fixture exists",
               QFileInfo(includeSourcePath).isFile(),
               true);
    expectBool("include target fixture exists",
               QFileInfo(includeTargetPath).isFile(),
               true);
    const int editorCountBeforeIncludeClick = window.tabManager->editorCount();
    if (QFileInfo(includeSourcePath).isFile()
        && QFileInfo(includeTargetPath).isFile()
        && window.tabManager->openFileInTab(includeSourcePath)) {
        MyCodeEditor* includeEditor = window.tabManager->getCurrentEditor();
        expectBool("include source editor opens", includeEditor != nullptr, true);
        if (includeEditor) {
            QTextBlock includeBlock =
                findBlockContaining(includeEditor->document(),
                                    QStringLiteral("SVH_interface.sv"));
            expectBool("include directive block found",
                       includeBlock.isValid(),
                       true);
            if (includeBlock.isValid()) {
                const int includeClickPosition =
                    includeBlock.position()
                    + includeBlock.text().indexOf(QStringLiteral("SVH_interface"));
                QTextCursor includeCursor(includeEditor->document());
                includeCursor.setPosition(includeClickPosition);
                includeEditor->setTextCursor(includeCursor);
                includeEditor->centerCursor();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                const QPoint includeClickPoint =
                    includeEditor->cursorRect(includeCursor).center();
                QTest::mouseClick(includeEditor->viewport(),
                                  Qt::LeftButton,
                                  Qt::ControlModifier,
                                  includeClickPoint);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                MyCodeEditor* openedIncludeEditor =
                    window.tabManager->getCurrentEditor();
                const DocumentSnapshot openedIncludeDocument =
                    window.tabManager->getDocumentForEditor(openedIncludeEditor);
                expectBool("Ctrl+Click include opens target",
                           openedIncludeEditor
                               && QDir::cleanPath(QDir::fromNativeSeparators(
                                      QFileInfo(openedIncludeDocument.fileName)
                                          .absoluteFilePath()))
                                      == QDir::cleanPath(QDir::fromNativeSeparators(
                                             QFileInfo(includeTargetPath)
                                                 .absoluteFilePath()))
                               && window.tabManager->editorCount()
                                      == editorCountBeforeIncludeClick + 2,
                           true);
                if (openedIncludeEditor) {
                    QTest::mouseClick(openedIncludeEditor->viewport(),
                                      Qt::BackButton,
                                      Qt::NoModifier,
                                      QPoint(4, 4));
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                }
                const DocumentSnapshot includeBackDocument =
                    window.tabManager->getCurrentDocument();
                expectBool("mouse back returns from include target",
                           QDir::cleanPath(QDir::fromNativeSeparators(
                               QFileInfo(includeBackDocument.fileName)
                                   .absoluteFilePath()))
                               == QDir::cleanPath(QDir::fromNativeSeparators(
                                   QFileInfo(includeSourcePath)
                                       .absoluteFilePath())),
                           true);

                const int editorCountAfterFirstIncludeClick =
                    window.tabManager->editorCount();
                expectBool("return to include source tab",
                           window.tabManager->activateOpenFile(includeSourcePath),
                           true);
                includeEditor = window.tabManager->getCurrentEditor();
                if (includeEditor) {
                    includeEditor->setTextCursor(includeCursor);
                    includeEditor->centerCursor();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                    const QPoint repeatClickPoint =
                        includeEditor->cursorRect(includeCursor).center();
                    QTest::mouseClick(includeEditor->viewport(),
                                      Qt::LeftButton,
                                      Qt::ControlModifier,
                                      repeatClickPoint);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                }
                expectBool("Ctrl+Click include reuses target tab",
                           window.tabManager->editorCount()
                               == editorCountAfterFirstIncludeClick,
                           true);

                MyCodeEditor* targetHistoryEditor =
                    window.tabManager->getCurrentEditor();
                const bool targetHasSecondLine =
                    targetHistoryEditor
                    && targetHistoryEditor->document()->blockCount() > 1;
                expectBool("include target has line-navigation history fixture",
                           targetHasSecondLine,
                           true);
                if (targetHasSecondLine) {
                    QTextCursor oldTargetCursor(
                        targetHistoryEditor->document()->findBlockByNumber(0));
                    targetHistoryEditor->setTextCursor(oldTargetCursor);
                    expectBool("return to include source before line navigation",
                               window.tabManager->activateOpenFile(includeSourcePath),
                               true);
                    includeEditor = window.tabManager->getCurrentEditor();
                    if (includeEditor) {
                        includeEditor->setTextCursor(includeCursor);
                        includeEditor->centerCursor();
                        QCoreApplication::processEvents(
                            QEventLoop::AllEvents,
                            50);
                    }
                    window.navigationCommandCoordinator->navigateToFileAndLine(
                        includeTargetPath,
                        2,
                        1);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                    MyCodeEditor* lineTargetEditor =
                        window.tabManager->getCurrentEditor();
                    if (lineTargetEditor) {
                        QTest::mouseClick(lineTargetEditor->viewport(),
                                          Qt::BackButton,
                                          Qt::NoModifier,
                                          QPoint(4, 4));
                        QCoreApplication::processEvents(
                            QEventLoop::AllEvents,
                            50);
                    }
                    const DocumentSnapshot lineBackDocument =
                        window.tabManager->getCurrentDocument();
                    expectBool("mouse back after cross-file line jump skips target old cursor",
                               QDir::cleanPath(QDir::fromNativeSeparators(
                                   QFileInfo(lineBackDocument.fileName)
                                       .absoluteFilePath()))
                                   == QDir::cleanPath(QDir::fromNativeSeparators(
                                       QFileInfo(includeSourcePath)
                                           .absoluteFilePath())),
                               true);
                }
            }
        }
    }

    expectBool("workspace symbol analysis completes",
               waitUntil([&]() { return workspaceSymbolsDone; }, 60000), true);

    const QString largeFile = largestFile(svFiles);
    expectBool("large file selected", QFileInfo(largeFile).size() > 20000, true);
    expectBool("open large file", window.tabManager->openFileInTab(largeFile), true);

    MyCodeEditor* largeEditor = window.tabManager->getCurrentEditor();
    expectBool("large editor exists", largeEditor != nullptr, true);
    if (largeEditor) {
        DocumentModel* documents = window.tabManager->getDocumentModel();
        expectBool("document model exists", documents != nullptr, true);
        const DocumentSnapshot beforeEditDoc = documents
            ? documents->documentForFile(largeFile)
            : DocumentSnapshot();
        expectBool("document model tracks large file",
                   beforeEditDoc.fileName == QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(largeFile).absoluteFilePath())),
                   true);
        expectBool("tab manager exposes active document snapshot",
                   window.tabManager->getCurrentDocument().documentId
                       == beforeEditDoc.documentId,
                   true);
        expectBool("opened document starts saved", beforeEditDoc.saved, true);
        expectBool("opened document saved version matches text version",
                   beforeEditDoc.savedTextVersion == beforeEditDoc.textVersion,
                   true);
        expectBool("document model caches opened text",
                   documents
                       && documents->documentTextForFile(largeFile)
                           == largeEditor->toPlainText(),
                   true);
        expectBool("tab manager reads model open-file text",
                   window.tabManager->getPlainTextFromOpenFile(largeFile)
                       == largeEditor->toPlainText(),
                   true);
        expectBool("tab manager reads model current-tab text",
                   window.tabManager->getPlainTextFromCurrentTab()
                       == largeEditor->toPlainText(),
                   true);

        largeEditor->setFocus();
        QTextCursor cursor = largeEditor->textCursor();
        cursor.movePosition(QTextCursor::Start);
        largeEditor->setTextCursor(cursor);
        const int beforeLength = largeEditor->toPlainText().size();

        QTest::keyClick(largeEditor, Qt::Key_Return);
        QTest::keyClicks(largeEditor, "x");
        QTest::keyClick(largeEditor, Qt::Key_Down);
        QTest::keyClick(largeEditor, Qt::Key_Up);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        expectBool("large file edit applied",
                   largeEditor->toPlainText().size() >= beforeLength + 2, true);
        const DocumentSnapshot afterEditDoc = documents
            ? documents->documentForEditor(largeEditor)
            : DocumentSnapshot();
        expectBool("document model marks edit dirty", afterEditDoc.dirty, true);
        expectBool("document model increments version",
                   afterEditDoc.textVersion > beforeEditDoc.textVersion, true);
        expectBool("document model keeps saved version across edit",
                   afterEditDoc.savedTextVersion == beforeEditDoc.savedTextVersion
                       && afterEditDoc.savedTextVersion < afterEditDoc.textVersion,
                   true);
        expectBool("document model tracks cursor line", afterEditDoc.cursorLine > 0, true);
        expectBool("tab manager active snapshot tracks edit",
                   window.tabManager->getCurrentDocument().textVersion
                       == afterEditDoc.textVersion
                       && window.tabManager->getCurrentDocument().dirty,
                   true);
        expectBool("document model updates cached text",
                   documents
                       && documents->documentTextForFile(largeFile)
                           == largeEditor->toPlainText(),
                   true);
        expectBool("tab manager current text follows model cache",
                   window.tabManager->getPlainTextFromCurrentTab()
                       == largeEditor->toPlainText(),
                   true);
        expectBool("document model owns editor snapshot state",
                   documents
                       && afterEditDoc.fileName
                           == QDir::cleanPath(QDir::fromNativeSeparators(
                                  QFileInfo(largeFile).absoluteFilePath()))
                       && documents->documentText(afterEditDoc.documentId)
                              == largeEditor->toPlainText()
                       && !afterEditDoc.saved
                       && afterEditDoc.cursorLine > 0,
                   true);
        drainRelationshipWork(window);
    }

    QTemporaryDir identityDir;
    expectBool("document identity temp dir valid", identityDir.isValid(), true);
    if (identityDir.isValid()) {
        QDir identityRoot(identityDir.path());
        expectBool("document identity dir A created",
                   identityRoot.mkpath(QStringLiteral("a")),
                   true);
        expectBool("document identity dir B created",
                   identityRoot.mkpath(QStringLiteral("b")),
                   true);
        const QString sameNameA =
            identityRoot.filePath(QStringLiteral("a/same_name.sv"));
        const QString sameNameB =
            identityRoot.filePath(QStringLiteral("b/same_name.sv"));
        QFile sameFileA(sameNameA);
        expectBool("same-name file A writable",
                   sameFileA.open(QIODevice::WriteOnly | QIODevice::Text),
                   true);
        if (sameFileA.isOpen()) {
            sameFileA.write("module same_name_a; logic from_a; endmodule\n");
            sameFileA.close();
        }
        QFile sameFileB(sameNameB);
        expectBool("same-name file B writable",
                   sameFileB.open(QIODevice::WriteOnly | QIODevice::Text),
                   true);
        if (sameFileB.isOpen()) {
            sameFileB.write("module same_name_b; logic from_b; endmodule\n");
            sameFileB.close();
        }

        expectBool("open same-name file A",
                   window.tabManager->openFileInTab(sameNameA),
                   true);
        MyCodeEditor* sameEditorA = window.tabManager->getCurrentEditor();
        expectBool("same-name editor A exists", sameEditorA != nullptr, true);
        expectBool("open same-name file B",
                   window.tabManager->openFileInTab(sameNameB),
                   true);
        MyCodeEditor* sameEditorB = window.tabManager->getCurrentEditor();
        DocumentModel* documents = window.tabManager->getDocumentModel();
        expectBool("same-name editor B exists", sameEditorB != nullptr, true);
        expectBool("document model resolves native path to editor",
                   sameEditorA
                       && documents
                       && documents->editorForFile(QDir::toNativeSeparators(sameNameA))
                              == sameEditorA,
                   true);
        QSignalSpy activeDocumentSpy(window.tabManager.get(),
                                     &TabManager::activeDocumentChanged);
        expectBool("tab manager activates model-indexed file",
                   sameEditorA
                       && window.tabManager->activateOpenFile(QDir::toNativeSeparators(sameNameA))
                       && window.tabManager->getCurrentEditor() == sameEditorA,
                   true);
        expectBool("tab manager emits active document snapshot",
                   activeDocumentSpy.count() == 1
                       && activeDocumentSpy.takeFirst().at(0).value<DocumentSnapshot>().fileName
                              == QDir::cleanPath(QDir::fromNativeSeparators(
                                     QFileInfo(sameNameA).absoluteFilePath())),
                   true);
        expectBool("tab manager keeps same-name file A text distinct",
                   window.tabManager->getPlainTextFromOpenFile(sameNameA)
                       .contains(QStringLiteral("from_a")),
                   true);
        expectBool("tab manager keeps same-name file B text distinct",
                   window.tabManager->getPlainTextFromOpenFile(sameNameB)
                       .contains(QStringLiteral("from_b")),
                   true);
        expectBool("tab manager rejects basename-only open-file text lookup",
                   window.tabManager
                       ->getPlainTextFromOpenFile(QStringLiteral("same_name.sv"))
                       .isNull(),
                   true);
        drainRelationshipWork(window);
    }

    bool symbolFixtureAnalyzed = false;
    QObject::connect(window.analysisScheduler.get(), &AnalysisScheduler::fileSymbolAnalysisFinished,
                     &window, [&](const QString& fileName, int symbolsFound) {
                         if (QFileInfo(fileName).absoluteFilePath()
                             == QFileInfo(symbolFixturePath).absoluteFilePath() && symbolsFound > 0) {
                             symbolFixtureAnalyzed = true;
                         }
                     });

    expectBool("open symbol fixture", window.tabManager->openFileInTab(symbolFixturePath), true);
    expectBool("symbol fixture analysis completes",
               waitUntil([&]() { return symbolFixtureAnalyzed; }, 10000), true);

    QTemporaryDir diagnosticDir;
    expectBool("diagnostic temp dir created", diagnosticDir.isValid(), true);
    const QString diagnosticPath =
        diagnosticDir.filePath(QStringLiteral("broken_diag.sv"));
    QFile diagnosticFile(diagnosticPath);
    expectBool("diagnostic fixture writable",
               diagnosticFile.open(QIODevice::WriteOnly | QIODevice::Text), true);
    if (diagnosticFile.isOpen()) {
        diagnosticFile.write(
            "module broken_diag(input logic clk);\n"
            "  logic bad;\n"
            "  assign bad = ;\n"
            "endmodule\n");
        diagnosticFile.close();
    }
    const QString cleanDiagnosticPath =
        diagnosticDir.filePath(QStringLiteral("clean_diag.sv"));
    QFile cleanDiagnosticFile(cleanDiagnosticPath);
    expectBool("clean diagnostic fixture writable",
               cleanDiagnosticFile.open(QIODevice::WriteOnly | QIODevice::Text), true);
    if (cleanDiagnosticFile.isOpen()) {
        cleanDiagnosticFile.write(
            "module clean_diag(input logic clk, output logic done);\n"
            "  assign done = clk;\n"
            "endmodule\n");
        cleanDiagnosticFile.close();
    }

    bool diagnosticFixtureAnalyzed = false;
    QObject::connect(window.analysisScheduler.get(), &AnalysisScheduler::fileSymbolAnalysisFinished,
                     &window, [&](const QString& fileName, int) {
                         if (QFileInfo(fileName).absoluteFilePath()
                             == QFileInfo(diagnosticPath).absoluteFilePath()) {
                             diagnosticFixtureAnalyzed = true;
                         }
                     });
    expectBool("open diagnostic fixture", window.tabManager->openFileInTab(diagnosticPath), true);
    expectBool("diagnostic fixture analysis completes",
               waitUntil([&]() { return diagnosticFixtureAnalyzed; }, 10000), true);
    expectBool("diagnostic fixture snapshot updates",
               waitUntil([&]() {
                   const auto snapshot = SemanticIndex::getInstance()->snapshot();
                   return snapshot && !snapshot->getDiagnostics(diagnosticPath).isEmpty();
               }, 5000),
               true);
    expectBool("problems tree exists", problemsTree(window) != nullptr, true);
    expectBool("problems tree shows diagnostic",
               waitUntil([&]() {
                   return problemsTree(window)
                          && navigableItemCount(problemsTree(window)) > 0;
               }, 2000),
               true);
    if (problemsScopeCombo(window)) {
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        expectBool("problems all-files groups diagnostics",
                   waitUntil([&]() {
                       QTreeWidget* tree = problemsTree(window);
                       return tree
                              && tree->topLevelItemCount() > 0
                              && tree->topLevelItem(0)->childCount() > 0;
                   }, 2000),
                   true);
        expectBool("problems all-files group shows count",
                   problemsTree(window)
                       && problemsTree(window)->topLevelItemCount() > 0
                       && problemsTree(window)->topLevelItem(0)->text(0).contains(QStringLiteral("(")),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("Current File")));
    }

    bool cleanDiagnosticFixtureAnalyzed = false;
    QObject::connect(window.analysisScheduler.get(), &AnalysisScheduler::fileSymbolAnalysisFinished,
                     &window, [&](const QString& fileName, int) {
                         if (QFileInfo(fileName).absoluteFilePath()
                             == QFileInfo(cleanDiagnosticPath).absoluteFilePath()) {
                             cleanDiagnosticFixtureAnalyzed = true;
                         }
                     });
    expectBool("open clean diagnostic fixture",
               window.tabManager->openFileInTab(cleanDiagnosticPath), true);
    expectBool("clean diagnostic fixture analysis completes",
               waitUntil([&]() { return cleanDiagnosticFixtureAnalyzed; }, 10000), true);
    if (problemsScopeCombo(window)) {
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        expectBool("problems keep previous file diagnostic",
                   waitUntil([&]() {
                       return hasNavigableFile(problemsTree(window), diagnosticPath);
                   }, 2000),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("Workspace Files")));
        expectBool("problems workspace scope hides external diagnostic",
                   waitUntil([&]() {
                       return !hasNavigableFile(problemsTree(window), diagnosticPath);
                   }, 2000),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        expectBool("problems all-files restores external diagnostic",
                   waitUntil([&]() {
                       return hasNavigableFile(problemsTree(window), diagnosticPath);
                   }, 2000),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("Current File")));
    }

    expectBool("reopen symbol fixture", window.tabManager->openFileInTab(symbolFixturePath), true);
    expectBool("symbol fixture analysis remains complete",
               waitUntil([&]() {
                   const auto snapshot = SemanticIndex::getInstance()->snapshot();
                   return snapshot
                       && !snapshot->getSymbolRecords(symbolFixturePath).isEmpty();
               }, 10000),
               true);

    MyCodeEditor* editor = window.tabManager->getCurrentEditor();
    expectBool("symbol editor exists", editor != nullptr, true);

    if (editor) {
        editor->setFocus();

        QTextBlock assignBlock = findBlockContaining(editor->document(), QStringLiteral("assign data_out"));
        expectBool("found insertion block", assignBlock.isValid(), true);
        if (assignBlock.isValid()) {
            QTextCursor cursor(editor->document());
            cursor.setPosition(assignBlock.position() + assignBlock.text().size());
            editor->setTextCursor(cursor);
            QTest::keyClick(editor, Qt::Key_Return);
            QTest::keyClicks(editor, "co");
        }

        QCompleter* completer = editor->findChild<QCompleter*>();
        expectBool("completion object exists", completer != nullptr, true);
        expectBool("completion popup/model becomes usable",
                   waitUntil([&]() {
                       return completer && completer->model() && completer->model()->rowCount() > 0;
                   }, 3000),
                   true);
        if (completer)
            completer->popup()->hide();
        drainRelationshipWork(window);

        QTextBlock jumpBlock = findBlockContaining(editor->document(),
                                                   QStringLiteral("counter       <= add_one(counter)"));
        expectBool("found Ctrl+Click source", jumpBlock.isValid(), true);
        if (jumpBlock.isValid()) {
            const int counterStart =
                jumpBlock.position()
                + jumpBlock.text().indexOf(QStringLiteral("counter"));
            const int clickPosition = counterStart + 3;
            QTextCursor clickCursor(editor->document());
            clickCursor.setPosition(clickPosition);
            QTextCursor selectedCounter(editor->document());
            selectedCounter.setPosition(counterStart);
            selectedCounter.setPosition(
                counterStart + QStringLiteral("counter").size(),
                QTextCursor::KeepAnchor);
            editor->setTextCursor(selectedCounter);
            editor->centerCursor();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QPoint clickPoint = editor->cursorRect(clickCursor).center();
            QTest::mousePress(editor->viewport(),
                              Qt::LeftButton,
                              Qt::ControlModifier,
                              clickPoint);
            expectBool("Ctrl+Click jumps to counter definition",
                       waitUntil([&]() { return editor->textCursor().blockNumber() == 78; }, 2000),
                       true);
            QTest::mouseRelease(editor->viewport(),
                                Qt::LeftButton,
                                Qt::ControlModifier,
                                clickPoint);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            expectBool("Ctrl+Click clears double-click style selection",
                       !editor->textCursor().hasSelection(),
                       true);
        }
    }

    NavigationWidget* navWidget = window.findChild<NavigationWidget*>();
    expectBool("navigation widget exists", navWidget != nullptr, true);
    runComModeRegression(window);
    runGlobalControlRegression(window, navWidget);
    if (navWidget && editor) {
        navWidget->setActiveTab(NavigationWidget::FileTab);
        window.navigationManager->setActiveView(NavigationManager::ModuleHierarchyView);
        window.navigationManager->refreshCurrentView();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QSignalSpy analysisNavigationRefreshSpy(
            window.navigationManager.get(),
            &NavigationManager::dataRefreshed);
        window.analysisScheduler->fileSymbolAnalysisFinished(
            normalizedSymbolFixturePath, 0);
        expectBool("analysis routes navigation refresh",
                   waitUntil([&]() { return analysisNavigationRefreshSpy.count() > 0; }, 1000),
                   true);
        analysisNavigationRefreshSpy.clear();
        window.analysisScheduler->workspaceSymbolAnalysisFinished(ProjectSnapshot(), 1, 0);
        expectBool("batch analysis routes navigation refresh",
                   waitUntil([&]() { return analysisNavigationRefreshSpy.count() > 0; }, 1000),
                   true);

        navWidget->setActiveTab(NavigationWidget::FileTab);
        window.navigationManager->setActiveView(NavigationManager::FileHierarchyView);
        window.navigationManager->refreshCurrentView();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const QStringList navigationFiles =
            window.workspaceManager->getSystemVerilogFiles();
        const QString navigationFile =
            navigationFiles.isEmpty() ? QString() : navigationFiles.first();
        QTreeWidget* fileTree = navWidget->fileTreeWidget;
        QTreeWidgetItem* fileItem =
            navWidget->findFileItemByPath(navigationFile);
        expectBool("navigation file item exists", fileTree && fileItem, true);
        if (fileTree && fileItem) {
            fileTree->expandAll();
            fileTree->scrollToItem(fileItem);
            fileTree->setCurrentItem(fileItem);
            fileTree->setFocus();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QRect rect = fileTree->visualItemRect(fileItem);
            expectBool("navigation item has visual rect", rect.isValid(), true);
            bool fileDoubleClicked = false;
            QObject::connect(navWidget, &NavigationWidget::fileDoubleClicked,
                             &window, [&](const QString& filePath) {
                                 if (filePath == navigationFile)
                                     fileDoubleClicked = true;
                             });
            QTest::mouseClick(fileTree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
            QTest::mouseDClick(fileTree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            expectBool("navigation file double-click signal emitted", fileDoubleClicked, true);

            expectBool("navigation file double-click activates file",
                       waitUntil([&]() {
                           MyCodeEditor* current = window.tabManager->getCurrentEditor();
                           return current
                                  && window.tabManager
                                         ->getDocumentForEditor(current)
                                         .fileName == navigationFile;
                       }, 2000),
                       true);
        }
    }

    runReferenceDockRegression(window, normalizedSymbolFixturePath);
    runRtlInsightsPanelRegression(window, normalizedSymbolFixturePath);
    runRtlInsightsSemanticDiffRegression(window, normalizedSymbolFixturePath);

    drainRelationshipWork(window);

    if (problemsScopeCombo(window)) {
        SemanticDiagnostic closeDiagnostic;
        closeDiagnostic.fileName = diagnosticPath;
        closeDiagnostic.line = 3;
        closeDiagnostic.column = 1;
        closeDiagnostic.message = QStringLiteral("workspace close probe");
        closeDiagnostic.severity = SemanticDiagnostic::Error;
        SemanticIndex::getInstance()->setSnapshot(
            snapshotFromRecords(
                QList<SemanticSymbolRecord>{},
                QList<SemanticRelationship>{},
                QList<SemanticDiagnostic>{closeDiagnostic}));
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        semanticPanelRefresh(window)->updateProblemsPanel();
        expectBool("problems close probe visible",
                   navigableItemCount(problemsTree(window)) == 1,
                   true);
        window.workspaceManager->closeWorkspace();
        expectBool("problems clear on workspace close",
                   waitUntil([&]() {
                       return navigableItemCount(problemsTree(window)) == 0;
                   }, 2000),
                   true);
        SemanticIndex::getInstance()->setSnapshot(
            snapshotFromRecords(
                QList<SemanticSymbolRecord>{},
                QList<SemanticRelationship>{},
                QList<SemanticDiagnostic>{closeDiagnostic}));
        semanticPanelRefresh(window)->updateProblemsPanel();
        expectBool("problems reopen probe visible",
                   navigableItemCount(problemsTree(window)) == 1,
                   true);
        workspaceSymbolsDone = false;
        workspaceFilesScanned = false;
        if (problemsBandCombo(window))
            problemsBandCombo(window)->setCurrentIndex(
                problemsBandCombo(window)->findText(QStringLiteral("All Bands")));
        expectBool("reopen workspace after close",
                   window.workspaceManager->openWorkspace(workspacePath), true);
        expectBool("reopened workspace file scan completes",
                   waitUntil([&]() { return workspaceFilesScanned; }, 10000),
                   true);
        expectBool("problems preserve external diagnostic on workspace analysis start",
                   waitUntil([&]() {
                       return navigableItemCount(problemsTree(window)) == 1;
                   }, 2000),
                   true);
        expectBool("reopened workspace analysis completes",
                   waitUntil([&]() { return workspaceSymbolsDone; }, 60000),
                   true);
        expectBool("problems snapshot keeps external diagnostic after workspace analysis",
                   waitUntil([&]() {
                       const auto snapshot = SemanticIndex::getInstance()->snapshot();
                       return snapshot
                              && !snapshot->getDiagnostics(diagnosticPath).isEmpty();
                   }, 2000),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        semanticPanelRefresh(window)->updateProblemsPanel();
        expectBool("problems keep external diagnostic after workspace analysis",
                   waitUntil([&]() {
                       return hasNavigableFile(problemsTree(window), diagnosticPath);
                   }, 2000),
                   true);
        drainRelationshipWork(window);
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
