#ifndef GLOBALCONTROLPANEL_H
#define GLOBALCONTROLPANEL_H

#include "globalcontrolservice.h"

#include <QFrame>
#include <functional>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

class GlobalControlPanel : public QFrame
{
public:
    explicit GlobalControlPanel(QWidget* parent = nullptr);

    void setItems(const QList<GlobalControlItem>& items);
    void showCentered(QWidget* anchor);
    void focusSearch();
    void setQueryChangedHandler(std::function<void(const QString&)> handler);
    void setItemActivatedHandler(std::function<void(const GlobalControlItem&)> handler);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QLineEdit* searchEdit = nullptr;
    QListWidget* resultList = nullptr;
    QList<GlobalControlItem> currentItems;
    std::function<void(const QString&)> queryChangedHandler;
    std::function<void(const GlobalControlItem&)> itemActivatedHandler;

    void activateCurrentItem();
    void moveSelection(int delta);
};

#endif // GLOBALCONTROLPANEL_H
