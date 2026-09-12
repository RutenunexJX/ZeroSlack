#include "documentchange.h"
#include "tsdocument.h"
#include "large_file_semantic_fixture.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cstring>
#include <cstdio>
#include <vector>

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

using LargeFileSemanticFixture::Fixture;
using LargeFileSemanticFixture::makeFixture;

bool structurallyEquivalent(TSNode left, TSNode right)
{
    struct NodePair {
        TSNode left;
        TSNode right;
    };
    std::vector<NodePair> pending{
        {left, right}
    };
    while (!pending.empty()) {
        const NodePair pair = pending.back();
        pending.pop_back();
        if (ts_node_symbol(pair.left)
                != ts_node_symbol(pair.right)
            || ts_node_is_named(pair.left)
                != ts_node_is_named(pair.right)
            || ts_node_is_missing(pair.left)
                != ts_node_is_missing(pair.right)
            || ts_node_start_byte(pair.left)
                != ts_node_start_byte(pair.right)
            || ts_node_end_byte(pair.left)
                != ts_node_end_byte(pair.right)
            || ts_node_start_point(pair.left).row
                != ts_node_start_point(pair.right).row
            || ts_node_start_point(pair.left).column
                != ts_node_start_point(pair.right).column
            || ts_node_end_point(pair.left).row
                != ts_node_end_point(pair.right).row
            || ts_node_end_point(pair.left).column
                != ts_node_end_point(pair.right).column) {
            return false;
        }
        const uint32_t childCount =
            ts_node_child_count(pair.left);
        if (childCount != ts_node_child_count(pair.right))
            return false;
        for (uint32_t index = 0;
             index < childCount;
             ++index) {
            pending.push_back(
                {ts_node_child(pair.left, index),
                 ts_node_child(pair.right, index)});
        }
    }
    return true;
}

bool containsNodeTypeAtRange(TSNode root,
                             const char* type,
                             int startChar,
                             int endChar)
{
    std::vector<TSNode> pending{root};
    while (!pending.empty()) {
        const TSNode node = pending.back();
        pending.pop_back();
        if (std::strcmp(ts_node_type(node), type) == 0
            && ts_node_start_byte(node)
                   == static_cast<uint32_t>(startChar) * 2u
            && ts_node_end_byte(node)
                   == static_cast<uint32_t>(endChar) * 2u) {
            return true;
        }
        const uint32_t childCount = ts_node_child_count(node);
        for (uint32_t index = 0; index < childCount; ++index)
            pending.push_back(ts_node_child(node, index));
    }
    return false;
}

