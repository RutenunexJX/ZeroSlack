#include "slangmanager.h"
#include "semanticanalysisinput.h"

QList<SemanticSymbolRecord> SlangManager::extractSymbolRecords(
    const QString& fileName, const QString& content,
    const QStringList& includeDirs, const QHash<QString, QString>& defines,
    QList<EffectiveValueFact>* effectiveValueFacts)
{
    return extractOverlayWorkspaceSymbolRecords({{fileName, content}}, includeDirs,
        defines, {}, effectiveValueFacts, {fileName});
}

QList<SemanticSymbolRecord> SlangManager::extractWorkspaceSymbolRecords(
    const QStringList& filePaths, const QStringList& includeDirs,
    const QHash<QString, QString>& defines, std::function<bool()> isCancelled,
    QList<EffectiveValueFact>* effectiveValueFacts,
    QHash<QString, QString>* analyzedFileContents)
{
    SemanticInputCapture input(SemanticAnalysisRequest{});
    auto result = analyzeCapturedWorkspace(input, filePaths, includeDirs, defines,
                                           {}, isCancelled, {true, false, false});
    if (effectiveValueFacts)
        *effectiveValueFacts = std::move(result.effectiveFacts);
    if (analyzedFileContents)
        *analyzedFileContents = result.error.isEmpty() && !result.cancelled
            ? input.contents() : QHash<QString, QString>{};
    return result.symbols;
}

QList<SemanticSymbolRecord> SlangManager::extractOverlayWorkspaceSymbolRecords(
    const QHash<QString, QString>& contents, const QStringList& includeDirs,
    const QHash<QString, QString>& defines, std::function<bool()> isCancelled,
    QList<EffectiveValueFact>* effectiveValueFacts, const QStringList& orderedFilePaths)
{
    auto result = analyzeOverlayWorkspace(contents, orderedFilePaths, includeDirs,
                                          defines, isCancelled, {true, false, false});
    if (effectiveValueFacts)
        *effectiveValueFacts = std::move(result.effectiveFacts);
    return result.symbols;
}
