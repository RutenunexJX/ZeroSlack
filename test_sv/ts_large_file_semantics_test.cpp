#include "documentchange.h"
#include "tsdocument.h"

#include <QCoreApplication>

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

struct Fixture {
    QString text;
    QString moduleName;
    int moduleProbe = -1;
    int editPosition = -1;
    int beginPosition = -1;
    int keywordCursor = -1;
    int unicodePosition = -1;
};

Fixture makeFixture()
{
    Fixture fixture;
    constexpr int moduleCount = 24000;
    constexpr int targetIndex = moduleCount / 2;
    fixture.text.reserve(3000000);
    for (int index = 0; index < moduleCount; ++index) {
        const QString moduleName =
            QStringLiteral("large_scope_%1")
                .arg(index, 5, 10, QLatin1Char('0'));
        if (index != targetIndex) {
            fixture.text +=
                QStringLiteral(
                    "module %1;\n"
                    "  logic enable;\n"
                    "  logic value;\n"
                    "  always_comb begin\n"
                    "    value = enable;\n"
                    "  end\n"
                    "endmodule\n")
                    .arg(moduleName);
            continue;
        }

        fixture.moduleName = moduleName;
        const int moduleStart = fixture.text.size();
        const QString feature =
            QStringLiteral(
                "module %1;\n"
                "  logic enable;\n"
                "  logic selected_signal;\n"
                "  always_comb begin\n"
                "    // UTF-16 boundary \u4e2d\u6587\U0001f600\n"
                "    selected_signal = enable;\n"
                "  end\n"
                "endmodule\n"
                "module %1_keyword_probe;\n"
                "  always_comb beg\n"
                "endmodule\n")
                .arg(moduleName);
        fixture.text += feature;
        fixture.moduleProbe =
            moduleStart
            + feature.indexOf(QStringLiteral("logic enable"));
        fixture.editPosition =
            moduleStart
            + feature.indexOf(
                QStringLiteral("selected_signal = enable;"));
        fixture.beginPosition =
            moduleStart
            + feature.indexOf(QStringLiteral("begin"));
        fixture.unicodePosition =
            moduleStart
            + feature.indexOf(
                QStringLiteral("\U0001f600"));
        const int keywordLine =
            feature.lastIndexOf(QStringLiteral("always_comb beg"));
        fixture.keywordCursor =
            moduleStart
            + feature.indexOf(QStringLiteral("beg"), keywordLine)
            + 3;
    }
    return fixture;
}

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

    const Fixture fixture = makeFixture();
    expect("fixture exceeds 2 MiB",
           fixture.text.size() > 2 * 1024 * 1024
               && fixture.unicodePosition > 0);

    TSDocument document;
    document.setText(fixture.text);
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
