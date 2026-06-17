// Headless completion-logic test. Populates sym_list from Slang, then drives CompletionManager's
// public query methods and asserts the results. No GUI window is shown.
#include "slangmanager.h"
#include "alternatecommandservice.h"
#include "completionmanager.h"
#include "completionmodel.h"
#include "completionservice.h"
#include "editorsemanticcontextservice.h"
#include "relationshipservice.h"
#include "semanticindexsnapshot.h"
#include "symboltaxonomy.h"
#include "syminfo.h"
#include <QApplication>
#include <QFile>
#include <QSet>
#include <QTextStream>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cstdio>

static int g_checks = 0;
static int g_fails = 0;

static QStringList sorted(QStringList l) { l.sort(); return l; }

static void expectEq(const char* what, const QString& got, const QString& want) {
    ++g_checks;
    bool ok = (got == want);
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=\"%s\" want=\"%s\"\n", ok ? "PASS" : "FAIL", what,
           got.toLocal8Bit().constData(), want.toLocal8Bit().constData());
}

static void expectBool(const char* what, bool got, bool want) {
    ++g_checks;
    bool ok = (got == want);
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=%s want=%s\n", ok ? "PASS" : "FAIL", what,
           got ? "true" : "false", want ? "true" : "false");
}

static void expectList(const char* what, QStringList got, QStringList want) {
    ++g_checks;
    got = sorted(got);
    want = sorted(want);
    bool ok = (got == want);
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", what,
           got.join(",").toLocal8Bit().constData(),
           want.join(",").toLocal8Bit().constData());
}

static void expectExcludes(const char* what, const QStringList& got, const QStringList& mustNot,
                           const QStringList& mustHave) {
    ++g_checks;
    bool ok = true;
    for (const QString& n : mustNot) if (got.contains(n)) ok = false;
    for (const QString& n : mustHave) if (!got.contains(n)) ok = false;
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=[%s]\n", ok ? "PASS" : "FAIL", what,
           got.join(",").toLocal8Bit().constData());
}

static QStringList symbolNames(const QList<sym_list::SymbolInfo>& symbols) {
    QStringList names;
    for (const auto& symbol : symbols)
        names << symbol.symbolName;
    return names;
}

static QStringList scoredNames(const QVector<QPair<QString, int>>& scored) {
    QStringList names;
    for (const auto& item : scored)
        names << item.first;
    return names;
}

static QStringList symbolNamesFromScored(
    const QVector<QPair<sym_list::SymbolInfo, int>>& scored) {
    QStringList names;
    for (const auto& item : scored)
        names << item.first.symbolName;
    return names;
}

