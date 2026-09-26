#pragma once
#include "zeroslackexport.h"
#include <QWidget>
#include <QIcon>
#include <QList>

class QAction;
class QCheckBox;
class QGridLayout;
class QLabel;
class QScrollArea;
class QToolButton;

class ZEROSLACK_API ContextToolbox final : public QWidget {
    Q_OBJECT
public:
    explicit ContextToolbox(QWidget* parent = nullptr);
    void addEntry(const QString& id, const QString& title, const QIcon& icon, bool pinned);
    void removeEntry(const QString& id);
    void setPinned(const QString& id, bool pinned);
    void setEntryIcon(const QString& id, const QIcon& icon);
    void setUtilityAction(QAction* action);
    void setStatus(const QString& message);
signals:
    void toolRequested(const QString& id);
    void pinChanged(const QString& id, bool pinned);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    struct Card { QString id; QWidget* widget; QToolButton* run; QCheckBox* pin; };
    QList<Card> cards;
    QScrollArea* scroll = nullptr;
    QGridLayout* grid = nullptr;
    QLabel* status = nullptr;
    QToolButton* utility = nullptr;
    int columns = 0;
    void reflow();
};
