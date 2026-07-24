#ifndef EDITORSELECTION_H
#define EDITORSELECTION_H

#include "documentchange.h"
#include "semanticindex.h"
#include "semanticdecorationservice.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class MyCodeEditor;
class QPlainTextEdit;
class QTimer;
struct EditorSourceNavigationTarget;
struct EditorOccurrenceNode;

struct EditorOccurrenceIndexStats {
    qsizetype handleCount = 0;
    qsizetype allocatedNodeCount = 0;
    qsizetype activeNodeCount = 0;
    qsizetype freeNodeCount = 0;
};

enum class OccurrenceIndexUpdate {
    None,
    Full,
    Incremental
};

struct OccurrenceChangeContext {
    int oldStart = 0;
    int oldEnd = 0;
    bool rebuild = false;
};

class EditorSelection
{
public:
    EditorSelection();
    ~EditorSelection();

    void highlightCurrentLine(MyCodeEditor* editor);
    void highlightCommand(MyCodeEditor* editor, int prefixPosition);
    void clearCommand(QPlainTextEdit* editor);
    void highlightHoveredSymbol(
        MyCodeEditor* editor,
        const EditorSourceNavigationTarget& target);
    void clearHoveredSymbol(QPlainTextEdit* editor);
    void highlightDiagnostics(
        MyCodeEditor* editor,
        const QList<SemanticDiagnostic>& diagnostics);
    void highlightSemanticDecorations(
        MyCodeEditor* editor,
        const QList<SemanticDecoration>& decorations);
    void highlightCurrentSymbolReferences(MyCodeEditor* editor);
    OccurrenceChangeContext prepareDocumentChange(
        const DocumentChange& change,
        const QString& oldText) const;
    OccurrenceChangeContext prepareDocumentLineChange(
        const DocumentChange& change,
        int oldLineStart,
        int oldLineEnd) const;
    OccurrenceIndexUpdate applyDocumentChange(
        MyCodeEditor* editor,
        const DocumentChange& change,
        const OccurrenceChangeContext& context,
        const QString& newText);
    OccurrenceIndexUpdate applyDocumentLineChange(
        MyCodeEditor* editor,
        const DocumentChange& change,
        const OccurrenceChangeContext& context,
        int newLineStart,
        const QString& newLineText);
    EditorOccurrenceIndexStats occurrenceIndexStatsForTest() const;
    QList<int> occurrencePositionsForTest(const QString& word) const;
    void highlightSearchMatches(MyCodeEditor* editor,
                                const QString& text,
                                bool caseSensitive);
    void clearSearchMatches(QPlainTextEdit* editor);
    void highlightTemplateSlots(MyCodeEditor* editor,
                                const QList<QPair<int, int>>& ranges,
                                int activeIndex,
                                bool pulseOn = true);
    void clearTemplateSlots(QPlainTextEdit* editor);
    void highlightSignalSelections(
        MyCodeEditor* editor,
        const QList<QPair<int, int>>& ranges);
    void clearSignalSelections(QPlainTextEdit* editor);
    void flashLine(MyCodeEditor* editor);
    void flashLine(MyCodeEditor* editor, int lineNumber);

private:
    void removeByProperty(QPlainTextEdit* editor, int property, int value);
    void rebuildOccurrenceIndex(MyCodeEditor* editor,
                                const QString& text);
    void appendOccurrenceRange(const QString& text,
                               int start,
                               int end,
                               int absoluteOffset = 0);
    void removeOccurrenceRangeAndShift(
        const OccurrenceChangeContext& context,
        int characterDelta);

    QHash<QString, QSet<EditorOccurrenceNode*>> occurrenceIndex;
    std::vector<std::unique_ptr<EditorOccurrenceNode>> occurrenceNodes;
    std::vector<EditorOccurrenceNode*> freeOccurrenceNodes;
    EditorOccurrenceNode* occurrenceRoot = nullptr;
    std::uint32_t occurrencePrioritySeed = 0x9e3779b9u;
    qsizetype activeOccurrenceCount = 0;
    bool occurrenceIndexInitialized = false;
};

class EditorHighlightRefresh
{
public:
    void attachToEditor(MyCodeEditor* editor, const std::function<void()>& refresh);
    void schedule() const;

private:
    std::function<void()> refreshHandler;
};

#endif // EDITORSELECTION_H
