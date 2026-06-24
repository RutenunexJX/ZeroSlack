#ifndef MYCODEEDITOR_H
#define MYCODEEDITOR_H

#include "semanticindex.h"
#include "semanticdecorationservice.h"
#include "ghostannotationservice.h"
#include "foldblockshelfmodel.h"

#include <QList>
#include <QPlainTextEdit>
#include <memory>

class QMenu;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMouseEvent;
class QPaintEvent;
class QRect;
class QContextMenuEvent;
class QKeyEvent;
class QResizeEvent;
class EditorDocumentGeometry;
class EditorSemanticContextService;
class EditorGutter;
class EditorFoldingController;
class EditorCompletionWorkflow;
class EditorSourceNavigationUi;
struct EditorAppearanceOptions;
struct EditorSemanticContext;
struct EditorSourceNavigationTarget;
struct SourceLineNavigationTarget;
enum class FormatterProfile;
enum class SourceSymbolAction;
struct MyCodeEditorState;

struct EditorBlockGeometry {
    qreal top = 0;
    qreal height = 0;
};

class MyCodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit MyCodeEditor(QWidget *parent = nullptr);
    ~MyCodeEditor();

    void applyLineNavigationTarget(const SourceLineNavigationTarget& target);
    EditorBlockGeometry blockGeometry(int blockNumber) const;
    qreal documentHeightPx() const;
    void refreshScopeAndCurrentLineHighlight();
    void setAlternateModeEnabled(bool enabled);
    void setSemanticContextService(EditorSemanticContextService* service);
    void setDocumentFileName(QString fileName);
    QString documentFileName() const;
    QString currentModuleName() const;
    void executeAlternateModeCommand(const QString& command);
    void setDiagnosticHighlights(
        const QList<SemanticDiagnostic>& diagnostics);
    void setSemanticDecorations(
        const QList<SemanticDecoration>& decorations);
    void setGhostAnnotations(
        const QList<GhostAnnotation>& annotations);
    void setFormatterProfile(FormatterProfile profile);
    FormatterProfile formatterProfile() const;
    void setFormatOnSaveEnabled(bool enabled);
    bool formatOnSaveEnabled() const;
    bool formatDocumentForSave();
    void formatDocument();
    void formatSelection();
    void highlightSearchMatches(const QString& text, bool caseSensitive);
    void clearSearchMatches();
    void applyAppearanceSettings(const EditorAppearanceOptions& options);
    void startFoldRegionMarkMode();
    void cancelFoldRegionMarkMode();
    bool foldRegionMarkModeActive() const;
    void startFoldShelfMode();
    void cancelFoldShelfMode();
    bool foldShelfModeActive() const;
    bool insertCustomFoldMarkersForTest(int startLine,
                                        int endLine,
                                        const QString& alias = QString());
    FoldShelfItem foldShelfItemAtLineForTest(
        int line,
        FoldShelfOriginKind origin = FoldShelfOriginKind::Copied) const;
    bool deleteCustomFoldAtLineForTest(int line);
    bool insertFoldShelfItemAtLineForTest(const FoldShelfItem& item, int line);
    EditorSemanticContext editorSemanticContextForPosition(
        int cursorPosition = -1,
        bool includeDocumentText = false) const;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    friend class EditorDocumentGeometry;
    friend class EditorCompletionWorkflow;
    friend class EditorGutter;
    friend class EditorFoldingController;
    friend class EditorSourceNavigationUi;
    friend struct MyCodeEditorState;

    std::unique_ptr<MyCodeEditorState> state;

signals:
    void fileNameChanged(const QString& fileName);
    void alternateCommandRequested(const QString& command);
    void sourceNavigationRequested(const EditorSourceNavigationTarget& target,
                                   const EditorSemanticContext& context);
    void sourceSymbolActionRequested(SourceSymbolAction action,
                                     const EditorSemanticContext& context);
    void sourceSymbolContextMenuRequested(QMenu* menu,
                                          const EditorSemanticContext& context);
    void definitionPreviewNavigationRequested(const QString& fileName,
                                              int line,
                                              int column);
    void navigationBackRequested();
    void navigationForwardRequested();
    void editorStatusMessageRequested(const QString& message);
    void formatterProfileChanged(FormatterProfile profile);
    void formatOnSaveChanged(bool enabled);
    void foldShelfRequested();
    void foldShelfItemConsumed(const QString& id);
};

#endif // MYCODEEDITOR_H
