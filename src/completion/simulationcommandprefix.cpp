#include "simulationcommandprefix.h"
#include "completiontypes.h"
#include "editorfileidentity.h"

bool SimulationCommandPrefix::refersToValue(const QString &expression, SemanticIndex &index,
                                            const SemanticQueryContext &context)
{
    if (index.resolveVisibleValueRecord(expression, context).isValid())
        return true;
    for (const auto category : {CompletionCommandKind::Parameter, CompletionCommandKind::Localparam})
        for (const auto &record : index.getCommandCompletionSymbolRecords(context, category))
            if (record.name == expression)
                return true;
    const auto records = index.getSymbolRecords();
    QStringList scopes;
    for (const auto &record : records)
        if (record.name == context.moduleName &&
            EditorFileIdentity::same(record.location.fileName, context.fileName))
            scopes += record.presentation.elaboratedInstancePaths;
    for (const auto &record : records)
    {
        if (record.presentation.objectInfoByPath.contains(expression))
            return true;
        for (const auto &scope : scopes)
            if (record.presentation.objectInfoByPath.contains(scope + '.' + expression))
                return true;
        if (record.owner.kind == SymbolTaxonomy::SymbolOwnerScope::Package &&
            record.presentation.defaultInfo.available && record.owner.name + "::" + record.name == expression)
            return true;
    }
    return false;
}
