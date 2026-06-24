// Offscreen GUI smoke test for the real MainWindow/TabManager/MyCodeEditor path.
// It keeps the assertions coarse on purpose: this target is a repeatable guard that
// the GUI workflow is alive, while detailed semantic behavior stays in the focused
// headless tests.
#include <QApplication>
#include <QAction>
#include <QCompleter>
#include <QComboBox>
#include <QDockWidget>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
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
#include "globalcontrolcoordinator.h"
#include "globalcontrolpanel.h"
#include "globalcontrolservice.h"
#include "navigationwidget.h"
#include "navigationpanecoordinator.h"
#include "semantic_fixture_records.h"
#include "symbolhoverservice.h"
#include "modemanager.h"
#include "navigationmanager.h"
#include "problemspanelcoordinator.h"
#include "referencespanelcoordinator.h"
#include "relationshipspanelcoordinator.h"
#include "rtlinsightspanelcoordinator.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "semanticdockcoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "semanticruntimecoordinator.h"
#include "smartrelationshipbuilder.h"
#include "tabmanager.h"
#include "tsdocument.h"
#include "wavepreviewpanelcoordinator.h"
#include "workspaceanalysisrequestqueue.h"
#include "workspacemanager.h"
#undef private

static int g_checks = 0;
static int g_fails = 0;

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
    expectBool("editor ctrl-click selects range body",
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
    for (int i = 0; i < rtlLines.size(); ++i) {
        if (moduleLine < 0 && rtlLines.at(i).contains(QStringLiteral("module rtl_top")))
            moduleLine = i + 1;
        if (signalLine < 0 && rtlLines.at(i).contains(QStringLiteral("clk_main")))
            signalLine = i + 1;
    }
    expectBool("hover fixture has module line", moduleLine > 0, true);
    expectBool("hover fixture has signal line", signalLine > 0, true);
    if (moduleLine <= 0 || signalLine <= 0)
        return;

    const int signalColumn =
        rtlLines.at(signalLine - 1).indexOf(QStringLiteral("clk_main"));
    expectBool("hover fixture has signal column", signalColumn >= 0, true);
    if (signalColumn < 0)
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

static QTreeWidgetItem* findItemByText(QTreeWidgetItem* item, const QString& text)
{
    if (!item)
        return nullptr;
    if (item->text(0) == text)
        return item;
    for (int i = 0; i < item->childCount(); ++i) {
        if (QTreeWidgetItem* found = findItemByText(item->child(i), text))
            return found;
    }
    return nullptr;
}

static QTreeWidgetItem* findItemByText(QTreeWidget* tree, const QString& text)
{
    if (!tree)
        return nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem* found = findItemByText(tree->topLevelItem(i), text))
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

    shortcutEditor.clear();
    editorCommandCoordinator.executeAlternateCommand(&shortcutEditor,
                                                    AlternateCommandAction::Comment);
    expectBool("file coordinator executes editor command",
               shortcutEditor.toPlainText() == QStringLiteral("// "),
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
        designItem = findItemByText(tree, QStringLiteral("u_stage : design_stage"));
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

static void runGlobalControlRegression(MainWindow& window,
                                       NavigationWidget* navWidget)
{
    printf("\n-- global control regression --\n");

    GlobalControlService service;
    const QList<GlobalControlItem> commandMatches =
        service.query(QStringLiteral("ow"),
                      window.workspaceManager->getProjectModel(),
                      SemanticIndex::getInstance());
    bool foundOpenWorkspaceAction = false;
    for (const GlobalControlItem& item : commandMatches) {
        if (item.id == QStringLiteral("ow"))
            foundOpenWorkspaceAction = true;
    }
    expectBool("global control finds ow command",
               foundOpenWorkspaceAction,
               true);

    const QList<GlobalControlItem> allCommands =
        service.query(QString(),
                      window.workspaceManager->getProjectModel(),
                      SemanticIndex::getInstance());
    QSet<QString> commandIds;
    for (const GlobalControlItem& item : allCommands) {
        if (item.kind == GlobalControlItemKind::Command)
            commandIds.insert(item.id);
    }
    expectBool("global control only exposes ow fd fds",
               commandIds == QSet<QString>({
                   QStringLiteral("ow"),
                   QStringLiteral("fd"),
                   QStringLiteral("fds"),
               }),
               true);

    const QList<GlobalControlItem> foldActionMatches =
        service.query(QStringLiteral("fd"),
                      window.workspaceManager->getProjectModel(),
                      SemanticIndex::getInstance());
    bool foundFoldRegionAction = false;
    bool foundFoldShelfAction = false;
    bool foldActionsExplainBehavior = false;
    for (const GlobalControlItem& item : foldActionMatches) {
        if (item.id == QStringLiteral("fd")) {
            foundFoldRegionAction = item.title == QStringLiteral("fd")
                && item.subtitle.contains(QStringLiteral("Fold Region"));
        }
        if (item.id == QStringLiteral("fds")) {
            foundFoldShelfAction = item.title == QStringLiteral("fds")
                && item.subtitle.contains(QStringLiteral("Fold Shelf"));
        }
    }
    foldActionsExplainBehavior = foundFoldRegionAction && foundFoldShelfAction;
    expectBool("global control finds fd fold actions",
               foundFoldRegionAction && foundFoldShelfAction,
               true);
    expectBool("global control fold actions include explanations",
               foldActionsExplainBehavior,
               true);

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
        dispatched = item.id == QStringLiteral("ow");
    });
    dispatcherProbe.dispatch(
        GlobalControlItem{GlobalControlItemKind::Command,
                          QStringLiteral("ow"),
                          QStringLiteral("ow"),
                          QStringLiteral("Open Workspace")});
    expectBool("global control dispatches command",
               dispatched,
               true);

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
        window.globalControlCoordinator->panel->searchEdit->setText(
            QStringLiteral("fd"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        bool panelShowsFoldRegion = false;
        bool panelShowsFoldShelf = false;
        for (const GlobalControlItem& item :
             window.globalControlCoordinator->panel->currentItems) {
            if (item.id == QStringLiteral("fd")
                && item.subtitle.contains(QStringLiteral("Fold Region"))) {
                panelShowsFoldRegion = true;
            }
            if (item.id == QStringLiteral("fds")
                && item.subtitle.contains(QStringLiteral("Fold Shelf"))) {
                panelShowsFoldShelf = true;
            }
        }
        expectBool("global control fd query shows fd and fds",
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
                          QStringLiteral("fd"),
                          QStringLiteral("fd"),
                          QStringLiteral("Fold Region")});
    expectBool("global control fd starts fold region mode",
               activeEditor && activeEditor->foldRegionMarkModeActive(),
               true);
    if (activeEditor)
        activeEditor->cancelFoldRegionMarkMode();

    window.globalControlCoordinator->dispatch(
        GlobalControlItem{GlobalControlItemKind::Command,
                          QStringLiteral("fds"),
                          QStringLiteral("fds"),
                          QStringLiteral("Fold Shelf")});
    QDockWidget* foldShelfDock =
        window.findChild<QDockWidget*>(QStringLiteral("FoldShelfDock"));
    expectBool("global control fds starts fold shelf mode",
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
    runEditorFormatterRegression();
    runEditorAppearanceCoordinatorRegression();
    runFormatterCoordinatorRegression();
    runTreeSitterFoldingProviderRegression();
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
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::workspaceSymbolAnalysisFinished,
                     &window,
                     [&](const ProjectSnapshot&, int filesAnalyzed, int totalSymbols) {
                         Q_UNUSED(filesAnalyzed)
                         Q_UNUSED(totalSymbols)
                         workspaceSymbolsDone = true;
                     });

    window.resize(1100, 760);
    window.show();
    expectBool("main window visible", waitUntil([&]() { return window.isVisible(); }, 2000), true);
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
    if (waveEditor) {
        waveEditor->setPlainText(
            QStringLiteral("module wave_ui;\n"
                           "logic clk;\n"
                           "input logic [7:0] data;\n"
                           "logic [7:0] q;\n"
                           "logic [7:0] out;\n"
                           "assign out = q + data;\n"
                           "always_ff @(posedge clk) begin\n"
                           "    if (data[0]) q <= data;\n"
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
    bool sawWaveContext = false;
    QTreeWidgetItem* waveQEventItem = nullptr;
    if (waveTree) {
        for (int i = 0; i < waveTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* laneItem = waveTree->topLevelItem(i);
            const QString name = laneItem->text(0);
            sawWaveQ = sawWaveQ || name == QStringLiteral("q");
            sawWaveOut = sawWaveOut || name == QStringLiteral("out");
            sawWaveClockReset = sawWaveClockReset
                || name == QStringLiteral("Clock/Reset Groups");
            if (name == QStringLiteral("q")) {
                sawWaveContext = sawWaveContext
                    || laneItem->text(5) == QStringLiteral("internal logic [7:0]");
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
    expectBool("wave preview renders declaration context",
               waveTree && sawWaveContext,
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
        const int timelineLeft = 10 + labelWidth;
        const int timelineRight = waveCanvas->width() - 12;
        const int timelineWidth = std::max(80, timelineRight - timelineLeft);
        const QPoint qEventPoint(timelineLeft + timelineWidth / 2,
                                 28 + 32 + 16 - 10 + 9);
        QTest::mouseMove(waveCanvas, qEventPoint);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("wave preview canvas hover has source target details",
               waveCanvas
                   && waveCanvas->toolTip().contains(QStringLiteral("target: q"))
                   && waveCanvas->toolTip().contains(QStringLiteral("sources: data")),
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
    bool sawWorkspaceActivity = false;
    for (const ActivityLogEvent& event : ActivityLogService::getInstance()->events()) {
        sawWorkspaceActivity = sawWorkspaceActivity
            || (event.source == QStringLiteral("Workspace")
                && event.message.startsWith(QStringLiteral("Opened ")));
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
            const int clickPosition = jumpBlock.position() + jumpBlock.text().indexOf(QStringLiteral("counter")) + 3;
            QTextCursor cursor(editor->document());
            cursor.setPosition(clickPosition);
            editor->setTextCursor(cursor);
            editor->centerCursor();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QPoint clickPoint = editor->cursorRect(cursor).center();
            QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::ControlModifier, clickPoint);

            expectBool("Ctrl+Click jumps to counter definition",
                       waitUntil([&]() { return editor->textCursor().blockNumber() == 78; }, 2000),
                       true);
        }
    }

    NavigationWidget* navWidget = window.findChild<NavigationWidget*>();
    expectBool("navigation widget exists", navWidget != nullptr, true);
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
        expectBool("reopen workspace after close",
                   window.workspaceManager->openWorkspace(workspacePath), true);
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
