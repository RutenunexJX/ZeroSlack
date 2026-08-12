#ifndef SETTINGSCENTERPANEL_H
#define SETTINGSCENTERPANEL_H

#include "zeroslackexport.h"

#include "settingscenterservice.h"

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QWidget>

class QAbstractItemModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QStandardItemModel;

Q_DECLARE_METATYPE(SettingsCenterScope)

class ZEROSLACK_API SettingsCenterPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsCenterPanel(
        const SettingsCenterService* service,
        const QString& workspaceRoot = QString(),
        QWidget* parent = nullptr);

    SettingsCenterScope scope() const;
    QString workspaceRoot() const;
    QString currentCategoryId() const;
    SettingsCenterSnapshot snapshot() const;
    QVariantMap draftValues(SettingsCenterScope scope) const;
    QVariant effectiveValue(const QString& fieldId) const;
    bool hasOverride(const QString& fieldId,
                     SettingsCenterScope scope) const;
    bool isScopeDirty(SettingsCenterScope scope) const;
    QList<SettingsCenterValidationIssue> currentIssues() const;

    QWidget* fieldEditor(const QString& fieldId) const;
    QAbstractItemModel* stringMapModel(
        const QString& fieldId) const;

    static QString categoryPageObjectName(
        const QString& categoryId);
    static QString fieldEditorObjectName(const QString& fieldId);
    static QString fieldOverrideObjectName(const QString& fieldId);
    static QString fieldStateObjectName(const QString& fieldId);
    static QString stringMapModelObjectName(const QString& fieldId);

public slots:
    void setWorkspaceRoot(const QString& workspaceRoot);
    void reload();
    void setScope(SettingsCenterScope scope);
    void selectCategory(const QString& categoryId);
    void applyCurrentScope();
    void revertCurrentScope();

signals:
    void scopeChanged(SettingsCenterScope scope);
    void statusChanged(const QString& message, bool hasError);
    void issuesReported(const QStringList& messages);
    void settingsApplied(SettingsCenterScope scope);

private:
    struct FieldBinding {
        SettingsCenterFieldDescriptor descriptor;
        QWidget* editor = nullptr;
        QCheckBox* overrideCheck = nullptr;
        QLabel* stateLabel = nullptr;
        QStandardItemModel* stringMapModel = nullptr;
    };

    void buildUi();
    QWidget* createEditor(const SettingsCenterFieldDescriptor& descriptor,
                          QWidget* parent,
                          FieldBinding* binding);
    void connectEditor(const QString& fieldId);
    void updateWorkspaceScopeAvailability();
    void populateFields();
    void populateField(FieldBinding* binding);
    void updateFieldStates();
    void updateFieldState(FieldBinding* binding);
    void updateButtons();
    void updateScopePresentation();
    void updateDraftFromEditor(const QString& fieldId);
    void applyImmediateField(const QString& fieldId);
    void setOverride(const QString& fieldId, bool enabled);

    QVariant editorValue(const FieldBinding& binding) const;
    void setEditorValue(FieldBinding* binding, const QVariant& value);
    QVariant inheritedValue(
        const SettingsCenterFieldDescriptor& descriptor,
        SettingsCenterScope scope) const;
    QVariantMap effectiveDraftValues() const;
    QVariantMap& activeDraftValues();
    const QVariantMap& activeDraftValues() const;
    const QVariantMap& loadedValues(SettingsCenterScope scope) const;

    QList<SettingsCenterValidationIssue>
    localStringMapIssues(SettingsCenterScope scope) const;
    void reportIssues(
        const QList<SettingsCenterValidationIssue>& issues,
        const QString& fallbackMessage,
        bool forceError = false);
    void reportStatus(const QString& message, bool hasError);

    const SettingsCenterService* settingsService = nullptr;
    QString activeWorkspaceRoot;
    SettingsCenterScope activeScope = SettingsCenterScope::Global;
    SettingsCenterSnapshot loadedSnapshot;
    QVariantMap globalDraft;
    QVariantMap workspaceDraft;
    QList<SettingsCenterValidationIssue> displayedIssues;

    QComboBox* scopeCombo = nullptr;
    QListWidget* categoryList = nullptr;
    QStackedWidget* categoryStack = nullptr;
    QLabel* scopeSummaryLabel = nullptr;
    QLabel* statusLabel = nullptr;
    QPushButton* applyButton = nullptr;
    QPushButton* revertButton = nullptr;
    QHash<QString, FieldBinding> fieldBindings;
    bool populating = false;
};

#endif // SETTINGSCENTERPANEL_H
