#include "usertemplateservice.h"
#include "ordinarytemplateplan.h"
#include "templatemanagerdialog.h"
#include "mycodeeditor.h"
#include <QtTest>
#include <QtWidgets>
#include <QLockFile>
#include <QSaveFile>

namespace {
QByteArray bytes(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
QJsonObject entry(QString id = "user:timescale", QString token = "/timescale", QString text = "`timescale 1ns / 1ps") {
    return {{"id", id}, {"command", token}, {"label", "Timescale"}, {"body", text}};
}
QByteArray catalog(const QJsonArray& entries = {entry()}) {
    return QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"templates", entries}}).toJson();
}
struct Fixture {
    QTemporaryDir root;
    QString bundled = root.filePath("install/templates/catalog.json");
    QString global = root.filePath("profile/user_templates.json");
    QString workspace = root.filePath("workspace/.zeroslack/user_templates.json");
    QString file = root.filePath("workspace/design.sv");
    UserTemplateService service{global, workspace};
    Fixture() { service.setBundledTemplateFilePath(bundled); }
    bool initialize() { return root.isValid() && write(bundled, catalog()); }
    UserTemplateRecord current() { return service.recordsForFile(file).value(0); }
};
}

class BundledTemplatesTest : public QObject {
    Q_OBJECT
private slots:
    void defaultsUpdatesAndCache() {
        Fixture f; QVERIFY(f.initialize());
        UserTemplateService installed;
        QCOMPARE(installed.bundledTemplateLocation(), QDir(QCoreApplication::applicationDirPath()).filePath("templates/catalog.json"));
        QVERIFY(f.service.reload().valid);
        QCOMPARE(f.current().insertText, QString("`timescale 1ns / 1ps"));
        QCOMPARE(f.current().storageSource, f.bundled);
        QVERIFY(!QFileInfo::exists(f.global));
        const auto reads = f.service.fileReadsForTesting();
        const auto revision = f.service.catalogRevision();
        for (int i = 0; i < 5; ++i) QCOMPARE(f.service.recordsForFile(f.file).size(), 1);
        QCOMPARE(f.service.fileReadsForTesting(), reads); QCOMPARE(f.service.catalogRevision(), revision);
        QVERIFY(write(f.bundled, catalog({entry("user:timescale", "/timescale", "`timescale 10ns / 1ps"), entry("package:other", "/other", "logic other;")})));
        QCOMPARE(f.service.recordsForFile(f.file).size(), 2);
        QVERIFY(f.service.catalogRevision() > revision);
        QCOMPARE(f.current().insertText, QString("`timescale 10ns / 1ps"));
        QVERIFY(write(f.bundled, catalog({entry("package:other", "/other", "logic changed;")})));
        QCOMPARE(f.service.recordsForFile(f.file).size(), 1);
        QCOMPARE(f.current().commandToken, QString("/other"));
        QVERIFY(write(f.bundled, catalog()));
        const auto plan = OrdinaryTemplatePlanner::prepare(f.current(), OrdinaryTemplatePlanner::catalog(&f.service, f.file));
        QVERIFY(plan.error.isEmpty()); QCOMPARE(plan.text, QString("`timescale 1ns / 1ps"));
    }
    void identityCommandAndScopePrecedence() {
        Fixture f; QVERIFY(f.initialize());
        auto local = entry("user:timescale", "/my_timescale", "`timescale 1us / 1ns");
        local.insert("label", "Personal name");
        QVERIFY(write(f.global, QJsonDocument(QJsonObject{{"templates", QJsonArray{local}}}).toJson()));
        const auto original = bytes(f.global);
        QVERIFY(f.service.reload().valid); QCOMPARE(f.service.recordsForFile(f.file).size(), 1);
        QCOMPARE(f.current().label, QString("Personal name"));
        QVERIFY(write(f.bundled, catalog({entry("user:timescale", "/changed_default", "`timescale 10ns / 1ps")})));
        QCOMPARE(f.service.recordsForFile(f.file).size(), 1); QCOMPARE(f.current().commandToken, QString("/my_timescale"));
        QCOMPARE(bytes(f.global), original);
        auto workspace = f.current(); workspace.id = "workspace:timescale";
        workspace.insertText = "`timescale 100ps / 1ps";
        QVERIFY(f.service.saveRecord(workspace, true).valid);
        QCOMPARE(f.current().insertText, workspace.insertText);
        QVERIFY(f.service.resetBundledOverride(f.current(), true).valid);
        QCOMPARE(f.current().insertText, QString("`timescale 1us / 1ns"));
        QVERIFY(write(f.bundled, catalog()));
        QVERIFY(write(f.global, QJsonDocument(QJsonObject{{"templates", QJsonArray{entry("legacy:custom", "/timescale", "my existing body")}}}).toJson()));
        const auto legacy = bytes(f.global);
        QVERIFY(f.service.reload().valid); QCOMPARE(f.service.recordsForFile(f.file).size(), 1);
        QCOMPARE(f.current().id, QString("legacy:custom")); QCOMPARE(f.current().bundledId, QString("user:timescale"));
        QCOMPARE(bytes(f.global), legacy);
    }
    void visibilityRestartUpgradeRemovalAndWorkspace() {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.service.reload().valid);
        QVERIFY(f.service.setBundledHidden(f.current(), true, false).valid);
        QVERIFY(OrdinaryTemplatePlanner::catalog(&f.service, f.file).isEmpty());
        const auto saved = QJsonDocument::fromJson(bytes(f.global)).object().value("templates").toArray().first().toObject();
        QVERIFY(saved.value("bundledStateOnly").toBool()); QVERIFY(saved.value("body").toString().isEmpty());
        QVERIFY(f.service.setBundledHidden(f.current(), false, true).valid);
        QCOMPARE(OrdinaryTemplatePlanner::catalog(&f.service, f.file).size(), 1);
        UserTemplateService restarted(f.global, f.workspace); restarted.setBundledTemplateFilePath(f.bundled);
        QVERIFY(restarted.reload().valid); QCOMPARE(OrdinaryTemplatePlanner::catalog(&restarted, f.file).size(), 1);
        restarted.setWorkspaceTemplateFilePath(f.root.filePath("other/.zeroslack/user_templates.json"));
        QVERIFY(OrdinaryTemplatePlanner::catalog(&restarted, f.file).isEmpty());
        restarted.setWorkspaceTemplateFilePath(f.workspace);
        QCOMPARE(OrdinaryTemplatePlanner::catalog(&restarted, f.file).size(), 1);
        const auto globalBytes = bytes(f.global), workspaceBytes = bytes(f.workspace);
        QVERIFY(write(f.bundled, catalog({}))); QVERIFY(restarted.reload().valid);
        QVERIFY(OrdinaryTemplatePlanner::catalog(&restarted, f.file).isEmpty());
        QVERIFY(write(f.bundled, catalog({entry("user:timescale", "/timescale", "`timescale 100ns / 1ps")})));
        QCOMPARE(OrdinaryTemplatePlanner::catalog(&restarted, f.file).first().insertText, QString("`timescale 100ns / 1ps"));
        QCOMPARE(bytes(f.global), globalBytes); QCOMPARE(bytes(f.workspace), workspaceBytes);
        auto current = restarted.recordsForFile(f.file).first();
        QVERIFY(restarted.resetBundledOverride(current, true).valid);
        QVERIFY(OrdinaryTemplatePlanner::catalog(&restarted, f.file).isEmpty());
        QVERIFY(restarted.resetBundledOverride(restarted.recordsForFile(f.file).first(), false).valid);
        QCOMPARE(OrdinaryTemplatePlanner::catalog(&restarted, f.file).first().insertText, QString("`timescale 100ns / 1ps"));
    }
    void renamedOverrideShadowsIdentityAndCommandWithoutDeletingData() {
        Fixture f; QVERIFY(f.initialize());
        QVERIFY(write(f.bundled, catalog({entry("other", "/other", "other default"), entry()})));
        QVERIFY(write(f.global, QJsonDocument(QJsonObject{{"templates", QJsonArray{entry("user:timescale", "/other", "personal override")}}}).toJson()));
        const auto original = bytes(f.global);
        QVERIFY(f.service.reload().valid); QCOMPARE(f.service.recordsForFile(f.file).size(), 1);
        QCOMPARE(f.current().bundledId, QString("user:timescale")); QCOMPARE(f.current().insertText, QString("personal override"));
        QCOMPARE(bytes(f.global), original);
        auto edited = f.current(); edited.insertText = "edited personal override";
        QVERIFY(f.service.saveRecord(edited, false).valid);
        QCOMPARE(f.service.recordsForFile(f.file).size(), 1);
        QCOMPARE(f.current().bundledId, QString("user:timescale"));
        edited.bundledId.clear();
        QVERIFY(f.service.saveRecord(edited, false).valid);
        QCOMPARE(f.service.recordsForFile(f.file).size(), 1);
        QCOMPARE(f.current().bundledId, QString("user:timescale"));
        QCOMPARE(f.current().insertText, edited.insertText);
    }
    void editingHideShowAndResetPreserveInputs() {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.service.reload().valid);
        auto custom = f.current(); custom.id = "custom:timescale"; custom.label = "My timing";
        custom.insertText = "`timescale ${unit} / 1ps";
        custom.command.insert("inputs", QVariantMap{{"unit", QVariantMap{{"kind", "Text"}, {"default", "1us"}}}});
        QVERIFY(f.service.saveRecord(custom, false).valid);
        QVERIFY(f.service.setBundledHidden(f.current(), true, false).valid);
        QVERIFY(write(f.bundled, catalog({entry("user:timescale", "/timescale", "`timescale 10ns / 1ps")})));
        QVERIFY(f.current().command.value("disabled").toBool());
        QVERIFY(f.service.setBundledHidden(f.current(), false, false).valid);
        QCOMPARE(f.current().insertText, custom.insertText); QCOMPARE(f.current().label, custom.label);
        QCOMPARE(f.current().command.value("inputs"), custom.command.value("inputs"));
        const auto plan = OrdinaryTemplatePlanner::prepare(f.current(), OrdinaryTemplatePlanner::catalog(&f.service, f.file));
        QVERIFY(plan.error.isEmpty()); QCOMPARE(plan.text, QString("`timescale 1us / 1ps"));
        QVERIFY(f.service.resetBundledOverride(f.current(), false).valid);
        QCOMPARE(f.current().insertText, QString("`timescale 10ns / 1ps"));
        QVERIFY(f.current().command.value("inputs").toMap().isEmpty());
    }
    void fileScopedVisibilityDoesNotLeak() {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.service.reload().valid);
        QVERIFY(f.service.setBundledHidden(f.current(), true, true, f.file).valid);
        QVERIFY(OrdinaryTemplatePlanner::catalog(&f.service, f.file).isEmpty());
        const auto other = f.root.filePath("workspace/other.sv");
        QCOMPARE(OrdinaryTemplatePlanner::catalog(&f.service, other).size(), 1);
        UserTemplateService restarted(f.global, f.workspace); restarted.setBundledTemplateFilePath(f.bundled);
        QVERIFY(OrdinaryTemplatePlanner::catalog(&restarted, f.file).isEmpty());
        QVERIFY(restarted.setBundledHidden(restarted.recordsForFile(f.file).first(), false, true, f.file).valid);
        QCOMPARE(OrdinaryTemplatePlanner::catalog(&restarted, f.file).size(), 1);
        QCOMPARE(OrdinaryTemplatePlanner::catalog(&restarted, other).size(), 1);
    }
    void scopeSwitchRetainsUnchangedValidLayers() {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.service.reload().valid);
        QVERIFY(f.service.setBundledHidden(f.current(), true, false).valid);
        QVERIFY(OrdinaryTemplatePlanner::catalog(&f.service, f.file).isEmpty());
        const auto original = bytes(f.global);
        QVERIFY(write(f.global, "{invalid user file"));
        f.service.setWorkspaceTemplateFilePath(f.root.filePath("other/.zeroslack/user_templates.json"));
        QVERIFY(!f.service.reload().valid);
        QVERIFY(OrdinaryTemplatePlanner::catalog(&f.service, f.file).isEmpty());
        QVERIFY(f.current().command.value("disabled").toBool());
        QVERIFY(write(f.global, original)); QVERIFY(f.service.reload().valid);
        f.service.setGlobalTemplateFilePath(f.root.filePath("another-profile/user_templates.json"));
        QCOMPARE(OrdinaryTemplatePlanner::catalog(&f.service, f.file).size(), 1);
    }
    void invalidCatalogRetainsWholeValidLayer_data() {
        QTest::addColumn<QByteArray>("invalid");
        QTest::newRow("missing") << QByteArray();
        QTest::newRow("malformed") << QByteArray("{");
        QTest::newRow("version-number") << QByteArray("{\"schemaVersion\":2,\"templates\":[]}");
        QTest::newRow("version-string") << QByteArray("{\"schemaVersion\":\"1\",\"templates\":[]}");
        QTest::newRow("version-bool") << QByteArray("{\"schemaVersion\":true,\"templates\":[]}");
        QTest::newRow("duplicate-id") << catalog({entry(), entry("user:timescale", "/other", "other")});
        QTest::newRow("duplicate-command") << catalog({entry(), entry("different", "/TIMESCALE", "other")});
        QTest::newRow("missing-id") << catalog({entry("", "/timescale", "body")});
        QTest::newRow("partial-valid") << catalog({entry("other", "/other", "other"), entry("invalid", "/bad", "")});
    }
    void invalidCatalogRetainsWholeValidLayer() {
        QFETCH(QByteArray, invalid);
        Fixture f; QVERIFY(f.initialize());
        QVERIFY(write(f.global, QJsonDocument(QJsonObject{{"templates", QJsonArray{entry("personal", "/personal", "personal body")}}}).toJson()));
        QVERIFY(f.service.reload().valid); const auto original = bytes(f.global);
        const auto revision = f.service.catalogRevision();
        if (invalid.isEmpty()) QVERIFY(QFile::remove(f.bundled)); else QVERIFY(write(f.bundled, invalid));
        const auto failed = f.service.reload(); QVERIFY(!failed.valid); QVERIFY(!failed.failureReason.isEmpty());
        QCOMPARE(f.service.catalogRevision(), revision);
        QCOMPARE(f.service.recordsForFile(f.file).size(), 2); QCOMPARE(f.current().commandToken, QString("/timescale"));
        UserTemplateService fresh(f.global, f.workspace); fresh.setBundledTemplateFilePath(f.bundled);
        QVERIFY(!fresh.reload().valid); QCOMPARE(fresh.recordsForFile(f.file).size(), 1);
        QCOMPARE(fresh.recordsForFile(f.file).first().insertText, QString("personal body"));
        QCOMPARE(bytes(f.global), original);
        QVERIFY(write(f.bundled, catalog())); QVERIFY(fresh.reload().valid);
        QCOMPARE(fresh.recordsForFile(f.file).size(), 2);
    }
    void conflictsLocksAndInvalidUserFilesDoNotWrite() {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.service.reload().valid);
        const auto revision = f.service.catalogRevision(); auto record = f.current(); record.insertText = "custom";
        QVERIFY(write(f.bundled, catalog({entry("user:timescale", "/timescale", "new default")})));
        QVERIFY(!f.service.saveRecord(record, false, revision).valid); QVERIFY(!QFileInfo::exists(f.global));
        QDir().mkpath(QFileInfo(f.global).absolutePath()); QLockFile lock(f.global + ".lock"); QVERIFY(lock.tryLock(0));
        QVERIFY(!f.service.saveRecord(record, false).valid); QVERIFY(!QFileInfo::exists(f.global)); lock.unlock();
        QVERIFY(write(f.global, "{bad user json")); const auto original = bytes(f.global);
        QVERIFY(!f.service.saveRecord(record, false).valid); QCOMPARE(bytes(f.global), original);
        QVERIFY(!f.service.deleteRecord(record.id, false).valid); QCOMPARE(bytes(f.global), original);
    }
    void managerOverridesVisibilityResetAndCustomDelete() {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.service.reload().valid);
        SimulationCommandRequest request; request.context.fileName = f.file;
        TemplateManagerDialog dialog(&f.service, request, QFileInfo(f.workspace).dir().absolutePath() + "/.."); dialog.show();
        auto* list = dialog.findChild<QListWidget*>("templateList");
        auto* scope = dialog.findChild<QComboBox*>("templateScope");
        auto* body = dialog.findChild<QPlainTextEdit*>("templateBody");
        auto* name = dialog.findChild<QLineEdit*>("templateName");
        auto* hide = dialog.findChild<QPushButton*>("templateDeleteOrHide");
        auto* reset = dialog.findChild<QPushButton*>("templateResetOverride");
        auto* box = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(list && scope && body && hide && reset && box && name);
        QAbstractButton* save = nullptr;
        for (auto* button : box->buttons()) if (box->buttonRole(button) == QDialogButtonBox::AcceptRole) save = button;
        QVERIFY(save); QCOMPARE(list->count(), 1); QVERIFY(list->item(0)->text().contains("Bundled"));
        list->setCurrentRow(0); scope->setCurrentText("User global"); body->setPlainText("`timescale 1us / 1ns"); save->click();
        QCOMPARE(f.current().insertText, QString("`timescale 1us / 1ns")); QVERIFY(list->item(0)->text().contains("Global override"));
        hide->click(); QCOMPARE(hide->text(), QString("Show")); QVERIFY(list->item(0)->text().contains("[hidden]"));
        QVERIFY(OrdinaryTemplatePlanner::catalog(&f.service, f.file).isEmpty());
        hide->click(); QCOMPARE(f.current().insertText, QString("`timescale 1us / 1ns"));
        reset->click(); QCOMPARE(body->toPlainText(), QString("`timescale 1ns / 1ps")); QVERIFY(!QJsonDocument::fromJson(bytes(f.global)).object().value("templates").toArray().size());
        QCOMPARE(bytes(f.bundled), catalog());
        for (auto* button : dialog.findChildren<QPushButton*>()) if (button->text() == "New") { button->click(); break; }
        scope->setCurrentText("User global"); name->setText("/personal"); body->setPlainText("logic personal;"); save->click();
        QCOMPARE(list->count(), 2); QCOMPARE(hide->text(), QString("Delete")); hide->click(); QCOMPARE(list->count(), 1);
    }
    void slashInsertionAndUndoWithoutImport() {
        Fixture f; QVERIFY(f.initialize());
        auto* service = UserTemplateService::getInstance();
        service->setGlobalTemplateFilePath(f.global); service->setWorkspaceTemplateFilePath(f.workspace); service->setBundledTemplateFilePath(f.bundled);
        QVERIFY(service->reload().valid);
        MyCodeEditor editor; editor.setDocumentFileName(f.file); editor.resize(740, 420); editor.show(); editor.setFocus();
        QTest::keyClicks(&editor, "/");
        auto* query = editor.findChild<QLineEdit*>("editorSlashQuery"); auto* candidates = editor.findChild<QListWidget*>("editorSlashCandidates");
        QVERIFY(query && candidates); query->setText("timescale"); QTRY_COMPARE(candidates->count(), 2);
        QCOMPARE(candidates->item(0)->text(), QString("Template"));
        QVERIFY(!(candidates->item(0)->flags() & Qt::ItemIsSelectable));
        QTest::keyClick(query, Qt::Key_Return); QCOMPARE(editor.toPlainText(), QString("`timescale 1ns / 1ps"));
        editor.undo(); QVERIFY(editor.toPlainText().isEmpty()); editor.redo(); QCOMPARE(editor.toPlainText(), QString("`timescale 1ns / 1ps"));
        QVERIFY(!QFileInfo::exists(f.global));
        QSignalSpy errors(&editor, &MyCodeEditor::editorStatusMessageRequested);
        QVERIFY(write(f.bundled, "{bad catalog"));
        editor.setFocus(); QTest::keyClicks(&editor, "/");
        QVERIFY(!errors.isEmpty()); QVERIFY(errors.last().first().toString().contains("Invalid"));
        QTest::keyClick(query, Qt::Key_Escape);
        service->setBundledTemplateFilePath({}); service->setGlobalTemplateFilePath(f.root.filePath("empty.json")); service->setWorkspaceTemplateFilePath({});
    }
};

QTEST_MAIN(BundledTemplatesTest)
#include "bundled_templates_test.moc"
