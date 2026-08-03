#include "settingscenterschema.h"

#include "settingscenterkeys.h"

#include <QHash>
#include <QKeySequence>
#include <QMetaType>
#include <QtGlobal>

#include <cmath>
#include <limits>

namespace {
SettingsCenterFieldDescriptor field(
    const QString& id,
    const QString& storageKey,
    SettingsCenterCategory category,
    const QString& title,
    const QString& description,
    SettingsCenterValueKind valueKind,
    const QVariant& defaultValue,
    const QVariant& minimumValue = {},
    const QVariant& maximumValue = {},
    const QStringList& choices = {})
{
    SettingsCenterFieldDescriptor result;
    result.id = id;
    result.storageKey = storageKey;
    result.category = category;
    result.title = title;
    result.description = description;
    result.valueKind = valueKind;
    result.defaultValue = defaultValue;
    result.minimumValue = minimumValue;
    result.maximumValue = maximumValue;
    result.choices = choices;
    return result;
}

QList<SettingsCenterCategoryDescriptor> makeCategories()
{
    using Category = SettingsCenterCategory;
    using Kind = SettingsCenterValueKind;

    QList<SettingsCenterCategoryDescriptor> result;
    result.append({
        Category::Font,
        QStringLiteral("font"),
        QStringLiteral("Font"),
        QStringLiteral("Editor font and line metrics."),
        {
            field(QStringLiteral("font.family"),
                  QString::fromLatin1(SettingsCenterKeys::FontFamily),
                  Category::Font,
                  QStringLiteral("Font family"),
                  QStringLiteral("Monospaced editor font family."),
                  Kind::String,
                  QStringLiteral("Maple Mono")),
            field(QStringLiteral("font.sizePt"),
                  QString::fromLatin1(SettingsCenterKeys::FontSizePt),
                  Category::Font,
                  QStringLiteral("Font size"),
                  QStringLiteral("Editor font size in points."),
                  Kind::Integer,
                  15,
                  8,
                  32),
            field(QStringLiteral("font.lineHeight"),
                  QString::fromLatin1(
                      SettingsCenterKeys::FontLineHeight),
                  Category::Font,
                  QStringLiteral("Line height"),
                  QStringLiteral("Line-height multiplier."),
                  Kind::Real,
                  1.4,
                  1.0,
                  2.0),
            field(QStringLiteral("font.ligaturesEnabled"),
                  QString::fromLatin1(
                      SettingsCenterKeys::FontLigaturesEnabled),
                  Category::Font,
                  QStringLiteral("Ligatures"),
                  QStringLiteral("Enable supported programming ligatures."),
                  Kind::Boolean,
                  false),
        },
    });
    result.append({
        Category::Formatter,
        QStringLiteral("formatter"),
        QStringLiteral("Formatter"),
        QStringLiteral("SystemVerilog whitespace formatting policy."),
        {
            field(QStringLiteral("formatter.profile"),
                  QString::fromLatin1(
                      SettingsCenterKeys::FormatterProfile),
                  Category::Formatter,
                  QStringLiteral("Profile"),
                  QStringLiteral("Formatter profile."),
                  Kind::String,
                  QStringLiteral("structured"),
                  {},
                  {},
                  {QStringLiteral("structured"),
                   QStringLiteral("indent_only")}),
            field(QStringLiteral("formatter.formatOnSave"),
                  QString::fromLatin1(
                      SettingsCenterKeys::FormatterFormatOnSave),
                  Category::Formatter,
                  QStringLiteral("Format on save"),
                  QStringLiteral("Format a document before saving it."),
                  Kind::Boolean,
                  false),
        },
    });
    result.append({
        Category::Shortcut,
        QStringLiteral("shortcut"),
        QStringLiteral("Shortcuts"),
        QStringLiteral(
            "Overrides keyed by canonical Action Registry identifier."),
        {
            field(QStringLiteral("shortcut.overrides"),
                  QString::fromLatin1(
                      SettingsCenterKeys::ShortcutOverrides),
                  Category::Shortcut,
                  QStringLiteral("Action shortcuts"),
                  QStringLiteral(
                      "Portable shortcut text keyed by Action Registry ID; "
                      "an empty value disables that binding."),
                  Kind::StringMap,
                  QVariantMap()),
        },
    });
    result.append({
        Category::Annotation,
        QStringLiteral("annotation"),
        QStringLiteral("Annotations"),
        QStringLiteral("Unified editor annotation visibility and density."),
        {
            field(QStringLiteral("annotation.enabled"),
                  QString::fromLatin1(
                      SettingsCenterKeys::AnnotationEnabled),
                  Category::Annotation,
                  QStringLiteral("Annotations"),
                  QStringLiteral("Show editor annotations."),
                  Kind::Boolean,
                  true),
            field(QStringLiteral("annotation.maxPerLine"),
                  QString::fromLatin1(
                      SettingsCenterKeys::AnnotationMaxPerLine),
                  Category::Annotation,
                  QStringLiteral("Maximum per line"),
                  QStringLiteral(
                      "Maximum resolved annotations on one visible line."),
                  Kind::Integer,
                  6,
                  1,
                  64),
            field(QStringLiteral("annotation.maxLanes"),
                  QString::fromLatin1(
                      SettingsCenterKeys::AnnotationMaxLanes),
                  Category::Annotation,
                  QStringLiteral("Maximum lanes"),
                  QStringLiteral(
                      "Maximum inline annotation layout lanes."),
                  Kind::Integer,
                  3,
                  1,
                  16),
        },
    });
    result.append({
        Category::Analysis,
        QStringLiteral("analysis"),
        QStringLiteral("Analysis"),
        QStringLiteral(
            "Analysis runtime policy. Project include paths, defines and "
            "top module remain in Project Configuration."),
        {
            field(QStringLiteral("analysis.enabled"),
                  QString::fromLatin1(
                      SettingsCenterKeys::AnalysisEnabled),
                  Category::Analysis,
                  QStringLiteral("Semantic analysis"),
                  QStringLiteral("Enable saved-file semantic analysis."),
                  Kind::Boolean,
                  true),
            field(QStringLiteral("analysis.incremental"),
                  QString::fromLatin1(
                      SettingsCenterKeys::AnalysisIncremental),
                  Category::Analysis,
                  QStringLiteral("Incremental analysis"),
                  QStringLiteral(
                      "Use dependency-aware incremental analysis plans."),
                  Kind::Boolean,
                  true),
            field(QStringLiteral("analysis.maxDiagnostics"),
                  QString::fromLatin1(
                      SettingsCenterKeys::AnalysisMaxDiagnostics),
                  Category::Analysis,
                  QStringLiteral("Diagnostic limit"),
                  QStringLiteral(
                      "Maximum diagnostics retained from one analysis result."),
                  Kind::Integer,
                  2000,
                  1,
                  100000),
        },
    });
    result.append({
        Category::Layout,
        QStringLiteral("layout"),
        QStringLiteral("Layout"),
        QStringLiteral(
            "Workspace layout persistence policy. The current panel geometry "
            "continues to use WorkspaceSessionStateService."),
        {
            field(QStringLiteral("layout.restoreWorkspaceSession"),
                  QString::fromLatin1(
                      SettingsCenterKeys::
                          LayoutRestoreWorkspaceSession),
                  Category::Layout,
                  QStringLiteral("Restore workspace"),
                  QStringLiteral(
                      "Restore the last tabs, views and window state."),
                  Kind::Boolean,
                  true),
            field(QStringLiteral("layout.rememberPanelState"),
                  QString::fromLatin1(
                      SettingsCenterKeys::LayoutRememberPanelState),
                  Category::Layout,
                  QStringLiteral("Remember panels"),
                  QStringLiteral(
                      "Persist panel order, visibility, pins and height."),
                  Kind::Boolean,
                  true),
        },
    });
    return result;
}

bool scopeAllowed(const SettingsCenterFieldDescriptor& descriptor,
                  SettingsCenterScope scope)
{
    return scope == SettingsCenterScope::Global
        ? descriptor.globalAllowed
        : descriptor.workspaceAllowed;
}

void addIssue(QList<SettingsCenterValidationIssue>* issues,
              SettingsCenterScope scope,
              SettingsCenterIssueKind kind,
              const QString& fieldId,
              const QString& message)
{
    if (!issues)
        return;
    issues->append({scope, kind, fieldId, message});
}

QVariant normalizeBoolean(
    const QVariant& value,
    const SettingsCenterFieldDescriptor& descriptor,
    SettingsCenterScope scope,
    QList<SettingsCenterValidationIssue>* issues)
{
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool();

    const QString text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1")) {
        addIssue(issues,
                 scope,
                 SettingsCenterIssueKind::NormalizedValue,
                 descriptor.id,
                 QStringLiteral("Boolean text was normalized."));
        return true;
    }
    if (text == QStringLiteral("false") || text == QStringLiteral("0")) {
        addIssue(issues,
                 scope,
                 SettingsCenterIssueKind::NormalizedValue,
                 descriptor.id,
                 QStringLiteral("Boolean text was normalized."));
        return false;
    }

