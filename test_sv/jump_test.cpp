// Headless jump-resolution test. Builds sym_list from Slang, constructs a MyCodeEditor offscreen
// (no window shown), sets its file/text/cursor, and drives definition availability / target resolution.
#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QTextCursor>
#include <QString>
#include <QFileInfo>
#include <QDir>
#include <QPlainTextEdit>
#include <QCompleter>
#include <QTimer>
#include <QMouseEvent>
#include <cstdio>
#include "slangmanager.h"
#include "syminfo.h"
#include "completionmodel.h"
#include "documentmodel.h"
#include "definitionservice.h"
#include "editorsemanticcontextservice.h"
#include "sourcenavigationservice.h"
#include "semanticindexsnapshot.h"
#include "mycodeeditor.h"

static int g_checks = 0, g_fails = 0;

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

// Put the caret on a given 0-based block (line) so getCurrentModuleScope resolves the module.
static void placeCursor(MyCodeEditor& ed, int block) {
    QTextCursor c = ed.textCursor();
    c.movePosition(QTextCursor::Start);
    c.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, block);
    ed.setTextCursor(c);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QString path = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                              : QStringLiteral("test_sv/test_symbols.sv");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QFile::Text)) { fprintf(stderr, "cannot open\n"); return 2; }
    QString content = QTextStream(&f).readAll();
    f.close();

    SlangManager mgr;
    sym_list::getInstance()->setSymbolsForFile(path, mgr.extractSymbols(path, content), content);

    // After the full GUI path (setSymbolsForFile -> rebuildScopeAndRelationships -> analyzeModuleContainment),
    // function-local symbols must KEEP their subroutine moduleScope (not get clobbered to the module).
    auto scopeOf = [](const QString& name, sym_list::sym_type_e t) -> QString {
        for (const auto& s : sym_list::getInstance()->findSymbolsByName(name))
            if (s.symbolType == t) return s.moduleScope;
        return QStringLiteral("<none>");
    };
    QString xScope = scopeOf("x", sym_list::sym_logic);
    printf("-- DB moduleScope after setSymbolsForFile: x=%s --\n", xScope.toLocal8Bit().constData());
    ++g_checks; if (xScope != QStringLiteral("add_one")) ++g_fails;
    printf("[%s] formal-arg x keeps moduleScope=add_one (not clobbered to top)\n",
           xScope == QStringLiteral("add_one") ? "PASS" : "FAIL");

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

    const QString syntheticFile =
        QFileInfo(path).dir().filePath(QStringLiteral("definition_service_member_context.sv"));
    QList<sym_list::SymbolInfo> syntheticSymbols;
    sym_list::SymbolInfo otherRed;
    otherRed.fileName = syntheticFile;
    otherRed.symbolName = QStringLiteral("red");
    otherRed.symbolType = sym_list::sym_struct_member;
    otherRed.moduleScope = QStringLiteral("other_t");
    otherRed.startLine = 7;
    otherRed.startColumn = 9;
    syntheticSymbols.append(otherRed);

    sym_list::SymbolInfo pixelRed = otherRed;
    pixelRed.moduleScope = QStringLiteral("pixel_t");
    pixelRed.startLine = 11;
    syntheticSymbols.append(pixelRed);

    sym_list::SymbolInfo pixelVar;
    pixelVar.fileName = syntheticFile;
    pixelVar.symbolName = QStringLiteral("pixel");
    pixelVar.symbolType = sym_list::sym_packed_struct_var;
    pixelVar.dataType = QStringLiteral("pixel_t");
    pixelVar.startLine = 20;
    pixelVar.startColumn = 5;
    syntheticSymbols.append(pixelVar);
    sym_list::getInstance()->setSymbolsForFile(syntheticFile, syntheticSymbols);

    DefinitionQuery memberQuery;
    memberQuery.symbolName = QStringLiteral("red");
    memberQuery.fileName = syntheticFile;
    memberQuery.linePrefixBeforeCursor = QStringLiteral("pixel.red");
    const DefinitionResult memberResult =
        DefinitionService::getInstance()->resolveDefinition(memberQuery);
    ++g_checks;
    bool memberOk = memberResult.found
        && memberResult.symbol.moduleScope == QStringLiteral("pixel_t")
        && memberResult.symbol.startLine == pixelRed.startLine;
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

    memberNavigationContext.column = 500;
    const DefinitionNavigationQuery clampedNavigationQuery =
        definitionNavigationService.navigationQueryForContext(memberNavigationContext);
    expectEq("DefinitionNavigation clamps prefix",
             clampedNavigationQuery.linePrefixBeforeCursor,
             memberNavigationContext.lineText);

    QList<sym_list::SymbolInfo> snapshotDefinitionSymbols;
    sym_list::SymbolInfo snapshotOtherRed = otherRed;
    snapshotOtherRed.fileName = QStringLiteral("snapshot_only.sv");
    snapshotOtherRed.symbolId = 6101;
    snapshotOtherRed.startLine = 1;
    snapshotOtherRed.moduleScope = QStringLiteral("other_t");
    snapshotDefinitionSymbols.append(snapshotOtherRed);

    sym_list::SymbolInfo snapshotPixelRed = otherRed;
    snapshotPixelRed.fileName = QStringLiteral("snapshot_only.sv");
    snapshotPixelRed.symbolId = 6102;
    snapshotPixelRed.startLine = 2;
    snapshotPixelRed.moduleScope = QStringLiteral("snap_pixel_t");
    snapshotDefinitionSymbols.append(snapshotPixelRed);

    sym_list::SymbolInfo snapshotPixelVar = pixelVar;
    snapshotPixelVar.fileName = QStringLiteral("snapshot_only.sv");
    snapshotPixelVar.symbolName = QStringLiteral("snap_pixel");
    snapshotPixelVar.symbolId = 6103;
    snapshotPixelVar.startLine = 3;
    snapshotPixelVar.moduleScope = QStringLiteral("snap_top");
    snapshotPixelVar.dataType = QStringLiteral("snap_pixel_t");
    snapshotDefinitionSymbols.append(snapshotPixelVar);

    sym_list::SymbolInfo snapshotHelperModule;
    snapshotHelperModule.fileName = QStringLiteral("snapshot_helper.sv");
    snapshotHelperModule.symbolName = QStringLiteral("snap_helper");
    snapshotHelperModule.symbolType = sym_list::sym_module;
    snapshotHelperModule.startLine = 12;
    snapshotHelperModule.startColumn = 1;
    snapshotHelperModule.endLine = 14;
    snapshotHelperModule.endColumn = 10;
    snapshotHelperModule.symbolId = 6104;
    snapshotDefinitionSymbols.append(snapshotHelperModule);

    sym_list::SymbolInfo snapshotHelperInterface = snapshotHelperModule;
    snapshotHelperInterface.fileName = QStringLiteral("snapshot_if.sv");
    snapshotHelperInterface.symbolName = QStringLiteral("snap_if");
    snapshotHelperInterface.symbolType = sym_list::sym_interface;
    snapshotHelperInterface.startLine = 15;
    snapshotHelperInterface.symbolId = 6107;
    snapshotDefinitionSymbols.append(snapshotHelperInterface);

    sym_list::SymbolInfo snapshotHelperPackage = snapshotHelperModule;
    snapshotHelperPackage.fileName = QStringLiteral("snapshot_pkg.sv");
    snapshotHelperPackage.symbolName = QStringLiteral("snap_pkg");
    snapshotHelperPackage.symbolType = sym_list::sym_package;
    snapshotHelperPackage.startLine = 18;
    snapshotHelperPackage.symbolId = 6108;
    snapshotDefinitionSymbols.append(snapshotHelperPackage);

    sym_list::SymbolInfo snapshotLocalDuplicate;
    snapshotLocalDuplicate.fileName = QStringLiteral("snapshot_only.sv");
    snapshotLocalDuplicate.symbolName = QStringLiteral("snap_dup");
    snapshotLocalDuplicate.symbolType = sym_list::sym_module;
    snapshotLocalDuplicate.startLine = 21;
    snapshotLocalDuplicate.startColumn = 1;
    snapshotLocalDuplicate.endLine = 23;
    snapshotLocalDuplicate.endColumn = 10;
    snapshotLocalDuplicate.symbolId = 6105;
    snapshotDefinitionSymbols.append(snapshotLocalDuplicate);

    sym_list::SymbolInfo snapshotRemoteDuplicate = snapshotLocalDuplicate;
    snapshotRemoteDuplicate.fileName = QStringLiteral("snapshot_remote.sv");
    snapshotRemoteDuplicate.startLine = 31;
    snapshotRemoteDuplicate.symbolId = 6106;
    snapshotDefinitionSymbols.append(snapshotRemoteDuplicate);

    SemanticIndex snapshotIndex;
    snapshotIndex.setSnapshot(
        std::make_shared<SemanticIndexSnapshot>(snapshotDefinitionSymbols));
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
        && snapshotMemberResult.symbol.symbolId == snapshotPixelRed.symbolId
        && snapshotMemberResult.symbol.moduleScope == QStringLiteral("snap_pixel_t");
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
        && snapshotModuleResult.symbol.symbolId == snapshotHelperModule.symbolId
        && snapshotModuleResult.symbol.fileName == QStringLiteral("snapshot_helper.sv");
    if (!snapshotModuleOk)
        ++g_fails;
    printf("[%s] DefinitionService resolves snapshot cross-file module\n",
           snapshotModuleOk ? "PASS" : "FAIL");
    expectBool("DefinitionService canResolve snapshot cross-file module",
               snapshotDefinitionService.canResolveDefinition(snapshotModuleQuery),
               true);

    DefinitionQuery snapshotInterfaceQuery;
    snapshotInterfaceQuery.symbolName = QStringLiteral("snap_if");
    snapshotInterfaceQuery.fileName = QStringLiteral("snapshot_only.sv");
    const DefinitionResult snapshotInterfaceResult =
        snapshotDefinitionService.resolveDefinition(snapshotInterfaceQuery);
    ++g_checks;
    const bool snapshotInterfaceOk = snapshotInterfaceResult.found
        && !snapshotInterfaceResult.localFile
        && snapshotInterfaceResult.symbol.symbolId == snapshotHelperInterface.symbolId
        && snapshotInterfaceResult.symbol.symbolType == sym_list::sym_interface;
    if (!snapshotInterfaceOk)
        ++g_fails;
    printf("[%s] DefinitionService resolves snapshot cross-file interface\n",
           snapshotInterfaceOk ? "PASS" : "FAIL");
    expectBool("DefinitionService canResolve snapshot cross-file interface",
               snapshotDefinitionService.canResolveDefinition(snapshotInterfaceQuery),
               true);

    DefinitionQuery snapshotPackageQuery;
    snapshotPackageQuery.symbolName = QStringLiteral("snap_pkg");
    snapshotPackageQuery.fileName = QStringLiteral("snapshot_only.sv");
    const QList<sym_list::SymbolInfo> snapshotPackageDefinitions =
        snapshotDefinitionService.findDefinitions(snapshotPackageQuery);
    ++g_checks;
    const bool snapshotPackageOk = snapshotPackageDefinitions.size() == 1
        && snapshotPackageDefinitions.first().symbolId == snapshotHelperPackage.symbolId
        && snapshotPackageDefinitions.first().symbolType == sym_list::sym_package;
    if (!snapshotPackageOk)
        ++g_fails;
    printf("[%s] DefinitionService findDefinitions resolves snapshot package\n",
           snapshotPackageOk ? "PASS" : "FAIL");

    DefinitionQuery snapshotLocalModuleQuery;
    snapshotLocalModuleQuery.symbolName = QStringLiteral("snap_dup");
    snapshotLocalModuleQuery.fileName = QStringLiteral("snapshot_only.sv");
    const DefinitionResult snapshotLocalModuleResult =
        snapshotDefinitionService.resolveDefinition(snapshotLocalModuleQuery);
    ++g_checks;
    const bool snapshotLocalModuleOk = snapshotLocalModuleResult.found
        && snapshotLocalModuleResult.localFile
        && snapshotLocalModuleResult.symbol.symbolId == snapshotLocalDuplicate.symbolId
        && snapshotLocalModuleResult.symbol.fileName == QStringLiteral("snapshot_only.sv");
    if (!snapshotLocalModuleOk)
        ++g_fails;
    printf("[%s] DefinitionService prefers snapshot local definition\n",
           snapshotLocalModuleOk ? "PASS" : "FAIL");

    const QList<sym_list::SymbolInfo> snapshotLocalDefinitions =
        snapshotDefinitionService.findDefinitions(snapshotLocalModuleQuery);
    ++g_checks;
    const bool snapshotFindDefinitionsOk = snapshotLocalDefinitions.size() == 1
        && snapshotLocalDefinitions.first().symbolId == snapshotLocalDuplicate.symbolId;
    if (!snapshotFindDefinitionsOk)
        ++g_fails;
    printf("[%s] DefinitionService findDefinitions keeps snapshot local result\n",
           snapshotFindDefinitionsOk ? "PASS" : "FAIL");

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
             path);
    expectEq("Editor symbol action module",
             editorActionContext.moduleName,
             QStringLiteral("top"));

    // Local definition target: editor context resolves to the definition location.
    sym_list::SymbolInfo counter;
    for (const auto& s : sym_list::getInstance()->findSymbolsByName("counter"))
        if (s.symbolType == sym_list::sym_reg) counter = s;
    placeCursor(ed, 95);
    const DefinitionNavigationTarget counterTarget =
        EditorSemanticContextService::getInstance()->resolveDefinitionTarget(
            QStringLiteral("counter"),
            ed.editorSemanticContextForPosition(ed.textCursor().position()));
    printf("-- target(counter): found=%d local=%d line=%d, symbol.startLine=%d --\n",
           counterTarget.found,
           counterTarget.localFile,
           counterTarget.line,
           counter.startLine);
    ++g_checks;
    bool counterOk = counterTarget.found
        && counterTarget.localFile
        && counterTarget.line == counter.startLine;
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
        sym_list::getInstance()->setSymbolsForFile(helperPath, mgr.extractSymbols(helperPath, hc), hc);

        int helperStartLine = -1;
        for (const auto& s : sym_list::getInstance()->findSymbolsByName("helper_mod"))
            if (s.symbolType == sym_list::sym_module) helperStartLine = s.startLine;

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
