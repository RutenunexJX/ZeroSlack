// Headless jump-resolution test. Builds semantic records from Slang, constructs a MyCodeEditor offscreen
// (no window shown), sets its file/text/cursor, and drives definition availability / target resolution.
#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QTextCursor>
#include <QString>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QTextBlock>
#include <QTextDocument>
#include <QPlainTextEdit>
#include <QCompleter>
#include <QCoreApplication>
#include <QTimer>
#include <QMouseEvent>
#include <QTextEdit>
#include <cstdio>
#include <memory>
#include "slangmanager.h"
#include "completionmodel.h"
#include "documentmodel.h"
#include "definitionservice.h"
#include "editorsemanticcontextservice.h"
#include "projectmodel.h"
#include "sourcenavigationservice.h"
#include "semanticdecorationservice.h"
#include "semantic_fixture_records.h"
#include "semanticindexsnapshot.h"
#include "saferenameservice.h"
#include "symboltaxonomy.h"
#include "mycodeeditor.h"

static int g_checks = 0, g_fails = 0;

static std::shared_ptr<SemanticIndexSnapshot> sharedSnapshotFromRecords(
    const QList<SemanticSymbolRecord>& records,
    const QList<SemanticRelationship>& relationships = {},
    const QList<SemanticDiagnostic>& diagnostics = {},
    const QHash<QString, QString>& fileContents = {})
{
    return std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            records,
            relationships,
            diagnostics,
            fileContents));
}

static void expectBool(const char* what, bool got, bool want) {
    ++g_checks; bool ok = (got == want); if (!ok) ++g_fails;
    printf("[%s] %-46s got=%s want=%s\n", ok ? "PASS" : "FAIL", what,
           got ? "true" : "false", want ? "true" : "false");
}

static void expectEq(const char* what, const QString& got, const QString& want) {
    ++g_checks; bool ok = (got == want); if (!ok) ++g_fails;
    printf("[%s] %-46s got=\"%s\" want=\"%s\"\n", ok ? "PASS" : "FAIL", what,
           got.toLocal8Bit().constData(), want.toLocal8Bit().constData());
}

// Put the caret on a given 0-based block (line) for semantic module context.
static void placeCursor(MyCodeEditor& ed, int block) {
    QTextCursor c = ed.textCursor();
    c.movePosition(QTextCursor::Start);
    c.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, block);
    ed.setTextCursor(c);
}

static bool safeRenamePlanHasEdit(const SafeRenamePlan& plan,
                                  const QString& fileName,
                                  int position,
                                  const QString& newText)
{
    const QString normalizedFile =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
    for (const SafeRenameFileEdits& fileEdits : plan.fileEdits) {
        const QString candidateFile =
            QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileEdits.fileName).absoluteFilePath()));
        if (candidateFile != normalizedFile)
            continue;
        for (const SafeRenameTextEdit& edit : fileEdits.edits) {
            if (edit.startPosition == position && edit.newText == newText)
                return true;
        }
    }
    return false;
}