    addIssue(issues,
             scope,
             SettingsCenterIssueKind::InvalidValue,
             descriptor.id,
             QStringLiteral("Invalid boolean; the default was used."));
    return descriptor.defaultValue;
}

QVariant normalizeInteger(
    const QVariant& value,
    const SettingsCenterFieldDescriptor& descriptor,
    SettingsCenterScope scope,
    QList<SettingsCenterValidationIssue>* issues)
{
    const int typeId = value.metaType().id();
    const bool numericType =
        typeId == QMetaType::Int
        || typeId == QMetaType::UInt
        || typeId == QMetaType::LongLong
        || typeId == QMetaType::ULongLong
        || typeId == QMetaType::Double
        || typeId == QMetaType::Float;
    const bool numericText = typeId == QMetaType::QString;
    bool ok = false;
    const double number = value.toDouble(&ok);
    if ((!numericType && !numericText)
        || !ok || !std::isfinite(number)
        || std::floor(number) != number
        || number < static_cast<double>(
                        std::numeric_limits<int>::min())
        || number > static_cast<double>(
                        std::numeric_limits<int>::max())) {
        addIssue(issues,
                 scope,
                 SettingsCenterIssueKind::InvalidValue,
                 descriptor.id,
                 QStringLiteral("Invalid integer; the default was used."));
        return descriptor.defaultValue;
    }
    if (numericText) {
        addIssue(issues,
                 scope,
                 SettingsCenterIssueKind::NormalizedValue,
                 descriptor.id,
                 QStringLiteral("Integer text was normalized."));
    }

    int normalized = static_cast<int>(number);
    const int minimum = descriptor.minimumValue.isValid()
        ? descriptor.minimumValue.toInt()
        : std::numeric_limits<int>::min();
    const int maximum = descriptor.maximumValue.isValid()
        ? descriptor.maximumValue.toInt()
        : std::numeric_limits<int>::max();
    const int bounded = qBound(minimum, normalized, maximum);
    if (bounded != normalized) {
        addIssue(issues,
                 scope,
                 SettingsCenterIssueKind::NormalizedValue,
                 descriptor.id,
                 QStringLiteral("Integer was clamped to the supported range."));
        normalized = bounded;
    }
    return normalized;
}

