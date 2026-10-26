#pragma once

#include "uidialogs.h"
#include <QMargins>
#include <QRect>

class SettingsCenterPanel;
class QScreen;

namespace SettingsDialogGeometry {
struct Screen {
    QString name;
    QString serial;
    QRect available;
};
ZEROSLACK_API int selectScreen(const QList<Screen>& screens, const QString& name,
                              const QString& serial, int primary);
ZEROSLACK_API QRect constrain(const QRect& client, const QRect& available,
                             const QMargins& frame, const QSize& preferred);
}

class ZEROSLACK_API SettingsDialog final : public UiDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(SettingsCenterPanel* panel, QWidget* parent);
    void showSettings();
    bool prepareForHostClose();
    void completeHostClose(bool accepted);
    void reject() override;
protected:
    bool event(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
private:
    bool confirmDiscard();
    void restorePlacement();
    void constrainPlacement();
    void savePlacement();
    void watchScreen(QScreen* screen);
    SettingsCenterPanel* settingsPanel;
    QPointer<QWidget> lastEditor;
    bool confirming = false;
    bool hostClosePrepared = false;
    bool placementLoaded = false;
};
