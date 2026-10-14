#include "../src/simulation/simdock/core/questasession.h"
#include "../src/simulation/simdock/core/workspace.h"
#include "../src/simulation/simdock/ui/workbench.h"
#include "applicationthememanager.h"
#include "documenttemplatemodel.h"
#include "mycodeeditor.h"
#include "simulationcommandplanner.h"
#include "simulationcommandservice.h"
#include "slangmanager.h"
#include "testuistyle.h"
#include "usertemplateservice.h"
#include <QScopeGuard>
#include <QtTest>
#include <QtWidgets>
#define private public
#include "globalcontrolcoordinator.h"
#include "globalcontrolpanel.h"
#include "mainwindow.h"
#undef private
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"

namespace {
QByteArray bytes(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool put(const QString &path, const QByteArray &data) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
void evidence(const QString &name, const QByteArray &data) {
  const auto root = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR");
  if (!root.isEmpty())
    put(QDir(root).filePath(name), data);
}
QString digest(const QByteArray &data) {
  return QString::fromLatin1(
      QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
bool copyFixture(const QString &source, const QString &target,
                 QJsonObject *hashes) {
  QDirIterator it(source, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const auto path = it.next();
    if (it.fileInfo().isSymLink())
      return false;
    const auto relative = QDir(source).relativeFilePath(path);
    const auto data = bytes(path);
    if (!put(QDir(target).filePath(relative), data))
      return false;
    hashes->insert(relative, digest(data));
  }
  return !hashes->isEmpty();
}
SimulationCommandRequest requestFor(const QString &file, const QString &source,
                                    const QString &token,
                                    const QStringList &support) {
  SimulationCommandRequest request;
  request.context.fileName = file;
  request.context.documentText = source;
  request.context.moduleName = "tb";
  request.context.editorAvailable = true;
  request.context.cursorPosition = source.indexOf("@command") + 8;
  request.context.cursorLine =
      source.left(request.context.cursorPosition).count('\n') + 1;
  request.item.replacementStart = source.indexOf("@command");
  request.item.replacementLength = 8;
  for (const auto &record : UserTemplateService::simulationBuiltins())
    if (record.commandToken == token)
      request.item.parameters = record.command;
  request.item.parameters.insert(
      "inputs",
      QVariantMap{{"signal", QVariantMap{{"kind", "Signal"},
                                         {"access", "write"},
                                         {"default",
                                          token == "@reset" ? "rst" : "data"}}},
                  {"clock", QVariantMap{{"kind", "Signal"},
                                        {"role", "Clock"},
                                        {"default", "clk"}}}});
  request.contents.insert(file, source);
  request.orderedFiles = support + QStringList{file};
  return request;
}
} // namespace

class SimulationDriverInsertionTest : public QObject {
  Q_OBJECT
private slots:
  void driverPlans_data() {
    QTest::addColumn<QString>("token");
    QTest::addColumn<int>("site");
    for (const auto &token : {"@reset", "@ramp", "@random", "@random_window"})
      for (int site = 0; site < (QString(token) == "@reset" ? 3 : 2); ++site)
        QTest::newRow(
            qPrintable(QString("%1-%2").arg(token + 1, site == 0   ? "module"
                                                       : site == 1 ? "procedure"
                                                                   : "task")))
            << QString(token) << site;
  }
  void driverPlans() {
    QFETCH(QString, token);
    QFETCH(int, site);
    QTemporaryDir root;
    QStringList support;
    const auto issue = SimulationCommandService::materializeSupport(
        root.path(), "driver", &support);
    QVERIFY2(issue.isEmpty(), qPrintable(issue));
    const auto file = root.filePath("tb.sv");
    const QString source =
        "// \u03a9 UTF-16 offsets\nmodule tb; timeunit 1ns; timeprecision "
        "1ps;\nlogic clk=0; logic rst=0; logic "
        "[7:0] data=0;\n"
        "localparam simulation_driver_1=1;\nalways #1ns clk=~clk;\n" +
        (site == 0   ? QString("@command\n")
         : site == 1 ? QString("initial begin\n  @command\nend\n")
                     : QString("task automatic run_driver();\n  "
                               "@command\nendtask\ninitial run_driver();\n")) +
        "endmodule\n";
    const auto request = requestFor(file, source, token, support);
    const auto plan = SimulationCommandPlanner::prepare(request);
    evidence(QString("plans/%1-error.txt").arg(QTest::currentDataTag()),
             plan.error.toUtf8());
    QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
    MyCodeEditor tracked;
    tracked.setPlainText(source);
    QTextCursor insertion(tracked.document());
    insertion.setPosition(plan.start);
    insertion.setPosition(plan.start + plan.length, QTextCursor::KeepAnchor);
    tracked.setTextCursor(insertion);
    QString boundaryError;
    QVERIFY2(tracked.insertSimulationTemplate(plan.text, plan.placeholders,
                                              plan.instance, {}, {},
                                              &boundaryError),
             qPrintable(boundaryError));
    const auto ownership = tracked.templateModel()->serialize(file);
    QCOMPARE(tracked.templateModel()->instances().size(), 1);
    if (site != 0) {
      const auto instance = tracked.templateModel()->instances().first();
      QCOMPARE(instance.fragments.size(), 2);
      QVERIFY(instance.fragments.first().end() <=
              instance.fragments.last().start);
    }
    QVERIFY(tracked.templateModel()->restore(ownership, file));
    tracked.undo();
    QCOMPARE(tracked.toPlainText(), source);
    QVERIFY(tracked.templateModel()->instances().isEmpty());
    tracked.redo(); // A restored model intentionally starts a new metadata
                    // history.
    QVERIFY(tracked.templateModel()->restore(ownership, file));
    QVERIFY(plan.text.contains("class simulation_driver_2 "));
    auto generated = source;
    generated.replace(plan.start, plan.length, plan.text);
    QCOMPARE(generated.count("initial"), 1);
    QVERIFY(generated.contains("localparam simulation_driver_1=1;"));
    QVERIFY(!plan.placeholders.isEmpty());
    for (const auto &slot : plan.placeholders) {
      QVERIFY(slot.start >= 0 && slot.start + slot.length <= plan.text.size());
      if (slot.name == "signal")
        QCOMPARE(plan.text.mid(slot.start, slot.length),
                 token == "@reset" ? QString("rst") : QString("data"));
      if (slot.name == "clock")
        QCOMPARE(plan.text.mid(slot.start, slot.length), QString("clk"));
    }
    generated.insert(plan.start + plan.text.lastIndexOf("\nend"),
                     "\n  if (status != tb_signal_package::TB_OK) $fatal(1, "
                     "\"driver failed\");");
    QVERIFY(put(file, generated.toUtf8()));
    evidence(QString("plans/%1.sv").arg(QTest::currentDataTag()),
             generated.toUtf8());
    auto project = simdock::newProject("Driver insertion");
    for (const auto &input : support)
      project.sources << QDir(root.path()).relativeFilePath(input);
    project.sources << "tb.sv";
    project.tbFile = "tb.sv";
    project.tbName = "tb";
    project.durationNs = 200;
    simdock::QuestaSession session;
    QSignalSpy finished(&session, &simdock::QuestaSession::finished);
    QString error, log;
    connect(&session, &simdock::QuestaSession::logText, this,
            [&](const QString &s) { log += s; });
    QVERIFY2(session.start(qEnvironmentVariable("SIMDOCK_TEST_QUESTA"),
                           root.path(), project, &error),
             qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 90000);
    evidence(QString("plans/%1-questa.log").arg(QTest::currentDataTag()),
             log.toUtf8());
    QVERIFY2(finished.first()[0].toBool(), qPrintable(log));
    session.stop();
    QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 10000);
  }
  void rejectedSites_data() {
    QTest::addColumn<QString>("body");
    const QList<QPair<const char *, QString>> cases{
        {"function", "function void f(); @command endfunction"},
        {"expression", "initial begin x = @command; end"},
        {"call-argument", "initial begin $display(@command); end"},
        {"comment", "// @command\n"},
        {"block-comment", "/* @command */"},
        {"string", "initial $display(\"@command\");"},
        {"declaration", "logic @command;"},
        {"initializer", "logic y = @command;"},
        {"port", "child u(.value(@command));"},
        {"continuous", "assign x = @command;"},
        {"event", "always @(@command) x=0;"}};
    for (const auto &row : cases)
      QTest::newRow(row.first) << row.second;
  }
  void rejectedSites() {
    QFETCH(QString, body);
    QTemporaryDir root;
    const auto source = "module tb; logic x;\n" + body + "\nendmodule\n";
    const auto plan = SimulationCommandPlanner::prepare(
        requestFor(root.filePath("tb.sv"), source, "@reset", {}));
    QVERIFY(!plan.error.isEmpty());
    QVERIFY2(plan.error.contains("position"), qPrintable(plan.error));
    QVERIFY(plan.text.isEmpty());
    evidence(QString("rejected/%1.txt").arg(QTest::currentDataTag()),
             plan.error.toUtf8());
  }
  void adapterSignalScope() {
    QTemporaryDir root;
    QStringList support;
    QVERIFY(SimulationCommandService::materializeSupport(root.path(), "driver",
                                                         &support)
                .isEmpty());
    const QString source =
        "module tb; logic clk; logic [7:0] data;\ninitial begin\nlogic [7:0] "
        "local_data;\n@command\nend\nendmodule";
    auto request = requestFor(root.filePath("tb.sv"), source, "@ramp", support);
    const auto plan = SimulationCommandPlanner::prepare(request);
    QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
    QVERIFY(!plan.placeholders.isEmpty());
    const auto input = plan.placeholders.first().input;
    QVERIFY(input.value("moduleVariable").toBool());
    const auto choices =
        TemplateInputService::candidates(input, *plan.inputs, {});
    QVERIFY(choices.values.contains("data"));
    QVERIFY(!choices.values.contains("local_data"));
    auto unrestricted = input;
    unrestricted.remove("moduleVariable");
    QVERIFY(TemplateInputService::candidates(unrestricted, *plan.inputs, {})
                .values.contains("local_data"));
  }
  void rejectedKeyboard_data() {
    QTest::addColumn<QString>("body");
    QTest::newRow("function")
        << QString("function void f(); @command endfunction");
    QTest::newRow("expression") << QString("initial begin x=@command; end");
    QTest::newRow("comment") << QString("// @command\n");
    QTest::newRow("port") << QString("child u(.value(@command));");
  }
  void rejectedKeyboard() {
    QFETCH(QString, body);
    QTemporaryDir root;
    const auto file = root.filePath("tb.sv");
    QString source = "module tb; logic x;\n" + body + "\nendmodule\n";
    const int position = source.indexOf("@command");
    source.remove(position, 8);
    QVERIFY(put(file, source.toUtf8()));
    MainWindow window;
    window.show();
    QVERIFY(window.workspaceSessionCoordinator->openWorkspace(root.path()));
    QVERIFY(window.tabManager->openFileInTab(file));
    auto *editor = window.tabManager->getCurrentEditor();
    auto cursor = editor->textCursor();
    cursor.setPosition(position);
    editor->setTextCursor(cursor);
    window.activateWindow();
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QSignalSpy messages(window.simulationCommands.get(),
                        &SimulationCommandService::message);
    QTest::keyClick(editor, Qt::Key_Space, Qt::ControlModifier);
    auto *panel = window.globalControlCoordinator->panel.get();
    QTRY_VERIFY(panel->isVisible());
    auto *search = panel->findChild<QLineEdit *>("globalControlSearchEdit");
    QTRY_VERIFY(search->hasFocus());
    QTest::keyClick(search, Qt::Key_Tab);
    QTest::keyClicks(search, "reset");
    auto *list = panel->findChild<QListWidget *>("globalControlResultList");
    auto *notice = panel->findChild<QLabel *>("globalControlNotice");
    QVERIFY(list && notice);
    QCOMPARE(list->count(), 0);
    QVERIFY(notice->isVisible());
    const auto message = notice->text();
    QVERIFY2(message.contains("position"), qPrintable(message));
    const auto undo = editor->document()->availableUndoSteps();
    QTest::keyClick(search, Qt::Key_Return);
    QVERIFY(messages.isEmpty());
    QVERIFY(panel->isVisible());
    QCOMPARE(editor->toPlainText(), source);
    QCOMPARE(editor->document()->availableUndoSteps(), undo);
    evidence(QString("rejected/%1-keyboard.txt").arg(QTest::currentDataTag()),
             message.toUtf8());
  }
  void projectKeyboard_data() {
    QTest::addColumn<bool>("prefix");
    QTest::addColumn<bool>("moduleSite");
    for (bool prefix : {false, true})
      for (bool module : {true, false})
        QTest::newRow(qPrintable(QString("%1-%2").arg(
            prefix ? "prefix" : "templates", module ? "module" : "procedure")))
            << prefix << module;
  }
  void projectKeyboard() {
    QFETCH(bool, prefix);
    QFETCH(bool, moduleSite);
    const auto configPath =
        qEnvironmentVariable("ZEROSLACK_DRIVER_PROJECT_CASE");
    if (configPath.isEmpty())
      QSKIP("Supply an isolated real-project case descriptor.");
    const auto config = QJsonDocument::fromJson(bytes(configPath)).object();
    QTemporaryDir root;
    QJsonObject fixtureHashes;
    QVERIFY(copyFixture(config.value("source").toString(), root.path(),
                        &fixtureHashes));
    const auto projects = simdock::loadProjects(root.path());
    QVERIFY(!projects.isEmpty());
    auto project = projects.first();
    const auto file = root.filePath(project.tbFile);
    MainWindow window;
    window.resize(1100, 800);
    window.show();
    window.workspaceSessionCoordinator->setRestoreOnActivation(false);
    window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(
        false);
    QVERIFY(window.workspaceSessionCoordinator->openWorkspace(root.path()));
    QString error;
    QVERIFY2(window.openSimDockTarget(root.path(), project.id, &error),
             qPrintable(error));
    for (const auto &opened : config.value("openFiles").toArray())
      QVERIFY(
          window.tabManager->openFileInTab(root.filePath(opened.toString())));
    QVERIFY(window.tabManager->openFileInTab(file));
    auto *editor = window.tabManager->getCurrentEditor();
    const auto original = editor->toPlainText();
    QVariantMap roles;
    const auto clock = config.value("clock").toString();
    const auto reset = config.value("reset").toString();
    SlangManager slang;
    for (const auto &record : slang.extractSymbolRecords(file, original))
      if (record.name == clock || record.name == reset)
        roles.insert(TemplateInputService::roleKey(
                         record, record.presentation.qualifiedScopePath + '.' +
                                     record.name),
                     record.name == clock ? "Clock" : "Reset");
    QCOMPARE(roles.size(), 2);
    QVERIFY(
        UserTemplateService::getInstance()->setSignalRoles(roles).isEmpty());
    const auto line =
        config.value(moduleSite ? "moduleLine" : "procedureLine").toInt() - 1;
    auto cursor = QTextCursor(editor->document()->findBlockByNumber(line));
    QVERIFY(cursor.block().isValid());
    QVERIFY(cursor.block().text().trimmed().isEmpty());
    editor->setTextCursor(cursor);
    if (prefix)
      editor->insertPlainText("@reset");
    const auto before = editor->toPlainText();
    for (int repeat = 0; repeat < 2; ++repeat) {
      window.activateWindow();
      editor->setFocus();
      QTRY_VERIFY_WITH_TIMEOUT(editor->hasFocus(), 5000);
      QSignalSpy messages(window.simulationCommands.get(),
                          &SimulationCommandService::message);
      QTest::keyClick(editor, Qt::Key_Space, Qt::ControlModifier);
      auto *panel = window.globalControlCoordinator->panel.get();
      QTRY_VERIFY(panel->isVisible());
      auto *search = panel->findChild<QLineEdit *>("globalControlSearchEdit");
      auto *list = panel->findChild<QListWidget *>("globalControlResultList");
      QVERIFY(search && list);
      QTRY_VERIFY(search->hasFocus());
      if (!prefix) {
        QTest::keyClick(search, Qt::Key_Tab);
        QCOMPARE(panel->category(), GlobalControlCategory::Templates);
        QTest::keyClicks(search, "reset");
      }
      QVERIFY(list->currentItem());
      QCOMPARE(list->currentItem()->text(), QString("@reset"));
      QTest::keyClick(search, Qt::Key_Return);
      QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 20000);
      const auto inserted = editor->toPlainText();
      evidence(
          QString("project/%1.json").arg(QTest::currentDataTag()),
          QJsonDocument(
              QJsonObject{{"fixtureHashes", fixtureHashes},
                          {"prefix", prefix},
                          {"moduleSite", moduleSite},
                          {"message", messages.last().first().toString()},
                          {"originalHash", digest(original.toUtf8())},
                          {"before", before},
                          {"after", inserted},
                          {"slotCount", editor->templateSlotModeSlotCount()}})
              .toJson());
      QVERIFY2(inserted != before,
               qPrintable(messages.last().first().toString()));
      QVERIFY(editor->templateSlotModeActive());
      QCOMPARE(editor->textCursor().selectedText(), QString("__signal__"));
      if (repeat == 0) {
        QTest::keyClick(editor, Qt::Key_Escape);
        QTest::keyClick(editor, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), before);
        QTest::keyClick(editor, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), inserted);
        QTest::keyClick(editor, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), before);
        cursor = QTextCursor(editor->document()->findBlockByNumber(line));
        if (prefix)
          cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 6);
        editor->setTextCursor(cursor);
        continue;
      }
      QSignalSpy inputMessages(editor,
                               &MyCodeEditor::editorStatusMessageRequested);
      QTest::keyClicks(editor, reset);
      QTRY_VERIFY(!editor->toPlainText().contains("__signal__"));
      QTest::keyClick(editor, Qt::Key_Tab);
      QVERIFY2(editor->templateSlotModeActiveIndex() == 1,
               qPrintable(inputMessages.isEmpty()
                              ? QString("No slot feedback")
                              : inputMessages.last().first().toString()));
      QCOMPARE(editor->textCursor().selectedText(), QString("__clock__"));
      QTest::keyClicks(editor, clock);
      QTRY_VERIFY(!editor->toPlainText().contains("__clock__"));
      QTest::keyClick(editor, Qt::Key_Tab);
      QVERIFY2(editor->templateSlotModeActiveIndex() == 2,
               qPrintable(inputMessages.isEmpty()
                              ? QString("No slot feedback")
                              : inputMessages.last().first().toString()));
      const auto report = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR");
      if (!report.isEmpty())
        window.grab().save(QDir(report).filePath(
            QString("project/%1.png").arg(QTest::currentDataTag())));
      QTest::keyClick(editor, Qt::Key_Escape);
      QVERIFY(window.tabManager->saveCurrentTab());
      const auto filled = editor->toPlainText();
      evidence(QString("project/%1-filled.sv").arg(QTest::currentDataTag()),
               filled.toUtf8());
      QCOMPARE(filled.count("initial begin"),
               original.count("initial begin") + (moduleSite ? 1 : 0));
      const auto updated = simdock::loadProjects(root.path());
      QCOMPARE(updated.size(), projects.size());
      project = updated.first();
      QCOMPARE(project.sources.size(), projects.first().sources.size() + 4);
      QCOMPARE(project.sources.mid(4), projects.first().sources);
      QVERIFY(project.sources.first().endsWith("tb_signal_package.sv"));
      QJsonObject preserved;
      for (auto it = fixtureHashes.begin(); it != fixtureHashes.end(); ++it) {
        if (it.key() == project.tbFile || it.key().endsWith("/project.json"))
          continue;
        QCOMPARE(digest(bytes(root.filePath(it.key()))), it.value().toString());
        preserved.insert(it.key(), true);
      }
      evidence(
          QString("project/%1-dependencies.json").arg(QTest::currentDataTag()),
          QJsonDocument(
              QJsonObject{{"project", simdock::projectJson(project)},
                          {"roles", QJsonObject::fromVariantMap(roles)},
                          {"originalFilesPreserved", preserved}})
              .toJson());
      project.durationNs = 200;
      simdock::QuestaSession session;
      QSignalSpy finished(&session, &simdock::QuestaSession::finished);
      QString log;
      connect(&session, &simdock::QuestaSession::logText, this,
              [&](const QString &s) { log += s; });
      QVERIFY2(session.start(qEnvironmentVariable("SIMDOCK_TEST_QUESTA"),
                             root.path(), project, &error),
               qPrintable(error));
      QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 90000);
      evidence(QString("project/%1-questa.log").arg(QTest::currentDataTag()),
               log.toUtf8());
      QVERIFY2(finished.first()[0].toBool(), qPrintable(log));
      session.stop();
      QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 10000);
    }
    editor->document()->setModified(false);
  }
};
int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QStandardPaths::setTestModeEnabled(true);
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  QCoreApplication::setOrganizationName("ZeroSlackDriverInsertionTest");
  QCoreApplication::setApplicationName("Isolated");
  QTemporaryDir profile;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     profile.path());
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                     profile.path());
  qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH",
          profile.filePath("legacy.ini").toUtf8());
  UserTemplateService::getInstance()->setGlobalTemplateFilePath(
      profile.filePath("templates.json"));
  if (!initializeUiStyleForTest())
    return 2;
  ApplicationThemeManager::instance().setAnimationsEnabled(false);
  SimulationDriverInsertionTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "simulation_driver_insertion_test.moc"
