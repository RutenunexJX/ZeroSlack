#ifndef FOLDBLOCKSHELFPANEL_H
#define FOLDBLOCKSHELFPANEL_H

#include "foldblockshelfmodel.h"

#include <QWidget>

class QListWidget;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

class FoldBlockShelfPanel : public QWidget
{
    Q_OBJECT

public:
    explicit FoldBlockShelfPanel(QWidget* parent = nullptr);

    void setModel(FoldBlockShelfModel* model);
    FoldBlockShelfModel* model() const;
    void requestDeleteSelectedItem();
    void setShelfModeActive(bool active);
    bool shelfModeActive() const;

signals:
    void restoreItemRequested(const QString& id);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    FoldBlockShelfModel* shelfModel = nullptr;
    QListWidget* listWidget = nullptr;
    bool activeShelfMode = false;

    void refresh();
    void updateModeStyle();
    void showPreview(const FoldShelfItem& item);
    void handleDeleteSelectedItem();
};

#endif // FOLDBLOCKSHELFPANEL_H
