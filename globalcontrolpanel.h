#ifndef GLOBALCONTROLPANEL_H
#define GLOBALCONTROLPANEL_H

#include "globalcontrolservice.h"

#include <QFrame>
#include <QPoint>
#include <functional>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTabBar;

class GlobalControlPanel : public QFrame
{
public:
    explicit GlobalControlPanel(QWidget* parent = nullptr);

    void setItems(const QList<GlobalControlItem>& items);
    void setCategory(GlobalControlCategory category);
    GlobalControlCategory category() const;
    QString queryText() const;
    void showCentered(QWidget* anchor);
    void showAt(QWidget* anchor, const QPoint& globalAnchor);
    void focusSearch();
    void setQueryChangedHandler(std::function<void(const QString&)> handler);
    void setCategoryChangedHandler(
        std::function<void(GlobalControlCategory)> handler);
    void setItemActivatedHandler(std::function<void(const GlobalControlItem&)> handler);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QLineEdit* searchEdit = nullptr;
    QTabBar* categoryTabs = nullptr;
    QListWidget* resultList = nullptr;
    QList<GlobalControlItem> currentItems;
    std::function<void(const QString&)> queryChangedHandler;
    std::function<void(GlobalControlCategory)> categoryChangedHandler;
    std::function<void(const GlobalControlItem&)> itemActivatedHandler;

    void activateCurrentItem();
    void moveSelection(int delta);
    void moveCategory(int delta);
    bool handleKey(QKeyEvent* event);
    void updatePlaceholder();
};

#endif // GLOBALCONTROLPANEL_H
