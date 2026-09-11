#ifndef EDITORHOVERPOPUP_H
#define EDITORHOVERPOPUP_H

#include "zeroslackexport.h"

#include "codepreviewservice.h"
#include "peekcontentmodel.h"
#include "symbolhoverreports.h"

#include <QFrame>
#include <QFont>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QStringList>
#include <functional>

class QLabel;
class QLineEdit;
class QMouseEvent;
class QToolButton;
class QScrollArea;
class QVBoxLayout;

class ZEROSLACK_API EditorHoverPopup : public QFrame
{
    Q_OBJECT
public:
    using NavigationHandler =
        std::function<void(const QString&, int, int)>;

    explicit EditorHoverPopup(QWidget* parent);
    ~EditorHoverPopup() override;

    void setNavigationHandler(NavigationHandler handler);
    void setTransientPreview(bool transient);
    void showContent(const PeekContentModel& content,
                     const QRect& globalAnchorRect,
                     const QFont& editorFont);
    void showHover(const SymbolHoverReport& report,
                   const QPoint& globalPosition,
                   const QFont& editorFont);
    void showPreview(const DefinitionPreviewReport& report,
                     const QPoint& globalPosition,
                     const QFont& editorFont);
    void showCodePreview(const CodePreviewReport& report,
                         const QPoint& globalPosition,
                         const QFont& editorFont);
    void showNumericLiteral(const QString& displayText,
                            const QPoint& globalPosition,
                            const QFont& editorFont);
    void showDiagnosticDetail(const QString& title,
                              const QString& message,
                              const QStringList& details,
                              const QRect& globalAnchorRect,
                              const QFont& editorFont);
    void showDeclarationPreview(const QString& title,
                                const QString& declaration,
                                const PeekNavigationTarget& target,
                                const QRect& globalAnchorRect,
                                const QFont& editorFont);
    // Legacy preview coordinators position this widget in screen coordinates.
    // Keep that contract while the container itself remains a child widget.
    void move(const QPoint& globalPosition);
    void move(int globalX, int globalY);
    void closePopup();
    bool hasNavigableTarget() const;
    QLineEdit* editableLineEdit() const;
    void setEditableMessage(const QString& message,
                            bool error = false);
    const PeekContentModel& contentModel() const;

signals:
    void closed();
    void actionTriggered(const QString& actionId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QVBoxLayout* layout = nullptr;
    QLabel* titleLabel = nullptr;
    QWidget* headerHost = nullptr;
    QLabel* symbolIcon = nullptr;
    QLabel* categoryLabel = nullptr;
    QPointer<QScrollArea> symbolScroll;
    QPointer<QWidget> symbolBody;
    QPointer<QWidget> symbolFooter;
    QRect anchorRect;
    QFont contentFont;
    QString defaultStyleSheet;
    QToolButton* closeButton = nullptr;
    QPointer<QLineEdit> editControl;
    QPointer<QLabel> editMessageControl;
    QPointer<QWidget> focusReturnWidget;
    QString targetFile;
    int targetLine = -1;
    int targetColumn = -1;
    bool transientPreview = false;
    NavigationHandler navigationHandler;
    PeekContentModel currentContent;

    void resetContent();
    void applyAppearance();
    void buildSymbolCard();
    void resizeSymbolCard();
    QLabel* addLabel(const QString& text,
                     PeekContentRowRole role,
                     bool wordWrap);
    void moveNear(const QRect& globalAnchorRect);
};

QString execPeekActionPrompt(
    QWidget* host,
    const PeekContentModel& content,
    const QRect& globalAnchorRect,
    const QFont& font);

#endif // EDITORHOVERPOPUP_H
