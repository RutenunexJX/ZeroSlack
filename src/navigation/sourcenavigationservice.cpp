#include "sourcenavigationservice.h"
#include "tsdocument.h"

#include <QStringList>

std::unique_ptr<SourceNavigationService> SourceNavigationService::instance = nullptr;

SourceNavigationService* SourceNavigationService::getInstance()
{
    if (!instance)
        instance = std::make_unique<SourceNavigationService>();
    return instance.get();
}

SourceNavigationService::SourceNavigationService() = default;

SourceNavigationService::~SourceNavigationService() = default;

namespace {
bool isPreprocessorDirectiveIdentifier(const QString& identifier)
{
    static const QStringList directives = {
        QStringLiteral("begin_keywords"),
        QStringLiteral("celldefine"),
        QStringLiteral("default_nettype"),
        QStringLiteral("define"),
        QStringLiteral("else"),
        QStringLiteral("elsif"),
        QStringLiteral("end_keywords"),
        QStringLiteral("endcelldefine"),
        QStringLiteral("endif"),
        QStringLiteral("ifdef"),
        QStringLiteral("ifndef"),
        QStringLiteral("include"),
        QStringLiteral("line"),
        QStringLiteral("pragma"),
        QStringLiteral("resetall"),
        QStringLiteral("timescale"),
        QStringLiteral("undef"),
        QStringLiteral("unconnected_drive"),
    };
    return directives.contains(identifier);
}
}

SourceNavigationTarget SourceNavigationService::targetAtPosition(
    const TSDocument& document, int position) const
{
    const auto& text = document.text();
    // Identifier/comment edits retain a valid, positionally updated tree even
    // while its optional reparse is pending. Structural edits do not.
    const bool unsafePending = document.hasPendingEdits()
        && !document.lastEditPreservedStructure();
    if (position < 0 || position > text.size() || text.isEmpty()
        || unsafePending || document.hasDeferredSyntaxEdits()) return {};
    int hit = qMin(position, text.size() - 1);
    if (document.isCommentAt(hit)) return {};
    TSNode node = ts_node_named_descendant_for_byte_range(document.rootNode(), hit * 2, hit * 2);
    for (TSNode parent = node; !ts_node_is_null(parent); parent = ts_node_parent(parent)) {
        if (qstrcmp(ts_node_type(parent), "include_compiler_directive") != 0) continue;
        for (uint32_t i = 0; i < ts_node_named_child_count(parent); ++i) {
            const TSNode path = ts_node_named_child(parent, i);
            const auto type = ts_node_type(path);
            if (qstrcmp(type, "quoted_string") && qstrcmp(type, "system_lib_string")) continue;
            const int start = int(ts_node_start_byte(path) / 2) + 1;
            const int end = int(ts_node_end_byte(path) / 2) - 1;
            if (hit >= start && hit < end)
                return {true, SourceNavigationTargetKind::IncludeDirective, text.mid(start, end-start), start, end};
        }
        return {};
    }
    if (document.isStringAt(hit)) return {};
    if ((text.at(hit) == QLatin1Char('.') || text.at(hit) == QLatin1Char('`')) && hit + 1 < text.size()) ++hit;
    const auto identifier = document.identifierAt(hit);
    if (!identifier.ok() || hit < identifier.startChar || hit >= identifier.endChar) return {};
    if (identifier.startChar > 0 && text.at(identifier.startChar-1) == QLatin1Char('`')
        && isPreprocessorDirectiveIdentifier(identifier.text)) return {};
    auto kind = SourceNavigationTargetKind::Identifier;
    node = ts_node_named_descendant_for_byte_range(document.rootNode(), identifier.startChar*2, identifier.endChar*2);
    for (; !ts_node_is_null(node); node = ts_node_parent(node)) {
        if (qstrcmp(ts_node_type(node), "package_import_declaration") != 0) continue;
        int next = identifier.endChar;
        while (next < text.size() && text.at(next).isSpace()) ++next;
        if (text.mid(next, 2) == QStringLiteral("::")) kind = SourceNavigationTargetKind::PackageImport;
        break;
    }
    return {true, kind, identifier.text, identifier.startChar, identifier.endChar};
}

SourceNavigationTarget SourceNavigationService::targetAtColumn(const QString& text, int column) const
{
    // Compatibility for pure-text callers: same syntax rules as the editor.
    TSDocument document; document.setText(text);
    return targetAtPosition(document, column);
}

IncludeDirectiveTarget SourceNavigationService::includeAtColumn(const QString& text, int column) const
{
    const auto target = targetAtColumn(text, column);
    if (target.kind != SourceNavigationTargetKind::IncludeDirective) return {};
    return {true, target.text, target.startColumn, target.endColumn};
}

PackageImportTarget SourceNavigationService::packageImportAtColumn(const QString& text, int column) const
{
    const auto target = targetAtColumn(text, column);
    if (target.kind != SourceNavigationTargetKind::PackageImport) return {};
    return {true, target.text, target.startColumn, target.endColumn};
}