QVariant normalizeReal(
    const QVariant& value,
    const SettingsCenterFieldDescriptor& descriptor,
    SettingsCenterScope scope,
    QList<SettingsCenterValidationIssue>* issues)
{
    const int typeId = value.metaType().id();
    const bool numericType =
        typeId == QMetaType::Int
        || typeId == QMetaType::UInt
        || typeId == QMetaType::LongLong
        || typeId == QMetaType::ULongLong
        || typeId == QMetaType::Double
        || typeId == QMetaType::Float;
    const bool numericText = typeId == QMetaType::QString;
    bool ok = false;
    double normalized = value.toDouble(&ok);
    if ((!numericType && !numericText)
        || !ok || !std::isfinite(normalized)) {
        addIssue(issues,
                 scope,
                 SettingsCenterIssueKind::InvalidValue,
                 descriptor.id,
                 QStringLiteral("Invalid number; the default was used."));
        return descriptor.defaultValue;
    }
    if (numericText) {
        addIssue(issues,
                 scope,
                 SettingsCenterIssueKind::NormalizedValue,
                 descriptor.id,
                 QStringLiteral("Number text was normalized."));
    }

    const double minimum = descriptor.minimumValue.isValid()
        ? descriptor.minimumValue.toDouble()
        : -std::numeric_limits<double>::max();
    const double maximum = descriptor.maximumValue.isValid()
        ? descriptor.maximumValue.toDouble()
        : std::numeric_limits<double>::max();
    const double bounded = qBound(minimum, normalized, maximum);
    if (!qFuzzyCompare(1.0 + bounded, 1.0 + normalized)) {
        addIssue(issues,
                 scope,
                 SettingsCenterIssueKind::NormalizedValue,
                 descriptor.id,
                 QStringLiteral("Number was clamped to the supported range."));
        normalized = bounded;
    }
    return normalized;
}

