// Focused regression for the supported insert palette and semantic catalog.
#include <QtWidgets>
#include <QtTest>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <optional>

#define private public
#include "mainwindow.h"
#include "analysisscheduler.h"
#include "temporaryeditorsearchprovider.h"
#include "globalcontrolcoordinator.h"
#include "globalcontrolpanel.h"
#undef private

#include "applicationthememanager.h"
#include "completionservice.h"
#include "editorinsertpaletteservice.h"
#include "usertemplateservice.h"
#include "customabbreviationservice.h"
#include "mycodeeditor.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "symbolanalyzer.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"

namespace {
int checks = 0;
int failures = 0;

void expect(const char* label, bool ok)
{
    ++checks;
    failures += !ok;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
}

bool waitUntil(const std::function<bool()>& ready, int timeoutMs = 15000)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < timeoutMs)
        QTest::qWait(5);
    return ready();
}

bool writeText(const QString& path, const QString& text)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(text.toUtf8()) == text.toUtf8().size();
}

QString fixtureText()
{
    return QStringLiteral(
        "module palette_child #(parameter WIDTH=8)"
        "(input logic clk, input logic [WIDTH-1:0] din, output logic [WIDTH-1:0] dout);\n"
        "assign dout = din;\nendmodule\n"
        "module palette_top;\n"
        "typedef enum logic [1:0] { IDLE, RUN, STOP } state_t;\n"
        "typedef struct packed { logic [7:0] payload; logic valid; } packet_t;\n"
        "state_t state;\npacket_t packet;\n"
        "wire wire_sig;\nreg reg_sig;\nlogic logic_sig;\n"
        "logic rank_aaa;\nlogic rank_bbb;\nlogic axxx_byyy_csss;\nlogic abc_signal;\n"
        "parameter SIZE=8;\nlocalparam LIMIT=4;\ntypedef logic [7:0] word_t;\n"
        "task do_task(); endtask\n"
        "function automatic logic ready_fn(); return 1'b1; endfunction\n"
        "always_comb state = IDLE;\n"
        "endmodule\n");
}

void installSource(const QString& path, const QString& text)
{
    SlangManager slang;
    const auto records = slang.extractSymbolRecords(path, text);
    SemanticIndex::getInstance()->clearSemanticState();
    SemanticIndex::getInstance()->setSnapshot(
        std::make_shared<SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                records, {}, {}, {{path, text}})));
}

QStringList titles(const QList<GlobalControlItem>& items)
{
    QStringList result;
    for (const auto& item : items)
        result.append(item.title);
    return result;
}

QJsonArray payload(const QList<GlobalControlItem>& items)
{
    QJsonArray result;
    for (const auto& item : items) {
        QJsonArray slotRows;
        for (const auto& slot : item.templateSlots) {
            slotRows.append(QJsonObject{
                {"name", slot.name}, {"start", slot.start},
                {"length", slot.length}, {"tabStop", slot.tabStop},
                {"visibleWhenEmpty", slot.visibleWhenEmpty}});
        }
        result.append(QJsonObject{
            {"kind", int(item.kind)}, {"operation", int(item.operation)},
            {"id", item.id}, {"title", item.title},
            {"subtitle", item.subtitle}, {"actionId", item.actionId},
            {"executionRoute", item.executionRoute},
            {"parameters", QJsonObject::fromVariantMap(item.parameters)},
            {"insertionText", item.insertionText},
            {"selectionStart", item.selectionStart},
            {"selectionLength", item.selectionLength}, {"slots", slotRows},
            {"replacementStart", item.replacementStart},
            {"replacementLength", item.replacementLength},
            {"revision", item.sourceDocumentRevision}});
    }
    return result;
}

GlobalControlQueryContext queryContext(const QString& path,
                                       const QString& source)
{
    GlobalControlQueryContext context;
    context.editorAvailable = true;
    context.fileName = path;
    context.documentText = source;
    context.moduleName = QStringLiteral("palette_top");
    context.cursorPosition = source.lastIndexOf(QStringLiteral("endmodule"));
    context.cursorLine = source.left(context.cursorPosition).count('\n') + 1;
    context.replacementStart = context.cursorPosition;
    context.replacementLength = 0;
    context.documentRevision = 17;
    return context;
}

