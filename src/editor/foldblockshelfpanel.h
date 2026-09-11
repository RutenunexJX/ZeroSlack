#ifndef FOLDBLOCKSHELFPANEL_H
#define FOLDBLOCKSHELFPANEL_H

#include "zeroslackexport.h"

#include "foldblockshelfmodel.h"

#include <QWidget>

#include <functional>

class QListWidget;
class QLineEdit;
class QPushButton;
class QKeyEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class EditorHoverPopup;

class ZEROSLACK_API FoldBlockShelfPanel : public QWidget
{
    Q_OBJECT

public:
    using ActionRequestHandler =
        std::function<bool(const QString&, QString*)>;

    explicit FoldBlockShelfPanel(QWidget* parent = nullptr);

    void setModel(FoldBlockShelfModel* model);
    FoldBlockShelfModel* model() const;
    void setActionRequestHandler(ActionRequestHandler handler);
    bool handleListShortcut(QKeyEvent* event);
    bool requestDeleteSelectedItem(
        QString* failureReason = nullptr);
    bool deleteSelectedItem(
        QString* failureReason = nullptr);
    void setShelfModeActive(bool active);
    bool shelfModeActive() const;

signals:
    void restoreItemRequested(const QString& id);
    void restoreToActiveEditorRequested(const QString& id);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    FoldBlockShelfModel* shelfModel = nullptr;
    QListWidget* listWidget = nullptr;
    QLineEdit* searchEdit = nullptr;
    QPushButton* restoreButton = nullptr;
    QPushButton* renameButton = nullptr;
    QPushButton* cleanButton = nullptr;
    EditorHoverPopup* previewPeek = nullptr;
    bool activeShelfMode = false;
    ActionRequestHandler actionRequestHandler;

    void refresh();
    void updateModeStyle();
    void showPreview(const FoldShelfItem& item);
    void handleRestoreSelectedItem();
    void handleRenameSelectedItem();
    void handleCleanItems();
    void updateActionState();
    bool hasCleanableItems() const;
    QString selectedItemId() const;
};

#endif // FOLDBLOCKSHELFPANEL_H
