#pragma once
#include "simulationcommandplanner.h"
#include "uidialogs.h"
#include "usertemplateservice.h"
#include "zeroslackexport.h"
#include <functional>
class QListWidget;
class QComboBox;
class QPlainTextEdit;
class QTableWidget;
class QCheckBox;
class QFileSystemWatcher;

class ZEROSLACK_API TemplateManagerDialog final : public UiDialog
{
    Q_OBJECT
  public:
    TemplateManagerDialog(UserTemplateService *service, const SimulationCommandRequest &request,
                          const QString &workspace, QWidget *parent = nullptr);
    void refreshAssets()
    {
        refreshAssetState();
    }
    void setPackageInputs(const QStringList &files);
    std::function<void()> openAssets;
    std::function<void(const QString &, const QString &, const QStringList &)> publishAssets;

  private:
    void reload();
    void load(int row);
    void save();
    void remove();
    void inspect(const std::function<void(const QList<SemanticSymbolRecord> &)> &done);
    void bindCallable();
    void createImplementation();
    void editRoles();
    void configureInput(int row);
    void addInput(const QString &name, const QVariantMap &input = {});
    QVariantMap inputs() const;
    void status(const QString &text);
    void refreshAssetState();
    UserTemplateService *service;
    SimulationCommandRequest request;
    QString workspace;
    QList<UserTemplateRecord> records;
    UserTemplateRecord selected;
    quint64 revision = 0;
    quint64 operation = 0;
    QFileSystemWatcher *assetWatcher;
    QListWidget *list;
    QLineEdit *name, *description, *symbol, *aliases;
    QComboBox *scope, *kind, *level;
    QPlainTextEdit *body, *packages;
    QTableWidget *inputTable, *fieldTable;
    QLineEdit *tag, *eventMessage, *condition;
    QCheckBox *messageInput;
    QLabel *notice, *assetNotice;
};
