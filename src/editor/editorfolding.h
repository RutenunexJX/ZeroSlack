#ifndef EDITORFOLDING_H
#define EDITORFOLDING_H

#include "documentchange.h"
#include "editorfoldviewstate.h"
#include "tsdocument.h"

#include <QList>
#include <QSet>
#include <QString>

class MyCodeEditor;
class QPainter;
class QRect;

class EditorFoldingController
{
public:
    void refresh(MyCodeEditor* editor, const TSDocument* document);
    bool applyDocumentChange(MyCodeEditor* editor,
                             const TSDocument* document,
                             const DocumentChange& change,
                             const QList<TSChangedRange>& changedRanges);
    bool toggleFoldAtLine(MyCodeEditor* editor, int line);
    bool hasFoldAtLine(int line) const;
    bool isCollapsedAtLine(int line) const;
    bool isLineVisible(int line) const;
    void revealLine(MyCodeEditor* editor, int line);
    TSFoldRange foldAtLine(int line) const;
    EditorFoldViewState captureViewState(MyCodeEditor* editor) const;
    void restoreViewState(MyCodeEditor* editor,
                          const EditorFoldViewState& state);
    void resetForDocumentChange(MyCodeEditor* editor);
    const QList<QPair<int, int>>& collapsedLineRanges() const;
    bool hasPaintOverlay() const;
    void paintGutter(MyCodeEditor* editor, QPainter& painter, const QRect& rect) const;
    void paintPlaceholders(MyCodeEditor* editor, QPainter& painter) const;

private:
    QList<TSFoldRange> ranges;
    QSet<int> collapsedStartLines;
    QList<QPair<int, int>> collapsedRangesCache;
    void applyVisibilityForLines(MyCodeEditor* editor,
                                 int startLine,
                                 int endLine);
    void markPresentationChanged(MyCodeEditor* editor);
    void rebuildCollapsedRangesCache();
};

#endif // EDITORFOLDING_H
