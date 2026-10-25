#pragma once

#include "zeroslackexport.h"
#include <QObject>
#include <QList>
#include <memory>

class QMainWindow;
class QStackedWidget;
class QWidget;
class TabManager;
class PinloomCodeLinkStore;
class PinloomHostClient;

class ZEROSLACK_API LinkedContextController final : public QObject {
    Q_OBJECT
public:
    LinkedContextController(QMainWindow* window, QStackedWidget* stack,
        QWidget* editorPage, TabManager* tabs, PinloomCodeLinkStore* links,
        PinloomHostClient* pinloom, const QList<QWidget*>& chrome, QObject* parent = nullptr);
    ~LinkedContextController() override;
    bool isActive() const;
    bool setActive(bool active);
    void refreshAnchors();
    void reloadBoard();
    void setPinloomHostClient(PinloomHostClient* client);
signals:
    void aboutToEnter();
    void activeChanged(bool active);
    void message(const QString& text);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    struct Private;
    std::unique_ptr<Private> d;
};