DocumentChange inlineChange(const QString& oldText,
                            int position,
                            int removedLength,
                            const QString& insertedText)
{
    DocumentChange change;
    change.position = position;
    change.removedLength = removedLength;
    change.removedText = oldText.mid(position, removedLength);
    change.insertedText = insertedText;
    change.oldLength = oldText.size();
    change.newLength = oldText.size()
        - removedLength + insertedText.size();
    change.startColumn = position;
    return change;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString smallDirectBody =
        QStringLiteral(
            "module direct_body;\n"
            "  always_comb beg\n"
            "endmodule\n");
    TSDocument smallDocument;
    smallDocument.setText(smallDirectBody);
    const int smallKeywordCursor =
        smallDirectBody.indexOf(QStringLiteral("beg")) + 3;
    expect("small direct always body resolves begin completion",
           smallDocument.uniqueKeywordCompletionAt(
                            smallKeywordCursor)
                       .keyword
               == QStringLiteral("begin"));

    const QString stableIdentifierText = QStringLiteral(
        "module stable_identifier;\n"
        "  logic selected_signal;\n"
        "endmodule\n");
    const int stableIdentifierEnd =
        stableIdentifierText.indexOf(QStringLiteral("selected_signal"))
        + QStringLiteral("selected_signal").size();
    TSDocument stableIdentifierDocument;
    stableIdentifierDocument.setText(stableIdentifierText);
    stableIdentifierDocument.resetTextStorageMetricsForTest();
    DocumentChange stableInsert;
    stableInsert.position = stableIdentifierEnd;
    stableInsert.insertedText = QStringLiteral("X");
    stableInsert.oldLength = stableIdentifierText.size();
    stableInsert.newLength = stableInsert.oldLength + 1;
    stableInsert.startLine = 1;
    stableInsert.startColumn = 23;
    stableInsert.oldEndLine = 1;
    stableInsert.newEndLine = 1;
    stableIdentifierDocument.applyEdit(stableInsert);
    QString stableIdentifierEdited = stableIdentifierText;
    stableIdentifierEdited.insert(stableIdentifierEnd,
                                  QLatin1Char('X'));
    TSDocument stableIdentifierFull;
    stableIdentifierFull.setText(stableIdentifierEdited);
    const TSTextStorageMetrics stableInsertMetrics =
        stableIdentifierDocument.textStorageMetricsForTest();
    expect("identifier suffix insertion preserves authoritative tree structure",
           structurallyEquivalent(
               stableIdentifierDocument.rootNode(),
               stableIdentifierFull.rootNode())
               && stableInsertMetrics.editCount == 1
               && stableInsertMetrics.structurePreservingEditCount == 1
               && stableInsertMetrics.syntaxParseCount == 0
               && stableInsertMetrics.inputReadCount == 0);

    stableIdentifierDocument.resetTextStorageMetricsForTest();
    DocumentChange stableDelete;
    stableDelete.position = stableIdentifierEnd;
    stableDelete.removedLength = 1;
    stableDelete.removedText = QStringLiteral("X");
    stableDelete.oldLength = stableIdentifierEdited.size();
    stableDelete.newLength = stableIdentifierText.size();
    stableDelete.startLine = 1;
    stableDelete.startColumn = 23;
    stableDelete.oldEndLine = 1;
    stableDelete.newEndLine = 1;
    stableIdentifierDocument.applyEdit(stableDelete);
    TSDocument stableIdentifierOriginal;
    stableIdentifierOriginal.setText(stableIdentifierText);
    const TSTextStorageMetrics stableDeleteMetrics =
        stableIdentifierDocument.textStorageMetricsForTest();
    expect("identifier suffix deletion preserves authoritative tree structure",
           structurallyEquivalent(
               stableIdentifierDocument.rootNode(),
               stableIdentifierOriginal.rootNode())
               && stableDeleteMetrics.editCount == 1
               && stableDeleteMetrics.structurePreservingEditCount == 1
               && stableDeleteMetrics.syntaxParseCount == 0
               && stableDeleteMetrics.inputReadCount == 0);

    const QString alwaysPrefixText = QStringLiteral(
        "module m; alway begin end endmodule");
    const int alwaysPrefixStart =
        alwaysPrefixText.indexOf(QStringLiteral("alway"));
    const int alwaysPrefixEnd =
        alwaysPrefixStart + QStringLiteral("alway").size();
    TSDocument alwaysBoundaryDocument;
    alwaysBoundaryDocument.setText(alwaysPrefixText);
    expect("always boundary probe starts as a simple identifier",
           containsNodeTypeAtRange(
               alwaysBoundaryDocument.rootNode(),
               "simple_identifier",
               alwaysPrefixStart,
               alwaysPrefixEnd));
    alwaysBoundaryDocument.resetTextStorageMetricsForTest();
    alwaysBoundaryDocument.applyEdit(
        inlineChange(alwaysPrefixText,
                     alwaysPrefixEnd,
                     0,
                     QStringLiteral("s")));
    QString alwaysKeywordText = alwaysPrefixText;
    alwaysKeywordText.insert(alwaysPrefixEnd, QLatin1Char('s'));
    TSDocument alwaysBoundaryFull;
    alwaysBoundaryFull.setText(alwaysKeywordText);
    const TSTextStorageMetrics alwaysEntryMetrics =
        alwaysBoundaryDocument.textStorageMetricsForTest();
    expect("identifier entering always reparses to the authoritative structure",
           structurallyEquivalent(
               alwaysBoundaryDocument.rootNode(),
               alwaysBoundaryFull.rootNode())
               && containsNodeTypeAtRange(
                   alwaysBoundaryDocument.rootNode(),
                   "always_keyword",
                   alwaysPrefixStart,
                   alwaysPrefixEnd + 1)
               && alwaysEntryMetrics.syntaxParseCount == 1
               && alwaysEntryMetrics.structurePreservingEditCount == 0);

    alwaysBoundaryDocument.resetTextStorageMetricsForTest();
    alwaysBoundaryDocument.applyEdit(
        inlineChange(alwaysKeywordText,
                     alwaysPrefixEnd,
                     1,
                     QString()));
    TSDocument alwaysReverseFull;
    alwaysReverseFull.setText(alwaysPrefixText);
    const TSTextStorageMetrics alwaysExitMetrics =
        alwaysBoundaryDocument.textStorageMetricsForTest();
    expect("keyword suffix deletion back to an identifier reparses immediately",
           structurallyEquivalent(
               alwaysBoundaryDocument.rootNode(),
               alwaysReverseFull.rootNode())
               && alwaysExitMetrics.syntaxParseCount == 1
               && alwaysExitMetrics.structurePreservingEditCount == 0);

    const QString logicPrefixText = QStringLiteral(
        "module keyword_probe; logi value; endmodule");
    const int logicPrefixStart =
        logicPrefixText.indexOf(QStringLiteral("logi"));
    const int logicPrefixEnd =
        logicPrefixStart + QStringLiteral("logi").size();
    TSDocument logicBoundaryDocument;
    logicBoundaryDocument.setText(logicPrefixText);
    expect("logic boundary probe starts as a simple identifier",
           containsNodeTypeAtRange(
               logicBoundaryDocument.rootNode(),
               "simple_identifier",
               logicPrefixStart,
               logicPrefixEnd));
    logicBoundaryDocument.resetTextStorageMetricsForTest();
    logicBoundaryDocument.applyEdit(
        inlineChange(logicPrefixText,
                     logicPrefixEnd,
                     0,
                     QStringLiteral("c")));
    QString logicKeywordText = logicPrefixText;
    logicKeywordText.insert(logicPrefixEnd, QLatin1Char('c'));
    TSDocument logicBoundaryFull;
    logicBoundaryFull.setText(logicKeywordText);
    const TSTextStorageMetrics logicEntryMetrics =
        logicBoundaryDocument.textStorageMetricsForTest();
    expect("keyword boundary handling is not specific to always",
           structurallyEquivalent(
               logicBoundaryDocument.rootNode(),
               logicBoundaryFull.rootNode())
               && logicEntryMetrics.syntaxParseCount == 1
               && logicEntryMetrics.structurePreservingEditCount == 0);

    const Fixture fixture = makeFixture();
    expect("fixture exceeds 2 MiB",
           fixture.text.size() > 2 * 1024 * 1024
               && fixture.unicodePosition > 0);

    TSDocument document;
    document.setText(fixture.text);
    const int earlyWhitespaceCursor =
        fixture.text.indexOf(QLatin1Char(' ')) + 1;
    QElapsedTimer earlyLookupTimer;
    earlyLookupTimer.start();
    const TSKeywordCompletionTarget earlyCompletion =
        document.uniqueKeywordCompletionAt(earlyWhitespaceCursor);
    const qint64 earlyLookupNanoseconds =
        earlyLookupTimer.nsecsElapsed();
    std::printf("early non-keyword lookup latency: %.3f ms\n",
                static_cast<double>(earlyLookupNanoseconds)
                    / 1000000.0);
    expect("early non-keyword lookup stays logarithmic in a large tree",
           !earlyCompletion.ok()
               && earlyLookupNanoseconds < 5000000);
    QElapsedTimer earlyPairTimer;
    earlyPairTimer.start();
    const TSKeywordPairTarget earlyPair =
        document.matchingKeywordPairAt(earlyWhitespaceCursor);
    const qint64 earlyPairNanoseconds =
        earlyPairTimer.nsecsElapsed();
    std::printf("early non-pair lookup latency: %.3f ms\n",
                static_cast<double>(earlyPairNanoseconds)
                    / 1000000.0);
    expect("early non-pair lookup bypasses the large syntax tree",
           !earlyPair.ok()
               && earlyPairNanoseconds < 5000000);

    const QString underscoreIdentifier = QStringLiteral(
        "module underscore_probe;\n"
        "  logic __NOT_A_KEYWORD__;\n"
        "endmodule\n");
    TSDocument underscoreDocument;
    underscoreDocument.setText(underscoreIdentifier);
    const int underscoreCursor =
        underscoreIdentifier.indexOf(
            QStringLiteral("__NOT_A_KEYWORD__"))
        + QStringLiteral("__NOT_A_KEYWORD__").size();
    expect("underscore identifier bypasses keyword lookahead",
           !underscoreDocument.uniqueKeywordCompletionAt(
                underscoreCursor).ok());
    const TSStructuralNewlineTarget newline =
        document.structuralNewlineTarget(
            fixture.beginPosition + 5);
    const TSKeywordCompletionTarget completion =
        document.uniqueKeywordCompletionAt(
            fixture.keywordCursor);
    const TSKeywordPairTarget pair =
        document.matchingKeywordPairAt(
            fixture.beginPosition + 1);
    expect("large syntax resolves enclosing module",
           document.enclosingModuleName(fixture.moduleProbe)
               == fixture.moduleName);
    expect("large syntax resolves structural newline",
           newline.ok());
    expect("large syntax resolves unique begin completion",
           completion.ok()
               && completion.keyword == QStringLiteral("begin"));
    expect("large syntax resolves begin/end pair",
           pair.ok()
               && pair.openingKeyword == QStringLiteral("begin")
               && pair.closingKeyword == QStringLiteral("end"));

    const QString removed =
        QStringLiteral("selected_signal = enable;");
    const QString inserted =
        QStringLiteral("if (enable) selected_signal = 1'b1;");
    DocumentChange change;
    change.position = fixture.editPosition;
    change.removedLength = removed.size();
    change.removedText = removed;
    change.insertedText = inserted;
    change.oldLength = fixture.text.size();
    change.newLength =
        change.oldLength + change.characterDelta();
    change.startLine =
        fixture.text.left(change.position).count(QLatin1Char('\n'));
    const int previousNewline =
        fixture.text.lastIndexOf(QLatin1Char('\n'),
                                 change.position - 1);
    change.startColumn =
        change.position - previousNewline - 1;
    change.oldEndLine = change.startLine;
    change.newEndLine = change.startLine;

    document.resetTextStorageMetricsForTest();
    const QList<TSChangedRange> ranges =
        document.applyEdit(change);
    const TSTextStorageMetrics editStorageMetrics =
        document.textStorageMetricsForTest();
    QString editedText = fixture.text;
    editedText.replace(change.position,
                       change.removedLength,
                       change.insertedText);
    TSDocument fullReparse;
    fullReparse.setText(editedText);
    const int identifierPosition =
        fixture.editPosition
        + inserted.indexOf(QStringLiteral("selected_signal"));
    const int shiftedKeywordCursor =
        fixture.keywordCursor + change.characterDelta();
    expect("large incremental parse reports bounded changes",
           !ranges.isEmpty() && ranges.size() <= 8);
    expect("large incremental parse reads piece storage without materializing full text",
           editStorageMetrics.editCount == 1
               && editStorageMetrics.materializationCount == 0
               && editStorageMetrics.inputReadCount > 0
               && editStorageMetrics.movedCharacterCount == 0);
    expect("large incremental parse retains module lookup",
           document.enclosingModuleName(identifierPosition)
               == fixture.moduleName);
    expect("large incremental parse retains identifier lookup",
           document.identifierAt(identifierPosition).text
               == QStringLiteral("selected_signal"));
    expect("large incremental parse retains assignment navigation",
           document.assignmentNavigationTarget(
                       identifierPosition, false)
               .ok());
    expect("large incremental parse retains keyword completion",
           document.uniqueKeywordCompletionAt(
                       shiftedKeywordCursor)
                   .keyword
               == QStringLiteral("begin"));
    expect("large incremental tree equals a full authoritative reparse",
           structurallyEquivalent(document.rootNode(),
                                  fullReparse.rootNode()));

    DocumentChange boundaryRead;
    boundaryRead.position = fixture.unicodePosition + 1;
    boundaryRead.oldLength = change.newLength;
    boundaryRead.newLength = change.newLength;
    boundaryRead.startLine =
        fixture.text.left(boundaryRead.position)
            .count(QLatin1Char('\n'));
    const int boundaryPreviousNewline =
        fixture.text.lastIndexOf(
            QLatin1Char('\n'),
            boundaryRead.position - 1);
    boundaryRead.startColumn =
        boundaryRead.position - boundaryPreviousNewline - 1;
    boundaryRead.oldEndLine = boundaryRead.startLine;
    boundaryRead.newEndLine = boundaryRead.startLine;

    document.resetTextStorageMetricsForTest();
    const QList<TSChangedRange> boundaryRanges =
        document.applyEdit(boundaryRead);
    const TSTextStorageMetrics boundaryStorageMetrics =
        document.textStorageMetricsForTest();
    expect("UTF-16 piece boundary input preserves unchanged syntax",
           boundaryRanges.isEmpty()
               && document.isCommentAt(fixture.unicodePosition)
               && document.enclosingModuleName(
                      fixture.unicodePosition)
                      == fixture.moduleName);
    expect("no-op UTF-16 boundary parse remains equal to full reparse",
           structurallyEquivalent(document.rootNode(),
                                  fullReparse.rootNode()));
    expect("UTF-16 piece storage is read directly without full materialization",
           boundaryStorageMetrics.editCount == 1
               && boundaryStorageMetrics.materializationCount == 0
               && boundaryStorageMetrics.inputReadCount > 0
               && document.text().mid(
                      fixture.unicodePosition, 2)
                      == QStringLiteral("\U0001f600"));

    std::printf("checks=%d failures=%d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