static QString normalizedTestPath(const QString& fileName)
{
    const QFileInfo directInfo(fileName);
    if (directInfo.exists() || directInfo.isAbsolute()) {
        return QDir::cleanPath(
            QDir::fromNativeSeparators(directInfo.absoluteFilePath()));
    }

    QStringList roots;
    roots << QDir::currentPath() << QCoreApplication::applicationDirPath();
    for (const QString& root : std::as_const(roots)) {
        QDir dir(root);
        for (int depth = 0; depth < 8; ++depth) {
            const QString candidate = dir.absoluteFilePath(fileName);
            if (QFileInfo(candidate).exists()) {
                return QDir::cleanPath(
                    QDir::fromNativeSeparators(
                        QFileInfo(candidate).absoluteFilePath()));
            }
            if (!dir.cdUp())
                break;
        }
    }

    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

static QString loadTestTextFile(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

static void runSafeRenameServiceRegression()
{
    const QString defFile =
        QFileInfo(QStringLiteral("test_sv/safe_rename_defs.sv")).absoluteFilePath();
    const QString useFile =
        QFileInfo(QStringLiteral("test_sv/safe_rename_use.sv")).absoluteFilePath();
    const QString defText =
        QStringLiteral("module top;\n"
                       "  parameter int P_WIDTH = 8;\n"
                       "endmodule\n");
    const QString useText =
        QStringLiteral("module top;\n"
                       "  logic [P_WIDTH-1:0] data;\n"
                       "endmodule\n");
    const int defPos = defText.indexOf(QStringLiteral("P_WIDTH"));
    const int usePos = useText.indexOf(QStringLiteral("P_WIDTH"));

    const SemanticSymbolRecord defModule =
        SemanticFixtureRecordBuilder(QStringLiteral("top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(defFile)
            .withLocalHandle(1)
            .withRange(1, 1, 3, 10)
            .record();
    const SemanticSymbolRecord useModule =
        SemanticFixtureRecordBuilder(QStringLiteral("top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(useFile)
            .withLocalHandle(2)
            .withRange(1, 1, 3, 10)
            .record();
    const SemanticSymbolRecord parameter =
        SemanticFixtureRecordBuilder(QStringLiteral("P_WIDTH"),
                                     SymbolTaxonomy::DeclarationKind::Parameter)
            .withFile(defFile)
            .withLocalHandle(3)
            .withLine(2, defPos - defText.lastIndexOf(QLatin1Char('\n'), defPos))
            .withTextSpan(defPos, QStringLiteral("P_WIDTH").size())
            .inModule(QStringLiteral("top"))
            .record();
    const SemanticSymbolRecord conflict =
        SemanticFixtureRecordBuilder(QStringLiteral("P_DEPTH"),
                                     SymbolTaxonomy::DeclarationKind::Parameter)
            .withFile(defFile)
            .withLocalHandle(4)
            .withLine(2, 3)
            .inModule(QStringLiteral("top"))
            .record();

    QHash<QString, QString> contents;
    contents.insert(defFile, defText);
    contents.insert(useFile, useText);

    SemanticIndex index;
    index.setSnapshot(sharedSnapshotFromRecords({defModule, useModule, parameter},
                                                {},
                                                {},
                                                contents));
    SafeRenameService renameService(&index);
    SafeRenamePlanQuery query;
    query.symbolName = QStringLiteral("P_WIDTH");
    query.newName = QStringLiteral("P_DATA");
    query.fileName = useFile;
    query.moduleName = QStringLiteral("top");
    query.documentText = useText;
    query.cursorPosition = usePos;

    const SafeRenamePlan plan = renameService.createRenamePlan(query);
    expectBool("safe rename service resolves use to definition",
               plan.isReady() && plan.editCount() == 2,
               true);
    expectBool("safe rename service includes definition edit",
               safeRenamePlanHasEdit(plan, defFile, defPos, QStringLiteral("P_DATA")),
               true);
    expectBool("safe rename service includes use edit",
               safeRenamePlanHasEdit(plan, useFile, usePos, QStringLiteral("P_DATA")),
               true);

    index.setSnapshot(sharedSnapshotFromRecords({defModule, useModule, parameter, conflict},
                                                {},
                                                {},
                                                contents));
    query.newName = QStringLiteral("P_DEPTH");
    const SafeRenamePlan conflictPlan = renameService.createRenamePlan(query);
    expectBool("safe rename service detects definition conflict",
               conflictPlan.status == SafeRenamePlanStatus::ConflictingDefinition
                   && conflictPlan.conflictingDefinitions.size() == 1,
               true);
    query.forceConflicts = true;
    const SafeRenamePlan forcedPlan = renameService.createRenamePlan(query);
    expectBool("safe rename service can force conflicting target",
               forcedPlan.isReady() && forcedPlan.editCount() == 2,
               true);
}

static void runRealWorkspaceEnumDecorationRegression()
{
    const QString workspaceRoot =
        normalizedTestPath(QStringLiteral("test_sv/new"));
    if (!QFileInfo(workspaceRoot).isDir()) {
        expectBool("real enum decoration workspace exists", false, true);
        return;
    }

    QStringList files;
    QDirIterator it(workspaceRoot,
                    QStringList{"*.sv", "*.svh", "*.v"},
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(normalizedTestPath(it.next()));

    ProjectModel project;
    project.setWorkspaceRoot(workspaceRoot);
    project.setScannedFiles(files);
    const ProjectSnapshot snapshot = project.snapshot();

    SlangManager slang;
    QList<SemanticSymbolRecord> records =
        slang.extractWorkspaceSymbolRecords(snapshot.systemVerilogFiles,
                                            snapshot.includeDirs,
                                            snapshot.defines);
    QHash<QString, QString> fileContents;
    for (const QString& fileName : files)
        fileContents.insert(fileName, loadTestTextFile(fileName));

    const QString enumUseFile = normalizedTestPath(
        QDir(workspaceRoot).filePath(
            QStringLiteral("elec_phy_import/elec/elec_inj.sv")));
    const QString enumUseContent = fileContents.value(enumUseFile);

    bool hasRealEnumRecord = false;
    for (const SemanticSymbolRecord& record : records) {
        if (record.name == QStringLiteral("E_NOISE_WGN")
            && record.collectorKind
                == SymbolTaxonomy::CollectorKind::EnumValue) {
            hasRealEnumRecord = true;
            break;
        }
    }
    expectBool("real workspace has E_NOISE_WGN enum value record",
               hasRealEnumRecord,
               true);

    const auto previousSnapshot = SemanticIndex::getInstance()->snapshot();
    SemanticIndex::getInstance()->setSnapshot(
        sharedSnapshotFromRecords(records,
                                  {},
                                  {},
                                  fileContents));

    SemanticDecorationQuery query;
    query.fileName = enumUseFile;
    query.documentText = enumUseContent;
    const SemanticDecorationReport report =
        SemanticDecorationService::getInstance()
            ->decorationsForDocument(query);

    bool hasEnumDecorationAtRealUse = false;
    for (const SemanticDecoration& decoration : report.decorations) {
        if (decoration.role != SemanticDecorationRole::EnumValue
            || decoration.text != QStringLiteral("E_NOISE_WGN")) {
            continue;
        }
        const QTextDocument document(enumUseContent);
        const QTextBlock block =
            document.findBlock(decoration.startPosition);
        if (block.isValid() && block.blockNumber() == 1093) {
            hasEnumDecorationAtRealUse = true;
            break;
        }
    }
    expectBool("real workspace decorates E_NOISE_WGN at elec_inj:1094",
               hasEnumDecorationAtRealUse,
               true);

    SemanticIndex::getInstance()->setSnapshot(previousSnapshot);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    runSafeRenameServiceRegression();
    runRealWorkspaceEnumDecorationRegression();

    QString path = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                              : QStringLiteral("test_sv/test_symbols.sv");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QFile::Text)) { fprintf(stderr, "cannot open\n"); return 2; }
    QString content = QTextStream(&f).readAll();
    f.close();

    SlangManager mgr;
    const QList<SemanticSymbolRecord> symbolRecords =
        mgr.extractSymbolRecords(path, content);
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        path,
        symbolRecords,
        content);

    // After the semantic record mirror, function-local symbols keep their
    // subroutine owner scope rather than being inferred from module bounds.
    auto scopeOf = [&symbolRecords](const QString& name,
                                    SymbolTaxonomy::CollectorKind kind)
        -> QString {
        for (const SemanticSymbolRecord& record : symbolRecords) {
            if (record.name == name && record.collectorKind == kind)
                return record.owner.name;
        }
        return QStringLiteral("<none>");
    };
    QString xScope = scopeOf("x", SymbolTaxonomy::CollectorKind::Logic);
    printf("-- DB moduleScope after setSymbolsForFile: x=%s --\n", xScope.toLocal8Bit().constData());
    ++g_checks; if (xScope != QStringLiteral("add_one")) ++g_fails;
    printf("[%s] formal-arg x keeps moduleScope=add_one (not clobbered to top)\n",
           xScope == QStringLiteral("add_one") ? "PASS" : "FAIL");
    bool sawAdderAPin = false;
    for (const SemanticSymbolRecord& record : symbolRecords) {
        sawAdderAPin = sawAdderAPin
            || (record.name == QStringLiteral("a")
                && record.collectorKind
                    == SymbolTaxonomy::CollectorKind::InstPin
                && record.owner.name == QStringLiteral("top")
                && record.type.resolvedTypeName == QStringLiteral("adder"));
    }
    expectBool("Slang collector emits named instance pin",
               sawAdderAPin,
               true);

    MyCodeEditor ed;
    ed.setPlainText(content);
    DocumentModel documentModel;
    documentModel.registerEditor(&ed, path);

    // Caret inside module `top` (top spans ~line 66..end; block 95 is well inside).
    placeCursor(ed, 95);
    printf("-- caret inside top --\n");
    const EditorSemanticContext topContext =
        ed.editorSemanticContextForPosition(ed.textCursor().position());
    expectBool("canResolve(counter) [module var]",
               EditorSemanticContextService::getInstance()
                   ->canResolveDefinitionTarget(QStringLiteral("counter"), topContext),
               true);
    expectBool("canResolve(clk) [module port]",
               EditorSemanticContextService::getInstance()
                   ->canResolveDefinitionTarget(QStringLiteral("clk"), topContext),
               true);
    expectBool("canResolve(DATA_WIDTH) [module param]",
               EditorSemanticContextService::getInstance()
                   ->canResolveDefinitionTarget(QStringLiteral("DATA_WIDTH"), topContext),
               true);
    expectBool("canResolve(a) [only in adder] isolated",
               EditorSemanticContextService::getInstance()
                   ->canResolveDefinitionTarget(QStringLiteral("a"), topContext),
               false);
    DefinitionQuery hiddenDefinitionQuery;
    hiddenDefinitionQuery.symbolName = QStringLiteral("a");
    hiddenDefinitionQuery.fileName = path;
    hiddenDefinitionQuery.moduleName = QStringLiteral("top");
    const DefinitionResult hiddenDefinitionResult =
        DefinitionService::getInstance()->resolveDefinition(hiddenDefinitionQuery);
    expectBool("DefinitionService hidden miss reason",
               !hiddenDefinitionResult.found
                   && hiddenDefinitionResult.matchingNameCandidateCount > 0
                   && hiddenDefinitionResult.typeCompatibleCandidateCount > 0
                   && hiddenDefinitionResult.visibleCandidateCount == 0
                   && hiddenDefinitionResult.missReason
                       == SemanticDefinitionMissReason::NotVisibleInContext,
               true);
    expectBool("canResolve(nonexistent)",
               EditorSemanticContextService::getInstance()
                   ->canResolveDefinitionTarget(QStringLiteral("nope_xyz"), topContext),
               false);
    // Type-name-scoped symbols (current behavior - surfaces the moduleScope vs module filter):
    expectBool("canResolve(STATE_IDLE) [enum value]",
               EditorSemanticContextService::getInstance()
                   ->canResolveDefinitionTarget(QStringLiteral("STATE_IDLE"), topContext),
               true);
    expectBool("canResolve(red) [struct member]",
               EditorSemanticContextService::getInstance()
                   ->canResolveDefinitionTarget(QStringLiteral("red"), topContext),
               true);

    const QString formalPortText = QStringLiteral(".a   (counter)");
    const int formalPortOffset = content.indexOf(formalPortText);
    expectBool("test fixture has named instance port .a",
               formalPortOffset >= 0,
               true);
    if (formalPortOffset >= 0) {
        const int formalPortDotPos = formalPortOffset;
        const EditorSemanticContext formalPortDotContext =
            ed.editorSemanticContextForPosition(formalPortDotPos);
        const SourceEditorNavigationTarget formalPortSourceTarget =
            EditorSemanticContextService::getInstance()
                ->definitionSourceNavigationTarget(formalPortDotContext);
        expectBool("Editor treats .formal dot as formal port target",
                   formalPortSourceTarget.matched
                       && formalPortSourceTarget.text == QStringLiteral("a")
                       && formalPortSourceTarget.jumpable
                       && formalPortSourceTarget.cursorColumn
                           == formalPortSourceTarget.startColumn,
                   true);
        const DefinitionNavigationTarget formalPortDotTarget =
            EditorSemanticContextService::getInstance()->resolveDefinitionTarget(
                QStringLiteral("a"),
                formalPortDotContext);
        expectBool("Editor jumps instance formal port from dot",
                   formalPortDotTarget.found
                       && formalPortDotTarget.symbolRecord.owner.name
                           == QStringLiteral("adder")
                       && formalPortDotTarget.symbolRecord.collectorKind
                           == SymbolTaxonomy::CollectorKind::PortInput,
                   true);

        const int formalPortNamePos = formalPortOffset + 1;
        const EditorSemanticContext formalPortContext =
            ed.editorSemanticContextForPosition(formalPortNamePos);
        const DefinitionNavigationTarget formalPortTarget =
            EditorSemanticContextService::getInstance()->resolveDefinitionTarget(
                QStringLiteral("a"),
                formalPortContext);
        expectBool("Editor jumps instance formal port to child declaration",
                   formalPortTarget.found
                       && formalPortTarget.symbolRecord.owner.name
                           == QStringLiteral("adder")
                       && formalPortTarget.symbolRecord.collectorKind
                           == SymbolTaxonomy::CollectorKind::PortInput,
                   true);

        const int actualSignalPos =
            formalPortOffset + formalPortText.indexOf(QStringLiteral("counter"));
        const EditorSemanticContext actualSignalContext =
            ed.editorSemanticContextForPosition(actualSignalPos);
        const DefinitionNavigationTarget actualSignalTarget =
            EditorSemanticContextService::getInstance()->resolveDefinitionTarget(
                QStringLiteral("counter"),
                actualSignalContext);
        expectBool("Editor keeps instance actual signal local",
                   actualSignalTarget.found
                       && actualSignalTarget.symbolRecord.name
                           == QStringLiteral("counter")
                       && actualSignalTarget.symbolRecord.owner.name
                           == QStringLiteral("top"),
                   true);
    }

    const QString decorationFile =
        QFileInfo(path).dir().filePath(QStringLiteral("semantic_decoration_fixture.sv"));
    const QString decorationContent =
        QStringLiteral("module top #(parameter int WIDTH = 8) (\n"
                       "  input logic clk,\n"
                       "  output logic done\n"
                       ");\n"
                       "  typedef enum logic [1:0] {IDLE, BUSY, DONE} state_t;\n"
                       "  state_t state;\n"
                       "  localparam int DEPTH = WIDTH + 1;\n"
                       "  assign done = (state == IDLE) && clk && (DEPTH > WIDTH);\n"
                       "  child u_child (\n"
                       "    .clk(clk)\n"
                       "  );\n"
                       "endmodule\n"
                       "module child(input logic clk); endmodule\n");
    QList<SemanticSymbolRecord> decorationRecords;
    decorationRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(decorationFile)
            .withLine(1, 8)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record());
    decorationRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("WIDTH"),
                                     SymbolTaxonomy::DeclarationKind::Parameter)
            .withFile(decorationFile)
            .withLine(1, 28)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Parameter)
            .inModule(QStringLiteral("top"))
            .record());
    decorationRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("clk"),
                                     SymbolTaxonomy::DeclarationKind::Port)
            .withFile(decorationFile)
            .withLine(2, 15)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .inModule(QStringLiteral("top"))
            .record());
    decorationRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("done"),
                                     SymbolTaxonomy::DeclarationKind::Port)
            .withFile(decorationFile)
            .withLine(3, 16)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortOutput)
            .inModule(QStringLiteral("top"))
            .record());
    decorationRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("state_t"),
                                     SymbolTaxonomy::DeclarationKind::Typedef)
            .withFile(decorationFile)
            .withLine(5, 57)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Typedef)
            .inModule(QStringLiteral("top"))
            .record());
    decorationRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("IDLE"),
                                     SymbolTaxonomy::DeclarationKind::Enum)
            .withFile(decorationFile)
            .withLine(5, 31)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumValue)
            .inModule(QStringLiteral("top"))
            .record());
    decorationRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("BUSY"),
                                     SymbolTaxonomy::DeclarationKind::Enum)
            .withFile(decorationFile)
            .withLine(5, 37)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumValue)
            .inModule(QStringLiteral("top"))
            .record());
    decorationRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("DONE"),
                                     SymbolTaxonomy::DeclarationKind::Enum)
            .withFile(decorationFile)
            .withLine(5, 43)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumValue)
            .inModule(QStringLiteral("top"))
            .record());
    decorationRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("state"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(decorationFile)
            .withLine(6, 11)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumVariable)
            .inModule(QStringLiteral("top"))
            .record());
    decorationRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("DEPTH"),
                                     SymbolTaxonomy::DeclarationKind::Localparam)
            .withFile(decorationFile)
            .withLine(7, 18)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Localparam)
            .inModule(QStringLiteral("top"))
            .record());
    decorationRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("u_child"),
                                     SymbolTaxonomy::DeclarationKind::Instance)
            .withFile(decorationFile)
            .withLine(9, 9)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Inst)
            .withType(QStringLiteral("child"),
                      QStringLiteral("child"),
                      SymbolTaxonomy::DeclarationKind::Module)
            .inModule(QStringLiteral("top"))
            .record());
    decorationRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("clk"),
                                     SymbolTaxonomy::DeclarationKind::Port)
            .withFile(decorationFile)
            .withRange(10, 6, 10, 9)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::InstPin)
            .withType(QStringLiteral("child"),
                      QStringLiteral("child"),
                      SymbolTaxonomy::DeclarationKind::Module)
            .inModule(QStringLiteral("top"))
            .record());
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        decorationFile,
        decorationRecords,
        decorationContent);

    SemanticDecorationQuery decorationQuery;
    decorationQuery.fileName = decorationFile;
    decorationQuery.documentText = decorationContent;
    const SemanticDecorationReport decorationReport =
        SemanticDecorationService::getInstance()
            ->decorationsForDocument(decorationQuery);
    auto hasDecoration = [&decorationReport](
        SemanticDecorationRole role,
        const QString& text) {
        for (const SemanticDecoration& decoration : decorationReport.decorations) {
            if (decoration.role == role && decoration.text == text)
                return true;
        }
        return false;
    };
    auto decorationCount = [&decorationReport](
        SemanticDecorationRole role,
        const QString& text) {
        int count = 0;
        for (const SemanticDecoration& decoration : decorationReport.decorations) {
            if (decoration.role == role && decoration.text == text)
                ++count;
        }
        return count;
    };
    expectBool("SemanticDecoration maps module/interface role",
               hasDecoration(SemanticDecorationRole::ModuleInterface,
                             QStringLiteral("top")),
               true);
    expectBool("SemanticDecoration maps parameter role",
               hasDecoration(SemanticDecorationRole::Parameter,
                             QStringLiteral("WIDTH")),
               true);
    expectBool("SemanticDecoration maps parameter references",
               decorationCount(SemanticDecorationRole::Parameter,
                               QStringLiteral("WIDTH")) >= 2
                   && decorationCount(SemanticDecorationRole::Parameter,
                                      QStringLiteral("DEPTH")) >= 2,
               true);
    expectBool("SemanticDecoration maps module port role",
               decorationCount(SemanticDecorationRole::ModulePort,
                               QStringLiteral("clk")) >= 2
                   && decorationCount(SemanticDecorationRole::ModulePort,
                                      QStringLiteral("done")) >= 2,
               true);
    expectBool("SemanticDecoration maps typedef use role",
               decorationCount(SemanticDecorationRole::TypeAlias,
                               QStringLiteral("state_t")) >= 2,
               true);
    expectBool("SemanticDecoration maps enum value use role",
               decorationCount(SemanticDecorationRole::EnumValue,
                               QStringLiteral("IDLE")) >= 2
                   && hasDecoration(SemanticDecorationRole::EnumValue,
                                    QStringLiteral("BUSY"))
                   && hasDecoration(SemanticDecorationRole::EnumValue,
                                    QStringLiteral("DONE")),
               true);
    expectBool("SemanticDecoration maps instance role",
               hasDecoration(SemanticDecorationRole::InstanceName,
                             QStringLiteral("u_child")),
               true);
    expectBool("SemanticDecoration maps formal port separately",
               hasDecoration(SemanticDecorationRole::FormalPort,
                             QStringLiteral("clk")),
               true);
    expectBool("SemanticDecoration maps actual signal separately",
               hasDecoration(SemanticDecorationRole::ActualSignal,
                             QStringLiteral("clk")),
               true);

    const QString packageFile =
        QFileInfo(path).dir().filePath(QStringLiteral("pkg_states.sv"));
    const QString packageUseFile =
        QFileInfo(path).dir().filePath(QStringLiteral("uses_pkg.sv"));
    const QString packageContent =
        QStringLiteral("package pkg_states;\n"
                       "  typedef enum logic {PKG_IDLE, PKG_RUN, PKG_LOCAL} pkg_state_t;\n"
                       "endpackage\n");
    const QString packageUseContent =
        QStringLiteral("module uses_pkg;\n"
                       "  import pkg_states::*;\n"
                       "  localparam int PKG_LOCAL = 1;\n"
                       "  assign a = (PKG_IDLE == PKG_RUN);\n"
                       "  assign b = (pkg_states::PKG_RUN == PKG_IDLE);\n"
                       "  assign c = PKG_LOCAL;\n"
                       "endmodule\n");
    QList<SemanticSymbolRecord> packageRecords;
    packageRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("pkg_states"),
                                     SymbolTaxonomy::DeclarationKind::Package)
            .withFile(packageFile)
            .withLine(1, 9)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Package)
            .record());
    packageRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("PKG_IDLE"),
                                     SymbolTaxonomy::DeclarationKind::Enum)
            .withFile(packageFile)
            .withLine(2, 23)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumValue)
            .inPackage(QStringLiteral("pkg_states"))
            .record());
    packageRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("PKG_RUN"),
                                     SymbolTaxonomy::DeclarationKind::Enum)
            .withFile(packageFile)
            .withLine(2, 33)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumValue)
            .inPackage(QStringLiteral("pkg_states"))
            .record());
    packageRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("PKG_LOCAL"),
                                     SymbolTaxonomy::DeclarationKind::Enum)
            .withFile(packageFile)
            .withLine(2, 42)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumValue)
            .inPackage(QStringLiteral("pkg_states"))
            .record());
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        packageFile,
        packageRecords,
        packageContent);

    QList<SemanticSymbolRecord> packageUseRecords;
    packageUseRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("uses_pkg"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(packageUseFile)
            .withRange(1, 8, 7, 10)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record());
    packageUseRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("PKG_LOCAL"),
                                     SymbolTaxonomy::DeclarationKind::Localparam)
            .withFile(packageUseFile)
            .withLine(3, 18)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Localparam)
            .inModule(QStringLiteral("uses_pkg"))
            .record());
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        packageUseFile,
        packageUseRecords,
        packageUseContent);

    SemanticDecorationQuery packageUseQuery;
    packageUseQuery.fileName = packageUseFile;
    packageUseQuery.documentText = packageUseContent;
    const SemanticDecorationReport packageUseReport =
        SemanticDecorationService::getInstance()
            ->decorationsForDocument(packageUseQuery);
    auto packageUseDecorationCount = [&packageUseReport](
        SemanticDecorationRole role,
        const QString& text) {
        int count = 0;
        for (const SemanticDecoration& decoration : packageUseReport.decorations) {
            if (decoration.role == role && decoration.text == text)
                ++count;
        }
        return count;
    };
    expectBool("SemanticDecoration maps imported package enum values",
               packageUseDecorationCount(SemanticDecorationRole::EnumValue,
                                         QStringLiteral("PKG_IDLE")) >= 2
                   && packageUseDecorationCount(SemanticDecorationRole::EnumValue,
                                                QStringLiteral("PKG_RUN")) >= 2,
               true);
    expectBool("SemanticDecoration keeps local symbols above package enum values",
               packageUseDecorationCount(SemanticDecorationRole::Parameter,
                                         QStringLiteral("PKG_LOCAL")) >= 2
                   && packageUseDecorationCount(SemanticDecorationRole::EnumValue,
                                                QStringLiteral("PKG_LOCAL")) == 0,
               true);

    const QString realLikeRoot =
        QFileInfo(path).dir().filePath(QStringLiteral("new"));
    const QString realLikePackageFile =
        QDir(realLikeRoot).filePath(QStringLiteral("PKG_global.sv"));
    const QString realLikeIncludeFile =
        QDir(realLikeRoot).filePath(QStringLiteral("_svh.svh"));
    const QString realLikeUseFile =
        QDir(realLikeRoot).filePath(
            QStringLiteral("elec_phy_import/elec/elec_inj.sv"));
    const QString realLikePackageContent =
        QStringLiteral("package gl_pkg;\n"
                       "  typedef enum logic[2:0]{\n"
                       "    E_NOISE_DIS,\n"
                       "    E_NOISE_WGN,\n"
                       "    E_NOISE_SAWT\n"
                       "  } noise_type_e;\n"
                       "endpackage\n");
    const QString realLikeIncludeContent =
        QStringLiteral("import gl_pkg::*;\n");
    const QString realLikeUseContent =
        QStringLiteral("`include \"_svh.svh\"\n"
                       "module elec_inj;\n"
                       "  assign wgn_addr = (elec_noise_type == E_NOISE_WGN) ? cnt : 19'd0;\n"
                       "endmodule\n");
    QList<SemanticSymbolRecord> realLikePackageRecords;
    realLikePackageRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("gl_pkg"),
                                     SymbolTaxonomy::DeclarationKind::Package)
            .withFile(realLikePackageFile)
            .withLine(1, 9)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Package)
            .record());
    realLikePackageRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("E_NOISE_WGN"),
                                     SymbolTaxonomy::DeclarationKind::Enum)
            .withFile(realLikePackageFile)
            .withLine(4, 5)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumValue)
            .inPackage(QStringLiteral("gl_pkg"))
            .record());
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        realLikePackageFile,
        realLikePackageRecords,
        realLikePackageContent);
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        realLikeIncludeFile,
        {},
        realLikeIncludeContent);
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        realLikeUseFile,
        {SemanticFixtureRecordBuilder(QStringLiteral("elec_inj"),
                                      SymbolTaxonomy::DeclarationKind::Module)
             .withFile(realLikeUseFile)
             .withRange(2, 8, 4, 10)
             .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
             .record()},
        realLikeUseContent);

    SemanticDecorationQuery realLikeUseQuery;
    realLikeUseQuery.fileName = realLikeUseFile;
    realLikeUseQuery.documentText = realLikeUseContent;
    const SemanticDecorationReport realLikeUseReport =
        SemanticDecorationService::getInstance()
            ->decorationsForDocument(realLikeUseQuery);
    int realLikeEnumUseCount = 0;
    for (const SemanticDecoration& decoration : realLikeUseReport.decorations) {
        if (decoration.role == SemanticDecorationRole::EnumValue
            && decoration.text == QStringLiteral("E_NOISE_WGN")) {
            ++realLikeEnumUseCount;
        }
    }
    expectBool("SemanticDecoration follows include import package enum values",
               realLikeEnumUseCount >= 1,
               true);

    MyCodeEditor diagnosticEditor;
    diagnosticEditor.setPlainText(QStringLiteral("module bad;\n  broken\nendmodule\n"));
    SemanticDiagnostic diagnostic;
    diagnostic.fileName = decorationFile;
    diagnostic.line = 2;
    diagnostic.column = 3;
    diagnostic.severity = SemanticDiagnostic::Error;
    diagnostic.message = QStringLiteral("expected ';'");
    diagnosticEditor.setDiagnosticHighlights({diagnostic});
    bool diagnosticLineBackground = false;
    bool diagnosticWaveUnderline = false;
    for (const QTextEdit::ExtraSelection& selection :
         diagnosticEditor.extraSelections()) {
        diagnosticLineBackground = diagnosticLineBackground
            || selection.format.hasProperty(QTextFormat::FullWidthSelection);
        diagnosticWaveUnderline = diagnosticWaveUnderline
            || selection.format.underlineStyle()
                == QTextCharFormat::WaveUnderline;
    }
    expectBool("Diagnostic decoration has line background",
               diagnosticLineBackground,
               true);
    expectBool("Diagnostic decoration has wave underline",
               diagnosticWaveUnderline,
               true);
    MyCodeEditor semanticStyleEditor;
    semanticStyleEditor.setPlainText(QStringLiteral("    .clk(clk)\n"));
    SemanticDecoration formalPortDecoration;
    formalPortDecoration.role = SemanticDecorationRole::FormalPort;
    formalPortDecoration.text = QStringLiteral("clk");
    formalPortDecoration.startPosition = 5;
    formalPortDecoration.length = 3;
    semanticStyleEditor.setSemanticDecorations({formalPortDecoration});
    bool semanticForegroundApplied = false;
    for (const QTextEdit::ExtraSelection& selection :
         semanticStyleEditor.extraSelections()) {
        semanticForegroundApplied = semanticForegroundApplied
            || selection.format.foreground().style() != Qt::NoBrush;
    }
    expectBool("Semantic decoration applies foreground style",
               semanticForegroundApplied,
               true);

    const QString syntheticFile =
        QFileInfo(path).dir().filePath(QStringLiteral("definition_service_member_context.sv"));
    QList<SemanticSymbolRecord> syntheticRecords;
    const SemanticSymbolRecord otherRedRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("red"),
                                     SymbolTaxonomy::DeclarationKind::StructMember)
            .withFile(syntheticFile)
            .withLocalHandle(6201)
            .withLine(7, 9)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::StructMember)
            .inStruct(QStringLiteral("other_t"))
            .record();
    syntheticRecords.append(otherRedRecord);

    const SemanticSymbolRecord pixelRedRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("red"),
                                     SymbolTaxonomy::DeclarationKind::StructMember)
            .withFile(syntheticFile)
            .withLocalHandle(6202)
            .withLine(11, 9)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::StructMember)
            .inStruct(QStringLiteral("pixel_t"))
            .record();
    syntheticRecords.append(pixelRedRecord);

    const SemanticSymbolRecord pixelVarRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("pixel"),
                                     SymbolTaxonomy::DeclarationKind::StructVariable)
            .withFile(syntheticFile)
            .withLocalHandle(6203)
            .withLine(20, 5)
            .withCollectorKind(
                SymbolTaxonomy::CollectorKind::PackedStructVariable)
            .withType(QStringLiteral("pixel_t"))
            .record();
    syntheticRecords.append(pixelVarRecord);

    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        syntheticFile,
        syntheticRecords,
        QString());

    DefinitionQuery memberQuery;
    memberQuery.symbolName = QStringLiteral("red");
    memberQuery.fileName = syntheticFile;
    memberQuery.linePrefixBeforeCursor = QStringLiteral("pixel.red");
    const DefinitionResult memberResult =
        DefinitionService::getInstance()->resolveDefinition(memberQuery);
    ++g_checks;
    bool memberOk = memberResult.found
        && memberResult.symbolRecord.owner.name == QStringLiteral("pixel_t")
        && memberResult.symbolRecord.location.startLine
            == pixelRedRecord.location.startLine;
    if (!memberOk) ++g_fails;
    printf("[%s] DefinitionService resolves pixel.red member context to pixel_t.red\n",
           memberOk ? "PASS" : "FAIL");

    DefinitionNavigationService definitionNavigationService;
    DefinitionNavigationContext memberNavigationContext;
    memberNavigationContext.symbolName = QStringLiteral("red");
    memberNavigationContext.fileName = syntheticFile;
    memberNavigationContext.moduleName = QStringLiteral("top");
    memberNavigationContext.lineText = QStringLiteral("assign out = pixel.red;");
    memberNavigationContext.column =
        memberNavigationContext.lineText.indexOf(QStringLiteral("red"))
        + QStringLiteral("red").size();
    const DefinitionNavigationQuery memberNavigationQuery =
        definitionNavigationService.navigationQueryForContext(memberNavigationContext);
    expectEq("DefinitionNavigation query symbol",
             memberNavigationQuery.symbolName,
             QStringLiteral("red"));
    expectEq("DefinitionNavigation query file",
             memberNavigationQuery.fileName,
             syntheticFile);
    expectEq("DefinitionNavigation query module",
             memberNavigationQuery.moduleName,
             QStringLiteral("top"));
    expectEq("DefinitionNavigation query prefix",
             memberNavigationQuery.linePrefixBeforeCursor,
             QStringLiteral("assign out = pixel.red"));
    const DefinitionNavigationTarget memberNavigationTarget =
        definitionNavigationService.resolveTarget(memberNavigationQuery);
    expectBool("DefinitionNavigation resolves member target",
               memberNavigationTarget.found
                   && memberNavigationTarget.symbolName == QStringLiteral("red")
                   && memberNavigationTarget.symbolTypeText == QStringLiteral("member")
                   && memberNavigationTarget.symbolRecord.isValid()
                   && memberNavigationTarget.symbolRecord.localHandle
                       == memberResult.symbolRecord.localHandle
                   && memberNavigationTarget.symbolRecord.stableKey
                       == memberNavigationTarget.symbolStableKey
                   && memberNavigationTarget.symbolRecord.name
                       == QStringLiteral("red")
                   && memberNavigationTarget.symbolRecord.collectorKind
                       == SymbolTaxonomy::CollectorKind::StructMember
                   && memberNavigationTarget.ownerDisplayName
                       == QStringLiteral("pixel_t")
                   && memberNavigationTarget.sourceRoleDisplayName
                       == QStringLiteral("design source"),
               true);

    const QString interfaceNavigationFile = QStringLiteral("interface_nav.sv");
    QList<SemanticSymbolRecord> interfaceNavigationRecords;
    const SemanticSymbolRecord interfaceRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("bus_if"),
                                     SymbolTaxonomy::DeclarationKind::Interface)
            .withFile(interfaceNavigationFile)
            .withLocalHandle(6250)
            .withLine(1, 1)
            .record();
    const SemanticSymbolRecord interfaceModportRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("master"),
                                     SymbolTaxonomy::DeclarationKind::Modport)
            .withFile(interfaceNavigationFile)
            .withLocalHandle(6251)
            .withLine(2, 5)
            .inInterface(QStringLiteral("bus_if"))
            .record();
    const SemanticSymbolRecord interfacePortRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("bus"),
                                     SymbolTaxonomy::DeclarationKind::Port)
            .withFile(interfaceNavigationFile)
            .withLocalHandle(6252)
            .withLine(10, 5)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"),
                       {},
                       true)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInterfaceModport)
            .withType(QStringLiteral("bus_if.master"),
                      QStringLiteral("bus_if"),
                      SymbolTaxonomy::DeclarationKind::Interface,
                      QStringLiteral("master"))
            .record();
    interfaceNavigationRecords.append(interfaceRecord);
    interfaceNavigationRecords.append(interfaceModportRecord);
    interfaceNavigationRecords.append(interfacePortRecord);
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        interfaceNavigationFile,
        interfaceNavigationRecords,
        QString());

    DefinitionNavigationContext interfaceMemberContext;
    interfaceMemberContext.symbolName = QStringLiteral("master");
    interfaceMemberContext.fileName = interfaceNavigationFile;
    interfaceMemberContext.moduleName = QStringLiteral("top");
    interfaceMemberContext.lineText = QStringLiteral("assign use_bus = bus.master;");
    interfaceMemberContext.column =
        interfaceMemberContext.lineText.indexOf(QStringLiteral("master")) + 2;
    const DefinitionNavigationQuery interfaceMemberNavigationQuery =
        definitionNavigationService.navigationQueryForContext(interfaceMemberContext);
    expectEq("DefinitionNavigation interface member prefix",
             interfaceMemberNavigationQuery.linePrefixBeforeCursor,
             QStringLiteral("assign use_bus = bus.master"));
    const DefinitionNavigationTarget interfaceMemberTarget =
        definitionNavigationService.resolveTarget(interfaceMemberNavigationQuery);
    expectBool("DefinitionNavigation resolves interface member from mid-token",
               interfaceMemberTarget.found
                   && interfaceMemberTarget.symbolName == QStringLiteral("master")
                   && interfaceMemberTarget.symbolRecord.owner.name
                       == QStringLiteral("bus_if")
                   && interfaceMemberTarget.symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Modport,
               true);

    const QDir testSvDir = QFileInfo(path).dir();
    const QString realNewRoot = testSvDir.filePath(QStringLiteral("new"));
    const QString realRtlTopFile =
        QDir(realNewRoot).filePath(
            QStringLiteral("elec_phy_import/top/rtl_top.sv"));
    const QString realSvhFile =
        QDir(realNewRoot).filePath(QStringLiteral("_svh.svh"));
    const QString realInterfaceFile =
        QDir(realNewRoot).filePath(QStringLiteral("SVH_interface.sv"));
    const QStringList realFixtureFiles = {
        realSvhFile,
        realInterfaceFile,
        realRtlTopFile,
    };
    QHash<QString, QString> realFixtureContentByFile;
    bool realFixtureAvailable = true;
    for (const QString& realFile : realFixtureFiles) {
        QFile realFixture(realFile);
        if (!realFixture.open(QIODevice::ReadOnly | QFile::Text)) {
            realFixtureAvailable = false;
            break;
        }
        realFixtureContentByFile.insert(
            QFileInfo(realFile).absoluteFilePath(),
            QTextStream(&realFixture).readAll());
    }
    expectBool("test_sv/new interface fixture files available",
               realFixtureAvailable,
               true);
    if (realFixtureAvailable) {
        const QList<SemanticSymbolRecord> realNewRecords =
            mgr.extractWorkspaceSymbolRecords(
                realFixtureFiles,
                {realNewRoot});
        for (const QString& realFile : realFixtureFiles) {
            const QString absoluteFile = QFileInfo(realFile).absoluteFilePath();
            QList<SemanticSymbolRecord> fileRecords;
            for (const SemanticSymbolRecord& record : realNewRecords) {
                if (QFileInfo(record.location.fileName).absoluteFilePath()
                    == absoluteFile) {
                    fileRecords.append(record);
                }
            }
            SemanticIndex::getInstance()->updateSymbolRecordsForFile(
                absoluteFile,
                fileRecords,
                realFixtureContentByFile.value(absoluteFile));
        }

        DefinitionNavigationQuery crossFileInterfaceQuery;
        crossFileInterfaceQuery.symbolName = QStringLiteral("lr_genr_if");
        crossFileInterfaceQuery.fileName =
            QFileInfo(realRtlTopFile).absoluteFilePath();
        crossFileInterfaceQuery.moduleName = QStringLiteral("rtl_top");
        const DefinitionNavigationTarget crossFileInterfaceTarget =
            definitionNavigationService.resolveTarget(crossFileInterfaceQuery);
        expectBool("DefinitionNavigation keeps test_sv/new interface file",
                   crossFileInterfaceTarget.found
                       && !crossFileInterfaceTarget.localFile
                       && QFileInfo(crossFileInterfaceTarget.fileName)
                               .absoluteFilePath()
                           == QFileInfo(realInterfaceFile).absoluteFilePath()
                       && crossFileInterfaceTarget.line == 43,
                   true);
    }

    memberNavigationContext.column = 500;
    const DefinitionNavigationQuery clampedNavigationQuery =
        definitionNavigationService.navigationQueryForContext(memberNavigationContext);
    expectEq("DefinitionNavigation clamps prefix",
             clampedNavigationQuery.linePrefixBeforeCursor,
             memberNavigationContext.lineText);

    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using SourceRole = SymbolTaxonomy::SourceRole;
    using SymbolOwnerScope = SymbolTaxonomy::SymbolOwnerScope;

    const QString snapshotOnlyFile = QStringLiteral("snapshot_only.sv");
    QList<SemanticSymbolRecord> snapshotDefinitionRecords;
    const SemanticSymbolRecord snapshotOtherRed =
        SemanticFixtureRecordBuilder(QStringLiteral("red"),
                                     DeclarationKind::StructMember)
            .withFile(snapshotOnlyFile)
            .withLocalHandle(6101)
            .withLine(1, 9)
            .withCollectorKind(CollectorKind::StructMember)
            .inStruct(QStringLiteral("other_t"))
            .record();
    snapshotDefinitionRecords.append(snapshotOtherRed);

    const SemanticSymbolRecord snapshotPixelRed =
        SemanticFixtureRecordBuilder(QStringLiteral("red"),
                                     DeclarationKind::StructMember)
            .withFile(snapshotOnlyFile)
            .withLocalHandle(6102)
            .withLine(2, 9)
            .withCollectorKind(CollectorKind::StructMember)
            .inStruct(QStringLiteral("snap_pixel_t"))
            .record();
    snapshotDefinitionRecords.append(snapshotPixelRed);

    const SemanticSymbolRecord snapshotPixelVar =
        SemanticFixtureRecordBuilder(QStringLiteral("snap_pixel"),
                                     DeclarationKind::StructVariable)
            .withFile(snapshotOnlyFile)
            .withLocalHandle(6103)
            .withLine(3, 5)
            .withCollectorKind(CollectorKind::PackedStructVariable)
            .inModule(QStringLiteral("snap_top"))
            .withType(QStringLiteral("snap_pixel_t"))
            .record();
    snapshotDefinitionRecords.append(snapshotPixelVar);

    const SemanticSymbolRecord snapshotHelperModule =
        SemanticFixtureRecordBuilder(QStringLiteral("snap_helper"),
                                     DeclarationKind::Module)
            .withFile(QStringLiteral("snapshot_helper.sv"))
            .withLocalHandle(6104)
            .withRange(12, 1, 14, 10)
            .withCollectorKind(CollectorKind::Module)
            .record();
    snapshotDefinitionRecords.append(snapshotHelperModule);

    const SemanticSymbolRecord snapshotHelperInterface =
        SemanticFixtureRecordBuilder(QStringLiteral("snap_if"),
                                     DeclarationKind::Interface)
            .withFile(QStringLiteral("snapshot_if.sv"))
            .withLocalHandle(6107)
            .withRange(15, 1, 15, 10)
            .withCollectorKind(CollectorKind::Interface)
            .record();
    snapshotDefinitionRecords.append(snapshotHelperInterface);

    const SemanticSymbolRecord snapshotHelperPackage =
        SemanticFixtureRecordBuilder(QStringLiteral("snap_pkg"),
                                     DeclarationKind::Package)
            .withFile(QStringLiteral("snapshot_pkg.sv"))
            .withLocalHandle(6108)
            .withRange(18, 1, 18, 10)
            .withCollectorKind(CollectorKind::Package)
            .record();
    snapshotDefinitionRecords.append(snapshotHelperPackage);

    const SemanticSymbolRecord snapshotPackageParam =
        SemanticFixtureRecordBuilder(QStringLiteral("SNAP_WIDTH"),
                                     DeclarationKind::Parameter)
            .withFile(QStringLiteral("snapshot_pkg.sv"))
            .withLocalHandle(6109)
            .withRange(20, 1, 20, 10)
            .withCollectorKind(CollectorKind::Parameter)
            .inPackage(QStringLiteral("snap_pkg"))
            .record();
    snapshotDefinitionRecords.append(snapshotPackageParam);

    const SemanticSymbolRecord snapshotPackageTypedef =
        SemanticFixtureRecordBuilder(QStringLiteral("snap_word_t"),
                                     DeclarationKind::Typedef)
            .withFile(QStringLiteral("snapshot_pkg.sv"))
            .withLocalHandle(6110)
            .withRange(21, 1, 21, 10)
            .withCollectorKind(CollectorKind::Typedef)
            .inPackage(QStringLiteral("snap_pkg"))
            .record();
    snapshotDefinitionRecords.append(snapshotPackageTypedef);

    const SemanticSymbolRecord snapshotInterfaceModport =
        SemanticFixtureRecordBuilder(QStringLiteral("slave"),
                                     DeclarationKind::Modport)
            .withFile(QStringLiteral("snapshot_if.sv"))
            .withLocalHandle(6111)
            .withRange(22, 1, 22, 10)
            .withCollectorKind(CollectorKind::InterfaceModport)
            .inInterface(QStringLiteral("snap_if"))
            .record();
    snapshotDefinitionRecords.append(snapshotInterfaceModport);

    const SemanticSymbolRecord snapshotInterfacePort =
        SemanticFixtureRecordBuilder(QStringLiteral("snap_bus"),
                                     DeclarationKind::Port)
            .withFile(snapshotOnlyFile)
            .withLocalHandle(6112)
            .withRange(23, 1, 23, 10)
            .withCollectorKind(CollectorKind::PortInterfaceModport)
            .withOwner(SymbolOwnerScope::Module,
                       QStringLiteral("snap_top"),
                       {},
                       true)
            .withType(QStringLiteral("snap_if.slave"),
                      QStringLiteral("snap_if"),
                      DeclarationKind::Interface,
                      QStringLiteral("slave"))
            .record();
    snapshotDefinitionRecords.append(snapshotInterfacePort);

    const SemanticSymbolRecord snapshotLocalDuplicate =
        SemanticFixtureRecordBuilder(QStringLiteral("snap_dup"),
                                     DeclarationKind::Module)
            .withFile(snapshotOnlyFile)
            .withLocalHandle(6105)
            .withRange(21, 1, 23, 10)
            .withCollectorKind(CollectorKind::Module)
            .record();
    snapshotDefinitionRecords.append(snapshotLocalDuplicate);

    const SemanticSymbolRecord snapshotRemoteDuplicate =
        SemanticFixtureRecordBuilder(QStringLiteral("snap_dup"),
                                     DeclarationKind::Module)
            .withFile(QStringLiteral("snapshot_remote.sv"))
            .withLocalHandle(6106)
            .withRange(31, 1, 31, 10)
            .withCollectorKind(CollectorKind::Module)
            .record();
    snapshotDefinitionRecords.append(snapshotRemoteDuplicate);

    const SemanticSymbolRecord snapshotMetadataScopedDuplicate =
        SemanticFixtureRecordBuilder(QStringLiteral("snap_meta_dup"))
            .withFile(QStringLiteral("snapshot_remote.sv"))
            .withLocalHandle(6113)
            .withRange(31, 1, 31, 10)
            .withMetadata(semanticFixtureMetadata(DeclarationKind::Module,
                                                  SymbolOwnerScope::Module,
                                                  CollectorKind::User,
                                                  SourceRole::DesignSource))
            .withOwner(SymbolOwnerScope::Module, QStringLiteral("snap_top"))
            .record();
    snapshotDefinitionRecords.append(snapshotMetadataScopedDuplicate);

    const SemanticSymbolRecord snapshotGlobalMetadataDuplicate =
        SemanticFixtureRecordBuilder(QStringLiteral("snap_meta_dup"))
            .withFile(QStringLiteral("snapshot_remote.sv"))
            .withLocalHandle(6114)
            .withRange(31, 1, 31, 10)
            .withMetadata(semanticFixtureMetadata(DeclarationKind::Module,
                                                  SymbolOwnerScope::Global,
                                                  CollectorKind::User,
                                                  SourceRole::DesignSource))
            .withOwner(SymbolOwnerScope::Global)
            .record();
    snapshotDefinitionRecords.append(snapshotGlobalMetadataDuplicate);

    QHash<QString, QString> snapshotContents;
    snapshotContents.insert(
        snapshotOnlyFile,
        QStringLiteral("module snap_top;\n"
                       "  import snap_pkg::*;\n"
                       "  snap_word_t imported_value;\n"
                       "  localparam int W = SNAP_WIDTH;\n"
                       "endmodule\n"));

    SemanticIndex snapshotIndex;
    snapshotIndex.setSnapshot(
        sharedSnapshotFromRecords(snapshotDefinitionRecords,
                                  {},
                                  {},
                                  snapshotContents));
    DefinitionService snapshotDefinitionService(&snapshotIndex);
    DefinitionQuery snapshotMemberQuery;
    snapshotMemberQuery.symbolName = QStringLiteral("red");
    snapshotMemberQuery.fileName = QStringLiteral("snapshot_only.sv");
    snapshotMemberQuery.moduleName = QStringLiteral("snap_top");
    snapshotMemberQuery.linePrefixBeforeCursor =
        QStringLiteral("assign out = snap_pixel.red");
    const DefinitionResult snapshotMemberResult =
        snapshotDefinitionService.resolveDefinition(snapshotMemberQuery);
    ++g_checks;
    const bool snapshotMemberOk = snapshotMemberResult.found
        && snapshotMemberResult.symbolRecord.localHandle
            == snapshotPixelRed.localHandle
        && snapshotMemberResult.symbolRecord.owner.name == QStringLiteral("snap_pixel_t");
    if (!snapshotMemberOk)
        ++g_fails;
    printf("[%s] DefinitionService resolves snapshot struct-member context\n",
           snapshotMemberOk ? "PASS" : "FAIL");

    DefinitionQuery snapshotModuleQuery;
    snapshotModuleQuery.symbolName = QStringLiteral("snap_helper");
    snapshotModuleQuery.fileName = QStringLiteral("snapshot_only.sv");
    const DefinitionResult snapshotModuleResult =
        snapshotDefinitionService.resolveDefinition(snapshotModuleQuery);
    ++g_checks;
    const bool snapshotModuleOk = snapshotModuleResult.found
        && !snapshotModuleResult.localFile
        && snapshotModuleResult.symbolRecord.localHandle
            == snapshotHelperModule.localHandle
        && QFileInfo(snapshotModuleResult.symbolRecord.location.fileName).fileName()
            == QStringLiteral("snapshot_helper.sv")
        && snapshotModuleResult.symbolStableKey
            == snapshotHelperModule.stableKey
        && snapshotModuleResult.missReason == SemanticDefinitionMissReason::None
        && snapshotModuleResult.visibleCandidateCount > 0;
    if (!snapshotModuleOk)
        ++g_fails;
    printf("[%s] DefinitionService resolves snapshot cross-file module\n",
           snapshotModuleOk ? "PASS" : "FAIL");
    expectBool("DefinitionService canResolve snapshot cross-file module",
               snapshotDefinitionService.canResolveDefinition(snapshotModuleQuery),
               true);

    DefinitionNavigationService snapshotDefinitionNavigationService(&snapshotIndex);
    DefinitionNavigationQuery snapshotNavigationQuery;
    snapshotNavigationQuery.symbolName = QStringLiteral("snap_helper");
    snapshotNavigationQuery.fileName = QStringLiteral("snapshot_only.sv");
    const DefinitionNavigationTarget snapshotNavigationTarget =
        snapshotDefinitionNavigationService.resolveTarget(snapshotNavigationQuery);
    expectBool("DefinitionNavigation carries snapshot symbol record",
               snapshotNavigationTarget.found
                   && snapshotNavigationTarget.symbolRecord.isValid()
                   && snapshotNavigationTarget.symbolRecord.localHandle
                       == snapshotHelperModule.localHandle
                   && snapshotNavigationTarget.symbolRecord.stableKey
                       == snapshotNavigationTarget.symbolStableKey
                   && snapshotNavigationTarget.symbolRecord.name
                       == QStringLiteral("snap_helper")
                   && snapshotNavigationTarget.fileName.endsWith(
                       QStringLiteral("snapshot_helper.sv"))
                   && snapshotNavigationTarget.line
                       == snapshotHelperModule.location.startLine
                   && snapshotNavigationTarget.symbolTypeText
                       == QStringLiteral("module")
                   && snapshotNavigationTarget.symbolRecord.collectorKind
                       == SymbolTaxonomy::CollectorKind::Module
                   && snapshotNavigationTarget.ownerDisplayName
                       == QStringLiteral("global")
                   && snapshotNavigationTarget.sourceRoleDisplayName
                       == QStringLiteral("design source"),
               true);

    DefinitionQuery emptyDefinitionQuery;
    const DefinitionResult emptyDefinitionResult =
        snapshotDefinitionService.resolveDefinition(emptyDefinitionQuery);
    expectBool("DefinitionService empty miss reason",
               !emptyDefinitionResult.found
                   && emptyDefinitionResult.inspectedCandidateCount == 0
                   && emptyDefinitionResult.missReason
                       == SemanticDefinitionMissReason::EmptySymbolName,
               true);

    DefinitionQuery missingDefinitionQuery;
    missingDefinitionQuery.symbolName = QStringLiteral("snap_missing");
    missingDefinitionQuery.fileName = QStringLiteral("snapshot_only.sv");
    const DefinitionResult missingDefinitionResult =
        snapshotDefinitionService.resolveDefinition(missingDefinitionQuery);
    expectBool("DefinitionService missing miss reason",
               !missingDefinitionResult.found
                   && missingDefinitionResult.inspectedCandidateCount > 0
                   && missingDefinitionResult.matchingNameCandidateCount == 0
                   && missingDefinitionResult.missReason
                       == SemanticDefinitionMissReason::NoMatchingName,
               true);

    DefinitionQuery snapshotInterfaceQuery;
    snapshotInterfaceQuery.symbolName = QStringLiteral("snap_if");
    snapshotInterfaceQuery.fileName = QStringLiteral("snapshot_only.sv");
    const DefinitionResult snapshotInterfaceResult =
        snapshotDefinitionService.resolveDefinition(snapshotInterfaceQuery);
    ++g_checks;
    const bool snapshotInterfaceOk = snapshotInterfaceResult.found
        && !snapshotInterfaceResult.localFile
        && snapshotInterfaceResult.symbolRecord.localHandle
            == snapshotHelperInterface.localHandle
        && snapshotInterfaceResult.symbolRecord.collectorKind
            == SymbolTaxonomy::CollectorKind::Interface;
    if (!snapshotInterfaceOk)
        ++g_fails;
    printf("[%s] DefinitionService resolves snapshot cross-file interface\n",
           snapshotInterfaceOk ? "PASS" : "FAIL");
    expectBool("DefinitionService canResolve snapshot cross-file interface",
               snapshotDefinitionService.canResolveDefinition(snapshotInterfaceQuery),
               true);

    DefinitionQuery snapshotInterfaceModportQuery;
    snapshotInterfaceModportQuery.symbolName = QStringLiteral("slave");
    snapshotInterfaceModportQuery.fileName = QStringLiteral("snapshot_only.sv");
    snapshotInterfaceModportQuery.moduleName = QStringLiteral("snap_top");
    const DefinitionResult snapshotInterfaceModportResult =
        snapshotDefinitionService.resolveDefinition(snapshotInterfaceModportQuery);
    ++g_checks;
    const bool snapshotInterfaceModportOk = snapshotInterfaceModportResult.found
        && snapshotInterfaceModportResult.symbolRecord.localHandle
            == snapshotInterfaceModport.localHandle
        && snapshotInterfaceModportResult.symbolRecord.owner.name
            == QStringLiteral("snap_if");
    if (!snapshotInterfaceModportOk)
        ++g_fails;
    printf("[%s] DefinitionService resolves snapshot interface modport\n",
           snapshotInterfaceModportOk ? "PASS" : "FAIL");

    DefinitionQuery snapshotInterfaceMemberQuery;
    snapshotInterfaceMemberQuery.symbolName = QStringLiteral("slave");
    snapshotInterfaceMemberQuery.fileName = QStringLiteral("snapshot_only.sv");
    snapshotInterfaceMemberQuery.moduleName = QStringLiteral("snap_top");
    snapshotInterfaceMemberQuery.linePrefixBeforeCursor =
        QStringLiteral("assign ready = snap_bus.slave");
    const DefinitionResult snapshotInterfaceMemberResult =
        snapshotDefinitionService.resolveDefinition(snapshotInterfaceMemberQuery);
    expectBool("DefinitionService resolves snapshot interface member context",
               snapshotInterfaceMemberResult.found
                   && snapshotInterfaceMemberResult.symbolRecord.localHandle
                       == snapshotInterfaceModport.localHandle
                   && snapshotInterfaceMemberResult.symbolRecord.isValid()
                   && snapshotInterfaceMemberResult.symbolRecord.stableKey
                       == snapshotInterfaceMemberResult.symbolStableKey
                   && snapshotInterfaceMemberResult.symbolRecord.owner.name
                       == QStringLiteral("snap_if")
                   && snapshotInterfaceMemberResult.symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Modport,
               true);

    DefinitionQuery snapshotPackageQuery;
    snapshotPackageQuery.symbolName = QStringLiteral("snap_pkg");
    snapshotPackageQuery.fileName = QStringLiteral("snapshot_only.sv");
    const DefinitionResult snapshotPackageDefinition =
        snapshotDefinitionService.resolveDefinition(snapshotPackageQuery);
    ++g_checks;
    const bool snapshotPackageOk = snapshotPackageDefinition.found
        && snapshotPackageDefinition.symbolRecord.localHandle
            == snapshotHelperPackage.localHandle
        && snapshotPackageDefinition.symbolRecord.declarationKind
            == SymbolTaxonomy::DeclarationKind::Package;
    if (!snapshotPackageOk)
        ++g_fails;
    printf("[%s] DefinitionService resolves snapshot package\n",
           snapshotPackageOk ? "PASS" : "FAIL");

    DefinitionQuery snapshotPackageParamQuery;
    snapshotPackageParamQuery.symbolName = QStringLiteral("SNAP_WIDTH");
    snapshotPackageParamQuery.fileName = snapshotOnlyFile;
    snapshotPackageParamQuery.moduleName = QStringLiteral("snap_top");
    snapshotPackageParamQuery.cursorLine = 4;
    const DefinitionResult snapshotPackageParamResult =
        snapshotDefinitionService.resolveDefinition(snapshotPackageParamQuery);
    ++g_checks;
    const bool snapshotPackageParamOk = snapshotPackageParamResult.found
        && snapshotPackageParamResult.symbolRecord.localHandle
            == snapshotPackageParam.localHandle
        && snapshotPackageParamResult.symbolRecord.isValid()
        && snapshotPackageParamResult.symbolRecord.owner.kind
            == SymbolTaxonomy::SymbolOwnerScope::Package
        && snapshotPackageParamResult.symbolRecord.owner.name
            == QStringLiteral("snap_pkg")
        && snapshotPackageParamResult.symbolRecord.visibility
            == SymbolTaxonomy::SymbolVisibility::PackageVisible;
    if (!snapshotPackageParamOk)
        ++g_fails;
    printf("[%s] DefinitionService resolves snapshot package parameter\n",
           snapshotPackageParamOk ? "PASS" : "FAIL");

    DefinitionQuery snapshotPackageTypedefQuery;
    snapshotPackageTypedefQuery.symbolName = QStringLiteral("snap_word_t");
    snapshotPackageTypedefQuery.fileName = snapshotOnlyFile;
    snapshotPackageTypedefQuery.moduleName = QStringLiteral("snap_top");
    snapshotPackageTypedefQuery.cursorLine = 3;
    const DefinitionResult snapshotPackageTypedefResult =
        snapshotDefinitionService.resolveDefinition(snapshotPackageTypedefQuery);
    ++g_checks;
    const bool snapshotPackageTypedefOk = snapshotPackageTypedefResult.found
        && snapshotPackageTypedefResult.symbolRecord.localHandle
            == snapshotPackageTypedef.localHandle;
    if (!snapshotPackageTypedefOk)
        ++g_fails;
    printf("[%s] DefinitionService resolves snapshot package typedef\n",
           snapshotPackageTypedefOk ? "PASS" : "FAIL");

    const SymbolTaxonomy::SemanticMetadata packageParameterMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Parameter,
                                SymbolTaxonomy::SymbolOwnerScope::Package,
                                SymbolTaxonomy::CollectorKind::Parameter);
    expectBool("SymbolTaxonomy package parameter visible",
               SymbolTaxonomy::isPackageVisibleDefinition(
                   packageParameterMetadata),
               true);
    SymbolTaxonomy::SemanticMetadata interfaceModportPortMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Port,
                                SymbolTaxonomy::SymbolOwnerScope::Module,
                                SymbolTaxonomy::CollectorKind::PortInterfaceModport);
    interfaceModportPortMetadata.interfaceLikeOwner = true;
    expectBool("SymbolTaxonomy interface owner includes modport port",
               interfaceModportPortMetadata.interfaceLikeOwner,
               true);
    expectBool("SymbolTaxonomy interface owner metadata",
               semanticFixtureMetadata(
                   SymbolTaxonomy::DeclarationKind::Interface,
                   SymbolTaxonomy::SymbolOwnerScope::Global)
                       .declarationKind
                   == SymbolTaxonomy::DeclarationKind::Interface,
               true);
    const QString taxonomyInterfacePortType =
        QStringLiteral("if_bus.master");
    expectEq("SymbolTaxonomy interface type name",
             SymbolTaxonomy::interfaceTypeName(taxonomyInterfacePortType),
             QStringLiteral("if_bus"));
    expectEq("SymbolTaxonomy interface modport name",
             SymbolTaxonomy::interfaceModportName(taxonomyInterfacePortType),
             QStringLiteral("master"));
    expectBool("SymbolTaxonomy modport member-scope candidate",
               SymbolTaxonomy::isMemberScopeDefinitionCandidate(
                   semanticFixtureMetadata(
                       SymbolTaxonomy::DeclarationKind::Modport,
                       SymbolTaxonomy::SymbolOwnerScope::Interface,
                       SymbolTaxonomy::CollectorKind::InterfaceModport)),
               true);
    expectBool("SymbolTaxonomy detects svh header role",
               SymbolTaxonomy::sourceRoleForFileName(QStringLiteral("rtl/pkg_defs.svh"))
                   == SymbolTaxonomy::SourceRole::Header,
               true);
    expectEq("SymbolTaxonomy design source role label",
             SymbolTaxonomy::sourceRoleDisplayName(
                 SymbolTaxonomy::SourceRole::DesignSource),
             QStringLiteral("design source"));
    expectEq("SymbolTaxonomy external header role label",
             SymbolTaxonomy::sourceRoleDisplayName(
                 SymbolTaxonomy::SourceRole::ExternalHeader),
             QStringLiteral("external header"));
    expectEq("SymbolTaxonomy generated role label",
             SymbolTaxonomy::sourceRoleDisplayName(
                 SymbolTaxonomy::SourceRole::Generated),
             QStringLiteral("generated source"));
    expectBool("SymbolTaxonomy external header is header",
               SymbolTaxonomy::isHeaderSourceRole(
                   SymbolTaxonomy::SourceRole::ExternalHeader),
               true);
    ProjectSnapshot sourceRoleSnapshot;
    sourceRoleSnapshot.sourceRoles.insert(
        QStringLiteral("rtl/top.sv"),
        SymbolTaxonomy::SourceRole::DesignSource);
    sourceRoleSnapshot.sourceRoles.insert(
        QStringLiteral("include/pkg_defs.svh"),
        SymbolTaxonomy::SourceRole::Header);
    sourceRoleSnapshot.sourceRoles.insert(
        QStringLiteral("vendor/ext_pkg.svh"),
        SymbolTaxonomy::SourceRole::ExternalHeader);
    expectBool("ProjectSnapshot includes external headers",
               sourceRoleSnapshot.headerSourceFiles().contains(
                   QStringLiteral("vendor/ext_pkg.svh")),
               true);
    expectBool("ProjectSnapshot excludes design from headers",
               sourceRoleSnapshot.headerSourceFiles().contains(
                   QStringLiteral("rtl/top.sv")),
               false);

    DefinitionQuery snapshotLocalModuleQuery;
    snapshotLocalModuleQuery.symbolName = QStringLiteral("snap_dup");
    snapshotLocalModuleQuery.fileName = QStringLiteral("snapshot_only.sv");
    const DefinitionResult snapshotLocalModuleResult =
        snapshotDefinitionService.resolveDefinition(snapshotLocalModuleQuery);
    ++g_checks;
    const bool snapshotLocalModuleOk = snapshotLocalModuleResult.found
        && snapshotLocalModuleResult.localFile
        && snapshotLocalModuleResult.symbolRecord.localHandle
            == snapshotLocalDuplicate.localHandle
        && QFileInfo(snapshotLocalModuleResult.symbolRecord.location.fileName).fileName()
            == QStringLiteral("snapshot_only.sv");
    if (!snapshotLocalModuleOk)
        ++g_fails;
    printf("[%s] DefinitionService prefers snapshot local definition\n",
           snapshotLocalModuleOk ? "PASS" : "FAIL");

    const DefinitionResult snapshotLocalDefinition =
        snapshotDefinitionService.resolveDefinition(snapshotLocalModuleQuery);
    ++g_checks;
    const bool snapshotResolveDefinitionOk = snapshotLocalDefinition.found
        && snapshotLocalDefinition.symbolRecord.localHandle
            == snapshotLocalDuplicate.localHandle;
    if (!snapshotResolveDefinitionOk)
        ++g_fails;
    printf("[%s] DefinitionService keeps snapshot local result\n",
           snapshotResolveDefinitionOk ? "PASS" : "FAIL");

    SemanticQueryContext metadataScopedContext;
    metadataScopedContext.moduleName = QStringLiteral("snap_top");
    const QList<SemanticSymbolRecord> metadataScopedDefinitions =
        snapshotIndex.findDefinitionRecords(QStringLiteral("snap_meta_dup"),
                                            metadataScopedContext);
    ++g_checks;
    const bool metadataScopedDefinitionsOk =
        metadataScopedDefinitions.size() == 2
        && metadataScopedDefinitions.first().localHandle
            == snapshotMetadataScopedDuplicate.localHandle
        && metadataScopedDefinitions.first().owner.name == QStringLiteral("snap_top")
        && metadataScopedDefinitions.first().declarationKind
               == SymbolTaxonomy::DeclarationKind::Module
        && metadataScopedDefinitions.first().collectorKind
            == SymbolTaxonomy::CollectorKind::User
        && metadataScopedDefinitions.first().stableKey.isValid();
    if (!metadataScopedDefinitionsOk)
        ++g_fails;
    printf("[%s] SemanticIndex sorts definitions by semantic owner\n",
           metadataScopedDefinitionsOk ? "PASS" : "FAIL");

    SourceNavigationService* sourceNavigationService =
        SourceNavigationService::getInstance();
    const QString includeLine =
        QStringLiteral("  `include \"rtl/pkg_defs.svh\"");
    const IncludeDirectiveTarget includeTarget =
        sourceNavigationService->includeAtColumn(
            includeLine,
            includeLine.indexOf(QStringLiteral("pkg_defs")));
    expectBool("SourceNavigation matches include path",
               includeTarget.matched,
               true);
    expectEq("SourceNavigation trims include path",
             includeTarget.includePath,
             QStringLiteral("rtl/pkg_defs.svh"));
    ++g_checks;
    const bool includeRangeOk =
        includeTarget.startColumn == includeLine.indexOf(QLatin1Char('"')) + 1
        && includeTarget.endColumn == includeLine.lastIndexOf(QLatin1Char('"'));
    if (!includeRangeOk)
        ++g_fails;
    printf("[%s] SourceNavigation returns include range\n",
           includeRangeOk ? "PASS" : "FAIL");
    expectBool("SourceNavigation ignores quote boundary",
               sourceNavigationService
                   ->includeAtColumn(includeLine, includeTarget.startColumn - 1)
                   .matched,
               false);

    const QString importLine =
        QStringLiteral("  import pkg_defs :: *;");
    const PackageImportTarget importTarget =
        sourceNavigationService->packageImportAtColumn(
            importLine,
            importLine.indexOf(QStringLiteral("pkg_defs")));
    expectBool("SourceNavigation matches import package",
               importTarget.matched,
               true);
    expectEq("SourceNavigation import package",
             importTarget.packageName,
             QStringLiteral("pkg_defs"));
    ++g_checks;
    const bool importRangeOk =
        importTarget.startColumn == importLine.indexOf(QStringLiteral("pkg_defs"))
        && importTarget.endColumn == importTarget.startColumn
            + QStringLiteral("pkg_defs").size();
    if (!importRangeOk)
        ++g_fails;
    printf("[%s] SourceNavigation returns import package range\n",
           importRangeOk ? "PASS" : "FAIL");
    expectBool("SourceNavigation ignores import boundary",
               sourceNavigationService
                   ->packageImportAtColumn(importLine, importTarget.endColumn)
                   .matched,
               false);

    const QString identifierLine =
        QStringLiteral("assign next_value = current_value + 1;");
    const SourceIdentifierTarget identifierTarget =
        sourceNavigationService->identifierAtColumn(
            identifierLine,
            identifierLine.indexOf(QStringLiteral("current")) + 3);
    expectBool("SourceNavigation matches identifier",
               identifierTarget.matched,
               true);
    expectEq("SourceNavigation identifier text",
             identifierTarget.identifier,
             QStringLiteral("current_value"));
    ++g_checks;
    const bool identifierRangeOk =
        identifierTarget.startColumn
            == identifierLine.indexOf(QStringLiteral("current_value"))
        && identifierTarget.endColumn == identifierTarget.startColumn
            + QStringLiteral("current_value").size();
    if (!identifierRangeOk)
        ++g_fails;
    printf("[%s] SourceNavigation returns identifier range\n",
           identifierRangeOk ? "PASS" : "FAIL");
    expectBool("SourceNavigation matches line-end identifier",
               sourceNavigationService
                   ->identifierAtColumn(QStringLiteral("  done_signal"),
                                        QStringLiteral("  done_signal").size())
                   .matched,
               true);
    expectBool("SourceNavigation rejects numeric start",
               sourceNavigationService
                   ->identifierAtColumn(QStringLiteral("123abc"), 2)
                   .matched,
               false);
    expectBool("SourceNavigation rejects punctuation",
               sourceNavigationService
                   ->identifierAtColumn(identifierLine,
                                        identifierLine.indexOf(QLatin1Char('+')))
                   .matched,
               false);
    const QString namedPortLine = QStringLiteral("    .data (data)");
    const SourceIdentifierTarget namedPortDotTarget =
        sourceNavigationService->identifierAtColumn(
            namedPortLine,
            namedPortLine.indexOf(QLatin1Char('.')));
    expectBool("SourceNavigation maps .formal dot to identifier",
               namedPortDotTarget.matched
                   && namedPortDotTarget.identifier == QStringLiteral("data")
                   && namedPortDotTarget.startColumn
                       == namedPortLine.indexOf(QStringLiteral("data")),
               true);
    const SourceSymbolActionContext symbolActionContext =
        sourceNavigationService->symbolActionContextAtColumn(
            identifierLine,
            identifierLine.indexOf(QStringLiteral("current_value")) + 3,
            QStringLiteral("unit.sv"),
            QStringLiteral("top"));
    expectBool("SourceNavigation symbol action available",
               symbolActionContext.available,
               true);
    expectEq("SourceNavigation symbol action name",
             symbolActionContext.symbolName,
             QStringLiteral("current_value"));
    expectEq("SourceNavigation symbol action file",
             symbolActionContext.fileName,
             QStringLiteral("unit.sv"));
    expectEq("SourceNavigation symbol action module",
             symbolActionContext.moduleName,
             QStringLiteral("top"));
    expectBool("SourceNavigation symbol action needs file",
               sourceNavigationService
                   ->symbolActionContextAtColumn(identifierLine,
                                                 identifierLine.indexOf(
                                                     QStringLiteral("current_value")),
                                                 QString(),
                                                 QStringLiteral("top"))
                   .available,
               false);
    expectBool("SourceNavigation symbol action rejects punctuation",
               sourceNavigationService
                   ->symbolActionContextAtColumn(identifierLine,
                                                 identifierLine.indexOf(QLatin1Char('+')),
                                                 QStringLiteral("unit.sv"),
                                                 QStringLiteral("top"))
                   .available,
               false);

    EditorSemanticContext editorSemanticContext;
    editorSemanticContext.fileName = path;
    editorSemanticContext.moduleName = QStringLiteral("top");
    editorSemanticContext.lineText = identifierLine;
    editorSemanticContext.column =
        identifierLine.indexOf(QStringLiteral("current_value")) + 3;
    const SourceIdentifierTarget contextIdentifierTarget =
        EditorSemanticContextService::getInstance()
            ->sourceIdentifierTarget(editorSemanticContext);
    expectBool("EditorSemanticContext returns identifier target",
               contextIdentifierTarget.matched
                   && contextIdentifierTarget.identifier == QStringLiteral("current_value"),
               true);
    const SourceEditorNavigationTarget contextEditorTarget =
        EditorSemanticContextService::getInstance()
            ->sourceNavigationTarget(editorSemanticContext,
                                     [](const QString& symbolName) {
                                         return symbolName == QStringLiteral("current_value");
                                     });
    expectBool("EditorSemanticContext returns source navigation target",
               contextEditorTarget.matched
                   && contextEditorTarget.identifierTarget
                   && contextEditorTarget.jumpable,
               true);
    EditorSemanticContext definitionSourceContext;
    definitionSourceContext.fileName = path;
    definitionSourceContext.moduleName = QStringLiteral("top");
    definitionSourceContext.lineText = QStringLiteral("      counter <= 8'd0;");
    definitionSourceContext.column =
        definitionSourceContext.lineText.indexOf(QStringLiteral("counter")) + 2;
    const SourceEditorNavigationTarget definitionEditorTarget =
        EditorSemanticContextService::getInstance()
            ->definitionSourceNavigationTarget(definitionSourceContext);
    expectBool("EditorSemanticContext returns definition-aware source navigation target",
               definitionEditorTarget.matched
                   && definitionEditorTarget.identifierTarget
                   && definitionEditorTarget.text == QStringLiteral("counter")
                   && definitionEditorTarget.jumpable,
               true);
    const SourceSymbolActionContext contextSymbolAction =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionContext(editorSemanticContext);
    expectBool("EditorSemanticContext returns symbol action context",
               contextSymbolAction.available
                   && contextSymbolAction.symbolName == QStringLiteral("current_value")
                   && contextSymbolAction.fileName == path
                   && contextSymbolAction.moduleName == QStringLiteral("top"),
               true);
    EditorSemanticContext definitionContext;
    definitionContext.fileName = path;
    definitionContext.moduleName = QStringLiteral("top");
    expectBool("EditorSemanticContext resolves definition target",
               EditorSemanticContextService::getInstance()
                   ->canResolveDefinitionTarget(QStringLiteral("counter"), definitionContext),
               true);
    expectBool("EditorSemanticContext returns definition tooltip",
               !EditorSemanticContextService::getInstance()
                    ->definitionTooltipText(QStringLiteral("counter"), definitionContext)
                    .isEmpty(),
               true);

    const SourceNavigationTarget includePriorityTarget =
        sourceNavigationService->targetAtColumn(
            includeLine,
            includeLine.indexOf(QStringLiteral("pkg_defs")));
    expectBool("SourceNavigation target include priority",
               includePriorityTarget.matched
                   && includePriorityTarget.kind
                       == SourceNavigationTargetKind::IncludeDirective,
               true);
    expectEq("SourceNavigation target include text",
             includePriorityTarget.text,
             QStringLiteral("rtl/pkg_defs.svh"));

    const SourceNavigationTarget importPriorityTarget =
        sourceNavigationService->targetAtColumn(
            importLine,
            importLine.indexOf(QStringLiteral("pkg_defs")));
    expectBool("SourceNavigation target import priority",
               importPriorityTarget.matched
                   && importPriorityTarget.kind
                       == SourceNavigationTargetKind::PackageImport,
               true);

    const SourceNavigationTarget identifierPriorityTarget =
        sourceNavigationService->targetAtColumn(
            identifierLine,
            identifierLine.indexOf(QStringLiteral("current_value")));
    expectBool("SourceNavigation target identifier fallback",
               identifierPriorityTarget.matched
                   && identifierPriorityTarget.kind
                       == SourceNavigationTargetKind::Identifier,
               true);
    const SourceEditorNavigationTarget includeEditorTarget =
        sourceNavigationService->editorNavigationTargetAtColumn(
            includeLine,
            includeLine.indexOf(QStringLiteral("pkg_defs")),
            [](const QString&) { return false; });
    expectBool("SourceNavigation editor include target",
               includeEditorTarget.matched
                   && includeEditorTarget.includeTarget
                   && includeEditorTarget.jumpable,
               true);
    expectEq("SourceNavigation editor include text",
             includeEditorTarget.text,
             QStringLiteral("rtl/pkg_defs.svh"));

    const SourceEditorNavigationTarget importEditorTarget =
        sourceNavigationService->editorNavigationTargetAtColumn(
            importLine,
            importLine.indexOf(QStringLiteral("pkg_defs")),
            [](const QString&) { return false; });
    expectBool("SourceNavigation editor import target",
               importEditorTarget.matched
                   && !importEditorTarget.includeTarget
                   && !importEditorTarget.identifierTarget
                   && importEditorTarget.jumpable,
               true);
    expectEq("SourceNavigation editor import text",
             importEditorTarget.text,
             QStringLiteral("pkg_defs"));

    const SourceEditorNavigationTarget identifierEditorTarget =
        sourceNavigationService->editorNavigationTargetAtColumn(
            identifierLine,
            identifierLine.indexOf(QStringLiteral("current_value")),
            [](const QString& symbolName) {
                return symbolName == QStringLiteral("current_value");
            });
    expectBool("SourceNavigation editor identifier target",
               identifierEditorTarget.matched
                   && identifierEditorTarget.identifierTarget
                   && identifierEditorTarget.jumpable,
               true);
    expectEq("SourceNavigation editor identifier text",
             identifierEditorTarget.text,
             QStringLiteral("current_value"));
    expectBool("SourceNavigation editor unresolved identifier",
               !sourceNavigationService
                    ->editorNavigationTargetAtColumn(
                        identifierLine,
                        identifierLine.indexOf(QStringLiteral("current_value")),
                        [](const QString&) { return false; })
                    .jumpable,
               true);
    const SourceLineNavigationTarget lineNavigationTarget =
        sourceNavigationService->lineNavigationTarget(12, 4);
    expectBool("SourceNavigation line target valid",
               lineNavigationTarget.matched,
               true);
    ++g_checks;
    const bool lineNavigationMovesOk =
        lineNavigationTarget.lineMoves == 11
        && lineNavigationTarget.columnMoves == 3;
    if (!lineNavigationMovesOk)
        ++g_fails;
    printf("[%s] SourceNavigation line target move counts\n",
           lineNavigationMovesOk ? "PASS" : "FAIL");
    expectBool("SourceNavigation rejects invalid line target",
               sourceNavigationService->lineNavigationTarget(0, 4).matched,
               false);
    ++g_checks;
    const bool defaultColumnOk =
        sourceNavigationService->lineNavigationTarget(3).columnMoves == 0;
    if (!defaultColumnOk)
        ++g_fails;
    printf("[%s] SourceNavigation line target default column\n",
           defaultColumnOk ? "PASS" : "FAIL");

    const int editorActionPos =
        content.indexOf(QStringLiteral("counter <= 8'd0"));
    expectBool("Editor symbol action fixture position",
               editorActionPos >= 0,
               true);
    QTextCursor editorActionCursor = ed.textCursor();
    editorActionCursor.setPosition(qMax(0, editorActionPos + 2));
    const EditorSemanticContext editorActionSemanticContext =
        ed.editorSemanticContextForPosition(editorActionCursor.position(), true);
    expectBool("Editor semantic context has document text",
               editorActionSemanticContext.documentText == content,
               true);
    expectEq("Editor semantic context line",
             editorActionSemanticContext.lineText.trimmed(),
             QStringLiteral("counter <= 8'd0;"));
    expectBool("Editor semantic context cursor fields",
               editorActionSemanticContext.cursorLine > 0
                   && editorActionSemanticContext.cursorPosition
                       == editorActionCursor.position()
                   && editorActionSemanticContext.column >= 0
                   && editorActionSemanticContext.lineUpToCursor
                       == editorActionSemanticContext.lineText.left(
                           editorActionSemanticContext.column),
               true);
    const SourceSymbolActionContext editorActionContext =
        EditorSemanticContextService::getInstance()->sourceSymbolActionContext(
            editorActionSemanticContext);
    expectBool("Editor symbol action available",
               editorActionContext.available,
               true);
    expectEq("Editor symbol action name",
             editorActionContext.symbolName,
             QStringLiteral("counter"));
    expectEq("Editor symbol action file",
             editorActionContext.fileName,
             QFileInfo(path).absoluteFilePath());
    expectEq("Editor symbol action module",
             editorActionContext.moduleName,
             QStringLiteral("top"));

    // Local definition target: editor context resolves to the definition location.
    SemanticSymbolRecord counterRecord;
    for (const SemanticSymbolRecord& record : symbolRecords) {
        if (record.name == QStringLiteral("counter")
            && record.declarationKind
                == SymbolTaxonomy::DeclarationKind::Signal
            && record.owner.name == QStringLiteral("top")) {
            counterRecord = record;
            break;
        }
    }
    placeCursor(ed, 95);
    const DefinitionNavigationTarget counterTarget =
        EditorSemanticContextService::getInstance()->resolveDefinitionTarget(
            QStringLiteral("counter"),
            ed.editorSemanticContextForPosition(ed.textCursor().position()));
    printf("-- target(counter): found=%d local=%d line=%d, symbol.startLine=%d --\n",
           counterTarget.found,
           counterTarget.localFile,
           counterTarget.line,
           counterRecord.location.startLine);
    ++g_checks;
    bool counterOk = counterTarget.found
        && counterTarget.localFile
        && counterRecord.isValid()
        && counterTarget.line == counterRecord.location.startLine
        && counterTarget.symbolRecord.isValid()
        && counterTarget.symbolRecord.stableKey == counterTarget.symbolStableKey
        && counterTarget.symbolRecord.name == QStringLiteral("counter")
        && counterTarget.symbolRecord.declarationKind
            == SymbolTaxonomy::DeclarationKind::Signal
        && counterTarget.ownerDisplayName == QStringLiteral("top")
        && counterTarget.sourceRoleDisplayName == QStringLiteral("design source");
    if (!counterOk) ++g_fails;
    printf("[%s] local target(counter) resolves to startLine\n",
           counterOk ? "PASS" : "FAIL");

    // --- cross-file target: definition is resolved in a second file. ---
    // Derive helper path from the main file's directory (robust to the run cwd).
    QString helperPath = QFileInfo(path).dir().filePath(QStringLiteral("helper_mod.sv"));
    QFile hf(helperPath);
    if (!hf.exists())
        printf("[WARN] helper file not found: %s\n", helperPath.toLocal8Bit().constData());
    if (hf.open(QIODevice::ReadOnly | QFile::Text)) {
        QString hc = QTextStream(&hf).readAll();
        hf.close();
        const QList<SemanticSymbolRecord> helperRecords =
            mgr.extractSymbolRecords(helperPath, hc);
        SemanticIndex::getInstance()->updateSymbolRecordsForFile(
            helperPath,
            helperRecords,
            hc);

        int helperStartLine = -1;
        for (const SemanticSymbolRecord& record : helperRecords) {
            if (record.name == QStringLiteral("helper_mod")
                && record.declarationKind
                    == SymbolTaxonomy::DeclarationKind::Module) {
                helperStartLine = record.location.startLine;
                break;
            }
        }

        placeCursor(ed, 29);  // outside any module in test_symbols.sv -> no scope filter
        const DefinitionNavigationTarget helperTarget =
            EditorSemanticContextService::getInstance()->resolveDefinitionTarget(
                QStringLiteral("helper_mod"),
                ed.editorSemanticContextForPosition(ed.textCursor().position()));
        printf("-- cross-file target(helper_mod): file=%s line=%d, startLine=%d --\n",
               helperTarget.fileName.toLocal8Bit().constData(),
               helperTarget.line,
               helperStartLine);
        ++g_checks;
        bool ok = helperTarget.found
            && !helperTarget.localFile
            && helperTarget.line == helperStartLine
            && helperTarget.fileName.endsWith("helper_mod.sv");
        if (!ok) ++g_fails;
        printf("[%s] cross-file target resolves to definition file/startLine\n",
               ok ? "PASS" : "FAIL");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