QVariant normalizeString(
    const QVariant& value,
    const SettingsCenterFieldDescriptor& descriptor,
    SettingsCenterScope scope,
    QList<SettingsCenterValidationIssue>* issues)
{
    if (value.metaType().id() != QMetaType::QString) {
        addIssue(issues,
                 scope,
                 SettingsCenterIssueKind::InvalidValue,
                 descriptor.id,
                 QStringLiteral("Invalid text; the default was used."));
        return descriptor.defaultValue;
    }
    QString normalized = value.toString().trimmed();
    if (normalized.isEmpty()) {
        addIssue(issues,
                 scope,
                 SettingsCenterIssueKind::InvalidValue,
                 descriptor.id,
                 QStringLiteral("Empty text is not supported; the default was used."));
        return descriptor.defaultValue;
    }
    if (descriptor.choices.isEmpty())
        return normalized;

    for (const QString& choice : descriptor.choices) {
        if (choice.compare(normalized, Qt::CaseInsensitive) == 0) {
            if (choice != normalized) {
                addIssue(
                    issues,
                    scope,
                    SettingsCenterIssueKind::NormalizedValue,
                    descriptor.id,
                    QStringLiteral("Choice text was normalized."));
            }
            return choice;
        }
    }

    addIssue(issues,
             scope,
             SettingsCenterIssueKind::InvalidValue,
             descriptor.id,
             QStringLiteral("Unsupported choice; the default was used."));
    return descriptor.defaultValue;
}

QVariant normalizeStringMap(
    const QVariant& value,
    const SettingsCenterFieldDescriptor& descriptor,
    SettingsCenterScope scope,
    QList<SettingsCenterValidationIssue>* issues)
{
    if (!value.canConvert<QVariantMap>()) {
        addIssue(issues,
                 scope,
                 SettingsCenterIssueKind::InvalidValue,
                 descriptor.id,
                 QStringLiteral("Invalid map; the default was used."));
        return descriptor.defaultValue;
    }

    const QVariantMap source = value.toMap();
    QVariantMap normalized;
    QHash<QString, QString> actionBySequence;
    for (auto it = source.cbegin(); it != source.cend(); ++it) {
        const QString key = it.key().trimmed();
        if (key.isEmpty()
            || it.value().metaType().id() != QMetaType::QString) {
            addIssue(issues,
                     scope,
                     SettingsCenterIssueKind::InvalidValue,
                     descriptor.id,
                     QStringLiteral("A malformed map entry was ignored."));
            continue;
        }
        const QString text = it.value().toString().trimmed();
        if (text.isEmpty()) {
            normalized.insert(key, QString());
            continue;
        }
        const QKeySequence sequence =
            QKeySequence::fromString(
                text,
                QKeySequence::PortableText);
        const QString portable =
            sequence.toString(QKeySequence::PortableText);
        if (sequence.isEmpty() || portable.isEmpty()) {
            addIssue(issues,
                     scope,
                     SettingsCenterIssueKind::InvalidValue,
                     descriptor.id,
                     QStringLiteral(
                         "An invalid shortcut entry was ignored."));
            continue;
        }
        const QString sequenceKey = portable.toCaseFolded();
        if (actionBySequence.contains(sequenceKey)) {
            addIssue(
                issues,
                scope,
                SettingsCenterIssueKind::InvalidValue,
                descriptor.id,
                QStringLiteral(
                    "Duplicate shortcut '%1' is assigned to '%2' and '%3'.")
                    .arg(portable,
                         actionBySequence.value(sequenceKey),
                         key));
        } else {
            actionBySequence.insert(sequenceKey, key);
        }
        normalized.insert(key, portable);
    }
    return normalized;
}

