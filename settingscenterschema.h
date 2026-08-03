#ifndef SETTINGSCENTERSCHEMA_H
#define SETTINGSCENTERSCHEMA_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

enum class SettingsCenterScope {
    Global,
    Workspace,
};

enum class SettingsCenterCategory {
    Font,
    Formatter,
    Shortcut,
    Annotation,
    Analysis,
    Layout,
};

enum class SettingsCenterValueKind {
    Boolean,
    Integer,
    Real,
    String,
    StringMap,
};

enum class SettingsCenterIssueKind {
    UnknownField,
    InvalidValue,
    NormalizedValue,
    UnsupportedDocument,
    StorageError,
    Conflict,
};

struct SettingsCenterFieldDescriptor {
    QString id;
    QString storageKey;
    SettingsCenterCategory category = SettingsCenterCategory::Font;
    QString title;
    QString description;
    SettingsCenterValueKind valueKind = SettingsCenterValueKind::String;
    QVariant defaultValue;
    QVariant minimumValue;
    QVariant maximumValue;
    QStringList choices;
    bool globalAllowed = true;
    bool workspaceAllowed = true;
};

struct SettingsCenterCategoryDescriptor {
    SettingsCenterCategory category = SettingsCenterCategory::Font;
    QString id;
    QString title;
    QString description;
    QList<SettingsCenterFieldDescriptor> fields;
};

struct SettingsCenterValidationIssue {
    SettingsCenterScope scope = SettingsCenterScope::Global;
    SettingsCenterIssueKind kind = SettingsCenterIssueKind::InvalidValue;
    QString fieldId;
    QString message;
};

struct SettingsCenterLayerValidation {
    QVariantMap values;
    QList<SettingsCenterValidationIssue> issues;
};

class SettingsCenterSchema
{
public:
    static const QList<SettingsCenterCategoryDescriptor>& categories();
    static QList<SettingsCenterFieldDescriptor> fields();
    static const SettingsCenterFieldDescriptor* field(const QString& id);
    static QVariantMap defaultValues();

    static SettingsCenterLayerValidation validateLayer(
        const QVariantMap& values,
        SettingsCenterScope scope);
    static QVariantMap merge(const QVariantMap& globalValues,
                             const QVariantMap& workspaceValues);

    static QString categoryId(SettingsCenterCategory category);
};

#endif // SETTINGSCENTERSCHEMA_H
