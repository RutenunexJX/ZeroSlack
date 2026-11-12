#pragma once
#include "uidialogs.h"
#include "pinloomhostclient.h"

class PinloomContextView;
class PinloomPreviewImageLoader;
class QTabWidget;
class QLabel;

class ZEROSLACK_API PinloomReferencePicker final : public UiDialog {
    Q_OBJECT
public:
    explicit PinloomReferencePicker(PinloomHostClient* client, const QString& initialUri, QWidget* parent);
    ~PinloomReferencePicker() override;
    QString selectedUri() const { return selected; }
    QString selectedCaption() const { return caption; }
private:
    QPointer<PinloomHostClient> client;
    PinloomContextView* browser;
    PinloomPreviewImageLoader* loader;
    QTabWidget* tabs;
    QLineEdit* link;
    QLabel* capability;
    QPushButton* insert;
    QString selected, caption, linkStatus;
    bool linkReady = false;
    quint64 generation = 0;
    void updateLink();
    void updateSelection();
};
