#ifndef MYCODEEDITOR_H
#define MYCODEEDITOR_H

#include <QPlainTextEdit>
#include <memory>

class LineNumberWidget;
class AnalysisCoordinator;
class DocumentModel;
class EditorCoordinator;
class NavigationCommandCoordinator;
class QMenu;
class QMouseEvent;
class QRect;
class QContextMenuEvent;
class QKeyEvent;
class QResizeEvent;
class ScopeBandWidget;
class EditorSemanticContextService;
struct EditorSemanticContext;
struct EditorSourceNavigationTarget;
struct SourceLineNavigationTarget;
enum class SourceSymbolAction;
struct MyCodeEditorState;

class MyCodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit MyCodeEditor(QWidget *parent = nullptr);
    ~MyCodeEditor();

private:
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    friend class AnalysisCoordinator;
    friend class DocumentModel;
    friend class EditorCoordinator;
    friend class LineNumberWidget;
    friend class NavigationCommandCoordinator;
    friend class ScopeBandWidget;
    friend struct MyCodeEditorState;

    void setFileName(QString fileName);
    QString getFileName() const;
    QString currentModuleName() const;
    void refreshScopeAndCurrentLineHighlight();
    void setAlternateModeEnabled(bool enabled);
    void setSemanticContextService(EditorSemanticContextService* service);
    void applyLineNavigationTarget(const SourceLineNavigationTarget& target);
    void moveMouseToCursor();
    qreal getBlockTopY(int blockNumber) const;
    qreal getBlockHeight(int blockNumber) const;
    qreal getDocumentHeightPx() const;
    EditorSemanticContext editorSemanticContextForPosition(
        int cursorPosition = -1,
        bool includeDocumentText = false) const;
    std::unique_ptr<MyCodeEditorState> state;

    void executeAlternateModeCommand(const QString &command);

signals:
    void fileNameChanged(const QString& fileName);
    void alternateCommandRequested(const QString& command);
    void sourceNavigationRequested(const EditorSourceNavigationTarget& target,
                                   const EditorSemanticContext& context);
    void sourceSymbolActionRequested(SourceSymbolAction action,
                                     const EditorSemanticContext& context);
    void sourceSymbolContextMenuRequested(QMenu* menu,
                                          const EditorSemanticContext& context);
};

#endif // MYCODEEDITOR_H
