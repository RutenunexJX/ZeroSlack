#ifndef EDITORSELECTION_H
#define EDITORSELECTION_H

#include "semanticindex.h"
#include "semanticdecorationservice.h"

#include <QList>
#include <QPair>
#include <functional>

class MyCodeEditor;
class QPlainTextEdit;
class QTimer;
struct EditorSourceNavigationTarget;

class EditorSelection
{
public:
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
    void highlightSearchMatches(MyCodeEditor* editor,
                                const QString& text,
                                bool caseSensitive);
    void clearSearchMatches(QPlainTextEdit* editor);
    void highlightTemplateSlots(MyCodeEditor* editor,
                                const QList<QPair<int, int>>& ranges,
                                int activeIndex,
                                bool pulseOn = true);
    void clearTemplateSlots(QPlainTextEdit* editor);
    void flashLine(MyCodeEditor* editor);
    void flashLine(MyCodeEditor* editor, int lineNumber);

private:
    void removeByProperty(QPlainTextEdit* editor, int property, int value);
};

class EditorHighlightRefresh
{
public:
    void attachToEditor(MyCodeEditor* editor, const std::function<void()>& refresh);
    void schedule() const;

private:
    QTimer* timer = nullptr;
};

#endif // EDITORSELECTION_H
