#ifndef EDITORHOVERPOPUP_H
#define EDITORHOVERPOPUP_H

#include "codepreviewservice.h"
#include "symbolhoverreports.h"

#include <QFrame>
#include <QFont>
#include <QPoint>
#include <functional>

class QLabel;
class QMouseEvent;
class QVBoxLayout;

class EditorHoverPopup : public QFrame
{
    Q_OBJECT
public:
    using NavigationHandler =
        std::function<void(const QString&, int, int)>;

    explicit EditorHoverPopup(QWidget* parent = nullptr);

    void setNavigationHandler(NavigationHandler handler);
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
    void closePopup();
    bool hasNavigableTarget() const;

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QVBoxLayout* layout = nullptr;
    QString targetFile;
    int targetLine = -1;
    int targetColumn = -1;
    NavigationHandler navigationHandler;

    void resetContent();
    QLabel* addLabel(const QString& text,
                     const QString& style = QString(),
                     const QFont& font = QFont());
    void moveNear(const QPoint& globalPosition);
};

#endif // EDITORHOVERPOPUP_H
