#include "settingscenterpanel.h"
#include "testuistyle.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>

#include <iostream>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray workspaceDocument(const QJsonObject& values,
                             int externalGeneration = 0)
{
    QJsonObject root{
        {QStringLiteral("schema"),
         QStringLiteral("ZeroSlack.SettingsCenter")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("values"), values},
    };
    if (externalGeneration > 0) {
        root.insert(QStringLiteral("externalGeneration"),
                    externalGeneration);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool hasIssue(
    const QList<SettingsCenterValidationIssue>& issues,
    SettingsCenterIssueKind kind)
{
    for (const SettingsCenterValidationIssue& issue : issues) {
        if (issue.kind == kind)
            return true;
    }
    return false;
}
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    if (!initializeUiStyleForTest()) return 2;

    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary directory is valid");
    if (!temporary.isValid())
        return 1;

    const QString globalPath =
        QDir(temporary.path()).absoluteFilePath(
            QStringLiteral("global.ini"));
    const QString workspaceRoot =
        QDir(temporary.path()).absoluteFilePath(
            QStringLiteral("workspace"));
    const QString workspacePath =
        QDir(temporary.path()).absoluteFilePath(
            QStringLiteral("workspace-settings.json"));
    check(QDir().mkpath(workspaceRoot),
          "workspace fixture exists");

    {
        QSettings global(globalPath, QSettings::IniFormat);
        global.setValue(
            QStringLiteral("editorAppearance/fontFamily"),
            QStringLiteral("Iosevka"));
        global.setValue(
            QStringLiteral("editorAppearance/fontSizePt"),
            14);
        global.setValue(
            QStringLiteral("settingsCenter/shortcut/overrides"),
            QVariantMap{
                {QStringLiteral("file.save"),
                 QStringLiteral("Ctrl+S")},
            });
        global.sync();
    }
    check(writeBytes(
              workspacePath,
              workspaceDocument(
                  QJsonObject{
                      {QStringLiteral("font.sizePt"), 20},
                  })),
          "workspace fixture is written");

    SettingsCenterService service(globalPath, workspacePath);
    SettingsCenterPanel panel(&service, workspaceRoot);
    panel.resize(900, 680);
    panel.show();
    QApplication::processEvents();

    check(panel.objectName()
              == QStringLiteral("settingsCenterPanel"),
          "panel object name is stable");
    auto* categories =
        panel.findChild<QListWidget*>(
            QStringLiteral("settingsCenterCategoryList"));
    check(categories
              && categories->count()
                     == SettingsCenterSchema::categories().size(),
          "all schema categories are shown");
    bool everyCategoryHasPage = true;
    bool everyFieldHasStableEditor = true;
    int generatedFieldCount = 0;
    for (const SettingsCenterCategoryDescriptor& category :
         SettingsCenterSchema::categories()) {
        everyCategoryHasPage =
            everyCategoryHasPage
            && panel.findChild<QWidget*>(
                   SettingsCenterPanel::categoryPageObjectName(
                       category.id));
        for (const SettingsCenterFieldDescriptor& field :
             category.fields) {
            QWidget* editor = panel.fieldEditor(field.id);
            everyFieldHasStableEditor =
                everyFieldHasStableEditor
                && editor
                && editor->objectName()
                       == SettingsCenterPanel::
                              fieldEditorObjectName(field.id);
            ++generatedFieldCount;
        }
    }
    check(everyCategoryHasPage,
          "category pages are generated from schema descriptors");
    check(everyFieldHasStableEditor
              && generatedFieldCount
                     == SettingsCenterSchema::fields().size(),
          "every schema field generates one stable editor");

    auto* scopeCombo =
        panel.findChild<QComboBox*>(
            QStringLiteral("settingsCenterScopeCombo"));
    check(scopeCombo && scopeCombo->count() == 2
              && panel.scope() == SettingsCenterScope::Global,
          "global and workspace scopes are explicit");

    auto* themeEditor = qobject_cast<QComboBox*>(
        panel.fieldEditor(
            QStringLiteral("appearance.theme")));
    auto* themeOverride =
        panel.findChild<QCheckBox*>(
            SettingsCenterPanel::fieldOverrideObjectName(
                QStringLiteral("appearance.theme")));
    check(themeEditor && themeOverride
              && themeEditor->currentText()
                     == QStringLiteral("Light")
              && themeEditor->isEnabled()
              && themeOverride->isHidden(),
          "Appearance exposes an always-active Light/Dark selector");

    check(!panel.fieldEditor(QStringLiteral("simulation.verilatorPath")),
          "Settings has no retired simulation page");
    auto* sizeEditor = qobject_cast<QSpinBox*>(
        panel.fieldEditor(QStringLiteral("font.sizePt")));
    auto* sizeOverride =
        panel.findChild<QCheckBox*>(
            SettingsCenterPanel::fieldOverrideObjectName(
                QStringLiteral("font.sizePt")));
    check(sizeEditor && sizeOverride
              && sizeEditor->minimum() == 8
              && sizeEditor->maximum() == 32
              && sizeEditor->value() == 14
              && sizeOverride->isChecked(),
          "integer editor and range come from its descriptor");

    panel.setScope(SettingsCenterScope::Workspace);
    QApplication::processEvents();
    check(panel.scope() == SettingsCenterScope::Workspace
              && sizeEditor->value() == 20
              && sizeOverride->isChecked()
              && panel.hasOverride(
                  QStringLiteral("font.sizePt"),
                  SettingsCenterScope::Workspace),
          "workspace override is loaded independently");
    check(themeEditor && !themeEditor->isEnabled(),
          "workspace scope cannot override the application theme");

    auto* familyEditor = qobject_cast<QLineEdit*>(
        panel.fieldEditor(QStringLiteral("font.family")));
    auto* familyOverride =
        panel.findChild<QCheckBox*>(
            SettingsCenterPanel::fieldOverrideObjectName(
                QStringLiteral("font.family")));
    auto* familyState =
        panel.findChild<QLabel*>(
            SettingsCenterPanel::fieldStateObjectName(
                QStringLiteral("font.family")));
    check(familyEditor && familyOverride && familyState
              && !familyOverride->isChecked()
              && !familyEditor->isEnabled()
              && familyEditor->text()
                     == QStringLiteral("Iosevka")
              && familyState->text().contains(
                  QStringLiteral("Inherited")),
          "inherited effective values and override state are visible");

    int appliedSignals = 0;
    int issueSignals = 0;
    int errorStatuses = 0;
    QObject::connect(
        &panel,
        &SettingsCenterPanel::settingsApplied,
        [&appliedSignals](SettingsCenterScope) {
            ++appliedSignals;
        });
    QObject::connect(
        &panel,
        &SettingsCenterPanel::issuesReported,
        [&issueSignals](const QStringList&) {
            ++issueSignals;
        });
    QObject::connect(
        &panel,
        &SettingsCenterPanel::statusChanged,
        [&errorStatuses](const QString&, bool error) {
            if (error)
                ++errorStatuses;
        });

    familyOverride->click();
    familyEditor->setText(QStringLiteral("JetBrains Mono"));
    familyEditor->setFocus();
    QApplication::processEvents();
    QWidget* focusBeforeWorkspaceApply =
        QApplication::focusWidget();
    panel.applyCurrentScope();
    QApplication::processEvents();
    const SettingsCenterSnapshot afterWorkspaceApply =
        service.load(workspaceRoot);
    check(afterWorkspaceApply.workspaceValues.value(
              QStringLiteral("font.family")).toString()
              == QStringLiteral("JetBrains Mono")
              && appliedSignals == 1,
          "workspace changes apply through the service");
    check(QApplication::focusWidget()
              == focusBeforeWorkspaceApply,
          "successful apply preserves focused editor");

    sizeEditor->setValue(23);
    sizeEditor->setFocus();
    QApplication::processEvents();
    QWidget* focusBeforeRevert = QApplication::focusWidget();
    panel.revertCurrentScope();
    QApplication::processEvents();
    check(sizeEditor->value() == 20
              && !panel.isScopeDirty(
                  SettingsCenterScope::Workspace),
          "revert restores the loaded workspace layer");
    check(QApplication::focusWidget() == focusBeforeRevert,
          "revert preserves focused editor");

    panel.setScope(SettingsCenterScope::Global);
    panel.selectCategory(QStringLiteral("appearance"));
    sizeEditor->setValue(16);
    {
        QSettings externalSettings(globalPath, QSettings::IniFormat);
        externalSettings.setValue(
            QStringLiteral("editorAppearance/fontSizePt"),
            19);
        externalSettings.sync();
    }
    themeEditor->setCurrentText(QStringLiteral("Dark"));
    QApplication::processEvents();
    const SettingsCenterSnapshot afterThemeSwitch =
        service.load(workspaceRoot);
    check(afterThemeSwitch.globalValues.value(
              QStringLiteral("appearance.theme")).toString()
              == QStringLiteral("Dark")
              && afterThemeSwitch.globalValues.value(
                     QStringLiteral("font.sizePt")).toInt()
                     == 19
              && appliedSignals == 2
              && panel.isScopeDirty(
                  SettingsCenterScope::Global),
          "theme retry preserves concurrent fields without applying local drafts");
    panel.revertCurrentScope();
    check(themeEditor->currentText() == QStringLiteral("Dark")
              && sizeEditor->value() == 19,
          "revert retains the applied theme and discards unrelated drafts");

    panel.selectCategory(QStringLiteral("shortcut"));
    QApplication::processEvents();
    QAbstractItemModel* shortcutModel =
        panel.stringMapModel(
            QStringLiteral("shortcut.overrides"));
    check(shortcutModel
              && shortcutModel->columnCount() == 2
              && shortcutModel->rowCount() == 1
              && (shortcutModel->flags(
                      shortcutModel->index(0, 0))
                  & Qt::ItemIsEditable),
          "shortcut map is exposed as a structured editable model");

    check(shortcutModel->insertRow(shortcutModel->rowCount()),
          "shortcut model accepts structured rows");
    const int duplicateRow = shortcutModel->rowCount() - 1;
    check(shortcutModel->setData(
              shortcutModel->index(duplicateRow, 0),
              QStringLiteral("file.saveAs"))
              && shortcutModel->setData(
                  shortcutModel->index(duplicateRow, 1),
                  QStringLiteral("Ctrl+S")),
          "shortcut action and portable sequence are editable");
    QWidget* shortcutEditor =
        panel.fieldEditor(
            QStringLiteral("shortcut.overrides"));
    shortcutEditor->setFocus();
    QApplication::processEvents();
    QWidget* focusBeforeValidation =
        QApplication::focusWidget();
    const QByteArray globalBeforeValidation =
        readBytes(globalPath);
    const int topLevelsBeforeValidation =
        QApplication::topLevelWidgets().size();
    panel.applyCurrentScope();
    QApplication::processEvents();
    check(hasIssue(panel.currentIssues(),
                   SettingsCenterIssueKind::InvalidValue)
              && readBytes(globalPath)
                     == globalBeforeValidation,
          "invalid duplicate shortcuts are reported without saving");
    check(QApplication::focusWidget()
                  == focusBeforeValidation
              && QApplication::topLevelWidgets().size()
                     == topLevelsBeforeValidation,
          "validation remains in-panel and does not leak focus");
    panel.revertCurrentScope();

    panel.selectCategory(QStringLiteral("font"));
    QApplication::processEvents();
    sizeEditor->setValue(17);
    sizeEditor->setFocus();
    QApplication::processEvents();
    QWidget* focusBeforeGlobalApply =
        QApplication::focusWidget();
    panel.applyCurrentScope();
    QApplication::processEvents();
    const SettingsCenterSnapshot afterGlobalApply =
        service.load(workspaceRoot);
    check(afterGlobalApply.globalValues.value(
              QStringLiteral("font.sizePt")).toInt()
              == 17,
          "valid global changes are persisted");
    check(QApplication::focusWidget()
              == focusBeforeGlobalApply,
          "global apply preserves focused editor");

    panel.setScope(SettingsCenterScope::Workspace);
    sizeEditor->setValue(21);
    sizeEditor->setFocus();
    QApplication::processEvents();
    QWidget* focusBeforeConflict =
        QApplication::focusWidget();
    check(writeBytes(
              workspacePath,
              workspaceDocument(
                  QJsonObject{
                      {QStringLiteral("font.sizePt"), 25},
                      {QStringLiteral("font.family"),
                       QStringLiteral("External Font")},
                  },
                  2)),
          "external workspace change is simulated");
    const QByteArray externalBytes = readBytes(workspacePath);
    const int topLevelsBeforeConflict =
        QApplication::topLevelWidgets().size();
    panel.applyCurrentScope();
    QApplication::processEvents();
    check(hasIssue(panel.currentIssues(),
                   SettingsCenterIssueKind::Conflict)
              && panel.isScopeDirty(
                  SettingsCenterScope::Workspace)
              && readBytes(workspacePath) == externalBytes,
          "revision conflict preserves external data and local draft");
    check(QApplication::focusWidget() == focusBeforeConflict
              && QApplication::topLevelWidgets().size()
                     == topLevelsBeforeConflict,
          "conflict reporting remains in-panel without focus leakage");

    auto* statusLabel =
        panel.findChild<QLabel*>(
            QStringLiteral("settingsCenterStatusLabel"));
    check(statusLabel && !statusLabel->text().isEmpty()
              && statusLabel->property(
                     "settingsCenterError").toBool()
              && issueSignals >= 2
              && errorStatuses >= 2,
          "validation and conflict state are visible and signalled");
    check(!(panel.windowFlags()
            & Qt::WindowStaysOnTopHint),
          "settings center never requests an always-on-top window");

    std::cout << "settings_center_panel_test: "
              << (checks - failures) << '/' << checks
              << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
