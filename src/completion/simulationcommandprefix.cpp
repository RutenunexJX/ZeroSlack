#include "simulationcommandprefix.h"
#include "editorfileidentity.h"
#include "tsdocument.h"
#include <QRegularExpression>

bool SimulationCommandPrefix::nativeEventInput(const QString& pending)
{
    if (!pending.trimmed().startsWith('@')) return false;
    // Parse a pending event control in the same SystemVerilog grammar used by
    // completion. A synthetic operand lets incomplete @(... and @* recover
    // immediately; named events remain pending until their terminating ';'.
    const auto tail = pending.trimmed().mid(1).trimmed();
    if (!(tail.startsWith('(') || tail.startsWith('*') || tail.endsWith(';'))) return false;
    QString probe = pending;
    if (tail == "(") probe += "posedge __template_event) ;";
    else if (tail == "*") probe += " ;";
    else if (tail.startsWith('(') && !tail.endsWith(';')) probe += ") ;";
    const QString prefix = QStringLiteral("module __template_event_probe; initial begin ");
    TSDocument syntax; syntax.setText(prefix + probe + QStringLiteral(" end endmodule"));
    const auto byte = quint32((prefix.size() + pending.indexOf('@')) * sizeof(QChar));
    auto node = ts_node_descendant_for_byte_range(syntax.rootNode(), byte, byte + sizeof(QChar));
    for (; !ts_node_is_null(node); node = ts_node_parent(node)) if (QByteArray(ts_node_type(node)) == "event_control") return true;
    return false;
}

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

bool SimulationCommandPrefix::capture(GlobalControlQueryContext &context)
{
    if (!context.editorAvailable || context.readOnly || context.cursorPosition < 0)
        return false;
    const auto before = context.documentText.left(context.cursorPosition);
    static const QRegularExpression prefix(
        QStringLiteral("(?:^|[^\\p{L}\\p{N}_$@])(@[\\p{L}_][\\p{L}\\p{N}_.:-]*|@)$"));
    const auto match = prefix.match(before);
    if (!match.hasMatch())
        return false;
    const int start = match.capturedStart(1);
    const auto keyword = match.captured(1).mid(1);
    if (context.cursorPosition < context.documentText.size())
    {
        const auto next = context.documentText[context.cursorPosition];
        if (next.isLetterOrNumber() || next == QLatin1Char('_') || next == QLatin1Char('(') ||
            next == QLatin1Char('*'))
            return false;
    }
    TSDocument syntax;
    syntax.setText(context.documentText);
    const auto byte = quint32(start * sizeof(QChar));
    auto node = ts_node_descendant_for_byte_range(syntax.rootNode(), byte, byte + sizeof(QChar));
    for (; !ts_node_is_null(node); node = ts_node_parent(node))
    {
        const auto type = QByteArray(ts_node_type(node));
        if (type.contains("comment") || type.contains("string") || type == "attribute_instance")
            return false;
        if (type == "event_control" && ts_node_start_byte(node) < quint32(byte))
            return false;
    }
    // A resolved value after @ is ordinary SV event control. Do not consume it
    // as an alias even when a command happens to have the same name.
    if (!keyword.isEmpty())
    {
        SemanticQueryContext query;
        query.fileName = context.fileName;
        query.moduleName = context.moduleName;
        query.packageName = context.packageName;
        query.cursorPosition = context.cursorPosition;
        query.cursorLine = context.cursorLine;
        if (refersToValue(keyword, *SemanticIndex::getInstance(), query))
            return false;
    }
    context.simulationCommands = true;
    context.replacementStart = start;
    context.replacementLength = context.cursorPosition - start;
    context.initialQuery = keyword;
    return true;
}
