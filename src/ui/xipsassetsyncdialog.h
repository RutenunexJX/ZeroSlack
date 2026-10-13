#pragma once
#include "simulationcommandplanner.h"
#include "uidialogs.h"
#include "zeroslackexport.h"
#include <functional>

class QComboBox;
class QPlainTextEdit;
class QPushButton;
class ZEROSLACK_API XipsAssetSyncDialog final : public UiDialog
{
    Q_OBJECT
  public:
    XipsAssetSyncDialog(const QString &workspace, const QStringList &files, const QString &name,
                        const QString &library, QWidget *parent = nullptr);
    ~XipsAssetSyncDialog() override;
    std::function<SimulationCommandRequest()> currentContext;
    bool isBusy() const
    {
        return busy;
    }
    void reject() override;
  signals:
    void referenceChanged();

  private:
    void preview();
    void run(const QString &action);
    void recover();
    void completed(const QVariantMap &result);
    void setBusy(bool value);
    QString currentError() const;
    QString workspace;
    QStringList files;
    QVariantMap operation;
    std::shared_ptr<std::atomic_bool> cancelled;
    bool busy = false, closeRequested = false;
    QComboBox *binding;
    QLineEdit *library, *name;
    QPlainTextEdit *details;
    QLabel *status;
    QPushButton *previewButton, *commitButton, *recoverButton, *cancelButton, *browseButton;
};
