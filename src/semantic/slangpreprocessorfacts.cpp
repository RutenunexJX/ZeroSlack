#include "slangpreprocessorfacts.h"

#include "slangsymbolcollectorhelpers.h"

#include <slang/parsing/Token.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxNode.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>

#include <QSet>

#include <string>
#include <string_view>

namespace slang_preprocessor_facts {
namespace {

QString fromUtf8(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

bool fillRange(const slang::SourceManager* sourceManager,
               slang::SourceLocation startLocation,
               slang::SourceLocation endLocation,
               SemanticSymbolRecord* record)
{
    if (!sourceManager || !record || !startLocation.valid()
        || !endLocation.valid()) {
        return false;
    }

    const auto start =
        slang_symbols::detail::qTextDocumentSourcePosition(sourceManager,
                                                           startLocation);
    const auto end =
        slang_symbols::detail::qTextDocumentSourcePosition(sourceManager,
                                                           endLocation);
    if (!start.isValid() || !end.isValid()
        || start.fileName != end.fileName || end.position < start.position) {
        return false;
    }

    record->location.fileName = start.fileName;
    record->location.startLine = start.line;
    record->location.startColumn = start.column;
    record->location.endLine = end.line;
    record->location.endColumn = end.column;
    record->location.position = start.position;
    record->location.length = end.position - start.position;
    record->sourceRole =
        SymbolTaxonomy::sourceRoleForFileName(start.fileName);
    return true;
}

void finalizeStableKey(SemanticSymbolRecord* record)
{
    if (!record || record->name.isEmpty() || !record->location.isValid())
        return;
    record->stableKey.fileName = record->location.fileName;
    record->stableKey.symbolName = record->name;
    record->stableKey.declarationKind = record->declarationKind;
    record->stableKey.sourcePosition = record->location.position;
    record->stableKey.sourceLength = record->location.length;
}

QString tokenListText(const slang::syntax::TokenList& tokens)
{
    QString text;
    for (const slang::parsing::Token token : tokens)
        text.append(fromUtf8(token.toString()));
    return text.trimmed();
}

SemanticSymbolRecord macroDefinitionRecord(
    const slang::SourceManager* sourceManager,
    const slang::syntax::DefineDirectiveSyntax& syntax)
{
    SemanticSymbolRecord record;
    record.name = fromUtf8(syntax.name.valueText());
    if (record.name.isEmpty()
        || !fillRange(sourceManager,
                      syntax.name.location(),
                      syntax.name.location() + syntax.name.rawText().size(),
                      &record)) {
        return {};
    }

    record.declarationKind = SymbolTaxonomy::DeclarationKind::Macro;
    record.usageRole = SymbolTaxonomy::SymbolUsageRole::Declaration;
    record.visibility = SymbolTaxonomy::SymbolVisibility::Global;
    record.collectorKind = SymbolTaxonomy::CollectorKind::DefDefine;
    record.owner.kind = SymbolTaxonomy::SymbolOwnerScope::Global;
    record.type.rawTypeText = tokenListText(syntax.body);
    if (syntax.formalArguments) {
        QStringList parameters;
        for (const auto* argument : syntax.formalArguments->args) {
            if (argument)
                parameters.append(fromUtf8(argument->toString()).trimmed());
        }
        record.type.modportName = parameters.join(QStringLiteral(", "));
    }
    record.presentation.declarationText =
        fromUtf8(syntax.toString()).trimmed();
    finalizeStableKey(&record);
    return record;
}

SemanticSymbolRecord macroReferenceRecord(
    const slang::SourceManager* sourceManager,
    const slang::syntax::MacroUsageSyntax& syntax)
{
    std::string_view spelling = syntax.directive.valueText();
    size_t nameOffset = 0;
    if (!spelling.empty() && spelling.front() == '`') {
        spelling.remove_prefix(1);
        ++nameOffset;
    }
    if (!spelling.empty() && spelling.front() == '\\') {
        spelling.remove_prefix(1);
        ++nameOffset;
    }
    while (!spelling.empty()
           && (spelling.back() == ' ' || spelling.back() == '\t')) {
        spelling.remove_suffix(1);
    }

    SemanticSymbolRecord record;
    record.name = fromUtf8(spelling);
    if (record.name.isEmpty()
        || !fillRange(sourceManager,
                      syntax.directive.location() + nameOffset,
                      syntax.directive.location() + nameOffset
                          + spelling.size(),
                      &record)) {
        return {};
    }

    record.declarationKind = SymbolTaxonomy::DeclarationKind::Macro;
    record.usageRole = SymbolTaxonomy::SymbolUsageRole::Reference;
    record.visibility = SymbolTaxonomy::SymbolVisibility::Global;
    record.collectorKind = SymbolTaxonomy::CollectorKind::MacroReference;
    record.owner.kind = SymbolTaxonomy::SymbolOwnerScope::Global;
    record.presentation.declarationText =
        fromUtf8(syntax.toString()).trimmed();
    finalizeStableKey(&record);
    return record;
}

template<typename TDirective>
SemanticSymbolRecord inactiveBranchRecord(
    const slang::SourceManager* sourceManager,
    const TDirective& syntax)
{
    slang::parsing::Token first;
    slang::parsing::Token last;
    for (const slang::parsing::Token token : syntax.disabledTokens) {
        if (!first)
            first = token;
        last = token;
    }
    if (!first || !last)
        return {};

    SemanticSymbolRecord record;
    record.name = QStringLiteral("$inactive-preprocessor-branch");
    if (!fillRange(sourceManager,
                   first.location(),
                   last.location() + last.rawText().size(),
                   &record)
        || record.location.length <= 0) {
        return {};
    }
    record.declarationKind = SymbolTaxonomy::DeclarationKind::Unknown;
    record.usageRole = SymbolTaxonomy::SymbolUsageRole::Unknown;
    record.visibility = SymbolTaxonomy::SymbolVisibility::ScopeLocal;
    record.collectorKind =
        SymbolTaxonomy::CollectorKind::InactivePreprocessorBranch;
    record.owner.kind = SymbolTaxonomy::SymbolOwnerScope::Unknown;
    finalizeStableKey(&record);
    return record;
}

void appendDirectiveFacts(const slang::SourceManager* sourceManager,
                          const slang::syntax::SyntaxNode* syntax,
                          QList<SemanticSymbolRecord>* records)
{
    if (!sourceManager || !syntax || !records)
        return;

    using slang::syntax::SyntaxKind;
    SemanticSymbolRecord record;
    switch (syntax->kind) {
    case SyntaxKind::DefineDirective:
        record = macroDefinitionRecord(
            sourceManager,
            syntax->as<slang::syntax::DefineDirectiveSyntax>());
        break;
    case SyntaxKind::MacroUsage:
        record = macroReferenceRecord(
            sourceManager,
            syntax->as<slang::syntax::MacroUsageSyntax>());
        break;
    case SyntaxKind::IfDefDirective:
    case SyntaxKind::IfNDefDirective:
    case SyntaxKind::ElsIfDirective:
        record = inactiveBranchRecord(
            sourceManager,
            syntax->as<slang::syntax::ConditionalBranchDirectiveSyntax>());
        break;
    case SyntaxKind::ElseDirective:
    case SyntaxKind::EndIfDirective:
        record = inactiveBranchRecord(
            sourceManager,
            syntax->as<slang::syntax::UnconditionalBranchDirectiveSyntax>());
        break;
    default:
        break;
    }
    if (record.isValid())
        records->append(std::move(record));
}

} // namespace

QList<SemanticSymbolRecord> collect(const slang::syntax::SyntaxTree& tree)
{
    QList<SemanticSymbolRecord> records;
    QSet<QString> seen;
    const slang::SourceManager* sourceManager = &tree.sourceManager();
    const slang::syntax::SyntaxNode& root = tree.root();
    for (auto iterator = root.tokens_begin();
         iterator != root.tokens_end();
         ++iterator) {
        const slang::parsing::Token token = *iterator;
        for (const slang::parsing::Trivia& trivia : token.trivia()) {
            const slang::syntax::SyntaxNode* directive = trivia.syntax();
            if (!directive)
                continue;
            QList<SemanticSymbolRecord> directiveRecords;
            appendDirectiveFacts(sourceManager,
                                 directive,
                                 &directiveRecords);
            for (SemanticSymbolRecord& record : directiveRecords) {
                const QString key = QStringLiteral("%1|%2|%3|%4")
                                        .arg(record.location.fileName)
                                        .arg(record.location.position)
                                        .arg(record.location.length)
                                        .arg(static_cast<int>(
                                            record.collectorKind));
                if (seen.contains(key))
                    continue;
                seen.insert(key);
                records.append(std::move(record));
            }
        }
    }
    return records;
}

} // namespace slang_preprocessor_facts
