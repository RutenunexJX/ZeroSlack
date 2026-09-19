#ifndef EDITORFOLDING_H
#define EDITORFOLDING_H

#include "documentchange.h"
#include "editorfoldviewstate.h"
#include "editormodecontroller.h"
#include "tsdocument.h"

#include <QList>
#include <QSet>
#include <QString>

class MyCodeEditor;
class QMouseEvent;
class QPainter;
class QRect;
class QScrollBar;

class EditorFoldingController
{
public:
    void bindModeController(EditorModeController* controller,
                            MyCodeEditor* editor);
    void refresh(MyCodeEditor* editor, const TSDocument* document);
    bool applyDocumentChange(MyCodeEditor* editor,
                             const TSDocument* document,
                             const DocumentChange& change,
                             const QList<TSChangedRange>& changedRanges);
    bool toggleFoldAtLine(MyCodeEditor* editor, int line);
    void startFoldRegionMarkMode(MyCodeEditor* editor);
    void cancelFoldRegionMarkMode(MyCodeEditor* editor);
    bool foldRegionMarkModeActive() const;
    bool handleFoldRegionHoverLine(MyCodeEditor* editor, int line);
    bool handleFoldRegionMouseMove(MyCodeEditor* editor, QMouseEvent* event);
    bool handleFoldRegionGutterLine(MyCodeEditor* editor, int line);
    bool insertCustomFoldMarkers(MyCodeEditor* editor,
                                 int startLine,
                                 int endLine,
                                 const QString& alias = QString());
    bool deleteCustomFoldAtLine(MyCodeEditor* editor, int line);
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
    enum class FoldRegionMarkMode {
        Inactive,
        WaitingForStart,
        WaitingForEnd
    };

    QList<TSFoldRange> ranges;
    QList<TSCustomFoldMarker> customMarkers;
    QSet<int> collapsedStartLines;
    QList<QPair<int, int>> collapsedRangesCache;
    FoldRegionMarkMode markMode = FoldRegionMarkMode::Inactive;
    int pendingStartLine = -1;
    int foldRegionHoverLine = -1;
    int defaultAliasCounter = 0;
    EditorModeController* modeController = nullptr;

    void resetFoldRegionMode(MyCodeEditor* editor);
    void applyVisibility(MyCodeEditor* editor);
    void applyVisibilityForLines(MyCodeEditor* editor,
                                 int startLine,
                                 int endLine);
    void markPresentationChanged(MyCodeEditor* editor);
    void rebuildCollapsedRangesCache();
    void updateStatus(MyCodeEditor* editor, const QString& message) const;
    QString defaultAlias();
    TSFoldRange customFoldContainingLine(int line) const;
    QString rangeText(MyCodeEditor* editor, const TSFoldRange& range) const;
    bool deleteRange(MyCodeEditor* editor, const TSFoldRange& range);
    void paintCustomFoldBackgrounds(MyCodeEditor* editor, QPainter& painter) const;
    void paintFoldRegionPreview(MyCodeEditor* editor, QPainter& painter) const;
};

#endif // EDITORFOLDING_H