static sym_list::SymbolInfo makeSymbol(const QString& name,
                                       sym_list::sym_type_e type,
                                       const QString& moduleScope,
                                       const QString& dataType,
                                       int symbolId)
{
    sym_list::SymbolInfo symbol;
    symbol.fileName = QStringLiteral("snapshot_only.sv");
    symbol.symbolName = name;
    symbol.symbolType = type;
    symbol.moduleScope = moduleScope;
    symbol.dataType = dataType;
    symbol.startLine = symbolId;
    symbol.startColumn = 1;
    symbol.endLine = symbolId;
    symbol.endColumn = 1;
    symbol.position = 0;
    symbol.length = name.size();
    symbol.symbolId = symbolId;
    return symbol;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QString path = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                              : QStringLiteral("test_sv/test_symbols.sv");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QFile::Text)) {
        fprintf(stderr, "cannot open %s\n", path.toLocal8Bit().constData());
        return 2;
    }
    QString content = QTextStream(&f).readAll();
    f.close();

    SlangManager mgr;
    QList<sym_list::SymbolInfo> syms = mgr.extractSymbols(path, content);
    sym_list::getInstance()->setSymbolsForFile(path, syms, content);

    CompletionManager* cm = CompletionManager::getInstance();

    expectList("CompletionService keyword names",
               CompletionService::getInstance()->findKeywordCompletions(
                   QStringLiteral("always_f")),
               {"always_ff"});
    expectList("CompletionService keyword scores",
               scoredNames(
                   CompletionService::getInstance()->findScoredKeywordCompletions(
                       QStringLiteral("always_f"))),
               {"always_ff"});
    expectList("CompletionService keyword abbrev",
               CompletionService::getInstance()->findKeywordAbbreviationMatches(
                   {"always_ff", "logic"}, QStringLiteral("af")),
               {"always_ff"});
    ++g_checks;
    const QList<int> keywordPositions =
        CompletionService::getInstance()->findCompletionAbbreviationPositions(
            QStringLiteral("always_ff"), QStringLiteral("af"));
    const bool keywordPositionsOk = keywordPositions == QList<int>({0, 7});
    if (!keywordPositionsOk)
        ++g_fails;
    printf("[%s] %-34s got=[%s]\n",
           keywordPositionsOk ? "PASS" : "FAIL",
           "CompletionService abbrev pos",
           QStringList({
               QString::number(keywordPositions.value(0, -1)),
               QString::number(keywordPositions.value(1, -1))
           }).join(",").toLocal8Bit().constData());
    ++g_checks;
    const bool managerKeywordScoreOk =
        cm->calculateMatchScore(QStringLiteral("always_ff"),
                                QStringLiteral("af"))
        == CompletionService::getInstance()->calculateCompletionMatchScore(
            QStringLiteral("always_ff"), QStringLiteral("af"));
    if (!managerKeywordScoreOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           managerKeywordScoreOk ? "PASS" : "FAIL",
           "CompletionManager score delegation");
    ++g_checks;
    const bool serviceItemScoreOk =
        CompletionService::getInstance()->completionItemScore(QStringLiteral("save"),
                                                              QString())
        == 100;
    if (!serviceItemScoreOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           serviceItemScoreOk ? "PASS" : "FAIL",
           "CompletionService item score");
    expectList("CompletionManager keyword names",
               cm->getKeywordCompletions(QStringLiteral("always_f")),
               {"always_ff"});
    expectList("CompletionManager keyword scores",
               scoredNames(cm->getScoredKeywordMatches(
                   QStringLiteral("always_f"))),
               {"always_ff"});
    expectList("CompletionManager keyword abbrev",
               cm->getAbbreviationMatches({"always_ff", "logic"},
                                          QStringLiteral("af")),
               {"always_ff"});
    QList<sym_list::SymbolInfo> modelScoringSymbols;
    modelScoringSymbols.append(makeSymbol(QStringLiteral("logic"),
                                          sym_list::sym_logic,
                                          QStringLiteral("top"),
                                          QString(),
                                          9001));
    modelScoringSymbols.append(makeSymbol(QStringLiteral("always_ff"),
                                          sym_list::sym_logic,
                                          QStringLiteral("top"),
                                          QString(),
                                          9002));
    CompletionModel modelScoring;
    modelScoring.updateSymbolCompletions(modelScoringSymbols,
                                         QStringLiteral("af"),
                                         sym_list::sym_logic);
    ++g_checks;
    const QString firstScoredModelSymbol =
        modelScoring.getItem(modelScoring.index(2, 0)).text;
    const bool modelScoringOk =
        firstScoredModelSymbol == QStringLiteral("always_ff");
    if (!modelScoringOk)
        ++g_fails;
    printf("[%s] %-34s got=\"%s\"\n",
           modelScoringOk ? "PASS" : "FAIL",
           "CompletionModel service scoring",
           firstScoredModelSymbol.toLocal8Bit().constData());
    expectEq("CompletionService symbol desc",
             CompletionService::getInstance()->symbolTypeDescription(sym_list::sym_logic),
             QStringLiteral("logic"));
    expectEq("CompletionModel symbol desc",
             modelScoring.getItem(modelScoring.index(2, 0)).description,
             QStringLiteral("logic"));
    expectEq("CompletionModel symbol display",
             modelScoring.getItem(modelScoring.index(2, 0)).displayText,
             QStringLiteral("always_ff (logic)"));
    expectBool("CompletionModel header selectable",
               modelScoring.getItem(modelScoring.index(0, 0)).selectable,
               false);
    expectBool("CompletionModel default metadata",
               modelScoring.getItem(modelScoring.index(1, 0)).emphasized
                   && modelScoring.getItem(modelScoring.index(1, 0))
                          .toolTipText.contains(QStringLiteral("default value")),
               true);
    expectEq("CompletionModel display role",
             modelScoring.data(modelScoring.index(2, 0), Qt::DisplayRole).toString(),
             QStringLiteral("always_ff (logic)"));
    CompletionModel commandDisplayModel;
    commandDisplayModel.updateCommandCompletions({QStringLiteral("save")},
                                                 QStringLiteral("s"));
    expectEq("CompletionModel command display",
             commandDisplayModel.getItem(commandDisplayModel.index(1, 0)).displayText,
             QStringLiteral("save - Execute save command"));
    CompletionModel noCommandModel;
    noCommandModel.updateCommandCompletions({QStringLiteral("save")},
                                            QStringLiteral("zz"));
    expectBool("CompletionModel no command selectable",
               !noCommandModel.getItem(noCommandModel.index(1, 0)).selectable
                   && noCommandModel.data(noCommandModel.index(1, 0),
                                          Qt::DisplayRole)
                          .toString()
                       == QStringLiteral("No matching commands - No commands match your input"),
               true);
    expectEq("CompletionService interface desc",
             CompletionService::getInstance()->symbolTypeDescription(
                 sym_list::sym_interface),
             QStringLiteral("interface"));
    expectEq("SymbolTaxonomy modport label",
             SymbolTaxonomy::symbolTypeLabel(sym_list::sym_interface_modport),
             QStringLiteral("modport"));
    expectBool("SymbolTaxonomy struct range type",
               SymbolTaxonomy::isModuleRangeType(sym_list::sym_packed_struct_var),
               true);
    expectBool("SymbolTaxonomy direct context completion",
               SymbolTaxonomy::isDirectModuleContextCompletionRequest(
                   sym_list::sym_unpacked_struct_var),
               true);
    expectBool("SymbolTaxonomy enum typedef typed completion",
               SymbolTaxonomy::typedCompletionSymbolTypeMatches(
                   sym_list::sym_typedef,
                   sym_list::sym_enum,
                   QStringLiteral("enum")),
               true);
    sym_list::SymbolInfo completionModuleSymbol;
    completionModuleSymbol.symbolType = sym_list::sym_module;
    completionModuleSymbol.fileName = QStringLiteral("rtl/top.sv");
    const SymbolTaxonomy::SemanticMetadata moduleMetadata =
        SymbolTaxonomy::semanticMetadata(completionModuleSymbol);
    expectBool("SymbolTaxonomy metadata global completion",
               SymbolTaxonomy::isGlobalCompletionCandidate(moduleMetadata),
               true);
    sym_list::SymbolInfo logicSymbol;
    logicSymbol.symbolType = sym_list::sym_logic;
    logicSymbol.fileName = QStringLiteral("rtl/top.sv");
    const SymbolTaxonomy::SemanticMetadata logicMetadata =
        SymbolTaxonomy::semanticMetadata(logicSymbol);
    expectBool("SymbolTaxonomy metadata internal completion",
               SymbolTaxonomy::isInternalCompletionCandidate(logicMetadata),
               true);
    expectBool("SymbolTaxonomy metadata raw compatibility",
               logicMetadata.rawCollectorKind == sym_list::sym_logic,
               true);
    SymbolTaxonomy::SemanticMetadata syntheticModuleMetadata;
    syntheticModuleMetadata.declarationKind =
        SymbolTaxonomy::DeclarationKind::Module;
    syntheticModuleMetadata.usageRole =
        SymbolTaxonomy::SymbolUsageRole::Declaration;
    syntheticModuleMetadata.ownerScope =
        SymbolTaxonomy::SymbolOwnerScope::Global;
    syntheticModuleMetadata.visibility =
        SymbolTaxonomy::SymbolVisibility::Global;
    syntheticModuleMetadata.rawCollectorKind = sym_list::sym_user;
    expectBool("SymbolTaxonomy search intent uses semantic metadata",
               SymbolTaxonomy::matchesSearchIntent(
                   syntheticModuleMetadata,
                   SymbolTaxonomy::SymbolSearchIntent::ModuleDeclarations),
               true);
    expectBool("SymbolTaxonomy global definition uses semantic metadata",
               SymbolTaxonomy::isGlobalDefinition(syntheticModuleMetadata),
               true);
    expectEq("SymbolTaxonomy metadata label",
             SymbolTaxonomy::symbolTypeLabel(syntheticModuleMetadata),
             QStringLiteral("module"));
    sym_list::SymbolInfo metadataKeySymbol;
    metadataKeySymbol.symbolName = QStringLiteral("metadata_top");
    metadataKeySymbol.symbolType = sym_list::sym_user;
    metadataKeySymbol.hasSemanticMetadata = true;
    metadataKeySymbol.semanticDeclarationKind =
        SymbolTaxonomy::DeclarationKind::Module;
    metadataKeySymbol.rawCollectorKind = sym_list::sym_user;
    const SymbolStableKey metadataKey =
        symbolStableKeyForSymbol(metadataKeySymbol);
    expectBool("stable key uses semantic declaration kind",
               metadataKey.declarationKind
                   == SymbolTaxonomy::DeclarationKind::Module,
               true);
    SymbolTaxonomy::SemanticMetadata syntheticPortMetadata;
    syntheticPortMetadata.declarationKind =
        SymbolTaxonomy::DeclarationKind::Port;
    syntheticPortMetadata.usageRole =
        SymbolTaxonomy::SymbolUsageRole::Declaration;
    expectBool("SymbolTaxonomy metadata declaration group",
               SymbolTaxonomy::declarationGroup(syntheticPortMetadata)
                   == SymbolTaxonomy::DeclarationGroup::Port,
               true);
    SymbolTaxonomy::SemanticMetadata syntheticPackageParameterMetadata;
    syntheticPackageParameterMetadata.declarationKind =
        SymbolTaxonomy::DeclarationKind::Parameter;
    expectBool("SymbolTaxonomy metadata package visibility",
               SymbolTaxonomy::isPackageVisibleDefinition(
                   syntheticPackageParameterMetadata),
               true);
    sym_list::SymbolInfo scopedStruct;
    scopedStruct.symbolType = sym_list::sym_packed_struct;
    scopedStruct.moduleScope = QStringLiteral("pkg_scope");
    expectBool("SymbolTaxonomy global struct visibility",
               SymbolTaxonomy::isGlobalSymbolInfoVisible(
                   scopedStruct,
                   sym_list::sym_packed_struct),
               true);
    sym_list::SymbolInfo scopedLogic;
    scopedLogic.symbolType = sym_list::sym_logic;
    scopedLogic.moduleScope = QStringLiteral("top");
    expectBool("SymbolTaxonomy scoped logic not global visible",
               SymbolTaxonomy::isGlobalSymbolInfoVisible(
                   scopedLogic,
                   sym_list::sym_logic),
               false);
    QSet<QString> packageScopes;
    packageScopes.insert(QStringLiteral("pkg_scope"));
    sym_list::SymbolInfo packageParameter;
    packageParameter.symbolType = sym_list::sym_parameter;
    packageParameter.moduleScope = QStringLiteral("pkg_scope");
    sym_list::SymbolInfo packageSymbol;
    packageSymbol.symbolType = sym_list::sym_package;
    packageSymbol.symbolName = QStringLiteral("pkg_scope");
    expectBool("SymbolTaxonomy package symbol overload",
               SymbolTaxonomy::isPackageDeclaration(packageSymbol),
               true);
    expectBool("SymbolTaxonomy package scope names",
               SymbolTaxonomy::packageScopeNames({packageSymbol})
                   .contains(QStringLiteral("pkg_scope")),
               true);
    expectBool("SymbolTaxonomy package command scope visible",
               SymbolTaxonomy::isCommandCompletionScopeVisible(
                   packageParameter,
                   sym_list::sym_parameter,
                   QStringLiteral("top"),
                   packageScopes),
               true);
    expectBool("SymbolTaxonomy scoped logic command scope hidden",
               SymbolTaxonomy::isCommandCompletionScopeVisible(
                   scopedLogic,
                   sym_list::sym_logic,
                   QString(),
                   packageScopes),
               false);
    expectBool("SymbolTaxonomy package definition visible",
               SymbolTaxonomy::isDefinitionVisibleInContext(
                   packageParameter,
                   QStringLiteral("top"),
                   packageScopes),
               true);
    expectBool("SymbolTaxonomy scoped logic definition hidden",
               SymbolTaxonomy::isDefinitionVisibleInContext(
                   scopedLogic,
                   QStringLiteral("other_top"),
                   packageScopes),
               false);
    expectBool("SymbolTaxonomy local definition priority wins",
               SymbolTaxonomy::definitionContextPriorityAdjustment(
                   scopedLogic,
                   QStringLiteral("top"),
                   packageScopes)
                   < SymbolTaxonomy::definitionContextPriorityAdjustment(
                       packageParameter,
                       QStringLiteral("top"),
                       packageScopes),
               true);
    sym_list::SymbolInfo moduleSymbol;
    moduleSymbol.symbolType = sym_list::sym_module;
    moduleSymbol.symbolName = QStringLiteral("top");
    expectBool("SymbolTaxonomy module symbol overload",
               SymbolTaxonomy::isModuleDeclaration(moduleSymbol),
               true);
    expectBool("SymbolTaxonomy symbol in module scope",
               SymbolTaxonomy::isSymbolInModuleScope(
                   scopedLogic,
                   QStringLiteral("top")),
               true);
    expectBool("SymbolTaxonomy module context endpoint",
               SymbolTaxonomy::isSymbolInModuleContext(
                   moduleSymbol,
                   QStringLiteral("top")),
               true);
    expectBool("SymbolTaxonomy scoped signal not other context",
               SymbolTaxonomy::isSymbolInModuleContext(
                   scopedLogic,
                   QStringLiteral("other_top")),
               false);

    AlternateCommandService* alternateCommandService =
        AlternateCommandService::getInstance();
    expectEq("AlternateCommand normalize",
             alternateCommandService->normalizeCommandInput(QStringLiteral(" SAVE_AS ")),
             QStringLiteral("save_as"));
    expectList("AlternateCommand catalog",
               alternateCommandService->commands(),
               {"save", "save_as", "open", "new", "close",
                "copy", "paste", "cut", "undo", "redo",
                "find", "replace", "goto_line", "select_all",
                "comment", "uncomment", "indent", "unindent"});
    expectList("AlternateCommand filter",
               alternateCommandService->matchingCommands(QStringLiteral("s")),
               {"save", "save_as", "select_all"});
    const AlternateCommandCompletionState alternateCompletionState =
        alternateCommandService->completionState(QStringLiteral(" S "));
    expectEq("AlternateCommand state input",
             alternateCompletionState.normalizedInput,
             QStringLiteral("s"));
    expectList("AlternateCommand state matches",
               alternateCompletionState.matches,
               {"save", "save_as", "select_all"});
    expectBool("AlternateCommand state visible",
               alternateCompletionState.showCompletions,
               true);
    const AlternateCommandCompletionState contextAlternateState =
        EditorSemanticContextService::getInstance()
            ->alternateCommandCompletionState(QStringLiteral(" S "));
    expectEq("EditorContext alternate input",
             contextAlternateState.normalizedInput,
             QStringLiteral("s"));
    expectList("EditorContext alternate matches",
               contextAlternateState.matches,
               {"save", "save_as", "select_all"});
    const EditorAlternateModeCompletionDisplayState alternateDisplayState =
        EditorSemanticContextService::getInstance()
            ->alternateModeCompletionDisplayState(QStringLiteral(" S "));
    expectBool("EditorContext alternate display",
               alternateDisplayState.updateCompletions
                   && alternateDisplayState.showPopup
                   && alternateDisplayState.normalizedInput == QStringLiteral("s")
                   && alternateDisplayState.matches
                       == QStringList{QStringLiteral("save"),
                                      QStringLiteral("save_as"),
                                      QStringLiteral("select_all")},
               true);
    const EditorAlternateModeCompletionDisplayState alternateNoMatchDisplayState =
        EditorSemanticContextService::getInstance()
            ->alternateModeCompletionDisplayState(QStringLiteral("zz"));
    expectBool("EditorContext alternate display no match",
               alternateNoMatchDisplayState.updateCompletions
                   && !alternateNoMatchDisplayState.showPopup
                   && alternateNoMatchDisplayState.normalizedInput
                       == QStringLiteral("zz")
                   && alternateNoMatchDisplayState.matches.isEmpty(),
               true);
    EditorAlternateModeKeyContext alternateKeyContext;
    alternateKeyContext.key = Qt::Key_Backspace;
    alternateKeyContext.buffer = QStringLiteral("sav");
    const EditorAlternateModeKeyState alternateBackspaceState =
        EditorSemanticContextService::getInstance()
            ->alternateModeKeyState(alternateKeyContext);
    expectBool("EditorContext alternate key backspace",
               alternateBackspaceState.action
                       == EditorAlternateModeKeyAction::UpdateInput
                   && alternateBackspaceState.nextInput == QStringLiteral("sa")
                   && alternateBackspaceState.completion.updateCompletions
                   && alternateBackspaceState.completion.showPopup
                   && alternateBackspaceState.completion.normalizedInput
                       == QStringLiteral("sa")
                   && alternateBackspaceState.completion.matches
                       == QStringList{QStringLiteral("save"),
                                      QStringLiteral("save_as")},
               true);
    alternateKeyContext.buffer.clear();
    const EditorAlternateModeKeyState alternateEmptyBackspaceState =
        EditorSemanticContextService::getInstance()
            ->alternateModeKeyState(alternateKeyContext);
    expectBool("EditorContext alternate key empty backspace",
               alternateEmptyBackspaceState.action
                       == EditorAlternateModeKeyAction::RefreshCompletions
                   && alternateEmptyBackspaceState.nextInput.isEmpty()
                   && alternateEmptyBackspaceState.completion.updateCompletions
                   && alternateEmptyBackspaceState.completion.showPopup
                   && alternateEmptyBackspaceState.completion.matches.size()
                       == alternateCommandService->commands().size(),
               true);
    alternateKeyContext.key = Qt::Key_Escape;
    alternateKeyContext.buffer = QStringLiteral("save");
    const EditorAlternateModeKeyState alternateEscapeState =
        EditorSemanticContextService::getInstance()
            ->alternateModeKeyState(alternateKeyContext);
    expectBool("EditorContext alternate key escape",
               alternateEscapeState.action
                       == EditorAlternateModeKeyAction::ClearAndHide
                   && alternateEscapeState.hidePopup
                   && alternateEscapeState.clearBuffer,
               true);
    alternateKeyContext.key = Qt::Key_Return;
    const EditorAlternateModeKeyState alternateReturnState =
        EditorSemanticContextService::getInstance()
            ->alternateModeKeyState(alternateKeyContext);
    expectBool("EditorContext alternate key return",
               alternateReturnState.action
                       == EditorAlternateModeKeyAction::ExecuteCommand
                   && alternateReturnState.command == QStringLiteral("save"),
               true);
    alternateKeyContext.key = Qt::Key_A;
    alternateKeyContext.text = QStringLiteral("a");
    alternateKeyContext.buffer = QStringLiteral("s");
    const EditorAlternateModeKeyState alternatePrintableState =
        EditorSemanticContextService::getInstance()
            ->alternateModeKeyState(alternateKeyContext);
    expectBool("EditorContext alternate key printable",
               alternatePrintableState.action
                       == EditorAlternateModeKeyAction::UpdateInput
                   && alternatePrintableState.nextInput == QStringLiteral("sa")
                   && alternatePrintableState.completion.updateCompletions
                   && alternatePrintableState.completion.normalizedInput
                       == QStringLiteral("sa")
                   && alternatePrintableState.completion.matches
                       == QStringList{QStringLiteral("save"),
                                      QStringLiteral("save_as")},
               true);
    alternateKeyContext.key = Qt::Key_F1;
    alternateKeyContext.text.clear();
    const EditorAlternateModeKeyState alternateConsumeState =
        EditorSemanticContextService::getInstance()
            ->alternateModeKeyState(alternateKeyContext);
    expectBool("EditorContext alternate key consume",
               alternateConsumeState.action
                   == EditorAlternateModeKeyAction::Consume,
               true);
    const AlternateCommandCompletionState alternateEmptyState =
        alternateCommandService->completionState(QString());
    expectBool("AlternateCommand empty visible",
               alternateEmptyState.showCompletions,
               true);
    const AlternateCommandCompletionState alternateNoMatchState =
        alternateCommandService->completionState(QStringLiteral("zz"));
    expectBool("AlternateCommand no-match hidden",
               alternateNoMatchState.showCompletions,
               false);
    ++g_checks;
    const bool alternateKnownOk =
        alternateCommandService->isKnownCommand(QStringLiteral(" SELECT_ALL "));
    if (!alternateKnownOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           alternateKnownOk ? "PASS" : "FAIL",
           "AlternateCommand known command");
    ++g_checks;
    const bool alternateActionOk =
        alternateCommandService->commandAction(QStringLiteral(" SAVE_AS "))
            == AlternateCommandAction::SaveAs
        && alternateCommandService->commandAction(QStringLiteral("new"))
            == AlternateCommandAction::NewFile
        && alternateCommandService->commandAction(QStringLiteral("select_all"))
            == AlternateCommandAction::SelectAll
        && alternateCommandService->commandAction(QStringLiteral("unknown"))
            == AlternateCommandAction::None;
    if (!alternateActionOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           alternateActionOk ? "PASS" : "FAIL",
           "AlternateCommand action mapping");

    CompletionModel commandSelectionModel;
    commandSelectionModel.updateCommandCompletions(
        QStringList{QStringLiteral("save"), QStringLiteral("open")},
        QStringLiteral("s"));
    ++g_checks;
    const QModelIndex commandSelectableIndex =
        commandSelectionModel.firstSelectableIndex();
    const bool commandSelectableOk = commandSelectableIndex.isValid()
        && commandSelectionModel.getItem(commandSelectableIndex).text == QStringLiteral("save")
        && commandSelectionModel.isSelectableIndex(commandSelectableIndex)
        && commandSelectionModel.getItem(commandSelectableIndex).score
            == CompletionService::getInstance()->completionItemScore(QStringLiteral("save"),
                                                                     QStringLiteral("s"));
    if (!commandSelectableOk)
        ++g_fails;
    printf("[%s] %-34s row=%d\n",
           commandSelectableOk ? "PASS" : "FAIL",
           "CompletionModel selectable command",
           commandSelectableIndex.row());

    CompletionModel noMatchSelectionModel;
    noMatchSelectionModel.updateCommandCompletions(
        QStringList{QStringLiteral("save"), QStringLiteral("open")},
        QStringLiteral("zz"));
    ++g_checks;
    const bool noMatchSelectableOk =
        !noMatchSelectionModel.firstSelectableIndex().isValid();
    if (!noMatchSelectableOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           noMatchSelectableOk ? "PASS" : "FAIL",
           "CompletionModel no-match hidden");

    CompletionModel defaultSelectionModel;
    defaultSelectionModel.updateSymbolCompletions({},
                                                  QStringLiteral("missing"),
                                                  sym_list::sym_logic);
    ++g_checks;
    const QModelIndex defaultSelectableIndex =
        defaultSelectionModel.firstSelectableIndex();
    const bool defaultSelectableOk = defaultSelectableIndex.isValid()
        && defaultSelectionModel.getItem(defaultSelectableIndex)
               .text
               .startsWith(QStringLiteral("[DEFAULT]"))
        && defaultSelectionModel.isSelectableIndex(defaultSelectableIndex);
    if (!defaultSelectableOk)
        ++g_fails;
    printf("[%s] %-34s row=%d\n",
           defaultSelectableOk ? "PASS" : "FAIL",
           "CompletionModel selectable default",
           defaultSelectableIndex.row());

    const CommandSymbolPresentation logicPresentation =
        CompletionService::getInstance()->commandSymbolPresentation(sym_list::sym_logic);
    expectEq("CompletionService command default",
             logicPresentation.defaultValue,
             QStringLiteral("logic"));
    expectEq("CompletionService command desc",
             logicPresentation.typeDescription,
             QStringLiteral("logic variables"));

    const CommandSymbolCompletionItem structPresentationItem =
        CompletionService::getInstance()->commandSymbolCompletionItem(
            makeSymbol(QStringLiteral("pixel"),
                       sym_list::sym_packed_struct_var,
                       QStringLiteral("pixel_t"),
                       QString(),
                       9003),
            sym_list::sym_packed_struct_var);
    expectEq("CompletionService struct text",
             structPresentationItem.text,
             QStringLiteral("pixel(pixel_t)"));
    expectEq("CompletionService struct key",
             structPresentationItem.uniqueKey,
             QStringLiteral("pixel:pixel_t"));

    const CommandSymbolCompletionItem enumPresentationItem =
        CompletionService::getInstance()->commandSymbolCompletionItem(
            makeSymbol(QStringLiteral("IDLE"),
                       sym_list::sym_enum_value,
                       QStringLiteral("top"),
                       QStringLiteral("state_t"),
                       9004),
            sym_list::sym_enum_value);
    expectEq("CompletionService enum desc",
             enumPresentationItem.description,
             QStringLiteral("state_t"));

    CompletionActivationQuery editorActivationQuery;
    editorActivationQuery.selectable = true;
    editorActivationQuery.mode = CompletionActivationMode::EditorWord;
    editorActivationQuery.itemText = QStringLiteral("enable");
    const CompletionActivationState editorActivationState =
        CompletionService::getInstance()->completionActivationState(
            editorActivationQuery);
    ++g_checks;
    const bool editorActivationOk =
        editorActivationState.action == CompletionActivationAction::ReplaceWord
        && editorActivationState.text == QStringLiteral("enable")
        && !editorActivationState.clearCommandMode
        && editorActivationState.hidePopup;
    if (!editorActivationOk)
        ++g_fails;
    printf("[%s] %-34s text=\"%s\"\n",
           editorActivationOk ? "PASS" : "FAIL",
           "CompletionService activate word",
           editorActivationState.text.toLocal8Bit().constData());

    CompletionActivationQuery commandActivationQuery;
    commandActivationQuery.selectable = true;
    commandActivationQuery.mode = CompletionActivationMode::CommandMode;
    commandActivationQuery.itemText = QStringLiteral("[DEFAULT] logic");
    commandActivationQuery.defaultValue = QStringLiteral("logic");
    const CompletionActivationState commandActivationState =
        CompletionService::getInstance()->completionActivationState(
            commandActivationQuery);
    ++g_checks;
    const bool commandActivationOk =
        commandActivationState.action == CompletionActivationAction::ReplaceLine
        && commandActivationState.text == QStringLiteral("logic")
        && commandActivationState.clearCommandMode
        && commandActivationState.hidePopup;
    if (!commandActivationOk)
        ++g_fails;
    printf("[%s] %-34s text=\"%s\"\n",
           commandActivationOk ? "PASS" : "FAIL",
           "CompletionService activate command",
           commandActivationState.text.toLocal8Bit().constData());

    CompletionActivationQuery commandFallbackQuery;
    commandFallbackQuery.selectable = true;
    commandFallbackQuery.mode = CompletionActivationMode::CommandMode;
    commandFallbackQuery.itemText = QStringLiteral("enable");
    const CompletionActivationState commandFallbackState =
        CompletionService::getInstance()->completionActivationState(
            commandFallbackQuery);
    ++g_checks;
    const bool commandFallbackOk =
        commandFallbackState.action == CompletionActivationAction::ReplaceLine
        && commandFallbackState.text == QStringLiteral("enable")
        && commandFallbackState.clearCommandMode
        && commandFallbackState.hidePopup;
    if (!commandFallbackOk)
        ++g_fails;
    printf("[%s] %-34s text=\"%s\"\n",
           commandFallbackOk ? "PASS" : "FAIL",
           "CompletionService activate fallback",
           commandFallbackState.text.toLocal8Bit().constData());

    CompletionActivationQuery alternateActivationQuery;
    alternateActivationQuery.selectable = true;
    alternateActivationQuery.mode = CompletionActivationMode::AlternateMode;
    alternateActivationQuery.itemText = QStringLiteral("save");
    const CompletionActivationState alternateActivationState =
        CompletionService::getInstance()->completionActivationState(
            alternateActivationQuery);
    ++g_checks;
    const bool alternateActivationOk =
        alternateActivationState.action
            == CompletionActivationAction::ExecuteAlternateCommand
        && alternateActivationState.text == QStringLiteral("save")
        && !alternateActivationState.clearCommandMode
        && !alternateActivationState.hidePopup;
    if (!alternateActivationOk)
        ++g_fails;
    printf("[%s] %-34s text=\"%s\"\n",
           alternateActivationOk ? "PASS" : "FAIL",
           "CompletionService activate alt",
           alternateActivationState.text.toLocal8Bit().constData());
    EditorCompletionActivationContext contextCommandActivation;
    contextCommandActivation.selectable = true;
    contextCommandActivation.commandModeActive = true;
    contextCommandActivation.itemText = QStringLiteral("clk");
    contextCommandActivation.defaultValue = QStringLiteral("logic clk");
    const CompletionActivationState contextCommandActivationState =
        EditorSemanticContextService::getInstance()
            ->completionActivationState(contextCommandActivation);
    expectBool("EditorContext command activation",
               contextCommandActivationState.action
                       == CompletionActivationAction::ReplaceLine
                   && contextCommandActivationState.text
                       == QStringLiteral("logic clk")
                   && contextCommandActivationState.clearCommandMode
                   && contextCommandActivationState.hidePopup,
               true);
    EditorCompletionActivationContext contextAlternateActivation;
    contextAlternateActivation.selectable = true;
    contextAlternateActivation.alternateModeActive = true;
    contextAlternateActivation.commandModeActive = true;
    contextAlternateActivation.itemText = QStringLiteral("save");
    const CompletionActivationState contextAlternateActivationState =
        EditorSemanticContextService::getInstance()
            ->completionActivationState(contextAlternateActivation);
    expectBool("EditorContext alternate activation",
               contextAlternateActivationState.action
                       == CompletionActivationAction::ExecuteAlternateCommand
                   && contextAlternateActivationState.text
                       == QStringLiteral("save")
                   && !contextAlternateActivationState.clearCommandMode,
               true);

    CompletionActivationQuery inactiveActivationQuery;
    inactiveActivationQuery.selectable = false;
    inactiveActivationQuery.mode = CompletionActivationMode::EditorWord;
    inactiveActivationQuery.itemText = QStringLiteral("enable");
    const CompletionActivationState inactiveActivationState =
        CompletionService::getInstance()->completionActivationState(
            inactiveActivationQuery);
    ++g_checks;
    const bool inactiveActivationOk =
        inactiveActivationState.action == CompletionActivationAction::None
        && inactiveActivationState.text.isEmpty()
        && !inactiveActivationState.clearCommandMode
        && !inactiveActivationState.hidePopup;
    if (!inactiveActivationOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           inactiveActivationOk ? "PASS" : "FAIL",
           "CompletionService activate inactive");

    CompletionPopupKeyQuery popupQuery;
    popupQuery.mode = CompletionActivationMode::EditorWord;
    popupQuery.key = Qt::Key_Down;
    expectBool("CompletionService popup forwards arrows",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action == CompletionPopupKeyAction::ForwardToPopup,
               true);
    popupQuery.key = Qt::Key_Return;
    popupQuery.hasRows = true;
    popupQuery.currentIndexValid = false;
    expectBool("CompletionService popup activates selectable",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action
                   == CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable,
               true);
    popupQuery.key = Qt::Key_Escape;
    expectBool("CompletionService popup hides",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action == CompletionPopupKeyAction::HidePopup,
               true);
    popupQuery.key = Qt::Key_Return;
    popupQuery.hasRows = true;
    popupQuery.currentIndexValid = false;
    expectBool("EditorContext popup activates selectable",
               EditorSemanticContextService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action
                   == CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable,
               true);
    EditorCompletionPopupKeyContext contextPopupQuery;
    contextPopupQuery.key = Qt::Key_Escape;
    contextPopupQuery.alternateModeActive = true;
    contextPopupQuery.commandModeActive = true;
    expectBool("EditorContext alternate popup clears",
               EditorSemanticContextService::getInstance()
                       ->completionPopupKeyState(contextPopupQuery)
                       .action
                   == CompletionPopupKeyAction::HidePopupAndClearAlternate,
               true);

    CompletionPopupKeyQuery alternatePopupQuery;
    alternatePopupQuery.mode = CompletionActivationMode::AlternateMode;
    alternatePopupQuery.key = Qt::Key_Backspace;
    alternatePopupQuery.alternateBufferEmpty = false;
    expectBool("CompletionService alternate popup edits buffer",
               CompletionService::getInstance()
                       ->completionPopupKeyState(alternatePopupQuery)
                       .action == CompletionPopupKeyAction::BackspaceAlternateInput,
               true);
    alternatePopupQuery.key = Qt::Key_Escape;
    expectBool("CompletionService alternate popup clears",
               CompletionService::getInstance()
                       ->completionPopupKeyState(alternatePopupQuery)
                       .action
                   == CompletionPopupKeyAction::HidePopupAndClearAlternate,
               true);
    alternatePopupQuery.key = Qt::Key_Return;
    alternatePopupQuery.currentIndexValid = false;
    expectBool("CompletionService alternate popup consumes empty return",
               CompletionService::getInstance()
                       ->completionPopupKeyState(alternatePopupQuery)
                       .action == CompletionPopupKeyAction::Consume,
               true);

    const CommandModeMatch commandModeMatch =
        CompletionService::getInstance()->matchCommandMode(QStringLiteral("l ena"));
    ++g_checks;
    const bool commandModeMatchOk = commandModeMatch.matched
        && commandModeMatch.prefixPosition == 0
        && commandModeMatch.input == QStringLiteral("ena")
        && commandModeMatch.command.symbolType == sym_list::sym_logic;
    if (!commandModeMatchOk)
        ++g_fails;
    printf("[%s] %-34s input=\"%s\"\n",
           commandModeMatchOk ? "PASS" : "FAIL",
           "CompletionService command match",
           commandModeMatch.input.toLocal8Bit().constData());

    const CommandModeInputState commandInputState =
        CompletionService::getInstance()->commandModeInputState(QStringLiteral("l ena"));
    ++g_checks;
    const bool commandInputStateOk = commandInputState.matched
        && !commandInputState.exitRequested
        && commandInputState.prefixPosition == 0
        && commandInputState.input == QStringLiteral("ena")
        && commandInputState.command.symbolType == sym_list::sym_logic;
    if (!commandInputStateOk)
        ++g_fails;
    printf("[%s] %-34s input=\"%s\"\n",
           commandInputStateOk ? "PASS" : "FAIL",
           "CompletionService command input",
           commandInputState.input.toLocal8Bit().constData());

    const CommandModeInputState commandExitState =
        CompletionService::getInstance()->commandModeInputState(QStringLiteral("l  "));
    ++g_checks;
    const bool commandExitStateOk = commandExitState.matched
        && commandExitState.exitRequested
        && commandExitState.prefixPosition == 0
        && commandExitState.input == QStringLiteral(" ")
        && commandExitState.command.symbolType == sym_list::sym_logic;
    if (!commandExitStateOk)
        ++g_fails;
    printf("[%s] %-34s input=\"%s\"\n",
           commandExitStateOk ? "PASS" : "FAIL",
           "CompletionService command exit",
           commandExitState.input.toLocal8Bit().constData());

    ++g_checks;
    const bool commandModeRejectOk =
        !CompletionService::getInstance()
             ->matchCommandMode(QStringLiteral("assign l ena"))
             .matched;
    if (!commandModeRejectOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           commandModeRejectOk ? "PASS" : "FAIL",
           "CompletionService command reject");

    ++g_checks;
    const bool commandInputRejectOk =
        !CompletionService::getInstance()
             ->commandModeInputState(QStringLiteral("assign l ena"))
             .matched;
    if (!commandInputRejectOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           commandInputRejectOk ? "PASS" : "FAIL",
           "CompletionService command input reject");

    CommandModeCompletionQuery commandCompletionQuery;
    commandCompletionQuery.lineUpToCursor = QStringLiteral("l en");
    commandCompletionQuery.fileName = path;
    commandCompletionQuery.moduleName = QStringLiteral("top");
    commandCompletionQuery.documentText = content;
    const CommandModeCompletionState commandCompletionState =
        CompletionService::getInstance()->commandModeCompletionState(
            commandCompletionQuery);
    expectList("CompletionService command state",
               symbolNames(commandCompletionState.symbols),
               {"enable"});
    ++g_checks;
    const bool commandCompletionStateOk = commandCompletionState.matched
        && !commandCompletionState.exitRequested
        && !commandCompletionState.hidePopup
        && commandCompletionState.showCompletions
        && commandCompletionState.completionPrefix == QStringLiteral("en")
        && commandCompletionState.command.symbolType == sym_list::sym_logic;
    if (!commandCompletionStateOk)
        ++g_fails;
    printf("[%s] %-34s prefix=\"%s\"\n",
           commandCompletionStateOk ? "PASS" : "FAIL",
           "CompletionService command state flags",
           commandCompletionState.completionPrefix.toLocal8Bit().constData());

    CommandModeCompletionQuery commandCompletionExitQuery;
    commandCompletionExitQuery.lineUpToCursor = QStringLiteral("l  ");
    const CommandModeCompletionState commandCompletionExitState =
        CompletionService::getInstance()->commandModeCompletionState(
            commandCompletionExitQuery);
    ++g_checks;
    const bool commandCompletionExitOk = commandCompletionExitState.matched
        && commandCompletionExitState.exitRequested
        && !commandCompletionExitState.showCompletions
        && commandCompletionExitState.symbols.isEmpty();
    if (!commandCompletionExitOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           commandCompletionExitOk ? "PASS" : "FAIL",
           "CompletionService command state exit");

    CommandModeCompletionQuery commandCompletionHideQuery;
    commandCompletionHideQuery.lineUpToCursor = QStringLiteral("sp pix");
    commandCompletionHideQuery.fileName = path;
    commandCompletionHideQuery.documentText = content;
    const CommandModeCompletionState commandCompletionHideState =
        CompletionService::getInstance()->commandModeCompletionState(
            commandCompletionHideQuery);
    ++g_checks;
    const bool commandCompletionHideOk = commandCompletionHideState.matched
        && commandCompletionHideState.hidePopup
        && !commandCompletionHideState.showCompletions
        && commandCompletionHideState.command.symbolType
            == sym_list::sym_packed_struct_var;
    if (!commandCompletionHideOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           commandCompletionHideOk ? "PASS" : "FAIL",
           "CompletionService command state hide");

    CompletionTriggerQuery commandTriggerQuery;
    commandTriggerQuery.lineUpToCursor = QStringLiteral("l ena ");
    commandTriggerQuery.commandModeActive = true;
    expectBool("CompletionService trigger command",
               CompletionService::getInstance()->shouldContinueCompletion(
                   commandTriggerQuery),
               true);
    const CompletionTriggerState commandTriggerState =
        CompletionService::getInstance()->completionTriggerState(commandTriggerQuery);
    expectBool("CompletionService trigger command state",
               commandTriggerState.continueCompletion,
               true);
    expectBool("CompletionService trigger command hide",
               commandTriggerState.hidePopup,
               false);

    CompletionTriggerQuery wordTriggerQuery;
    wordTriggerQuery.lineUpToCursor = QStringLiteral("assign en");
    expectBool("CompletionService trigger word",
               CompletionService::getInstance()->shouldContinueCompletion(
                   wordTriggerQuery),
               true);
    const CompletionTriggerState wordTriggerState =
        CompletionService::getInstance()->completionTriggerState(wordTriggerQuery);
    expectBool("CompletionService trigger word state",
               wordTriggerState.continueCompletion,
               true);
    expectBool("CompletionService trigger word hide",
               wordTriggerState.hidePopup,
               false);

    CompletionTriggerQuery dotTriggerQuery;
    dotTriggerQuery.lineUpToCursor = QStringLiteral("pixel.");
    expectBool("CompletionService trigger dot",
               CompletionService::getInstance()->shouldContinueCompletion(
                   dotTriggerQuery),
               true);

    CompletionTriggerQuery structSpaceTriggerQuery;
    structSpaceTriggerQuery.lineUpToCursor = QStringLiteral("pixel. ");
    structSpaceTriggerQuery.moduleName = QStringLiteral("top");
    expectBool("CompletionService trigger struct space",
               CompletionService::getInstance()->shouldContinueCompletion(
                   structSpaceTriggerQuery),
               true);
    const CompletionTriggerState structSpaceTriggerState =
        CompletionService::getInstance()->completionTriggerState(
            structSpaceTriggerQuery);
    expectBool("CompletionService trigger struct state",
               structSpaceTriggerState.continueCompletion,
               true);
    expectBool("CompletionService trigger struct hide",
               structSpaceTriggerState.hidePopup,
               false);

    CompletionTriggerQuery plainSpaceTriggerQuery;
    plainSpaceTriggerQuery.lineUpToCursor = QStringLiteral("assign value ");
    plainSpaceTriggerQuery.moduleName = QStringLiteral("top");
    expectBool("CompletionService trigger plain space",
               CompletionService::getInstance()->shouldContinueCompletion(
                   plainSpaceTriggerQuery),
               false);
    const CompletionTriggerState plainSpaceTriggerState =
        CompletionService::getInstance()->completionTriggerState(
            plainSpaceTriggerQuery);
    expectBool("CompletionService trigger plain state",
               plainSpaceTriggerState.continueCompletion,
               false);
    expectBool("CompletionService trigger plain hide",
               plainSpaceTriggerState.hidePopup,
               true);

    CompletionTriggerQuery commandStopTriggerQuery;
    commandStopTriggerQuery.lineUpToCursor = QStringLiteral("l ena;");
    commandStopTriggerQuery.commandModeActive = true;
    const CompletionTriggerState commandStopTriggerState =
        CompletionService::getInstance()->completionTriggerState(
            commandStopTriggerQuery);
    expectBool("CompletionService trigger command stop",
               commandStopTriggerState.continueCompletion,
               false);
    expectBool("CompletionService trigger command stop hide",
               commandStopTriggerState.hidePopup,
               false);

    CompletionTriggerQuery emptyTriggerQuery;
    const CompletionTriggerState emptyTriggerState =
        CompletionService::getInstance()->completionTriggerState(emptyTriggerQuery);
    expectBool("CompletionService trigger empty state",
               emptyTriggerState.continueCompletion,
               false);
    expectBool("CompletionService trigger empty hide",
               emptyTriggerState.hidePopup,
               true);

    const CommandSymbolPresentation interfacePresentation =
        CompletionService::getInstance()->commandSymbolPresentation(sym_list::sym_interface);
    expectEq("CompletionService interface default",
             interfacePresentation.defaultValue,
             QStringLiteral("interface"));

    // --- struct member completion (typedef'd) ---
    expectEq("getStructTypeForVariable(pixel)", cm->getStructTypeForVariable("pixel", "top"), "pixel_t");
    expectList("members of pixel_t", cm->getStructMemberCompletions("", "pixel_t"),
               {"red", "green", "blue"});

    // --- struct member completion (inline anonymous) ---
    expectEq("getStructTypeForVariable(byte_split)", cm->getStructTypeForVariable("byte_split", "top"), "byte_split");
    expectList("members of byte_split", cm->getStructMemberCompletions("", "byte_split"),
               {"hi", "lo"});

    // --- prefix filtering on members (note: matching is fuzzy/abbreviation, not strict prefix) ---
    // 'bl' is a subsequence only of "blue"; "green"/"red" don't contain b..l in order.
    expectList("members of pixel_t prefix 'bl'", cm->getStructMemberCompletions("bl", "pixel_t"),
               {"blue"});

    // --- module-internal logic must NOT leak function locals (x, add_one return var) ---
    QStringList logicNames;
    for (const auto& s : cm->getModuleInternalSymbolsByType("top", sym_list::sym_logic, ""))
        logicNames << s.symbolName;
    expectExcludes("top logic excludes fn-locals", logicNames,
                   /*mustNot*/ {"x", "add_one"}, /*mustHave*/ {"enable", "result"});

    CompletionQuery query;
    query.prefix = "en";
    query.fileName = path;
    query.moduleName = "top";
    expectList("CompletionService module prefix", CompletionService::getInstance()->findCompletions(query),
               {"enable"});
    const CompletionResult moduleCompletion =
        CompletionService::getInstance()->findCompletionResult(query);
    expectList("CompletionService module result",
               moduleCompletion.names,
               {"enable"});
    ++g_checks;
    const bool moduleResultSymbolsOk = moduleCompletion.symbols.size() == 1
        && moduleCompletion.symbols.first().symbolName == QStringLiteral("enable");
    if (!moduleResultSymbolsOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           moduleResultSymbolsOk ? "PASS" : "FAIL",
           "CompletionService result symbols",
           moduleCompletion.symbols.size());

    CompletionQuery memberQuery;
    memberQuery.structTypeNameForMember = "pixel_t";
    expectList("CompletionService struct members",
               CompletionService::getInstance()->findCompletions(memberQuery),
               {"red", "green", "blue"});

    memberQuery.prefix = "bl";
    const CompletionResult memberCompletion =
        CompletionService::getInstance()->findCompletionResult(memberQuery);
    const QList<sym_list::SymbolInfo> memberSymbols = memberCompletion.symbols;
    expectList("CompletionService struct prefix",
               memberCompletion.names,
               {"blue"});
    ++g_checks;
    const bool serviceSymbolOk = memberSymbols.size() == 1
        && memberSymbols.first().symbolName == QStringLiteral("blue")
        && memberSymbols.first().symbolType == sym_list::sym_struct_member
        && memberSymbols.first().moduleScope == QStringLiteral("pixel_t");
    if (!serviceSymbolOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           serviceSymbolOk ? "PASS" : "FAIL",
           "CompletionService struct symbols",
           memberSymbols.size());
    QString parsedVariableName;
    QString parsedMemberPrefix;
    ++g_checks;
    const bool parsedMemberContext =
        CompletionService::getInstance()->tryParseStructMemberContext(
            QStringLiteral("assign result = pixel.bl"),
            parsedVariableName,
            parsedMemberPrefix);
    const bool parseOk = parsedMemberContext
        && parsedVariableName == QStringLiteral("pixel")
        && parsedMemberPrefix == QStringLiteral("bl");
    if (!parseOk)
        ++g_fails;
    printf("[%s] %-34s var=\"%s\" prefix=\"%s\"\n",
           parseOk ? "PASS" : "FAIL",
           "CompletionService member parse",
           parsedVariableName.toLocal8Bit().constData(),
           parsedMemberPrefix.toLocal8Bit().constData());
    ++g_checks;
    const bool rejectsNonMemberContext =
        !CompletionService::getInstance()->tryParseStructMemberContext(
            QStringLiteral("assign result = pixel"),
            parsedVariableName,
            parsedMemberPrefix);
    if (!rejectsNonMemberContext)
        ++g_fails;
    printf("[%s] %-34s\n",
           rejectsNonMemberContext ? "PASS" : "FAIL",
           "CompletionService member parse reject");

    EditorCompletionQuery editorMemberQuery;
    editorMemberQuery.lineUpToCursor = QStringLiteral("assign result = pixel.bl");
    editorMemberQuery.wordPrefix = QStringLiteral("bl");
    editorMemberQuery.fileName = path;
    editorMemberQuery.moduleName = QStringLiteral("top");
    editorMemberQuery.cursorLine = 1;
    editorMemberQuery.cursorPosition = editorMemberQuery.lineUpToCursor.size();
    const EditorCompletionState editorMemberState =
        CompletionService::getInstance()->editorCompletionState(editorMemberQuery);
    expectList("CompletionService editor member",
               editorMemberState.completion.names,
               {"blue"});
    ++g_checks;
    const bool editorMemberStateOk = editorMemberState.available
        && editorMemberState.prefix == QStringLiteral("bl")
        && editorMemberState.replacementStartColumn
            == editorMemberQuery.lineUpToCursor.lastIndexOf(QLatin1Char('.')) + 1
        && editorMemberState.completion.symbols.size() == 1
        && editorMemberState.completion.symbols.first().symbolType
            == sym_list::sym_struct_member;
    if (!editorMemberStateOk)
        ++g_fails;
    printf("[%s] %-34s start=%d\n",
           editorMemberStateOk ? "PASS" : "FAIL",
           "CompletionService editor member state",
           editorMemberState.replacementStartColumn);

    EditorCompletionQuery editorWordQuery;
    editorWordQuery.lineUpToCursor = QStringLiteral("assign en");
    editorWordQuery.wordPrefix = QStringLiteral("en");
    editorWordQuery.fileName = path;
    editorWordQuery.moduleName = QStringLiteral("top");
    editorWordQuery.cursorLine = 1;
    editorWordQuery.cursorPosition = editorWordQuery.lineUpToCursor.size();
    const EditorCompletionState editorWordState =
        CompletionService::getInstance()->editorCompletionState(editorWordQuery);
    expectList("CompletionService editor word",
               editorWordState.completion.names,
               {"enable"});
    ++g_checks;
    const bool editorWordStateOk = editorWordState.available
        && editorWordState.prefix == QStringLiteral("en")
        && editorWordState.replacementStartColumn
            == editorWordQuery.lineUpToCursor.size() - editorWordQuery.wordPrefix.size()
        && editorWordState.completion.symbols.size() == 1
        && editorWordState.completion.symbols.first().symbolName
            == QStringLiteral("enable");
    if (!editorWordStateOk)
        ++g_fails;
    printf("[%s] %-34s start=%d\n",
           editorWordStateOk ? "PASS" : "FAIL",
           "CompletionService editor word state",
           editorWordState.replacementStartColumn);

    EditorCompletionQuery editorEmptyQuery;
    editorEmptyQuery.lineUpToCursor = QStringLiteral("assign ");
    editorEmptyQuery.fileName = path;
    editorEmptyQuery.moduleName = QStringLiteral("top");
    ++g_checks;
    const bool editorEmptyStateOk =
        !CompletionService::getInstance()
             ->editorCompletionState(editorEmptyQuery)
             .available;
    if (!editorEmptyStateOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           editorEmptyStateOk ? "PASS" : "FAIL",
           "CompletionService editor empty");

    EditorSemanticContext editorCompletionContext;
    editorCompletionContext.lineUpToCursor = editorWordQuery.lineUpToCursor;
    editorCompletionContext.wordPrefix = editorWordQuery.wordPrefix;
    editorCompletionContext.fileName = path;
    editorCompletionContext.moduleName = QStringLiteral("top");
    editorCompletionContext.cursorLine = 1;
    editorCompletionContext.cursorPosition =
        editorCompletionContext.lineUpToCursor.size();
    const EditorCompletionState contextEditorState =
        EditorSemanticContextService::getInstance()
            ->editorCompletionState(editorCompletionContext);
    expectList("EditorSemanticContext editor word",
               contextEditorState.completion.names,
               {"enable"});
    expectList("EditorSemanticContext names",
               EditorSemanticContextService::getInstance()
                   ->completionNames(QStringLiteral("en"), editorCompletionContext),
               {"enable"});

    EditorSemanticContext triggerContext;
    triggerContext.lineUpToCursor = QStringLiteral("assign en");
    const CompletionTriggerState contextTriggerState =
        EditorSemanticContextService::getInstance()
            ->completionTriggerState(triggerContext);
    expectBool("EditorSemanticContext trigger state",
               contextTriggerState.continueCompletion,
               true);

    EditorSemanticContext commandContext;
    commandContext.lineUpToCursor = QStringLiteral("l ena ");
    commandContext.fileName = path;
    commandContext.moduleName = QStringLiteral("top");
    commandContext.documentText = content;
    const CommandModeCompletionState contextCommandState =
        EditorSemanticContextService::getInstance()
            ->commandModeCompletionState(commandContext);
    expectList("EditorSemanticContext command state",
               symbolNames(contextCommandState.symbols),
               {"enable"});
    expectBool("EditorSemanticContext command state range",
               contextCommandState.matched
                   && contextCommandState.prefixPosition == 0,
               true);
    const EditorCommandModeCompletionRefreshState contextRefreshState =
        EditorSemanticContextService::getInstance()
            ->commandModeCompletionRefreshState(commandContext, false);
    expectBool("EditorSemanticContext command refresh show",
               contextRefreshState.matched
                   && contextRefreshState.commandModeActive
                   && contextRefreshState.highlightCommand
                   && contextRefreshState.showCompletions
                   && !contextRefreshState.hidePopup
                   && contextRefreshState.completion.completionPrefix
                       == QStringLiteral("ena"),
               true);
    const EditorCommandModeCompletionRefreshState suppressedRefreshState =
        EditorSemanticContextService::getInstance()
            ->commandModeCompletionRefreshState(commandContext, true);
    expectBool("EditorSemanticContext command refresh suppress",
               suppressedRefreshState.matched
                   && suppressedRefreshState.commandModeActive
                   && suppressedRefreshState.suppressAfterExit
                   && !suppressedRefreshState.showCompletions,
               true);
    EditorSemanticContext commandExitContext;
    commandExitContext.lineUpToCursor = QStringLiteral("l  ");
    const EditorCommandModeCompletionRefreshState contextExitRefreshState =
        EditorSemanticContextService::getInstance()
            ->commandModeCompletionRefreshState(commandExitContext, false);
    expectBool("EditorSemanticContext command refresh exit",
               contextExitRefreshState.matched
                   && !contextExitRefreshState.commandModeActive
                   && contextExitRefreshState.exitRequested
                   && contextExitRefreshState.markExitedByDoubleSpace
                   && contextExitRefreshState.clearCommandHighlight,
               true);
    EditorSemanticContext noCommandRefreshContext;
    noCommandRefreshContext.lineUpToCursor = QStringLiteral("assign value");
    const EditorCommandModeCompletionRefreshState noCommandRefreshState =
        EditorSemanticContextService::getInstance()
            ->commandModeCompletionRefreshState(noCommandRefreshContext, true);
    expectBool("EditorSemanticContext command refresh reset",
               !noCommandRefreshState.matched
                   && noCommandRefreshState.resetExitedByDoubleSpace,
               true);
    const CommandModeInputState contextInputState =
        EditorSemanticContextService::getInstance()
            ->commandModeInputState(commandContext);
    expectBool("EditorSemanticContext command input",
               contextInputState.matched,
               true);
    const EditorCompletionTextChangeState contextTextChangeState =
        EditorSemanticContextService::getInstance()
            ->completionTextChangeState(commandContext);
    expectBool("EditorSemanticContext text-change command",
               contextTextChangeState.commandModeActive
                   && contextTextChangeState.commandInput.matched
                   && contextTextChangeState.startCompletionTimer
                   && !contextTextChangeState.hidePopup,
               true);
    EditorSemanticContext plainTextChangeContext;
    plainTextChangeContext.lineUpToCursor = QStringLiteral("assign value ");
    plainTextChangeContext.moduleName = QStringLiteral("top");
    const EditorCompletionTextChangeState plainTextChangeState =
        EditorSemanticContextService::getInstance()
            ->completionTextChangeState(plainTextChangeContext);
    expectBool("EditorSemanticContext text-change hide",
               !plainTextChangeState.commandModeActive
                   && !plainTextChangeState.startCompletionTimer
                   && plainTextChangeState.hidePopup,
               true);
    const CommandModeMatch contextCommandMatch =
        EditorSemanticContextService::getInstance()
            ->commandModeMatch(commandContext);
    expectBool("EditorSemanticContext command match",
               contextCommandMatch.matched,
               true);

    CompletionActivationQuery contextActivationQuery;
    contextActivationQuery.selectable = true;
    contextActivationQuery.mode = CompletionActivationMode::EditorWord;
    contextActivationQuery.itemText = QStringLiteral("enable");
    const CompletionActivationState contextActivationState =
        EditorSemanticContextService::getInstance()
            ->completionActivationState(contextActivationQuery);
    expectBool("EditorSemanticContext activation",
               contextActivationState.action == CompletionActivationAction::ReplaceWord,
               true);

    EditorSemanticContext includeNavigationContext;
    includeNavigationContext.lineText = QStringLiteral("`include \"defs.svh\"");
    includeNavigationContext.column =
        includeNavigationContext.lineText.indexOf(QStringLiteral("defs"));
    const EditorSourceNavigationTarget includeNavigationTarget =
        EditorSemanticContextService::getInstance()
            ->editorSourceNavigationTarget(includeNavigationContext, 100);
    expectBool("EditorSemanticContext nav include target",
               includeNavigationTarget.matched
                   && includeNavigationTarget.includeTarget
                   && includeNavigationTarget.jumpable
                   && includeNavigationTarget.text == QStringLiteral("defs.svh")
                   && includeNavigationTarget.startPos > 100,
               true);
    const EditorSourceNavigationClickState includeClickState =
        EditorSemanticContextService::getInstance()
            ->sourceNavigationClickState(includeNavigationTarget);
    expectBool("EditorSemanticContext nav include click",
               includeClickState.action
                       == EditorSourceNavigationClickAction::OpenInclude
                   && includeClickState.text == QStringLiteral("defs.svh")
                   && includeClickState.acceptEvent,
               true);

    sym_list::SymbolInfo navSymbol =
        makeSymbol(QStringLiteral("jump_sig"),
                   sym_list::sym_logic,
                   QStringLiteral("top"),
                   QStringLiteral("logic"),
                   2100);
    navSymbol.fileName = path;
    SemanticIndex::getInstance()->setSnapshot(
        std::make_shared<SemanticIndexSnapshot>(
            QList<sym_list::SymbolInfo>{navSymbol},
            QList<SemanticRelationship>{},
            QList<SemanticDiagnostic>{},
            QHash<QString, QString>{{path, content}}));
    EditorSemanticContext identifierNavigationContext;
    identifierNavigationContext.fileName = path;
    identifierNavigationContext.moduleName = QStringLiteral("top");
    identifierNavigationContext.lineText = QStringLiteral("assign jump_sig = 1'b1;");
    identifierNavigationContext.column =
        identifierNavigationContext.lineText.indexOf(QStringLiteral("jump_sig"));
    const EditorSourceNavigationTarget identifierNavigationTarget =
        EditorSemanticContextService::getInstance()
            ->editorSourceNavigationTarget(identifierNavigationContext, 200);
    expectBool("EditorSemanticContext nav identifier target",
               identifierNavigationTarget.matched
                   && identifierNavigationTarget.identifierTarget
                   && identifierNavigationTarget.jumpable
                   && identifierNavigationTarget.text == QStringLiteral("jump_sig")
                   && identifierNavigationTarget.cursorPosition
                       == 200 + identifierNavigationContext.column,
               true);
    const EditorSourceNavigationClickState identifierClickState =
        EditorSemanticContextService::getInstance()
            ->sourceNavigationClickState(identifierNavigationTarget);
    expectBool("EditorSemanticContext nav identifier click",
               identifierClickState.action
                       == EditorSourceNavigationClickAction::NavigateToDefinition
                   && identifierClickState.contextCursorPosition
                       == identifierNavigationTarget.cursorPosition
                   && identifierClickState.acceptEvent,
               true);

    identifierNavigationContext.lineText =
        QStringLiteral("assign missing_sig = 1'b1;");
    identifierNavigationContext.column =
        identifierNavigationContext.lineText.indexOf(QStringLiteral("missing_sig"));
    const EditorSourceNavigationTarget unresolvedNavigationTarget =
        EditorSemanticContextService::getInstance()
            ->editorSourceNavigationTarget(identifierNavigationContext, 300);
    expectBool("EditorSemanticContext nav unresolved target",
               unresolvedNavigationTarget.matched
                   && unresolvedNavigationTarget.identifierTarget
                   && !unresolvedNavigationTarget.jumpable
                   && unresolvedNavigationTarget.text
                       == QStringLiteral("missing_sig"),
               true);
    const EditorSourceNavigationClickState emptyClickState =
        EditorSemanticContextService::getInstance()
            ->sourceNavigationClickState(EditorSourceNavigationTarget{});
    expectBool("EditorSemanticContext nav empty click",
               emptyClickState.action == EditorSourceNavigationClickAction::None
                   && !emptyClickState.acceptEvent,
               true);
    SemanticIndex::getInstance()->clearSnapshot();

    EditorSourceSymbolShortcutContext sourceShortcutContext;
    sourceShortcutContext.key = Qt::Key_F12;
    sourceShortcutContext.modifiers = int(Qt::ShiftModifier);
    sourceShortcutContext.semanticContext = identifierNavigationContext;
    const EditorSourceSymbolShortcutState findReferencesShortcutState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolShortcutState(sourceShortcutContext);
    expectBool("EditorSemanticContext source shortcut refs",
               findReferencesShortcutState.matched
                   && findReferencesShortcutState.acceptEvent
                   && findReferencesShortcutState.action
                       == SourceSymbolAction::FindReferences
                   && findReferencesShortcutState.semanticContext.lineText
                       == identifierNavigationContext.lineText,
               true);
    sourceShortcutContext.key = Qt::Key_R;
    sourceShortcutContext.modifiers =
        int(Qt::ControlModifier | Qt::ShiftModifier);
    const EditorSourceSymbolShortcutState relationshipsShortcutState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolShortcutState(sourceShortcutContext);
    expectBool("EditorSemanticContext source shortcut rels",
               relationshipsShortcutState.matched
                   && relationshipsShortcutState.acceptEvent
                   && relationshipsShortcutState.action
                       == SourceSymbolAction::ShowRelationships,
               true);
    sourceShortcutContext.key = Qt::Key_F12;
    sourceShortcutContext.modifiers = 0;
    const EditorSourceSymbolShortcutState plainF12State =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolShortcutState(sourceShortcutContext);
    expectBool("EditorSemanticContext source shortcut plain f12",
               !plainF12State.matched && !plainF12State.acceptEvent,
               true);
    sourceShortcutContext.key = Qt::Key_R;
    sourceShortcutContext.modifiers = int(Qt::ControlModifier);
    const EditorSourceSymbolShortcutState plainControlRState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolShortcutState(sourceShortcutContext);
    expectBool("EditorSemanticContext source shortcut plain ctrl-r",
               !plainControlRState.matched && !plainControlRState.acceptEvent,
               true);

    EditorSemanticContext sourceSymbolContext;
    sourceSymbolContext.fileName = path;
    sourceSymbolContext.moduleName = QStringLiteral("top");
    sourceSymbolContext.lineText = QStringLiteral("assign menu_sig = 1'b1;");
    sourceSymbolContext.column =
        sourceSymbolContext.lineText.indexOf(QStringLiteral("menu_sig"));
    const EditorSourceSymbolContextMenuState sourceMenuState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolContextMenuState(sourceSymbolContext);
    expectBool("EditorSemanticContext source menu enabled",
               sourceMenuState.items.size() == 2
                   && sourceMenuState.items.at(0).enabled
                   && sourceMenuState.items.at(0).action
                       == SourceSymbolAction::FindReferences
                   && sourceMenuState.items.at(1).enabled
                   && sourceMenuState.items.at(1).action
                       == SourceSymbolAction::ShowRelationships,
               true);
    const EditorSourceSymbolActionRequestState sourceRefsRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::FindReferences,
                sourceSymbolContext);
    expectBool("EditorSemanticContext source refs request",
               sourceRefsRequest.available
                   && sourceRefsRequest.action == SourceSymbolAction::FindReferences
                   && sourceRefsRequest.symbolName == QStringLiteral("menu_sig")
                   && sourceRefsRequest.fileName == path
                   && sourceRefsRequest.moduleName == QStringLiteral("top"),
               true);
    const EditorSourceSymbolActionRequestState sourceRelsRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::ShowRelationships,
                sourceSymbolContext);
    expectBool("EditorSemanticContext source rels request",
               sourceRelsRequest.available
                   && sourceRelsRequest.action
                       == SourceSymbolAction::ShowRelationships
                   && sourceRelsRequest.symbolName == QStringLiteral("menu_sig"),
               true);
    EditorSemanticContext unavailableSourceSymbolContext;
    unavailableSourceSymbolContext.lineText = sourceSymbolContext.lineText;
    unavailableSourceSymbolContext.column = sourceSymbolContext.column;
    const EditorSourceSymbolContextMenuState disabledSourceMenuState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolContextMenuState(unavailableSourceSymbolContext);
    expectBool("EditorSemanticContext source menu disabled",
               disabledSourceMenuState.items.size() == 2
                   && !disabledSourceMenuState.items.at(0).enabled
                   && !disabledSourceMenuState.items.at(1).enabled,
               true);
    const EditorSourceSymbolActionRequestState unavailableSourceRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::FindReferences,
                unavailableSourceSymbolContext);
    expectBool("EditorSemanticContext source request unavailable",
               !unavailableSourceRequest.available,
               true);

    CommandCompletionQuery commandQuery;
    commandQuery.fileName = path;
    commandQuery.moduleName = "top";
    commandQuery.symbolType = sym_list::sym_logic;
    commandQuery.prefix = "en";
    expectList("CompletionService command logic",
               CompletionService::getInstance()->findCommandCompletions(commandQuery),
               {"enable"});

    const QList<sym_list::SymbolInfo> commandLogicSymbols =
        CompletionService::getInstance()->findCommandCompletionSymbols(commandQuery);
    ++g_checks;
    const bool commandLogicOk = commandLogicSymbols.size() == 1
        && commandLogicSymbols.first().symbolName == QStringLiteral("enable")
        && commandLogicSymbols.first().symbolType == sym_list::sym_logic
        && commandLogicSymbols.first().moduleScope == QStringLiteral("top");
    if (!commandLogicOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           commandLogicOk ? "PASS" : "FAIL",
           "CompletionService command symbols",
           commandLogicSymbols.size());

    commandQuery.symbolType = sym_list::sym_packed_struct_var;
    commandQuery.prefix = "pix";
    commandQuery.documentText = content;
    const QList<sym_list::SymbolInfo> packedStructVars =
        CompletionService::getInstance()->findCommandCompletionSymbols(commandQuery);
    ++g_checks;
    const bool packedStructOk = packedStructVars.size() == 1
        && packedStructVars.first().symbolName == QStringLiteral("pixel")
        && packedStructVars.first().symbolType == sym_list::sym_packed_struct_var
        && packedStructVars.first().moduleScope == QStringLiteral("top");
    if (!packedStructOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           packedStructOk ? "PASS" : "FAIL",
           "CompletionService command struct",
           packedStructVars.size());

    commandQuery.moduleName.clear();
    ++g_checks;
    const bool structGlobalHidden =
        CompletionService::getInstance()->findCommandCompletionSymbols(commandQuery).isEmpty();
    if (!structGlobalHidden)
        ++g_fails;
    printf("[%s] %-34s\n",
           structGlobalHidden ? "PASS" : "FAIL",
           "CompletionService command struct hidden");

    QList<sym_list::SymbolInfo> snapshotSymbols;
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_top"),
                                      sym_list::sym_module,
                                      QString(),
                                      QString(),
                                      4000));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_child"),
                                      sym_list::sym_module,
                                      QString(),
                                      QString(),
                                      7000));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_if"),
                                      sym_list::sym_interface,
                                      QString(),
                                      QString(),
                                      4002));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_pkg"),
                                      sym_list::sym_package,
                                      QString(),
                                      QString(),
                                      4004));
    snapshotSymbols.append(makeSymbol(QStringLiteral("SNAP_FEATURE"),
                                      sym_list::sym_def_define,
                                      QString(),
                                      QString(),
                                      4003));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_pixel"),
                                      sym_list::sym_packed_struct_var,
                                      QString(),
                                      QStringLiteral("global_pixel_t"),
                                      5001));
    snapshotSymbols.last().fileName = QStringLiteral("other_snapshot.sv");
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_pixel"),
                                      sym_list::sym_packed_struct_var,
                                      QStringLiteral("snap_top"),
                                      QStringLiteral("snap_pixel_t"),
                                      5002));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_pair"),
                                      sym_list::sym_unpacked_struct_var,
                                      QStringLiteral("snap_top"),
                                      QStringLiteral("snap_pair_t"),
                                      5003));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_enable"),
                                      sym_list::sym_logic,
                                      QStringLiteral("snap_top"),
                                      QString(),
                                      5008));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_other_enable"),
                                      sym_list::sym_logic,
                                      QStringLiteral("other_top"),
                                      QString(),
                                      5009));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_task"),
                                      sym_list::sym_task,
                                      QString(),
                                      QString(),
                                      5010));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_state_t"),
                                      sym_list::sym_typedef,
                                      QString(),
                                      QStringLiteral("enum"),
                                      5011));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_local_state_t"),
                                      sym_list::sym_typedef,
                                      QStringLiteral("snap_top"),
                                      QStringLiteral("enum"),
                                      5012));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_state"),
                                      sym_list::sym_enum_var,
                                      QStringLiteral("snap_top"),
                                      QString(),
                                      5013));
    snapshotSymbols.append(makeSymbol(QStringLiteral("SNAP_IDLE"),
                                      sym_list::sym_enum_value,
                                      QStringLiteral("snap_top"),
                                      QString(),
                                      5014));
    snapshotSymbols.append(makeSymbol(QStringLiteral("SNAP_RUN"),
                                      sym_list::sym_enum_value,
                                      QStringLiteral("snap_top"),
                                      QString(),
                                      5015));
    snapshotSymbols.append(makeSymbol(QStringLiteral("red"),
                                      sym_list::sym_struct_member,
                                      QStringLiteral("other_t"),
                                      QString(),
                                      5004));
    snapshotSymbols.append(makeSymbol(QStringLiteral("red"),
                                      sym_list::sym_struct_member,
                                      QStringLiteral("snap_pixel_t"),
                                      QString(),
                                      5005));
    snapshotSymbols.append(makeSymbol(QStringLiteral("green"),
                                      sym_list::sym_struct_member,
                                      QStringLiteral("snap_pixel_t"),
                                      QString(),
                                      5006));
    snapshotSymbols.append(makeSymbol(QStringLiteral("blue"),
                                      sym_list::sym_struct_member,
                                      QStringLiteral("snap_pixel_t"),
                                      QString(),
                                      5007));
    const QString snapshotScopeFile = QStringLiteral("snapshot_scope.sv");
    const QString snapshotScopeContent =
        QStringLiteral("module snap_scope;\n"
                       "  logic snap_signal;\n"
                       "endmodule\n"
                       "module other_scope;\n"
                       "endmodule\n");
    sym_list::SymbolInfo snapshotScopeModule =
        makeSymbol(QStringLiteral("snap_scope"),
                   sym_list::sym_module,
                   QString(),
                   QString(),
                   6000);
    snapshotScopeModule.fileName = snapshotScopeFile;
    snapshotScopeModule.position = snapshotScopeContent.indexOf(QStringLiteral("module snap_scope"));
    snapshotScopeModule.startLine = 1;
    snapshotScopeModule.endLine = 3;
    snapshotSymbols.append(snapshotScopeModule);
    sym_list::SymbolInfo snapshotScopeSignal =
        makeSymbol(QStringLiteral("snap_signal"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_scope"),
                   QString(),
                   6001);
    snapshotScopeSignal.fileName = snapshotScopeFile;
    snapshotScopeSignal.position = snapshotScopeContent.indexOf(QStringLiteral("snap_signal"));
    snapshotScopeSignal.startLine = 2;
    snapshotScopeSignal.endLine = 2;
    snapshotSymbols.append(snapshotScopeSignal);
    sym_list::SymbolInfo snapshotOtherModule =
        makeSymbol(QStringLiteral("other_scope"),
                   sym_list::sym_module,
                   QString(),
                   QString(),
                   6002);
    snapshotOtherModule.fileName = snapshotScopeFile;
    snapshotOtherModule.position = snapshotScopeContent.indexOf(QStringLiteral("module other_scope"));
    snapshotOtherModule.startLine = 4;
    snapshotOtherModule.endLine = 5;
    snapshotSymbols.append(snapshotOtherModule);
    sym_list::SymbolInfo snapshotClock =
        makeSymbol(QStringLiteral("snap_clk"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_top"),
                   QString(),
                   6003);
    snapshotClock.fileName = snapshotScopeFile;
    snapshotSymbols.append(snapshotClock);
    sym_list::SymbolInfo snapshotReset =
        makeSymbol(QStringLiteral("snap_rst_n"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_top"),
                   QString(),
                   6004);
    snapshotReset.fileName = snapshotScopeFile;
    snapshotSymbols.append(snapshotReset);
    QList<SemanticRelationship> snapshotRelationships;
    SemanticRelationship containsSnapEnable;
    containsSnapEnable.fromId = 4000;
    containsSnapEnable.toId = 5008;
    containsSnapEnable.type = SymbolRelationshipEngine::CONTAINS;
    snapshotRelationships.append(containsSnapEnable);
    SemanticRelationship otherReferencesEnable;
    otherReferencesEnable.fromId = 5009;
    otherReferencesEnable.toId = 5008;
    otherReferencesEnable.type = SymbolRelationshipEngine::REFERENCES;
    snapshotRelationships.append(otherReferencesEnable);
    SemanticRelationship clockDrivesTop;
    clockDrivesTop.fromId = 6003;
    clockDrivesTop.toId = 4000;
    clockDrivesTop.type = SymbolRelationshipEngine::CLOCKS;
    snapshotRelationships.append(clockDrivesTop);
    SemanticRelationship resetDrivesTop;
    resetDrivesTop.fromId = 6004;
    resetDrivesTop.toId = 4000;
    resetDrivesTop.type = SymbolRelationshipEngine::RESETS;
    snapshotRelationships.append(resetDrivesTop);
    QHash<QString, QString> snapshotFileContents;
    snapshotFileContents.insert(snapshotScopeFile, snapshotScopeContent);
    SemanticIndex snapshotIndex;
    snapshotIndex.setSnapshot(
        std::make_shared<SemanticIndexSnapshot>(
            snapshotSymbols,
            snapshotRelationships,
            QList<SemanticDiagnostic>{},
            snapshotFileContents));
    CompletionService snapshotCompletionService(&snapshotIndex);
    RelationshipService snapshotRelationshipService(&snapshotIndex);
    expectBool("RelationshipService named contains",
               snapshotRelationshipService.hasNamedRelationship(
                   QStringLiteral("snap_top"),
                   QStringLiteral("snap_enable"),
                   SymbolRelationshipEngine::CONTAINS),
               true);
    expectBool("RelationshipService named rejects missing",
               snapshotRelationshipService.hasNamedRelationship(
                   QStringLiteral("snap_enable"),
                   QStringLiteral("snap_top"),
                   SymbolRelationshipEngine::CONTAINS),
               false);
    expectEq("snapshot struct prefers module",
             snapshotCompletionService.getStructTypeForVariable("snap_pixel", "snap_top"),
             "snap_pixel_t");
    expectEq("snapshot struct fallback",
             snapshotCompletionService.getStructTypeForVariable("snap_pixel", "other_top"),
             "global_pixel_t");
    expectEq("snapshot unpacked struct var",
             snapshotCompletionService.getStructTypeForVariable("snap_pair", "snap_top"),
             "snap_pair_t");
    CompletionQuery snapshotModuleQuery;
    snapshotModuleQuery.prefix = QStringLiteral("snap_e");
    snapshotModuleQuery.moduleName = QStringLiteral("snap_top");
    expectList("snapshot module completions",
               snapshotCompletionService.findCompletions(snapshotModuleQuery),
               {"snap_enable"});
    const QList<sym_list::SymbolInfo> snapshotModuleSymbols =
        snapshotCompletionService.findCompletionSymbols(snapshotModuleQuery);
    ++g_checks;
    const bool snapshotModuleSymbolOk = snapshotModuleSymbols.size() == 1
        && snapshotModuleSymbols.first().symbolName == QStringLiteral("snap_enable")
        && snapshotModuleSymbols.first().symbolType == sym_list::sym_logic
        && snapshotModuleSymbols.first().moduleScope == QStringLiteral("snap_top");
    if (!snapshotModuleSymbolOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotModuleSymbolOk ? "PASS" : "FAIL",
           "snapshot module symbols",
           snapshotModuleSymbols.size());
    CommandCompletionQuery snapshotLogicCommandQuery;
    snapshotLogicCommandQuery.fileName = QStringLiteral("snapshot_only.sv");
    snapshotLogicCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotLogicCommandQuery.symbolType = sym_list::sym_logic;
    snapshotLogicCommandQuery.prefix = QStringLiteral("snap_e");
    expectList("snapshot command logic names",
               snapshotCompletionService.findCommandCompletions(snapshotLogicCommandQuery),
               {"snap_enable"});
    const QList<sym_list::SymbolInfo> snapshotLogicCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotLogicCommandQuery);
    ++g_checks;
    const bool snapshotLogicCommandOk = snapshotLogicCommandSymbols.size() == 1
        && snapshotLogicCommandSymbols.first().symbolName == QStringLiteral("snap_enable")
        && snapshotLogicCommandSymbols.first().symbolType == sym_list::sym_logic
        && snapshotLogicCommandSymbols.first().moduleScope == QStringLiteral("snap_top");
    if (!snapshotLogicCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotLogicCommandOk ? "PASS" : "FAIL",
           "snapshot command logic symbols",
           snapshotLogicCommandSymbols.size());
    CompletionQuery snapshotGlobalQuery;
    snapshotGlobalQuery.prefix = QStringLiteral("snap");
    expectList("snapshot global completions",
               snapshotCompletionService.findCompletions(snapshotGlobalQuery),
               {"snap_child", "snap_if", "snap_pkg", "snap_scope", "snap_task", "snap_top"});
    CommandCompletionQuery snapshotTaskCommandQuery;
    snapshotTaskCommandQuery.symbolType = sym_list::sym_task;
    snapshotTaskCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command task names",
               snapshotCompletionService.findCommandCompletions(snapshotTaskCommandQuery),
               {"snap_task"});
    const QList<sym_list::SymbolInfo> snapshotTaskCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotTaskCommandQuery);
    ++g_checks;
    const bool snapshotTaskCommandOk = snapshotTaskCommandSymbols.size() == 1
        && snapshotTaskCommandSymbols.first().symbolName == QStringLiteral("snap_task")
        && snapshotTaskCommandSymbols.first().symbolType == sym_list::sym_task
        && snapshotTaskCommandSymbols.first().moduleScope.isEmpty();
    if (!snapshotTaskCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotTaskCommandOk ? "PASS" : "FAIL",
           "snapshot command task symbols",
           snapshotTaskCommandSymbols.size());
    CommandCompletionQuery snapshotModuleCommandQuery;
    snapshotModuleCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotModuleCommandQuery.symbolType = sym_list::sym_module;
    snapshotModuleCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command module in scope",
               snapshotCompletionService.findCommandCompletions(snapshotModuleCommandQuery),
               {"snap_child", "snap_scope", "snap_top"});
    const QList<sym_list::SymbolInfo> snapshotModuleCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotModuleCommandQuery);
    ++g_checks;
    const bool snapshotModuleCommandOk = snapshotModuleCommandSymbols.size() == 3
        && snapshotModuleCommandSymbols.first().symbolId == 7000
        && snapshotModuleCommandSymbols.at(1).symbolId == 6000
        && snapshotModuleCommandSymbols.last().symbolId == 4000;
    if (!snapshotModuleCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotModuleCommandOk ? "PASS" : "FAIL",
           "snapshot command module symbols",
           snapshotModuleCommandSymbols.size());
    CommandCompletionQuery snapshotInterfaceCommandQuery;
    snapshotInterfaceCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotInterfaceCommandQuery.symbolType = sym_list::sym_interface;
    snapshotInterfaceCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command interface in scope",
               snapshotCompletionService.findCommandCompletions(snapshotInterfaceCommandQuery),
               {"snap_if"});
    const QList<sym_list::SymbolInfo> snapshotInterfaceCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotInterfaceCommandQuery);
    ++g_checks;
    const bool snapshotInterfaceCommandOk = snapshotInterfaceCommandSymbols.size() == 1
        && snapshotInterfaceCommandSymbols.first().symbolId == 4002;
    if (!snapshotInterfaceCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotInterfaceCommandOk ? "PASS" : "FAIL",
           "snapshot command interface symbols",
           snapshotInterfaceCommandSymbols.size());
    CommandCompletionQuery snapshotPackageCommandQuery;
    snapshotPackageCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotPackageCommandQuery.symbolType = sym_list::sym_package;
    snapshotPackageCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command package in scope",
               snapshotCompletionService.findCommandCompletions(snapshotPackageCommandQuery),
               {"snap_pkg"});
    const QList<sym_list::SymbolInfo> snapshotPackageCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotPackageCommandQuery);
    ++g_checks;
    const bool snapshotPackageCommandOk = snapshotPackageCommandSymbols.size() == 1
        && snapshotPackageCommandSymbols.first().symbolId == 4004;
    if (!snapshotPackageCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotPackageCommandOk ? "PASS" : "FAIL",
           "snapshot command package symbols",
           snapshotPackageCommandSymbols.size());
    CommandCompletionQuery snapshotDefineCommandQuery;
    snapshotDefineCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotDefineCommandQuery.symbolType = sym_list::sym_def_define;
    snapshotDefineCommandQuery.prefix = QStringLiteral("SNAP");
    expectList("snapshot command define in scope",
               snapshotCompletionService.findCommandCompletions(snapshotDefineCommandQuery),
               {"SNAP_FEATURE"});
    const QList<sym_list::SymbolInfo> snapshotDefineCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotDefineCommandQuery);
    ++g_checks;
    const bool snapshotDefineCommandOk = snapshotDefineCommandSymbols.size() == 1
        && snapshotDefineCommandSymbols.first().symbolId == 4003;
    if (!snapshotDefineCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotDefineCommandOk ? "PASS" : "FAIL",
           "snapshot command define symbols",
           snapshotDefineCommandSymbols.size());
    CommandCompletionQuery snapshotEnumCommandQuery;
    snapshotEnumCommandQuery.symbolType = sym_list::sym_enum;
    snapshotEnumCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command enum typedef",
               snapshotCompletionService.findCommandCompletions(snapshotEnumCommandQuery),
               {"snap_state_t"});
    snapshotEnumCommandQuery.moduleName = QStringLiteral("snap_top");
    expectList("snapshot command local enum typedef",
               snapshotCompletionService.findCommandCompletions(snapshotEnumCommandQuery),
               {"snap_local_state_t"});
    CompletionQuery snapshotMemberQuery;
    snapshotMemberQuery.structTypeNameForMember = QStringLiteral("snap_pixel_t");
    expectList("snapshot struct members",
               snapshotCompletionService.findCompletions(snapshotMemberQuery),
               {"red", "green", "blue"});
    snapshotMemberQuery.prefix = QStringLiteral("bl");
    const QList<sym_list::SymbolInfo> snapshotMemberSymbols =
        snapshotCompletionService.findCompletionSymbols(snapshotMemberQuery);
    expectList("snapshot struct member prefix",
               snapshotCompletionService.findCompletions(snapshotMemberQuery),
               {"blue"});
    ++g_checks;
    const bool snapshotMemberSymbolOk = snapshotMemberSymbols.size() == 1
        && snapshotMemberSymbols.first().symbolName == QStringLiteral("blue")
        && snapshotMemberSymbols.first().symbolType == sym_list::sym_struct_member
        && snapshotMemberSymbols.first().moduleScope == QStringLiteral("snap_pixel_t");
    if (!snapshotMemberSymbolOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotMemberSymbolOk ? "PASS" : "FAIL",
           "snapshot struct member symbols",
           snapshotMemberSymbols.size());
    const int snapshotScopeCursor =
        snapshotScopeContent.indexOf(QStringLiteral("snap_signal")) + 2;
    expectEq("snapshot current module",
             snapshotCompletionService.currentModuleAt(snapshotScopeFile, snapshotScopeCursor),
             "snap_scope");
    CompletionQuery snapshotScopeQuery;
    snapshotScopeQuery.prefix = QStringLiteral("snap");
    snapshotScopeQuery.fileName = snapshotScopeFile;
    snapshotScopeQuery.cursorLine = 2;
    expectList("snapshot scope completions",
               snapshotCompletionService.findScopeCompletions(snapshotScopeQuery),
               {"snap_scope", "snap_signal"});
    SemanticQueryContext snapshotFacadeQuery;
    snapshotFacadeQuery.prefix = QStringLiteral("snap");
    snapshotFacadeQuery.fileName = snapshotScopeFile;
    snapshotFacadeQuery.cursorLine = 2;
    expectList("SemanticIndex snapshot completions",
               snapshotIndex.findCompletions(snapshotFacadeQuery),
               {"snap_scope", "snap_signal"});
    expectList("snapshot child completions",
               snapshotCompletionService.findModuleChildCompletions(
                   QStringLiteral("snap_top"),
                   QStringLiteral("snap")),
               {"snap_enable"});
    expectList("snapshot related completions",
               snapshotCompletionService.findRelatedSymbolCompletions(
                   QStringLiteral("snap_enable"),
                   QStringLiteral("snap_other")),
               {"snap_other_enable"});
    expectList("snapshot reference completions",
               snapshotCompletionService.findSymbolReferenceCompletions(
                   QStringLiteral("snap_enable"),
                   QStringLiteral("snap_other")),
               {"snap_other_enable"});
    ++g_checks;
    const bool snapshotRelationshipsAvailable =
        snapshotCompletionService.relationshipCompletionsAvailable();
    if (!snapshotRelationshipsAvailable)
        ++g_fails;
    printf("[%s] %-34s\n",
           snapshotRelationshipsAvailable ? "PASS" : "FAIL",
           "snapshot relationship availability");
    expectList("snapshot clock completions",
               snapshotCompletionService.findClockDomainCompletions(QStringLiteral("snap_c")),
               {"snap_clk"});
    expectList("snapshot reset completions",
               snapshotCompletionService.findResetSignalCompletions(QStringLiteral("snap_r")),
               {"snap_rst_n"});
    expectList("snapshot module internal variables",
               snapshotCompletionService.findModuleInternalVariableCompletions(
                   QStringLiteral("snap_top"),
                   QStringLiteral("snap_")),
               {"snap_clk", "snap_enable", "snap_rst_n"});
    expectList("snapshot module symbols by type",
               snapshotCompletionService.findModuleSymbolsByType(
                   QStringLiteral("snap_top"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_e")),
               {"snap_enable"});
    expectList("snapshot global symbol names",
               snapshotCompletionService.findGlobalSymbolCompletions(QStringLiteral("snap")),
               {"snap_child", "snap_if", "snap_pkg", "snap_scope", "snap_task", "snap_top"});
    expectList("snapshot global symbols by type",
               snapshotCompletionService.findGlobalSymbolsByType(
                   sym_list::sym_task,
                   QStringLiteral("snap")),
               {"snap_task"});
    expectList("snapshot global struct variables are not type completions",
               snapshotCompletionService.findGlobalSymbolsByType(
                   sym_list::sym_packed_struct_var,
                   QStringLiteral("snap")),
               {});
    expectList("snapshot scoped variables by type",
               snapshotCompletionService.findVariableCompletionsInScope(
                   QStringLiteral("snap_top"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_r")),
               {"snap_rst_n"});
    expectList("snapshot task/function completions",
               snapshotCompletionService.findTaskFunctionCompletions(QStringLiteral("snap")),
               {"snap_task"});
    expectList("snapshot instantiable modules",
               snapshotCompletionService.findInstantiableModuleCompletions(QStringLiteral("snap")),
               {"snap_child", "snap_scope", "snap_top"});
    expectList("snapshot struct member service",
               snapshotCompletionService.findStructMemberCompletions(
                   QStringLiteral("bl"),
                   QStringLiteral("snap_pixel_t")),
               {"blue"});
    expectList("snapshot enum value service",
               snapshotCompletionService.findEnumValueCompletions(
                   QStringLiteral("SNAP_"),
                   QStringLiteral("snap_top")),
               {"SNAP_IDLE", "SNAP_RUN"});
    expectEq("snapshot enum variable type",
             snapshotCompletionService.findEnumTypeForVariable(
                 QStringLiteral("snap_state"),
                 QStringLiteral("snap_top")),
             "snap_top");
    ContextCompletionQuery snapshotStructContextQuery;
    snapshotStructContextQuery.prefix = QStringLiteral("bl");
    snapshotStructContextQuery.currentModule = QStringLiteral("snap_top");
    snapshotStructContextQuery.context = QStringLiteral("snap_pixel.");
    expectList("snapshot context struct members",
               snapshotCompletionService.findContextAwareCompletions(snapshotStructContextQuery),
               {"blue"});
    ContextCompletionQuery snapshotEnumContextQuery;
    snapshotEnumContextQuery.prefix = QStringLiteral("SNAP_");
    snapshotEnumContextQuery.currentModule = QStringLiteral("snap_top");
    snapshotEnumContextQuery.context = QStringLiteral("case(snap_state)");
    expectList("snapshot context enum values",
               snapshotCompletionService.findContextAwareCompletions(snapshotEnumContextQuery),
               {"SNAP_IDLE", "SNAP_RUN", "snap_enable"});
    ContextCompletionQuery snapshotGeneralContextQuery;
    snapshotGeneralContextQuery.prefix = QStringLiteral("snap_e");
    snapshotGeneralContextQuery.currentModule = QStringLiteral("snap_top");
    snapshotGeneralContextQuery.context = QStringLiteral("general");
    expectList("snapshot context general",
               snapshotCompletionService.findContextAwareCompletions(snapshotGeneralContextQuery),
               {"snap_enable", "snap_scope", "snap_state_t"});
    expectList("snapshot all-symbol completions",
               snapshotCompletionService.findAllSymbolCompletions(QStringLiteral("snap_clk")),
               {"snap_clk"});
    expectList("snapshot scored all-symbol completions",
               scoredNames(snapshotCompletionService.findScoredAllSymbolCompletions(
                   QStringLiteral("snap_clk"))),
               {"snap_clk"});
    expectList("snapshot typed symbol completions",
               snapshotCompletionService.findSymbolCompletionsByType(
                   sym_list::sym_logic,
                   QStringLiteral("snap_e")),
               {"snap_enable", "snap_other_enable"});
    expectList("snapshot scored typed symbol completions",
               symbolNamesFromScored(snapshotCompletionService.findScoredSymbolCompletionsByType(
                   sym_list::sym_logic,
                   QStringLiteral("snap_e"))),
               {"snap_enable", "snap_other_enable"});
    expectList("snapshot smart completions no relationships",
               scoredNames(snapshotCompletionService.findSmartCompletions(
                   QStringLiteral("snap_clk"),
                   QString(),
                   -1,
                   false)),
               {"snap_clk"});
    expectList("snapshot smart completions in scope",
               scoredNames(snapshotCompletionService.findSmartCompletions(
                   QStringLiteral("snap_sig"),
                   snapshotScopeFile,
                   snapshotScopeCursor,
                   true)),
               {"snap_signal"});
    expectList("snapshot module info symbols by type",
               symbolNames(snapshotCompletionService.findModuleInternalSymbolInfosByType(
                   QStringLiteral("snap_top"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_"))),
               {"snap_clk", "snap_enable", "snap_rst_n"});
    expectList("snapshot module context info symbols",
               symbolNames(snapshotCompletionService.findModuleContextSymbolInfosByType(
                   QStringLiteral("snap_scope"),
                   snapshotScopeFile,
                   sym_list::sym_logic,
                   QStringLiteral("snap"))),
               {"snap_signal"});
    expectList("snapshot global info symbols by type",
               symbolNames(snapshotCompletionService.findGlobalSymbolInfosByType(
                   sym_list::sym_packed_struct_var,
                   QStringLiteral("snap"))),
               {"snap_pixel"});
    SemanticIndex::getInstance()->setSnapshot(
        std::make_shared<SemanticIndexSnapshot>(
            snapshotSymbols,
            snapshotRelationships,
            QList<SemanticDiagnostic>{},
            snapshotFileContents));
    expectList("CompletionManager child delegation",
               cm->getModuleChildrenCompletions(QStringLiteral("snap_top"),
                                                QStringLiteral("snap")),
               {"snap_enable"});
    expectList("CompletionManager related delegation",
               cm->getRelatedSymbolCompletions(QStringLiteral("snap_enable"),
                                               QStringLiteral("snap_other")),
               {"snap_other_enable"});
    expectList("CompletionManager reference delegation",
               cm->getSymbolReferencesCompletions(QStringLiteral("snap_enable"),
                                                  QStringLiteral("snap_other")),
               {"snap_other_enable"});
    expectList("CompletionManager clock delegation",
               cm->getClockDomainCompletions(QStringLiteral("snap_c")),
               {"snap_clk"});
    expectList("CompletionManager reset delegation",
               cm->getResetSignalCompletions(QStringLiteral("snap_r")),
               {"snap_rst_n"});
    expectList("CompletionManager module variable delegation",
               cm->getModuleInternalVariables(QStringLiteral("snap_top"),
                                              QStringLiteral("snap_e")),
               {"snap_enable"});
    expectList("CompletionManager global delegation",
               cm->getGlobalSymbolCompletions(QStringLiteral("snap")),
               {"snap_child", "snap_if", "snap_pkg", "snap_scope", "snap_task", "snap_top"});
    expectList("CompletionManager type delegation",
               cm->getGlobalSymbolsByType(sym_list::sym_task, QStringLiteral("snap")),
               {"snap_task"});
    expectList("CompletionManager task/function delegation",
               cm->getTaskFunctionCompletions(QStringLiteral("snap")),
               {"snap_task"});
    expectList("CompletionManager module delegation",
               cm->getInstantiableModules(QStringLiteral("snap")),
               {"snap_child", "snap_scope", "snap_top"});
    expectList("CompletionManager context struct delegation",
               cm->getContextAwareCompletions(QStringLiteral("bl"),
                                              QStringLiteral("snap_top"),
                                              QStringLiteral("snap_pixel.")),
               {"blue"});
    expectList("CompletionManager context enum delegation",
               cm->getContextAwareCompletions(QStringLiteral("SNAP_"),
                                              QStringLiteral("snap_top"),
                                              QStringLiteral("case(snap_state)")),
               {"SNAP_IDLE", "SNAP_RUN", "snap_enable"});
    expectList("CompletionManager struct member delegation",
               cm->getStructMemberCompletions(QStringLiteral("bl"),
                                              QStringLiteral("snap_pixel_t")),
               {"blue"});
    expectList("CompletionManager all-symbol delegation",
               cm->getAllSymbolCompletions(QStringLiteral("snap_clk")),
               {"snap_clk"});
    expectList("CompletionManager scored all-symbol delegation",
               scoredNames(cm->getScoredAllSymbolMatches(QStringLiteral("snap_clk"))),
               {"snap_clk"});
    expectList("CompletionManager typed symbol delegation",
               cm->getSymbolCompletions(sym_list::sym_logic, QStringLiteral("snap_e")),
               {"snap_enable", "snap_other_enable"});
    expectList("CompletionManager scored typed symbol delegation",
               symbolNamesFromScored(cm->getScoredSymbolMatches(
                   sym_list::sym_logic,
                   QStringLiteral("snap_e"))),
               {"snap_enable", "snap_other_enable"});
    expectList("CompletionManager smart delegation",
               scoredNames(cm->getSmartCompletions(QStringLiteral("snap_sig"),
                                                   snapshotScopeFile,
                                                   snapshotScopeCursor)),
               {"snap_signal"});
    expectList("CompletionManager module info delegation",
               symbolNames(cm->getModuleInternalSymbolsByType(
                   QStringLiteral("snap_top"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_"))),
               {"snap_clk", "snap_enable", "snap_rst_n"});
    expectList("CompletionManager context info delegation",
               symbolNames(cm->getModuleContextSymbolsByType(
                   QStringLiteral("snap_scope"),
                   snapshotScopeFile,
                   sym_list::sym_logic,
                   QStringLiteral("snap"))),
               {"snap_signal"});
    expectList("CompletionManager global info delegation",
               symbolNames(cm->getGlobalSymbolsByType_Info(
                   sym_list::sym_packed_struct_var,
                   QStringLiteral("snap"))),
               {"snap_pixel"});
    SemanticIndex::getInstance()->clearSnapshot();

    CommandCompletionQuery snapshotCommandQuery;
    snapshotCommandQuery.fileName = QStringLiteral("snapshot_only.sv");
    snapshotCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotCommandQuery.documentText = QStringLiteral("module snap_top;\nendmodule\n");
    snapshotCommandQuery.symbolType = sym_list::sym_packed_struct_var;
    snapshotCommandQuery.prefix = QStringLiteral("snap");
    const QList<sym_list::SymbolInfo> snapshotCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotCommandQuery);
    ++g_checks;
    const bool snapshotCommandOk = snapshotCommandSymbols.size() == 1
        && snapshotCommandSymbols.first().symbolName == QStringLiteral("snap_pixel")
        && snapshotCommandSymbols.first().symbolType == sym_list::sym_packed_struct_var
        && snapshotCommandSymbols.first().moduleScope == QStringLiteral("snap_top")
        && snapshotCommandSymbols.first().dataType == QStringLiteral("snap_pixel_t");
    if (!snapshotCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotCommandOk ? "PASS" : "FAIL",
           "snapshot command struct symbols",
           snapshotCommandSymbols.size());

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
