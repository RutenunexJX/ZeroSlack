#ifndef WORKSPACECONFIGURATIONDIALOG_H
#define WORKSPACECONFIGURATIONDIALOG_H

#include "zeroslackexport.h"

#include "workspaceconfigurationservice.h"

#include "uidialogs.h"

class QLineEdit;
class QListWidget;
class QTableWidget;

class ZEROSLACK_API WorkspaceConfigurationDialog : public UiDialog
{
    Q_OBJECT

public:
    explicit WorkspaceConfigurationDialog(QWidget* parent = nullptr);

    void setConfiguration(const WorkspaceConfiguration& configuration);
    WorkspaceConfiguration configuration() const;

private:
    QString workspaceRoot;
    QListWidget* includeDirsList = nullptr;
    QTableWidget* definesTable = nullptr;
    QListWidget* ignoredDirsList = nullptr;
    QListWidget* fileExtensionsList = nullptr;
    QLineEdit* topModuleEdit = nullptr;
    QTableWidget* virtualSourceGroupsTable = nullptr;

    QListWidget* createStringListEditor(
        QWidget* parent,
        const QString& title,
        const QString& addPrompt,
        bool directoryPicker,
        bool allowMove);
    void addListValue(QListWidget* list,
                      const QString& value,
                      bool directoryPicker,
                      const QString& prompt);
    void editSelectedListValue(QListWidget* list,
                               const QString& prompt);
    void removeSelectedListValue(QListWidget* list);
    void moveSelectedListValue(QListWidget* list, int delta);
    QStringList listValues(QListWidget* list) const;
    void setListValues(QListWidget* list, const QStringList& values);

    void addDefineRow(const QString& key = QString(),
                      const QString& value = QString());
    void removeSelectedDefineRows();
    void addVirtualSourceGroupRow(
        const WorkspaceVirtualSourceGroup& group = {});
    void addFilesToSelectedVirtualSourceGroup();
    void removeSelectedVirtualSourceGroup();
    void moveSelectedVirtualSourceGroup(int delta);
    void refreshVirtualSourceGroupFilesCell(int row);
    QList<WorkspaceVirtualSourceGroup>
    virtualSourceGroupsFromTable() const;
};

#endif // WORKSPACECONFIGURATIONDIALOG_H