QVariant normalizeValue(
    const QVariant& value,
    const SettingsCenterFieldDescriptor& descriptor,
    SettingsCenterScope scope,
    QList<SettingsCenterValidationIssue>* issues)
{
    switch (descriptor.valueKind) {
    case SettingsCenterValueKind::Boolean:
        return normalizeBoolean(value, descriptor, scope, issues);
    case SettingsCenterValueKind::Integer:
        return normalizeInteger(value, descriptor, scope, issues);
    case SettingsCenterValueKind::Real:
        return normalizeReal(value, descriptor, scope, issues);
    case SettingsCenterValueKind::String:
        return normalizeString(value, descriptor, scope, issues);
    case SettingsCenterValueKind::StringMap:
        return normalizeStringMap(value, descriptor, scope, issues);
    }
    return descriptor.defaultValue;
}

QVariantMap mergedMap(const QVariantMap& base,
                      const QVariantMap& overrides)
{
    QVariantMap result = base;
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
        result.insert(it.key(), it.value());
    return result;
}
}

const QList<SettingsCenterCategoryDescriptor>&
SettingsCenterSchema::categories()
{
    static const QList<SettingsCenterCategoryDescriptor> descriptors =
        makeCategories();
    return descriptors;
}

QList<SettingsCenterFieldDescriptor> SettingsCenterSchema::fields()
{
    QList<SettingsCenterFieldDescriptor> result;
    for (const SettingsCenterCategoryDescriptor& category : categories())
        result.append(category.fields);
    return result;
}

const SettingsCenterFieldDescriptor* SettingsCenterSchema::field(
    const QString& id)
{
    for (const SettingsCenterCategoryDescriptor& category : categories()) {
        for (const SettingsCenterFieldDescriptor& descriptor :
             category.fields) {
            if (descriptor.id == id)
                return &descriptor;
        }
    }
    return nullptr;
}

QVariantMap SettingsCenterSchema::defaultValues()
{
    QVariantMap result;
    for (const SettingsCenterFieldDescriptor& descriptor : fields())
        result.insert(descriptor.id, descriptor.defaultValue);
    return result;
}

SettingsCenterLayerValidation SettingsCenterSchema::validateLayer(
    const QVariantMap& values,
    SettingsCenterScope scope)
{
    SettingsCenterLayerValidation result;
    for (const SettingsCenterFieldDescriptor& descriptor : fields()) {
        if (!values.contains(descriptor.id))
            continue;
        if (!scopeAllowed(descriptor, scope)) {
            addIssue(&result.issues,
                     scope,
                     SettingsCenterIssueKind::InvalidValue,
                     descriptor.id,
                     QStringLiteral("Field is not allowed in this scope."));
            continue;
        }
        result.values.insert(
            descriptor.id,
            normalizeValue(values.value(descriptor.id),
                           descriptor,
                           scope,
                           &result.issues));
    }

    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (field(it.key()))
            continue;
        addIssue(&result.issues,
                 scope,
                 SettingsCenterIssueKind::UnknownField,
                 it.key(),
                 QStringLiteral(
                     "Unknown field was ignored by this version; existing "
                     "persisted data with that key is not removed."));
    }
    return result;
}

QVariantMap SettingsCenterSchema::merge(
    const QVariantMap& globalValues,
    const QVariantMap& workspaceValues)
{
    QVariantMap result = defaultValues();
    const auto applyLayer =
        [&result](const QVariantMap& layer) {
            for (auto it = layer.cbegin(); it != layer.cend(); ++it) {
                const SettingsCenterFieldDescriptor* descriptor =
                    SettingsCenterSchema::field(it.key());
                if (!descriptor)
                    continue;
                if (descriptor->valueKind
                    == SettingsCenterValueKind::StringMap) {
                    result.insert(
                        it.key(),
                        mergedMap(result.value(it.key()).toMap(),
                                  it.value().toMap()));
                } else {
                    result.insert(it.key(), it.value());
                }
            }
        };
    applyLayer(globalValues);
    applyLayer(workspaceValues);
    return result;
}

QString SettingsCenterSchema::categoryId(SettingsCenterCategory category)
{
    for (const SettingsCenterCategoryDescriptor& descriptor : categories()) {
        if (descriptor.category == category)
            return descriptor.id;
    }
    return QString();
}