SourceIdentifierTarget SourceNavigationService::identifierAtColumn(const QString& text, int column) const
{
    const auto target = targetAtColumn(text, column);
    if (!target.matched || target.kind == SourceNavigationTargetKind::IncludeDirective) return {};
    return {true, target.text, target.startColumn, target.endColumn};
}

SourceMemberAccessTarget SourceNavigationService::memberAccessAtColumn(const QString& text, int column) const
{
    TSDocument document; document.setText(text);
    const auto selected = targetAtPosition(document, column);
    if (!selected.matched || selected.kind != SourceNavigationTargetKind::Identifier) return {};
    int start = selected.startColumn, end = selected.endColumn;
    QStringList names{selected.text};
    while (start > 0) {
        int dot = start-1;
        while (dot >= 0 && text.at(dot).isSpace()) --dot;
        if (dot < 0 || text.at(dot) != QLatin1Char('.')) break;
        int hit = dot-1;
        while (hit >= 0 && text.at(hit).isSpace()) --hit;
        const auto previous = targetAtPosition(document, hit);
        if (!previous.matched || previous.kind != SourceNavigationTargetKind::Identifier || previous.endColumn > dot) break;
        names.prepend(previous.text); start = previous.startColumn;
    }
    while (end < text.size()) {
        int dot = end;
        while (dot < text.size() && text.at(dot).isSpace()) ++dot;
        if (dot >= text.size() || text.at(dot) != QLatin1Char('.')) break;
        int hit = dot+1;
        while (hit < text.size() && text.at(hit).isSpace()) ++hit;
        const auto next = targetAtPosition(document, hit);
        if (!next.matched || next.kind != SourceNavigationTargetKind::Identifier || next.startColumn <= dot) break;
        names.append(next.text); end = next.endColumn;
    }
    if (names.size() < 2) return {};
    return {true,names.join('.'),names.first(),names.sliced(1).join('.'),start,end};
}

SourceSymbolActionContext SourceNavigationService::symbolActionContextAtColumn(
    const QString& lineText,
    int column,
    const QString& fileName,
    const QString& moduleName) const
{
    SourceSymbolActionContext context;
    if (fileName.isEmpty())
        return context;

    const SourceIdentifierTarget identifier = identifierAtColumn(lineText, column);
    if (!identifier.matched || identifier.identifier.isEmpty())
        return context;

    const SourceMemberAccessTarget memberAccess =
        memberAccessAtColumn(lineText, column);

    context.available = true;
    context.symbolName = identifier.identifier;
    if (memberAccess.matched) {
        context.memberAccessPath = memberAccess.accessPath;
        context.memberAccessRootName = memberAccess.rootIdentifier;
    }
    context.fileName = fileName;
    context.moduleName = moduleName;
    return context;
}

SourceEditorNavigationTarget SourceNavigationService::editorNavigationTargetAtColumn(
    const QString& lineText,
    int column,
    const std::function<bool(const QString&)>& canResolveIdentifier) const
{
    TSDocument document; document.setText(lineText);
    return editorNavigationTargetAtPosition(document, column, canResolveIdentifier);
}

SourceEditorNavigationTarget SourceNavigationService::editorNavigationTargetAtPosition(
    const TSDocument& document, int column,
    const std::function<bool(const QString&)>& canResolveIdentifier) const
{
    SourceEditorNavigationTarget editorTarget;
    const SourceNavigationTarget sourceTarget = targetAtPosition(document, column);
    if (!sourceTarget.matched)
        return editorTarget;

    editorTarget.matched = true;
    editorTarget.text = sourceTarget.text;
    editorTarget.startColumn = sourceTarget.startColumn;
    editorTarget.endColumn = sourceTarget.endColumn;
    editorTarget.cursorColumn =
        sourceTarget.kind == SourceNavigationTargetKind::Identifier
            ? sourceTarget.startColumn
            : column;
    editorTarget.includeTarget =
        sourceTarget.kind == SourceNavigationTargetKind::IncludeDirective;
    editorTarget.identifierTarget =
        sourceTarget.kind == SourceNavigationTargetKind::Identifier;
    editorTarget.jumpable = editorTarget.includeTarget
        || sourceTarget.kind == SourceNavigationTargetKind::PackageImport
        || (editorTarget.identifierTarget
            && canResolveIdentifier
            && canResolveIdentifier(editorTarget.text));
    return editorTarget;
}

SourceLineNavigationTarget SourceNavigationService::lineNavigationTarget(
    int lineNumber,
    int columnNumber) const
{
    SourceLineNavigationTarget target;
    if (lineNumber <= 0)
        return target;

    target.matched = true;
    target.lineNumber = lineNumber;
    target.columnNumber = columnNumber > 1 ? columnNumber : -1;
    target.lineMoves = lineNumber - 1;
    target.columnMoves = target.columnNumber > 1 ? target.columnNumber - 1 : 0;
    return target;
}
