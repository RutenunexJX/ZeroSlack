#ifndef WORKSPACECONFIGURATIONDIALOG_H
#define WORKSPACECONFIGURATIONDIALOG_H

#include "workspaceconfigurationservice.h"

#include <QDialog>

class QLineEdit;
class QListWidget;
class QTableWidget;

class WorkspaceConfigurationDialog : public QDialog
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
};

#endif // WORKSPACECONFIGURATIONDIALOG_H