void runPaletteRegression(const QString& path)
{
    const QString source = fixtureText();
    installSource(path, source);
    const EditorInsertPaletteService service;
    auto context = queryContext(path, source);
    const auto query = [&](const QString& text) {
        return service.query(GlobalControlCategory::Symbols, text, context);
    };
    const auto modules = query(QStringLiteral("m palette_"));
    expect("module selector returns both modules", modules.size() == 2);
    const auto child = std::find_if(modules.cbegin(), modules.cend(),
                                   [](const auto& item) {
        return item.title == QStringLiteral("palette_child");
    });
    expect("module insertion retains parameter and port mappings",
           child != modules.cend()
               && child->insertionText.contains(QStringLiteral(".WIDTH(WIDTH)"))
               && child->insertionText.contains(QStringLiteral(".clk(clk)"))
               && child->insertionText.contains(QStringLiteral(".din(din)"))
               && child->templateSlots.size() == 5);
    if (child != modules.cend()) {
        expect("selection still selects the instance slot",
               child->selectionStart == child->templateSlots.first().start
                   && child->selectionLength == child->templateSlots.first().length
                   && child->insertionText.mid(child->selectionStart,
                                              child->selectionLength)
                          == QStringLiteral("u_palette_child"));
    }
    const QList<QPair<QString, QStringList>> selectors{
        {"w ", {"wire_sig"}}, {"r ", {"reg_sig"}}, {"p ", {"LIMIT", "SIZE"}},
        {"lp ", {"LIMIT"}}, {"et ", {"state_t"}}, {"ev ", {"state"}},
        {"st ", {}}, {"sv ", {"packet"}},
        {"ee ", {"IDLE", "RUN", "STOP"}}, {"sm ", {"payload", "valid"}},
        {"t ", {"do_task"}}, {"f ", {"ready_fn"}}
    };
    for (const auto& entry : selectors) {
        auto actual = titles(query(entry.first));
        auto expected = entry.second;
        actual.sort();
        expected.sort();
        const QByteArray label = ("selector " + entry.first + " preserves candidates").toUtf8();
        expect(label.constData(), actual == expected);
    }
    expect("all typedef families are visible",
           titles(query("td ")).contains("word_t")
               && titles(query("td ")).contains("state_t")
               && titles(query("td ")).contains("packet_t"));
    expect("combined enum selector retains all families",
           query("e ").size() == 5);
    expect("combined struct selector retains all families",
           query("s ").size() == 3);
    expect("equal score candidates preserve source order",
           titles(query("l rank_")) == QStringList({"rank_aaa", "rank_bbb"}));
    expect("prefix match still outranks fuzzy subsequence",
           titles(query("l abc")).value(0) == QStringLiteral("abc_signal"));
    expect("filter scores do not leak between queries",
           titles(query("l rank_b")) == QStringList({"rank_bbb"})
               && titles(query("l rank_a")) == QStringList({"rank_aaa"}));

    context.memberAccess = true;
    context.memberPath = {"packet"};
    expect("member receiver filters the default query",
           titles(query(QString())).contains("payload")
               && query(QString()).size() == 2);
    expect("specialized member selector respects receiver",
           payload(query("sm pay")) == payload(query("pay")));
    context.memberAccess = false;
    context.memberPath.clear();
    context.expectedTypeIdentifier = QStringLiteral("state");
    const auto expectedEnumItems = query(QString());
    expect("expected enum values retain precedence",
           titles(expectedEnumItems).mid(0, 3)
               == QStringList({"IDLE", "RUN", "STOP"}));
    QSet<QString> ids;
    for (const auto& item : expectedEnumItems)
        ids.insert(item.id);
    expect("overlapping enum and visible queries stay deduplicated",
           ids.size() == expectedEnumItems.size());
    bool metadataCorrect = true;
    for (const auto& item : expectedEnumItems) {
        metadataCorrect &= item.replacementStart == context.replacementStart
            && item.replacementLength == context.replacementLength
            && item.sourceDocumentRevision == context.documentRevision;
    }
    expect("replacement and source revision are retained", metadataCorrect);

    QString many = QStringLiteral("module many_top;\n");
    for (int i = 0; i < 180; ++i)
        many += QStringLiteral("logic sig_%1;\n").arg(i, 3, 10, QLatin1Char('0'));
    many += QStringLiteral("endmodule\n");
    installSource(path, many);
    context = queryContext(path, many);
    context.moduleName = QStringLiteral("many_top");
    expect("symbol results keep the existing 120 row limit", query("l sig_").size() == 120);
    expect("a later candidate remains discoverable by a narrower filter",
           titles(query("l sig_179")) == QStringList({"sig_179"}));
    context.editorAvailable = false;
    expect("no editor still returns no symbols", query("m ").isEmpty());
}

