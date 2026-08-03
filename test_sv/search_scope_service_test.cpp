#include "searchservice.h"
#include "editorfileidentity.h"
#include "tsdocument.h"

#include <rtledit/edit_plan.h>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* label, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n",
                condition ? "PASS" : "FAIL",
                label);
}

SemanticSymbolRecord signalRecord(const QString& fileName,
                                  const QString& text,
                                  int position,
                                  const QString& name)
{
    SemanticSymbolRecord record;
    record.name = name;
    record.declarationKind =
        SymbolTaxonomy::DeclarationKind::Signal;
    record.usageRole =
        SymbolTaxonomy::SymbolUsageRole::Declaration;
    record.owner.kind =
        SymbolTaxonomy::SymbolOwnerScope::Module;
    record.owner.name = QStringLiteral("first");
    record.location.fileName = fileName;
    record.location.position = position;
    record.location.length = name.size();
    record.location.startLine =
        text.left(position).count(QLatin1Char('\n')) + 1;
    const int lineStart =
        text.lastIndexOf(QLatin1Char('\n'), position - 1) + 1;
    record.location.startColumn = position - lineStart + 1;
    record.location.endLine = record.location.startLine;
    record.location.endColumn =
        record.location.startColumn + name.size();
    record.stableKey.fileName = fileName;
    record.stableKey.symbolName = name;
    record.stableKey.declarationKind =
        record.declarationKind;
    record.stableKey.ownerScope = record.owner.name;
    record.stableKey.sourcePosition = position;
    record.stableKey.sourceLength = name.size();
    return record;
}

