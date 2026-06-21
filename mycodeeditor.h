#ifndef MYCODEEDITOR_H
#define MYCODEEDITOR_H

#include "semanticindex.h"
#include "semanticdecorationservice.h"

#include <QList>
#include <QPlainTextEdit>
#include <memory>

class QMenu;
class QMouseEvent;
class QRect;
class QContextMenuEvent;
class QKeyEvent;
class QResizeEvent;
class EditorDocumentGeometry;
class EditorSemanticContextService;
class EditorGutter;
class EditorSourceNavigationUi;
struct EditorSemanticContext;
struct EditorSourceNavigationTarget;
struct SourceLineNavigationTarget;
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
    void highlightSearchMatches(const QString& text, bool caseSensitive);
    void clearSearchMatches();
    EditorSemanticContext editorSemanticContextForPosition(
        int cursorPosition = -1,
        bool includeDocumentText = false) const;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    friend class EditorDocumentGeometry;
    friend class EditorGutter;
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
    void navigationBackRequested();
    void navigationForwardRequested();
};

#endif // MYCODEEDITOR_H
