#ifndef GLOBALCONTROLPANEL_H
#define GLOBALCONTROLPANEL_H
#include <QPointer>

#include "globalcontrolservice.h"

#include <QFrame>
#include <QPoint>
#include <functional>

class QLineEdit;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QTabBar;

class GlobalControlPanel : public QFrame
{
public:
    explicit GlobalControlPanel(QWidget* parent = nullptr);

    void setItems(const QList<GlobalControlItem>& items, const QString& notice = {});
    void setSimulationOnly(bool enabled);
    void setCategory(GlobalControlCategory category);
    GlobalControlCategory category() const;
    QString queryText() const;
    void resetQuery(const QString& text);
    void showCentered(QWidget* anchor);
    void showAt(QWidget* anchor, const QPoint& globalAnchor);
    void focusSearch(bool selectAll = true);
    void setCloseOnActivation(bool value) { closeOnActivation = value; }
    void setQueryChangedHandler(std::function<void(const QString&)> handler);
    void setCategoryChangedHandler(
        std::function<void(GlobalControlCategory)> handler);
    void setItemActivatedHandler(std::function<void(const GlobalControlItem&)> handler);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QPointer<QWidget> previousFocus;
    QPointer<QWidget> positioningWidget;
    QPoint popupAnchor;
    bool opensAbove = false;
    bool compactLayout = false;
    bool closeOnActivation = true;
    QLineEdit* searchEdit = nullptr;
    QTabBar* categoryTabs = nullptr;
    QListWidget* resultList = nullptr;
    QLabel* availabilityNotice = nullptr;
    QList<GlobalControlItem> currentItems;
    std::function<void(const QString&)> queryChangedHandler;
    std::function<void(GlobalControlCategory)> categoryChangedHandler;
    std::function<void(const GlobalControlItem&)> itemActivatedHandler;

    void activateCurrentItem();
    void moveSelection(int delta);
    void moveCategory(int delta);
    bool handleKey(QKeyEvent* event);
    void updatePlaceholder();
    void updatePopupGeometry(bool chooseDirection = false);
};

#endif // GLOBALCONTROLPANEL_H