void runCatalogAndPanelRegression(const QString& path)
{
    SemanticIndex::getInstance()->clearSemanticState();
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    auto* index = SemanticIndex::getInstance();
    auto* provider = window.temporaryEditorSearchProvider.get();
    expect("main window owns the live semantic search provider", provider != nullptr);
    if (!provider)
        return;
    const QString source = fixtureText();
    installSource(path, source);
    ProjectSnapshot project;
    project.workspaceRoot = QFileInfo(path).absolutePath();
    project.systemVerilogFiles = {path};
    const auto notifyBatch = [&] {
        emit window.analysisScheduler->workspaceSymbolAnalysisFinished(
            project, 1, index->getSymbolRecords().size());
    };
    const auto notifyFile = [&] {
        emit window.analysisScheduler->fileSymbolAnalysisFinished(
            path, index->getSymbolRecords().size());
    };
    notifyBatch();
    expect("workspace notification publishes a nonempty catalog",
           waitUntil([&] { return provider->semanticCatalogReady()
               && !provider->query("palette_child").isEmpty(); }));
    const auto firstRevision = window.temporaryEditorCatalogSnapshotRevision;
    notifyFile();
    expect("one publication's two notifications reuse one catalog",
           provider->semanticCatalogReady()
               && window.temporaryEditorCatalogSnapshotRevision == firstRevision);
    const QString updated = source + "\nmodule added_later; endmodule\n";
    installSource(path, updated);
    notifyBatch();
    expect("a new publication replaces the catalog",
           waitUntil([&] { return provider->semanticCatalogReady()
               && !provider->query("added_later").isEmpty(); })
               && window.temporaryEditorCatalogSnapshotRevision > firstRevision);
    const auto secondRevision = window.temporaryEditorCatalogSnapshotRevision;
    notifyFile();
    expect("the next publication is also rebuilt only once",
           provider->semanticCatalogReady()
               && window.temporaryEditorCatalogSnapshotRevision == secondRevision);
    index->setSnapshot(index->snapshot());
    notifyBatch();
    expect("republishing the same snapshot object is a new revision",
           window.temporaryEditorCatalogSnapshotRevision > secondRevision
               && waitUntil([&] { return provider->semanticCatalogReady()
                   && !provider->query("added_later").isEmpty(); }));

    window.analysisScheduler->symbolAnalyzer->analyzeFileContent(
        path, QStringLiteral("module single_file_new; endmodule\n"));
    expect("standalone file completion refreshes new published data",
           waitUntil([&] { return provider->semanticCatalogReady()
               && !provider->query("single_file_new").isEmpty()
               && provider->query("added_later").isEmpty(); }));

    // Exercise the actual Ctrl+Space coordinator, panel and Enter dispatch.
    expect("editor file opens", writeText(path, source)
               && window.tabManager->openFileInTab(path));
    auto* editor = window.tabManager->getCurrentEditor();
    if (editor) {
        installSource(path, source);
        editor->setPlainText(source);
        QTextCursor cursor(editor->document());
        cursor.setPosition(source.lastIndexOf(QStringLiteral("endmodule")));
        editor->setTextCursor(cursor);
        editor->setFocus();
        QTest::qWait(25);
        QTest::keyClick(editor, Qt::Key_Space, Qt::ControlModifier);
        auto* panel = window.globalControlCoordinator->panel.get();
        expect("Ctrl+Space opens the supported Symbols panel",
               panel && panel->isVisible()
                   && panel->category() == GlobalControlCategory::Symbols);
        if (panel) {
            panel->searchEdit->setText(QStringLiteral("m palette_child"));
            expect("live panel finds the module",
                   panel->currentItems.size() == 1
                       && panel->currentItems.first().title == QStringLiteral("palette_child"));
            if (!panel->currentItems.isEmpty()) {
                const auto item = panel->currentItems.first();
                const QString before = editor->toPlainText();
                QString expected = before;
                expected.replace(item.replacementStart, item.replacementLength,
                                 item.insertionText);
                QTest::keyClick(panel->searchEdit, Qt::Key_Return);
                expect("Enter inserts the complete current module payload",
                       editor->toPlainText() == expected);
                expect("Enter preserves template slot selection",
                       editor->textCursor().selectedText()
                           == item.insertionText.mid(item.selectionStart,
                                                     item.selectionLength));
            }
            panel->hide();
        }
        editor->document()->setModified(false);
    }
    index->clearSemanticState();
    SlangManager slang;
    const QString nativeA = QStringLiteral("module native_first; endmodule\n");
    index->updateSymbolRecordsForFile(
        path, slang.extractSymbolRecords(path, nativeA), nativeA);
    notifyFile();
    expect("compatibility writes publish refreshable records",
           waitUntil([&] { return provider->semanticCatalogReady()
               && !provider->query("native_first").isEmpty(); }));
    const QString nativeB = QStringLiteral("module native_second; endmodule\n");
    index->updateSymbolRecordsForFile(
        path, slang.extractSymbolRecords(path, nativeB), nativeB);
    notifyFile();
    expect("compatibility writes advance the publication and replace old records",
           waitUntil([&] { return provider->semanticCatalogReady()
               && !provider->query("native_second").isEmpty()
               && provider->query("native_first").isEmpty(); }));

    QTemporaryDir workspaces;
    QDir root(workspaces.path());
    root.mkpath("a");
    root.mkpath("b");
    expect("small workspace fixtures are created",
           writeText(root.filePath("a/a.sv"), "module catalog_a; endmodule\n")
               && writeText(root.filePath("b/b.sv"), "module catalog_b; endmodule\n"));
    const auto containsName = [&](const QString& name) {
        const auto candidates = provider->query(name);
        return std::any_of(candidates.cbegin(), candidates.cend(),
                           [&](const auto& result) { return result.title == name; });
    };
    auto* sessions = window.findChild<WorkspaceSessionCoordinator*>();
    const auto ready = [&](const QString& name) {
        return waitUntil([&] {
            return !window.analysisScheduler->isSemanticAnalysisActive()
                && containsName(name);
        });
    };
    expect("opening workspace A refreshes its catalog",
           sessions && sessions->openWorkspace(root.filePath("a")) && ready("catalog_a"));
    expect("switching to workspace B replaces A's catalog",
           sessions && sessions->openWorkspace(root.filePath("b")) && ready("catalog_b")
               && !containsName("catalog_a"));
    expect("closing B and activating A refreshes A again",
           sessions && sessions->closeWorkspace(window.workspaceManager->activeWorkspaceIndex())
               && ready("catalog_a") && !containsName("catalog_b"));
    expect("closing the final workspace clears the catalog",
           sessions && sessions->closeWorkspace(window.workspaceManager->activeWorkspaceIndex())
               && waitUntil([&] { return provider->semanticSourceCatalog.isEmpty(); }));
    index->clearSemanticState();
}
void runTemplateCacheAndWriterRegression()
{
    QTemporaryDir temp;
    const auto global = temp.filePath("global.json");
    const auto workspace = temp.filePath("workspace.json");
    auto* service = UserTemplateService::getInstance();
    const auto previousGlobal = service->globalTemplateLocation();
    const auto previousWorkspace = service->workspaceTemplateLocation();
    service->setGlobalTemplateFilePath(global);
    service->setWorkspaceTemplateFilePath(workspace);
    UserTemplateRecord record;
    record.id = "cache"; record.commandToken = ";;cache"; record.label = "Cache fixture";
    record.insertText = "logic cached_a;";
    expect("retained template writer commits valid input", service->setRecords({record}).valid);
    auto context = queryContext(temp.filePath("fixture.sv"), fixtureText());
    EditorInsertPaletteService palette;
    const auto first = palette.query(GlobalControlCategory::Templates, "cache", context);
    const auto reads = service->fileReadsForTesting();
    for (int i = 0; i < 25; ++i) palette.query(GlobalControlCategory::Templates, "cache", context);
    expect("repeated template filters reuse context and file contents",
        !first.isEmpty() && palette.contextAnalysesForTesting() == 1 && service->fileReadsForTesting() == reads);
    const auto revision = service->catalogRevision();
    expect("external same-size template update writes",
        writeText(global, QStringLiteral("{\"templates\":[{\"command\":\";;cache\",\"body\":\"logic cached_b;\"}]}")));
    const auto updated = palette.query(GlobalControlCategory::Templates, "cache", context);
    expect("external file version invalidates catalog without reparsing unchanged document",
        service->catalogRevision() > revision && palette.contextAnalysesForTesting() == 1
        && std::any_of(updated.cbegin(), updated.cend(), [](const auto& item) { return item.insertionText == "logic cached_b;"; }));
    const auto beforeReload = service->fileReadsForTesting();
    service->reload();
    expect("manual Reload rereads the catalog", service->fileReadsForTesting() > beforeReload);
    ++context.documentRevision;
    palette.query(GlobalControlCategory::Templates, "cache", context);
    --context.cursorPosition;
    palette.query(GlobalControlCategory::Templates, "cache", context);
    palette.invalidate();
    palette.query(GlobalControlCategory::Templates, "cache", context);
    expect("revision cursor and session invalidation refresh facts", palette.contextAnalysesForTesting() == 4);
    context.fileName = temp.filePath("renamed.sv");
    palette.query(GlobalControlCategory::Templates, "cache", context);
    ++context.documentInstance;
    palette.query(GlobalControlCategory::Templates, "cache", context);
    expect("file and view identity invalidate captured template facts", palette.contextAnalysesForTesting() == 6);
    expect("workspace templates can be added externally",
        writeText(workspace, "[{\"command\":\";;workspace\",\"body\":\"logic workspace;\"}]"));
    expect("new workspace file invalidates absence cache", service->matchingTemplates(";;workspace").size() == 1);
    service->setWorkspaceRoot({});
    expect("closing workspace removes its catalog", service->matchingTemplates(";;workspace").isEmpty());
    expect("corrupt template fixture writes", writeText(global, "{broken"));
    expect("append and removal preserve corrupt source bytes",
        !service->addOrUpdateRecord(record).valid && !service->removeRecord("cache"));
    QFile bad(global); expect("corrupt bytes remain readable", bad.open(QIODevice::ReadOnly));
    expect("corrupt bytes were not replaced", bad.readAll() == QByteArray("{broken")); bad.close();
    const auto blocked = temp.filePath("blocked");
    expect("blocked writer fixture writes", writeText(blocked, "block"));
    UserTemplateService blockedTemplates(blocked + "/templates.json");
    expect("template atomic write failure is reported", !blockedTemplates.setRecords({record}).valid);
    CustomAbbreviationService abbreviations(blocked + "/aliases.ini");
    CustomAbbreviationRecord alias{"alias", "lg", ";l", "Logic", "Logic command"};
    const auto failed = abbreviations.setRecords({alias});
    expect("abbreviation write failure is reported", !failed.valid && !failed.failureReason.isEmpty());
    service->setGlobalTemplateFilePath(previousGlobal);
    service->setWorkspaceTemplateFilePath(previousWorkspace);
}
void runTemplateInsertionVersionGuards(const QString& path)
{
    QTemporaryDir templates;
    auto* service = UserTemplateService::getInstance();
    const auto previousGlobal = service->globalTemplateLocation();
    const auto previousWorkspace = service->workspaceTemplateLocation();
    MainWindow window;
    window.workspaceSessionCoordinator->setRestoreOnActivation(false);
    window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
    expect("template guard source is writable", writeText(path, "module guarded;\n\nendmodule\n"));
    expect("template guard source opens", window.tabManager->openFileInTab(path));
    service->setGlobalTemplateFilePath(templates.filePath("global.json"));
    service->setWorkspaceTemplateFilePath(templates.filePath("workspace.json"));
    auto* editor = window.tabManager->getCurrentEditor();
    QTextCursor cursor(editor->document()); cursor.setPosition(16); editor->setTextCursor(cursor);
    window.show();
    window.activateWindow();
    auto* coordinator = window.globalControlCoordinator.get();
    auto capture = [&] {
        editor->setFocus();
        QApplication::processEvents();
        const auto context = coordinator->contextProvider();
        expect("production insertion context belongs to the focused document", context.editorAvailable
            && context.documentInstance == window.tabManager->sharedDocumentForEditor(editor)->instanceSerial());
        const auto items = coordinator->itemProvider(GlobalControlCategory::Templates, "", context);
        const auto found = std::find_if(items.begin(), items.end(), [](const auto& item) {
            return item.kind == GlobalControlItemKind::Template
                && item.operation == GlobalControlItemOperation::InsertText && !item.insertionText.isEmpty();
        });
        expect("production template provider returns an insertion", found != items.end());
        return found != items.end() ? *found : GlobalControlItem{};
    };
    auto item = capture();
    auto before = editor->toPlainText();
    service->reload();
    coordinator->dispatch(item);
    expect("manual template reload invalidates a captured insertion", editor->toPlainText() == before);
    item = capture();
    cursor = editor->textCursor(); cursor.movePosition(QTextCursor::NextCharacter); editor->setTextCursor(cursor);
    coordinator->dispatch(item);
    expect("cursor movement invalidates a captured template insertion", editor->toPlainText() == before);
    item = capture();
    editor->insertPlainText(" ");
    before = editor->toPlainText();
    coordinator->dispatch(item);
    expect("document edit invalidates a captured template insertion", editor->toPlainText() == before);
    item = capture();
    coordinator->dispatch(item);
    expect("fresh production template insertion still succeeds", editor->toPlainText() != before);
    window.tabManager->clearCrashRecoveryAfterNormalClose();
    service->setGlobalTemplateFilePath(previousGlobal);
    service->setWorkspaceTemplateFilePath(previousWorkspace);
}
} // namespace

int main(int argc, char** argv)
{
    QTemporaryDir settings;
    QCoreApplication::setOrganizationName("ZeroSlackInsertCatalogTest");
    QCoreApplication::setApplicationName("Isolated");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH",
            settings.filePath("sessions.ini").toUtf8());
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
#ifdef ZEROSLACK_ENABLE_ELA
    ApplicationThemeManager::instance().selectBackend(UiStyleBackend::Ela);
#endif
    ApplicationThemeManager::instance().applyToApplication();
    expect("isolated settings directory is available", settings.isValid());
    const QString path = settings.filePath("palette.sv");
    runPaletteRegression(path);
    runTemplateCacheAndWriterRegression();
    runCatalogAndPanelRegression(path);
    runTemplateInsertionVersionGuards(settings.filePath("guarded.sv"));
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