const ScopedSearchResult* resultAt(
    const QList<ScopedSearchResult>& results,
    const QString& fileName,
    int position)
{
    for (const ScopedSearchResult& result : results) {
        if (EditorFileIdentity::same(
                result.fileName, fileName)
            && result.matchStartChar == position) {
            return &result;
        }
    }
    return nullptr;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir workspace;
    expect("temporary workspace is available",
           workspace.isValid());
    if (!workspace.isValid())
        return 1;

    const QString firstFile =
        workspace.filePath(QStringLiteral("first.sv"));
    const QString secondFile =
        workspace.filePath(QStringLiteral("second.sv"));
    const QString firstText =
        QStringLiteral(
            "module first;\n"
            "  logic target;\n"
            "  always_comb begin\n"
            "    target = target + 1;\n"
            "  end\n"
            "endmodule\n"
            "\n"
            "module second;\n"
            "  logic target;\n"
            "endmodule\n");
    const QString secondText =
        QStringLiteral(
            "module other;\n"
            "  // 前缀 target\n"
            "  logic target;\n"
            "endmodule\n");

    TSDocument firstSyntax;
    firstSyntax.setText(firstText);
    TSDocument secondSyntax;
    secondSyntax.setText(secondText);
    const QList<SearchDocumentSnapshot> documents = {
        {firstFile, firstText, &firstSyntax, 17},
        {secondFile, secondText, &secondSyntax, 29},
    };

    const int declaration =
        firstText.indexOf(QStringLiteral("target"));
    SemanticIndex semanticIndex;
    semanticIndex.updateSymbolRecordsForFile(
        firstFile,
        {signalRecord(firstFile,
                      firstText,
                      declaration,
                      QStringLiteral("target"))},
        firstText);
    SearchService service(&semanticIndex);

    ScopedSearchQuery query;
    query.text = QStringLiteral("target");
    query.activeFileName = firstFile;
    query.cursorChar =
        firstText.indexOf(QStringLiteral("target ="));
    query.contextLineCount = 1;

    query.scope = ScopedSearchScope::SyntaxBlock;
    const ScopedSearchResponse block =
        service.search(query, documents);
    expect("Tree-sitter syntax-block scope is resolved",
           block.ready()
               && block.scopeStartChar
                   < query.cursorChar
               && block.scopeEndChar
                   > query.cursorChar);
    expect("syntax-block search excludes module declarations",
           block.results.size() == 2);

    query.scope = ScopedSearchScope::Module;
    const ScopedSearchResponse module =
        service.search(query, documents);
    expect("Tree-sitter module scope includes only current module",
           module.ready()
               && module.results.size() == 3
               && module.scopeEndChar
                   < firstText.indexOf(
                       QStringLiteral("module second")));

    query.scope = ScopedSearchScope::File;
    const ScopedSearchResponse file =
        service.search(query, documents);
    expect("file scope searches the complete active buffer",
           file.ready()
               && file.results.size() == 4
               && file.scopeStartChar == 0
               && file.scopeEndChar == firstText.size());

    query.scope = ScopedSearchScope::Workspace;
    const ScopedSearchResponse workspaceResults =
        service.search(query, documents);
    expect("workspace scope searches all supplied snapshots",
           workspaceResults.ready()
               && workspaceResults.results.size() == 6);

    const ScopedSearchResult* declarationResult =
        resultAt(workspaceResults.results,
                 firstFile,
                 declaration);
    expect("text and semantic declaration results share one identity",
           declarationResult
               && declarationResult->fromTextSearch
               && declarationResult->fromSemanticSearch
               && declarationResult->hasSemanticRecord
               && declarationResult->semanticScore > 0);
    expect("merged identity is a Tree-sitter identifier occurrence",
           declarationResult
               && declarationResult->identity.kind
                   == SearchMatchIdentityKind::
                       SyntaxIdentifier
               && declarationResult->identity.anchorStartChar
                   == declaration
               && declarationResult->identity.anchorEndChar
                   == declaration
                       + QStringLiteral("target").size());

    const QString capturedContext =
        declarationResult
        ? declarationResult->context.text
        : QString();
    const int capturedFirstLine =
        declarationResult
        ? declarationResult->context.firstLine
        : -1;
    firstSyntax.setText(
        QStringLiteral("module changed; endmodule\n"));
    expect("result context is a stable revision snapshot",
           declarationResult
               && declarationResult->context.isValid()
               && declarationResult->context.documentRevision == 17
               && declarationResult->context.text
                   == capturedContext
               && declarationResult->context.firstLine
                   == capturedFirstLine
               && capturedContext.contains(
                   QStringLiteral("logic target")));

    ScopedSearchQuery syntaxMismatch = query;
    syntaxMismatch.scope = ScopedSearchScope::Module;
    const ScopedSearchResponse rejectedMismatch =
        service.search(syntaxMismatch, documents);
    expect("syntax scopes reject a stale Tree-sitter snapshot",
           rejectedMismatch.status
               == ScopedSearchStatus::
                   SyntaxSnapshotRequired);

    // Restore the syntax snapshot for the structured Replace preview.
    firstSyntax.setText(firstText);
    const ScopedSearchResponse replaceResults =
        service.search(query, documents);
    const int commentMatch =
        secondText.indexOf(QStringLiteral("target"));
    const int secondDeclaration =
        secondText.indexOf(
            QStringLiteral("target"), commentMatch + 1);
    const ScopedSearchResult* commentResult =
        resultAt(replaceResults.results,
                 secondFile,
                 commentMatch);
    const ScopedSearchResult* secondDeclarationResult =
        resultAt(replaceResults.results,
                 secondFile,
                 secondDeclaration);
    expect("replace candidates retain comment and declaration identities",
           commentResult && secondDeclarationResult
               && commentResult->identity.toString()
                   != secondDeclarationResult
                          ->identity.toString());

    ReplacePreviewRequest replaceRequest;
    replaceRequest.replacementText =
        QStringLiteral("renamed");
    replaceRequest.filesSelectedByDefault = false;
    replaceRequest.matchesSelectedByDefault = true;
    if (commentResult) {
        replaceRequest.fileSelectionByIdentity.insert(
            commentResult->identity.fileIdentity, true);
    }
    if (secondDeclarationResult) {
        replaceRequest.matchSelectionByIdentity.insert(
            secondDeclarationResult->identity.toString(),
            false);
    }
    const ReplacePreviewPlan replacePlan =
        service.planReplace(
            replaceResults.results,
            documents,
            replaceRequest);
    expect("Replace produces a per-file selectable preview",
           replacePlan.ready()
               && replacePlan.files.size() == 2
               && replacePlan.transactionPlan.edits.size() == 1);
    expect("Replace preview emits a high-risk diff transaction plan",
           replacePlan.transactionPlan.intent.kind
                   == rtledit::SemanticEditKind::ReplaceText
               && replacePlan.transactionPlan.riskLevel
                   == rtledit::RiskLevel::High
               && replacePlan.transactionPlan.previewPolicy
                   == rtledit::PreviewPolicy::Diff
               && rtledit::validateWorkspaceEditPlan(
                      replacePlan.transactionPlan)
                      .valid());
    if (replacePlan.transactionPlan.edits.size() == 1) {
        const rtledit::WorkspaceTextEdit& edit =
            replacePlan.transactionPlan.edits.front();
        const QString commentLinePrefix =
            secondText.mid(
                secondText.lastIndexOf(
                    QLatin1Char('\n'), commentMatch - 1)
                    + 1,
                commentMatch
                    - (secondText.lastIndexOf(
                           QLatin1Char('\n'),
                           commentMatch - 1)
                       + 1));
        expect("transaction columns use UTF-8 byte coordinates",
               edit.range.start.line == 1
                   && edit.range.start.column
                       == static_cast<std::size_t>(
                           commentLinePrefix.toUtf8().size())
                   && edit.expectedText == "target"
                   && edit.newText == "renamed");
    } else {
        expect("transaction columns use UTF-8 byte coordinates",
               false);
    }
    expect("planning does not mutate any document text",
           documents.at(0).text == firstText
               && documents.at(1).text == secondText);

    QList<SearchDocumentSnapshot> changedDocuments =
        documents;
    changedDocuments[1].revision = 30;
    const ReplacePreviewPlan stalePlan =
        service.planReplace(
            replaceResults.results,
            changedDocuments,
            replaceRequest);
    expect("Replace rejects stale selected search results atomically",
           stalePlan.status
                   == ReplacePreviewStatus::
                       StaleSearchResult
               && stalePlan.transactionPlan.edits.empty());

    std::printf("%d checks, %d failures\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
