#include "applicationthememanager.h"
#include "documenttemplatemodel.h"
#include "editorinsertpaletteservice.h"
#include "mycodeeditor.h"
#include "simulationcommandplanner.h"
#include "simulationcommandprefix.h"
#include "simulationcommandservice.h"
#include "testuistyle.h"
#include "tsdocument.h"
#include "usertemplateservice.h"
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
#include "workspacesessionstateservice.h"

namespace {
void put(const QString &path, const QByteArray &bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (file.open(QIODevice::WriteOnly))
    file.write(bytes);
}
void evidence(const QString &name, const QByteArray &bytes) {
  const auto dir = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR");
  if (!dir.isEmpty())
    put(QDir(dir).filePath(name), bytes);
}
void type(const QString &text) {
  for (const auto character : text) {
    if (character == '\n')
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    else
      QTest::keyClicks(QApplication::focusWidget(), QString(character));
    QCoreApplication::processEvents();
  }
}
void lock(MyCodeEditor *editor) {
  QTest::keyPress(editor, Qt::Key_F24);
  QTest::keyClick(editor, Qt::Key_Space);
  QTest::keyRelease(editor, Qt::Key_F24);
}
void cursor(MyCodeEditor *editor, int start, int end = -1) {
  QTextCursor position(editor->document());
  position.setPosition(start);
  if (end >= 0)
    position.setPosition(end, QTextCursor::KeepAnchor);
  editor->setTextCursor(position);
}
void returnToParentCode(MyCodeEditor* editor, const QString& parent) {
  for (int i = 0; i < 5 && (editor->templateInputEditing() || editor->selectedTemplateId() != parent); ++i)
    QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
  QTest::keyClick(editor, Qt::Key_Return);
}
class PopupLifecycleObserver : public QObject {
public:
  int shows = 0, hides = 0, activations = 0;
  bool eventFilter(QObject *, QEvent *event) override {
    shows += event->type() == QEvent::Show;
    hides += event->type() == QEvent::Hide;
    activations += event->type() == QEvent::WindowActivate;
    return false;
  }
};
class CatalogFixture {
public:
  QTemporaryDir root;
  UserTemplateService *service = UserTemplateService::getInstance();
  QString global = service->globalTemplateLocation();
  QString workspace = service->workspaceTemplateLocation();
  CatalogFixture() {
    service->setGlobalTemplateFilePath(root.filePath("global.json"));
    service->setWorkspaceTemplateFilePath(root.filePath("workspace.json"));
  }
  ~CatalogFixture() {
    service->setGlobalTemplateFilePath(global);
    service->setWorkspaceTemplateFilePath(workspace);
  }
  GlobalControlQueryContext context() const {
    GlobalControlQueryContext result;
    result.editorAvailable = result.simulationCommands = true;
    result.fileName = root.filePath("tb.sv");
    result.moduleName = "tb";
    result.documentText =
        "module tb; int hits; initial begin\n  \nend endmodule\n";
    result.cursorPosition = result.documentText.indexOf("  \n") + 2;
    result.replacementStart = result.cursorPosition;
    result.templateContent = "statements";
    return result;
  }
};
QList<UserTemplateRecord> rankedCatalogRecords(int fillers) {
  QList<UserTemplateRecord> records;
  const auto append = [&](const QString &token, int value) {
    UserTemplateRecord record;
    record.id = token.mid(1);
    record.commandToken = record.label = token;
    record.insertText = QString("hits += %1;\n").arg(value);
    record.command = {{"kind", "snippet"}, {"content", "statements"},
                      {"placement", "statement"}};
    records << record;
  };
  for (int i = 0; i < fillers; ++i) {
    append(QString("@filler_%1").arg(i), 1);
    records.last().description = "Description mentions @needle";
  }
  append("@needle_b", 2);
  append("@needle_a", 3);
  append("@needle", 77);
  records.last().label = "Exact needle template";
  return records;
}
QList<UserTemplateRecord> mixedCatalogRecords(int declarations = 125,
                                             int statements = 1) {
  QList<UserTemplateRecord> records;
  for (int i = 0; i < declarations + statements; ++i) {
    const bool declaration = i < declarations;
    UserTemplateRecord record;
    record.id = declaration ? QString("mix_decl_%1").arg(i, 3, 10, QLatin1Char('0'))
                            : QString("mix_stmt_%1").arg(i - declarations, 3, 10, QLatin1Char('0'));
    record.commandToken = record.label = "@" + record.id;
    record.insertText = declaration ? QString("logic value_%1;\n").arg(i) : QString("hits += 2;\n");
    record.command = {{"kind", "snippet"},
                      {"content", declaration ? "declarations" : "statements"},
                      {"placement", declaration ? "module" : "statement"}};
    records << record;
  }
  return records;
}
class Fixture {
public:
  QTemporaryDir root;
  MainWindow window;
  MyCodeEditor *editor = nullptr;
  QString path;
  Fixture() {
    path = root.filePath("tb.sv");
    put(path, "module tb;\n  int hits = 0;\n\nendmodule\n");
    window.resize(1180, 850);
    window.show();
    window.activateWindow();
    window.workspaceSessionCoordinator->setRestoreOnActivation(false);
    window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(
        false);
    window.workspaceSessionCoordinator->openWorkspace(root.path());
    window.tabManager->openFileInTab(path);
    editor = window.tabManager->getCurrentEditor();
    cursor(editor, editor->toPlainText().indexOf("\nendmodule"));
    editor->setFocus();
  }
  ~Fixture() {
    for (auto *view : window.tabManager->openEditors())
      view->document()->setModified(false);
  }
};
} // namespace

class TemplateAuthoringTest : public QObject {
  Q_OBJECT
  void insert(Fixture &f, const QString &token, bool prefix = false) {
    auto *editor = f.editor;
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    const int count = editor->templateModel()->instances().size();
    qInfo() << "insert" << token << "prefix" << prefix << "editing"
            << editor->templateInputEditing() << "empty"
            << editor->templateCodeSegmentEmpty() << "selected"
            << editor->textCursor().selectedText() << "slot"
            << editor->templateSlotModeActiveIndex() << "position"
            << editor->textCursor().position();
    if (prefix)
      type("@" + token);
    else {
      QTest::keyPress(editor, Qt::Key_F24);
      type(token);
      QTest::keyRelease(QApplication::focusWidget(), Qt::Key_F24);
    }
    auto *panel = f.window.globalControlCoordinator->panel.get();
    if (!panel->isVisible())
      evidence("missing-" + token + ".json",
               QJsonDocument(
                   QJsonObject{
                       {"text", editor->toPlainText()},
                       {"model", editor->templateModel()->serialize(f.path)}})
                   .toJson());
    QTRY_VERIFY(panel->isVisible());
    auto *list = panel->findChild<QListWidget *>("globalControlResultList");
    QVERIFY(list && list->currentItem());
    if (list->currentItem()->text() != "@" + token)
      for (int i = 0; i < list->count(); ++i)
        qInfo() << "candidate" << list->item(i)->text()
                << list->item(i)->toolTip() << list->item(i)->flags();
    QCOMPARE(list->currentItem()->text(), "@" + token);
    QSignalSpy messages(f.window.simulationCommands.get(),
                        &SimulationCommandService::message);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
    QVERIFY2(editor->templateModel()->instances().size() == count + 1,
             qPrintable(messages.last().first().toString()));
    QVERIFY(editor->templateAuthoringLocked());
    QTRY_VERIFY(editor->hasFocus());
  }
private slots:
  void compactPopupGeometry_data() {
    QTest::addColumn<int>("edge");
    QTest::addColumn<bool>("largeFont");
    for (int edge = 0; edge < 3; ++edge)
      for (bool large : {false, true})
        QTest::newRow(qPrintable(QString("edge-%1-%2").arg(edge).arg(large ? "large" : "normal"))) << edge << large;
  }
  void compactPopupGeometry() {
    QFETCH(int, edge);
    QFETCH(bool, largeFont);
    QWidget owner;
    owner.resize(900, 600);
    owner.show();
    owner.activateWindow();
    GlobalControlPanel panel(&owner);
    panel.ensurePolished();
    auto *search = panel.findChild<QLineEdit *>("globalControlSearchEdit");
    auto *list = panel.findChild<QListWidget *>("globalControlResultList");
    auto *notice = panel.findChild<QLabel *>("globalControlNotice");
    panel.setSimulationOnly(true);
    panel.setCategory(GlobalControlCategory::Templates);
    QList<GlobalControlItem> pool;
    for (int i = 0; i < 14; ++i) {
      GlobalControlItem item;
      item.id = item.title = QString("@choice_%1").arg(i);
      item.parameterSummary = "signal · value";
      pool << item;
    }
    const QString reason = QString("This template does not match the container content type. ")
        + "Select a procedural code container or use a declaration template at a module declaration position. "
          "The current selection remains unchanged.";
    panel.setQueryChangedHandler([&](const QString &query) {
      if (query == "one") panel.setItems({pool.at(4)});
      else if (query == "none") panel.setItems({}, reason);
      else if (query == "few") panel.setItems(pool.mid(0, 3));
      else panel.setItems(pool);
    });
    panel.setItems(pool);
    const auto bounds = owner.screen()->availableGeometry();
    const QPoint anchor(bounds.right() - 20,
        edge == 0 ? bounds.top() + 12 : edge == 1 ? bounds.bottom() - 12 : bounds.center().y());
    panel.showAt(&owner, anchor);
    QTRY_VERIFY(search->hasFocus());
    if (largeFont) {
      list->setStyleSheet("QListWidget { font-size: 24px; }");
      notice->setStyleSheet("QLabel { font-size: 24px; }");
      panel.setItems(pool);
      QCoreApplication::processEvents();
    }
    if (largeFont) QVERIFY(list->visualItemRect(list->item(0)).height() > 30);
    QVERIFY(bounds.contains(panel.geometry()));
    QVERIFY(panel.rect().contains(search->geometry()));
    QVERIFY(panel.rect().contains(list->geometry()));
    QVERIFY(list->verticalScrollBar()->maximum() > 0);
    int visibleRows = 0;
    for (int i = 0; i < list->count(); ++i)
      visibleRows += list->viewport()->rect().contains(list->visualItemRect(list->item(i)));
    QVERIFY(visibleRows > 0 && visibleRows <= 8);
    const int fullHeight = panel.height();
    const int fullWidth = panel.width();
    qInfo() << "initial popup" << panel.geometry() << "list" << list->geometry()
            << "viewport" << list->viewport()->geometry() << "visible rows" << visibleRows;
    const bool above = panel.geometry().bottom() < anchor.y();
    for (int i = 0; i < 11; ++i) QTest::keyClick(search, Qt::Key_Down);
    QCOMPARE(list->currentRow(), 11);
    QTRY_VERIFY(list->viewport()->rect().contains(list->visualItemRect(list->currentItem())));
    QVERIFY(list->verticalScrollBar()->value() > 0);
    QTest::keyClick(search, Qt::Key_Up);
    QCOMPARE(list->currentRow(), 10);
    PopupLifecycleObserver lifecycle;
    panel.installEventFilter(&lifecycle);
    for (const auto &query : {QString("one"), QString("none"), QString("few"), QString("one")}) {
      search->selectAll();
      QTest::keyClicks(search, query);
      QTRY_VERIFY(search->hasFocus());
      QCOMPARE(search->text(), query);
      qInfo() << "filtered popup" << query << panel.geometry() << "list" << list->geometry()
              << "viewport" << list->viewport()->geometry();
      QVERIFY(panel.isVisible());
      QCOMPARE(panel.width(), fullWidth);
      QVERIFY(bounds.contains(panel.geometry()));
      QVERIFY(panel.rect().contains(search->geometry()));
      QCOMPARE(panel.geometry().bottom() < anchor.y(), above);
      if (query == "none") {
        QVERIFY(list->isHidden());
        QCOMPARE(list->count(), 0);
        QVERIFY(notice->isVisible());
        QVERIFY(panel.rect().contains(notice->geometry()));
        QVERIFY(notice->height() >= notice->heightForWidth(notice->width()));
        QVERIFY(notice->height() > notice->fontMetrics().height());
        QTest::keyClick(search, Qt::Key_Return);
        QVERIFY(panel.isVisible());
      } else {
        QCOMPARE(list->count(), query == "few" ? 3 : 1);
        QVERIFY(!list->isHidden());
        QVERIFY(panel.rect().contains(list->geometry()));
        QVERIFY(notice->isHidden());
        QCOMPARE(list->currentRow(), 0);
        QVERIFY(list->viewport()->rect().contains(list->visualItemRect(list->item(list->count() - 1))));
        QVERIFY(panel.height() < fullHeight);
        QCOMPARE(list->verticalScrollBar()->maximum(), 0);
      }
      const auto tag = QString("popup-%1-%2").arg(QTest::currentDataTag(), query);
      const auto dir = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR");
      if (!dir.isEmpty()) panel.grab().save(QDir(dir).filePath(tag + ".png"));
      evidence(tag + ".json", QJsonDocument(QJsonObject{
          {"query", query}, {"count", list->count()}, {"width", panel.width()}, {"height", panel.height()},
          {"noticeHeight", notice->height()}, {"noticeRequiredHeight", notice->heightForWidth(notice->width())},
          {"listHeight", list->height()}, {"viewportHeight", list->viewport()->height()},
          {"rowHeight", list->count() ? list->visualItemRect(list->item(0)).height() : 0},
          {"scale", qEnvironmentVariable("QT_SCALE_FACTOR")}, {"devicePixelRatio", panel.devicePixelRatioF()},
          {"focusPreserved", search->hasFocus()}}).toJson());
    }
    QCOMPARE(lifecycle.shows, 0);
    QCOMPARE(lifecycle.hides, 0);
    QCOMPARE(lifecycle.activations, 0);
    QString selected;
    panel.setItemActivatedHandler([&](const auto &item) { selected = item.id; });
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(selected, pool.at(4).id);
    QVERIFY(!panel.isVisible());
    panel.showAt(&owner, anchor);
    QTest::keyClick(search, Qt::Key_Escape);
    QVERIFY(!panel.isVisible());
  }
  void nonTemplatePopupSizing() {
    QWidget owner;
    owner.show();
    GlobalControlPanel panel(&owner);
    GlobalControlItem item;
    item.title = "one item";
    auto *list = panel.findChild<QListWidget *>("globalControlResultList");
    auto *search = panel.findChild<QLineEdit *>("globalControlSearchEdit");
    panel.setItems({item});
    panel.showCentered(&owner);
    const int ordinaryHeight = panel.height();
    QCOMPARE(list->minimumHeight(), 260);
    for (auto category : {GlobalControlCategory::Templates, GlobalControlCategory::Symbols,
                          GlobalControlCategory::Templates, GlobalControlCategory::Commands}) {
      panel.setCategory(category);
      panel.setItems({item});
      QTRY_VERIFY(search->hasFocus());
      if (category == GlobalControlCategory::Templates) QVERIFY(panel.height() < ordinaryHeight);
      else {
        QCOMPARE(list->minimumHeight(), 260);
        QCOMPARE(panel.height(), ordinaryHeight);
        panel.setItems({});
        QVERIFY(!list->isHidden());
        QCOMPARE(panel.height(), ordinaryHeight);
      }
    }
    panel.hide();
  }
  void codeNewlineMatchesOrdinary_data() {
    QTest::addColumn<QString>("body");
    QTest::addColumn<bool>("locked");
    const QList<QPair<QString, QString>> cases = {
        {"statement", "    hits = 1;|"},
        {"blank", "        |"},
        {"split", "    hits =| 1;"},
        {"tabs", "\thits = 1;|"},
        {"begin", "    if (hits) begin| end"},
        {"closing", "    if (hits) begin\n        hits++;\n        end|"},
        {"else", "    if (hits)\n        hits++;\n        else|"},
        {"native", "    @(posedge clk);|"}};
    for (const auto &entry : cases)
      for (bool locked : {false, true})
        QTest::newRow(qPrintable(entry.first + (locked ? "-locked" : "-unlocked")))
            << entry.second << locked;
  }
  void codeNewlineMatchesOrdinary() {
    QFETCH(QString, body);
    QFETCH(bool, locked);
    Fixture f;
    auto *e = f.editor;
    const QString prefix = "module tb;\n  initial begin\n";
    const int position = prefix.size() + body.indexOf('|');
    body.remove('|');
    const QString source = prefix + body + "\n  end\nendmodule\n";
    MyCodeEditor ordinary;
    ordinary.setPlainText(source);
    cursor(&ordinary, position);
    QTest::keyClick(&ordinary, Qt::Key_Return);
    e->setPlainText({});
    CodeTemplateSlotList inputs{{"body", int(prefix.size()), int(body.size()), 1,
                               true, {{"kind", "Code"}, {"content", "statements"}}}};
    TemplateInsertionSpec spec;
    spec.label = "newline fixture";
    spec.snapshot = {{"kind", "snippet"}};
    spec.snapshot = {{"kind", "snippet"}};
    spec.fragments = {{0, int(source.size()), false}};
    QVERIFY(e->insertSimulationTemplate(source, inputs, spec, {}, {}, nullptr));
    const auto id = e->selectedTemplateId();
    if (locked) {
      e->setTemplateAuthoringLocked(true);
      QVERIFY(e->editTemplateInstance(id));
    }
    cursor(e, position);
    const auto metadata = e->templateModel()->serialize(f.path);
    QTest::keyClick(e, Qt::Key_Return);
    evidence(QString("newline-%1.json").arg(QTest::currentDataTag()),
             QJsonDocument(QJsonObject{{"before", source}, {"actual", e->toPlainText()},
                                      {"ordinary", ordinary.toPlainText()},
                                      {"position", e->textCursor().position()},
                                      {"ordinaryPosition", ordinary.textCursor().position()}}).toJson());
    QCOMPARE(e->toPlainText(), ordinary.toPlainText());
    QCOMPARE(e->textCursor().position(), ordinary.textCursor().position());
    QCOMPARE(e->templateModel()->instances().size(), 1);
    const auto result = e->templateModel()->serialize(f.path);
    e->undo();
    QCOMPARE(e->toPlainText(), source);
    QCOMPARE(e->templateModel()->serialize(f.path), metadata);
    e->redo();
    QCOMPARE(e->toPlainText(), ordinary.toPlainText());
    QCOMPARE(e->templateModel()->serialize(f.path), result);
  }
  void indentedMixedWorkflow() {
    Fixture f;
    auto *e = f.editor;
    QTRY_VERIFY(e->hasFocus());
    lock(e);
    insert(f, "process");
    const auto parent = e->selectedTemplateId();
    const int parentColumn = e->textCursor().positionInBlock();
    QVERIFY(parentColumn > 0);
    type("hits = 1;\n");
    QCOMPARE(e->textCursor().positionInBlock(), parentColumn);
    QStringList children;
    for (int i = 0; i < 2; ++i) {
      insert(f, "repeat", true);
      const auto child = e->selectedTemplateId();
      children << child;
      const auto fragment = e->templateModel()->instance(child)->fragments.first();
      QCOMPARE(fragment.start - e->document()->findBlock(fragment.start).position(), parentColumn);
      type(QString::number(i + 2));
      QTest::keyClick(e, Qt::Key_Tab);
      const int childColumn = e->textCursor().positionInBlock();
      QVERIFY(childColumn > parentColumn);
      type(QString("hits += %1;\n").arg(i + 1));
      QCOMPARE(e->textCursor().positionInBlock(), childColumn);
      const auto before = e->toPlainText();
      const auto beforeData = e->templateModel()->serialize(f.path);
      returnToParentCode(e, parent);
      QCOMPARE(e->selectedTemplateId(), parent);
      QCOMPARE(e->textCursor().positionInBlock(), parentColumn);
      const auto continued = e->toPlainText();
      const auto continuedData = e->templateModel()->serialize(f.path);
      if (continued != before) {
        e->undo();
        QCOMPARE(e->toPlainText(), before);
        QCOMPARE(e->templateModel()->serialize(f.path), beforeData);
        e->redo();
        QCOMPARE(e->toPlainText(), continued);
        QCOMPARE(e->templateModel()->serialize(f.path), continuedData);
        const int position = e->textCursor().position();
        QVERIFY(e->editTemplateInstance(parent, {}, position));
      }
      type(i == 0 ? "hits += 10;\n" : "hits += 4;\n");
      QVERIFY(e->toPlainText().contains(i == 0 ? "  hits += 10;\n" : "  hits += 4;\n"));
    }
    const auto source = e->toPlainText();
    const auto metadata = e->templateModel()->serialize(f.path);
    // Returning again must not rewrite existing handwritten continuation or siblings.
    QVERIFY(e->editTemplateInstance(children.first()));
    QTest::keyClick(e, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(e->toPlainText(), source);
    QCOMPARE(e->templateModel()->serialize(f.path), metadata);
    const auto child = *e->templateModel()->instance(children.last());
    cursor(e, child.fragments.first().end() - 1);
    QTest::keyClick(e, Qt::Key_Return);
    QCOMPARE(e->toPlainText(), source);
    QVERIFY(f.window.tabManager->saveCurrentTab());
    e->setTemplateAuthoringLocked(false);
    QVERIFY(f.window.tabManager->closeAllTabs());
    QVERIFY(f.window.tabManager->openFileInTab(f.path));
    f.editor = e = f.window.tabManager->getCurrentEditor();
    QCOMPARE(e->toPlainText(), source);
    QCOMPARE(e->templateModel()->serialize(f.path), metadata);
    evidence("indented-mixed.sv", source.toUtf8());
    evidence("indented-mixed.json", QJsonDocument(metadata).toJson());
  }
  void codeNewlineProtectedEdges() {
    Fixture f;
    auto *e = f.editor;
    QTRY_VERIFY(e->hasFocus());
    const QString initial = "module tb;\ninitial begin\n  \nend\nendmodule\n";
    const int start = initial.indexOf("  \n") + 2;
    e->setPlainText({});
    CodeTemplateSlotList inputs{{"body", start, 0, 1, true, {{"kind", "Code"}}}};
    TemplateInsertionSpec spec;
    spec.label = "fixed indent fixture";
    spec.snapshot = {{"kind", "snippet"}};
    spec.fragments = {{0, int(initial.size()), false}};
    QVERIFY(e->insertSimulationTemplate(initial, inputs, spec, {}, {}, nullptr));
    e->setTemplateAuthoringLocked(true);
    QVERIFY(e->editTemplateInstance());
    const auto source = e->toPlainText();
    const auto metadata = e->templateModel()->serialize(f.path);
    const auto position = e->textCursor().position();
    const auto indent = e->textCursor().block().text().left(e->textCursor().positionInBlock());
    QTest::keyClick(e, Qt::Key_Return);
    QString expected = source;
    expected.insert(position, "\n" + indent);
    QCOMPARE(e->toPlainText(), expected); // fixed leading whitespace is retained
    e->undo();
    QCOMPARE(e->toPlainText(), source);
    QCOMPARE(e->templateModel()->serialize(f.path), metadata);
    QVERIFY(e->editTemplateInstance());
    cursor(e, position - 1, position + 1);
    QTest::keyClick(e, Qt::Key_Return);
    QCOMPARE(e->toPlainText(), source);
    cursor(e, position);
    e->setReadOnly(true);
    QTest::keyClick(e, Qt::Key_Return);
    QCOMPARE(e->toPlainText(), source);
    QCOMPARE(e->templateModel()->serialize(f.path), metadata);
  }
  void handwrittenGeneratedEndHistory() {
    Fixture f;
    auto *e = f.editor;
    QTRY_VERIFY(e->hasFocus());
    const QString prefix = "module tb;\n";
    const QString body = "  initial begin";
    const QString source = prefix + body + "\nendmodule\n";
    e->setPlainText({});
    CodeTemplateSlotList inputs{{"body", int(prefix.size()), int(body.size()), 1,
                                true, {{"kind", "Code"}}}};
    TemplateInsertionSpec spec;
    spec.label = "handwritten process";
    spec.snapshot = {{"kind", "snippet"}};
    spec.fragments = {{0, int(source.size()), false}};
    QVERIFY(e->insertSimulationTemplate(source, inputs, spec, {}, {}, nullptr));
    e->setTemplateAuthoringLocked(true);
    QVERIFY(e->editTemplateInstance());
    cursor(e, prefix.size() + body.size());
    QTest::keyClick(e, Qt::Key_Return);
    QVERIFY(e->toPlainText().contains("  initial begin\n      \n  end\n"));
    type("end");
    const auto before = e->toPlainText();
    const auto beforeData = e->templateModel()->serialize(f.path);
    QTest::keyClick(e, Qt::Key_Return);
    QCOMPARE(e->toPlainText().count("end\n"), 1);
    QVERIFY(e->toPlainText().contains("  initial begin\n  end\n"));
    const auto after = e->toPlainText();
    const auto afterData = e->templateModel()->serialize(f.path);
    e->undo();
    QCOMPARE(e->toPlainText(), before);
    QCOMPARE(e->templateModel()->serialize(f.path), beforeData);
    e->redo();
    QCOMPARE(e->toPlainText(), after);
    QCOMPARE(e->templateModel()->serialize(f.path), afterData);
  }
  void catalogRanksBeforeLimit_data() {
    QTest::addColumn<int>("fillers");
    QTest::addColumn<QString>("query");
    QTest::newRow("small") << 3 << QString("needle");
    QTest::newRow("large") << 125 << QString("needle");
    QTest::newRow("large-case") << 125 << QString(" NeEdLe ");
    QTest::newRow("large-at") << 125 << QString("@needle");
  }
  void catalogRanksBeforeLimit() {
    QFETCH(int, fillers);
    QFETCH(QString, query);
    CatalogFixture catalog;
    QVERIFY(catalog.service->setRecords(rankedCatalogRecords(fillers)).valid);
    auto context = catalog.context();
    context.documentInstance = 23;
    context.documentTextRevision = 7;
    const auto revision = catalog.service->catalogRevision();
    const auto items = EditorInsertPaletteService().query(
        GlobalControlCategory::Templates, query, context);
    QCOMPARE(items.size(), qMin(fillers + 3, 120));
    QCOMPARE(items.at(0).id, QString("@needle"));
    QCOMPARE(items.at(1).id, QString("@needle_b"));
    QCOMPARE(items.at(2).id, QString("@needle_a"));
    QJsonArray ids;
    for (int i = 0; i < items.size(); ++i) {
      if (i >= 3)
        QCOMPARE(items.at(i).id, QString("@filler_%1").arg(i - 3));
      QVERIFY(items.at(i).disabledReason.isEmpty());
      QCOMPARE(items.at(i).sourceTemplateCatalogRevision, revision);
      QCOMPARE(items.at(i).sourceDocumentInstance, context.documentInstance);
      QCOMPARE(items.at(i).sourceDocumentTextRevision,
               context.documentTextRevision);
      ids << items.at(i).id;
    }
    evidence(QString("catalog-%1.json").arg(QTest::currentDataTag()),
             QJsonDocument(QJsonObject{{"query", query}, {"fillers", fillers},
                                      {"ids", ids}}).toJson());
  }
  void catalogOverridesAndGuards() {
    CatalogFixture catalog;
    auto records = rankedCatalogRecords(125);
    records[0].label = "label_only_match";
    records[1].command.insert("symbol", "pkg::symbol_only_match");
    records[2].command.insert("aliases", QStringList{"alias_only_match"});
    QVERIFY(catalog.service->setRecords(records).valid);
    auto context = catalog.context();
    auto target = records.last();
    target.id = "workspace-target";
    target.insertText = "hits += 70;\n";
    QVERIFY(catalog.service->saveRecord(target, true).valid);
    target.id = "file-target";
    target.command.insert("file", context.fileName);
    target.insertText = "hits += 88;\n";
    QVERIFY(catalog.service->saveRecord(target, true).valid);
    EditorInsertPaletteService palette;
    auto items = palette.query(GlobalControlCategory::Templates, "needle", context);
    QCOMPARE(items.first().id, QString("@needle"));
    QCOMPARE(items.first().insertionText, QString("hits += 88;\n"));
    QCOMPARE(std::count_if(items.begin(), items.end(), [](const auto &item) {
      return item.id == "@needle";
    }), 1);
    context.fileName = catalog.root.filePath("other.sv");
    items = palette.query(GlobalControlCategory::Templates, "needle", context);
    QCOMPARE(items.first().insertionText, QString("hits += 70;\n"));
    const QStringList filters{"label_only_match", "symbol_only_match", "alias_only_match"};
    for (int i = 0; i < filters.size(); ++i) {
      const auto matched = palette.query(GlobalControlCategory::Templates, filters.at(i), context);
      QCOMPARE(matched.size(), 1);
      QCOMPARE(matched.first().id, QString("@filler_%1").arg(i));
    }
    const auto original = catalog.service->catalogForFile(context.fileName, true);
    items = palette.query(GlobalControlCategory::Templates, {}, context);
    QCOMPARE(items.size(), 120);
    int catalogIndex = 0;
    for (const auto &item : items) {
      while (catalogIndex < original.size() && original.at(catalogIndex).commandToken != item.id)
        ++catalogIndex;
      QVERIFY(catalogIndex < original.size());
      ++catalogIndex;
    }
    const auto valid = palette.query(GlobalControlCategory::Templates, "needle", context).first();
    context.templateContent = "declarations";
    QString notice;
    items = palette.query(GlobalControlCategory::Templates, "needle", context, &notice);
    QVERIFY(items.isEmpty());
    QVERIFY(notice.contains("@needle"));
    QVERIFY(notice.contains("content type"));
    SimulationCommandRequest request;
    request.context = context;
    request.item = valid;
    request.item.templateContent = context.templateContent;
    request.item.disabledReason.clear();
    const auto rejected = SimulationCommandPlanner::prepare(request);
    QVERIFY(!rejected.error.isEmpty());
    QVERIFY(rejected.text.isEmpty());
    context.templateContent = "statements";
    context.readOnly = true;
    items = palette.query(GlobalControlCategory::Templates, "needle", context);
    QCOMPARE(items.first().disabledReason, QString("This document is read-only."));
  }
  void applicableCandidatesBeforeLimit() {
    CatalogFixture catalog;
    QVERIFY(catalog.service->setRecords(mixedCatalogRecords()).valid);
    auto context = catalog.context();
    EditorInsertPaletteService palette;
    QString notice;
    auto items = palette.query(GlobalControlCategory::Templates, "mix", context, &notice);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().id, QString("@mix_stmt_000"));
    QVERIFY(items.first().disabledReason.isEmpty());
    QVERIFY(notice.isEmpty());
    for (const auto &query : {QString("mix_decl"), QString("@mix_decl_124")}) {
      items = palette.query(GlobalControlCategory::Templates, query, context, &notice);
      QVERIFY(items.isEmpty());
      QVERIFY(notice.contains("content type"));
      if (query.startsWith('@')) QVERIFY(notice.startsWith(query));
      else QVERIFY(notice.startsWith("No applicable"));
    }
    items = palette.query(GlobalControlCategory::Templates, "missing_token", context, &notice);
    QVERIFY(items.isEmpty());
    QCOMPARE(notice, QString("No matching templates."));
    context.readOnly = true;
    items = palette.query(GlobalControlCategory::Templates, "mix", context, &notice);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().disabledReason, QString("This document is read-only."));
    context.readOnly = false;
    context.templateContent.clear();
    items = palette.query(GlobalControlCategory::Templates, "mix", context);
    QCOMPARE(items.size(), 1); // Syntax scope is checked without a container hint.
    context.documentText = "module other;\n  \nendmodule\n";
    context.fileName = catalog.root.filePath("other.sv");
    context.cursorPosition = context.replacementStart = context.documentText.indexOf("  \n") + 2;
    context.templateContent = "declarations";
    items = palette.query(GlobalControlCategory::Templates, "mix", context, &notice);
    QCOMPARE(items.size(), 120);
    for (int i = 0; i < items.size(); ++i)
      QCOMPARE(items.at(i).id, QString("@mix_decl_%1").arg(i, 3, 10, QLatin1Char('0')));
    items = palette.query(GlobalControlCategory::Templates, "mix_decl_124", context);
    QCOMPARE(items.size(), 1);
    context = catalog.context();
    items = palette.query(GlobalControlCategory::Templates, "mix", context);
    QCOMPARE(items.size(), 1);
    auto records = mixedCatalogRecords(125, 125);
    records[0].description = "mentions mix_stmt_124";
    QVERIFY(catalog.service->setRecords(records).valid);
    items = palette.query(GlobalControlCategory::Templates, "mix", context);
    QCOMPARE(items.size(), 120);
    for (int i = 0; i < items.size(); ++i)
      QCOMPARE(items.at(i).id, QString("@mix_stmt_%1").arg(i, 3, 10, QLatin1Char('0')));
    items = palette.query(GlobalControlCategory::Templates, "mix_stmt_124", context);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().id, QString("@mix_stmt_124"));
  }
  void applicabilityRefreshAndStatus() {
    CatalogFixture catalog;
    auto records = mixedCatalogRecords(1, 1);
    QVERIFY(catalog.service->setRecords(records).valid);
    auto context = catalog.context();
    EditorInsertPaletteService palette;
    auto items = palette.query(GlobalControlCategory::Templates, "mix", context);
    const auto revision = items.first().sourceTemplateCatalogRevision;
    auto override = records.first();
    override.id = "workspace-override";
    override.command = records.last().command;
    override.insertText = "hits += 3;\n";
    QVERIFY(catalog.service->saveRecord(override, true).valid);
    items = palette.query(GlobalControlCategory::Templates, "mix_decl_000", context);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().insertionText, override.insertText);
    QVERIFY(items.first().sourceTemplateCatalogRevision != revision);
    override.id = "file-override";
    override.command = records.first().command;
    override.command.insert("file", context.fileName);
    override.insertText = records.first().insertText;
    QVERIFY(catalog.service->saveRecord(override, true).valid);
    QString notice;
    QVERIFY(palette.query(GlobalControlCategory::Templates, "mix_decl_000", context, &notice).isEmpty());
    QVERIFY(!notice.isEmpty());
    context.fileName = catalog.root.filePath("second.sv");
    QCOMPARE(palette.query(GlobalControlCategory::Templates, "mix_decl_000", context).size(), 1);
    using Status = SimulationPlacementStatus;
    context = catalog.context();
    const QVariantMap command{{"kind", "snippet"}, {"content", "statements"}, {"placement", "statement"}};
    QCOMPARE(SimulationCommandPlanner::placement(context, command, "statements", "hits++;").status,
             Status::Applicable);
    context.readOnly = true;
    QCOMPARE(SimulationCommandPlanner::placement(context, command, "statements", "hits++;").status,
             Status::Unavailable);
    QCOMPARE(SimulationCommandPlanner::placement(context, records.first().command, "statements", records.first().insertText).status,
             Status::Incompatible);
    context.readOnly = false;
    context.replacementStart = context.documentText.size() + 1;
    QCOMPARE(SimulationCommandPlanner::placement(context, command, "statements", "hits++;").status,
             Status::Unavailable);
    context = catalog.context();
    context.templateBoundaryError = "Insert templates only in a code container or an unowned source gap.";
    QVERIFY(palette.query(GlobalControlCategory::Templates, "mix", context, &notice).isEmpty());
    QVERIFY(notice.contains("code container"));
  }
  void largeCatalogKeyboardInsertion_data() {
    QTest::addColumn<int>("entry");
    QTest::newRow("templates") << 0;
    QTest::newRow("f24-hold") << 1;
    QTest::newRow("f24-locked") << 2;
  }
  void mixedCatalogNestedKeyboard_data() {
    QTest::addColumn<int>("entry");
    QTest::newRow("ctrl-space-templates") << 0;
    QTest::newRow("ctrl-space-at-prefix") << 1;
    QTest::newRow("f24-hold") << 2;
    QTest::newRow("f24-locked-at-prefix") << 3;
  }
  void mixedCatalogNestedKeyboard() {
    QFETCH(int, entry);
    Fixture f;
    CatalogFixture catalog;
    auto *e = f.editor;
    QTRY_VERIFY(e->hasFocus());
    lock(e);
    insert(f, "process");
    const auto parent = e->selectedTemplateId();
    const int position = e->textCursor().selectionStart();
    QVERIFY(catalog.service->setRecords(mixedCatalogRecords()).valid);
    if (entry < 3) {
      e->setTemplateAuthoringLocked(false);
      cursor(e, position);
    }
    const auto original = e->toPlainText();
    if (entry == 0) {
      QTest::keyClick(e, Qt::Key_Space, Qt::ControlModifier);
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
      type("mix");
    } else if (entry == 1) {
      type("@mix");
      QTest::keyClick(e, Qt::Key_Space, Qt::ControlModifier);
    } else if (entry == 2) {
      QTest::keyPress(e, Qt::Key_F24);
      type("mix");
      QTest::keyRelease(QApplication::focusWidget(), Qt::Key_F24);
    } else {
      type("@mix");
    }
    auto *panel = f.window.globalControlCoordinator->panel.get();
    QTRY_VERIFY(panel->isVisible());
    auto *list = panel->findChild<QListWidget *>("globalControlResultList");
    QVERIFY(list && list->currentItem());
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->currentItem()->text(), QString("@mix_stmt_000"));
    QCOMPARE(panel->currentItems.first().templateParentId, parent);
    QSignalSpy messages(f.window.simulationCommands.get(), &SimulationCommandService::message);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
    QVERIFY2(e->templateModel()->instances().size() == 2, qPrintable(messages.last().first().toString()));
    const auto child = e->templateModel()->instance(e->selectedTemplateId());
    QVERIFY(child);
    QCOMPARE(child->parentId, parent);
    QCOMPARE(e->toPlainText().count("hits += 2;"), 1);
    QVERIFY(!e->toPlainText().contains("@mix"));
    e->undo();
    QCOMPARE(e->templateModel()->instances().size(), 1);
    QCOMPARE(e->toPlainText(), entry == 1 ? QString(original).insert(position, "@mix") : original);
  }
  void scalarCandidateFeedback() {
    Fixture f;
    CatalogFixture catalog;
    auto *e = f.editor;
    QTRY_VERIFY(e->hasFocus());
    lock(e);
    insert(f, "process");
    insert(f, "repeat", true);
    QVERIFY(catalog.service->setRecords(mixedCatalogRecords()).valid);
    const auto source = e->toPlainText();
    const auto metadata = e->templateModel()->serialize(f.path);
    QTest::keyPress(e, Qt::Key_F24);
    type("mix");
    QTest::keyRelease(QApplication::focusWidget(), Qt::Key_F24);
    auto *panel = f.window.globalControlCoordinator->panel.get();
    QTRY_VERIFY(panel->isVisible());
    auto *list = panel->findChild<QListWidget *>("globalControlResultList");
    auto *notice = panel->findChild<QLabel *>("globalControlNotice");
    QVERIFY(list && notice);
    QCOMPARE(list->count(), 0);
    QVERIFY(notice->isVisible());
    const auto boundary = f.window.globalControlCoordinator->currentContext.templateBoundaryError;
    QVERIFY(!boundary.isEmpty());
    QCOMPARE(notice->text(), "No applicable templates here. " + boundary);
    evidence("scalar-notice.txt", notice->text().toUtf8());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QVERIFY(panel->isVisible());
    QCOMPARE(e->toPlainText(), source);
    QCOMPARE(e->templateModel()->serialize(f.path), metadata);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
  }
  void dependencyUnavailableRemainsCandidate() {
    Fixture f;
    CatalogFixture catalog;
    auto *e = f.editor;
    QTRY_VERIFY(e->hasFocus());
    lock(e);
    insert(f, "process");
    auto records = mixedCatalogRecords();
    records.last().command.insert("compileOrder", QStringList{f.root.filePath("absent-package.sv")});
    QVERIFY(catalog.service->setRecords(records).valid);
    const auto source = e->toPlainText();
    const auto metadata = e->templateModel()->serialize(f.path);
    type("@mix");
    auto *panel = f.window.globalControlCoordinator->panel.get();
    QTRY_VERIFY(panel->isVisible());
    QCOMPARE(panel->currentItems.size(), 1);
    QVERIFY(panel->currentItems.first().disabledReason.isEmpty());
    QSignalSpy messages(f.window.simulationCommands.get(), &SimulationCommandService::message);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QTRY_VERIFY(!messages.isEmpty());
    QVERIFY(messages.last().first().toString().contains("package input is missing"));
    QCOMPARE(e->toPlainText(), source);
    QCOMPARE(e->templateModel()->serialize(f.path), metadata);
  }
  void largeCatalogKeyboardInsertion() {
    QFETCH(int, entry);
    Fixture f;
    CatalogFixture catalog;
    QVERIFY(catalog.service->setRecords(rankedCatalogRecords(125)).valid);
    auto *editor = f.editor;
    editor->setPlainText("module tb; int hits = 0; initial begin\n  \nend\nendmodule\n");
    cursor(editor, editor->toPlainText().indexOf("  \n") + 2);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    const auto original = editor->toPlainText();
    if (entry == 0) {
      QTest::keyClick(editor, Qt::Key_Space, Qt::ControlModifier);
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    } else {
      if (entry == 2)
        lock(editor);
      QTest::keyPress(editor, Qt::Key_F24);
    }
    type("@needle");
    if (entry != 0)
      QTest::keyRelease(QApplication::focusWidget(), Qt::Key_F24);
    auto *panel = f.window.globalControlCoordinator->panel.get();
    QVERIFY(panel->isVisible());
    auto *list = panel->findChild<QListWidget *>("globalControlResultList");
    QVERIFY(list && list->currentItem());
    QCOMPARE(list->count(), 120);
    QCOMPARE(list->currentItem()->text(), QString("Exact needle template"));
    QCOMPARE(editor->toPlainText(), original);
    QSignalSpy messages(f.window.simulationCommands.get(), &SimulationCommandService::message);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
    QCOMPARE(editor->templateModel()->instances().size(), 1);
    QCOMPARE(editor->templateModel()->instances().first().label, QString("Exact needle template"));
    QCOMPARE(editor->toPlainText().count("hits += 77;"), 1);
    QVERIFY(!editor->toPlainText().contains("hits += 1;"));
    QCOMPARE(editor->templateAuthoringLocked(), entry == 2);
    evidence(QString("catalog-keyboard-%1.sv").arg(QTest::currentDataTag()),
             editor->toPlainText().toUtf8());
  }
  void nativeSyntax_data() {
    QTest::addColumn<QString>("text");
    for (const auto &value :
         {"@(posedge clk)", "@ (negedge clk)", "@*", "@(*)", "@event_name;"})
      QTest::newRow(value) << QString(value);
  }
  void nativeSyntax() {
    QFETCH(QString, text);
    QVERIFY(SimulationCommandPrefix::nativeEventInput(text));
  }
  void f24RepeatAndFocusBoundary() {
    Fixture f;
    auto* editor=f.editor;
    auto* coordinator=f.window.globalControlCoordinator.get();
    QTRY_VERIFY(editor->hasFocus());
    const auto original=editor->toPlainText();
    const int undo=editor->document()->availableUndoSteps();
    QTest::keyPress(editor,Qt::Key_F24);
    QVERIFY(coordinator->f24Held);
    QKeyEvent repeatedRelease(QEvent::KeyRelease,Qt::Key_F24,Qt::NoModifier,{},true);
    QApplication::sendEvent(editor,&repeatedRelease);
    QVERIFY(coordinator->f24Held);
    QTest::keyPress(editor,Qt::Key_Space);
    QVERIFY(editor->templateAuthoringLocked());
    QKeyEvent repeatedSpace(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier," ",true);
    QApplication::sendEvent(editor,&repeatedSpace);
    QVERIFY(editor->templateAuthoringLocked());
    QTest::keyRelease(editor,Qt::Key_Space);
    QTest::keyRelease(editor,Qt::Key_F24);
    QVERIFY(!coordinator->f24Held);
    lock(editor);
    QVERIFY(!editor->templateAuthoringLocked());
    QCOMPARE(editor->toPlainText(),original);
    QCOMPARE(editor->document()->availableUndoSteps(),undo);
    QDialog dialog(&f.window);
    QLineEdit field(&dialog);
    dialog.show();field.setFocus();QTRY_VERIFY(field.hasFocus());
    QTest::keyPress(&field,Qt::Key_F24);
    QTest::keyClick(&field,Qt::Key_Space);
    QTest::keyRelease(&field,Qt::Key_F24);
    QVERIFY(!coordinator->f24Held);
    QVERIFY(!editor->templateAuthoringLocked());
    QVERIFY(!coordinator->panel->isVisible());
    QCOMPARE(field.text(),QString(" "));
  }
  void lifecycleAndProtection() {
    Fixture f;
    auto *editor = f.editor;
    QTRY_VERIFY(editor->hasFocus());
    const auto original = editor->toPlainText();
    const int undo = editor->document()->availableUndoSteps();
    QTest::keyClick(editor, Qt::Key_F24);
    QCOMPARE(editor->toPlainText(), original);
    QTest::keyPress(editor, Qt::Key_F24);
    type("d");
    QTest::keyRelease(QApplication::focusWidget(), Qt::Key_F24);
    QVERIFY(f.window.globalControlCoordinator->panel->isVisible());
    QCOMPARE(editor->toPlainText(), original);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    editor->setFocus();
    QTest::keyPress(editor, Qt::Key_F24);
    type("rep");
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QVERIFY(editor->templateAuthoringLocked());
    QVERIFY(!f.window.globalControlCoordinator->panel->isVisible());
    QTest::keyRelease(editor, Qt::Key_F24);
    QTest::keyClick(editor, Qt::Key_Escape);
    QVERIFY(editor->templateAuthoringLocked());
    QCOMPARE(editor->document()->availableUndoSteps(), undo);
    insert(f, "process");
    QVERIFY(editor->templateInputEditing());
    type("hits += 1;\n");
    QVERIFY(editor->toPlainText().contains("hits += 1;"));
    const auto before = editor->toPlainText();
    cursor(editor, 0, before.size());
    QTest::keyClick(editor, Qt::Key_Delete);
    QCOMPARE(editor->toPlainText(), before);
    QApplication::clipboard()->setText("CORRUPTION");
    editor->paste();
    editor->cut();
    QCOMPARE(editor->toPlainText(), before);
    QInputMethodEvent ime;
    ime.setCommitString("CORRUPTION");
    QApplication::sendEvent(editor, &ime);
    QCOMPARE(editor->toPlainText(), before);
    editor->replaceAllText("initial", "always", true);
    QCOMPARE(editor->toPlainText(), before);
    QVERIFY(!editor->insertCompletionText("CORRUPTION", -1, 0, {}));
    QCOMPARE(editor->toPlainText(), before);
    editor->editTemplateInstance();
    QApplication::clipboard()->setText("hits += 2;");
    if (QApplication::clipboard()->text() != "hits += 2;") {
      QTest::qWait(100);
      QApplication::clipboard()->setText("hits += 2;");
    }
    QTRY_COMPARE(QApplication::clipboard()->text(), QString("hits += 2;"));
    editor->paste();
    QVERIFY(editor->toPlainText().contains("hits += 2;"));
    QInputMethodEvent commit;
    commit.setCommitString(" // input");
    QApplication::sendEvent(editor, &commit);
    QVERIFY(editor->toPlainText().contains("// input"));
    lock(editor);
    QVERIFY(!editor->templateAuthoringLocked());
    cursor(editor, 0);
    type("// free\n");
    QVERIFY(editor->toPlainText().startsWith("// free"));
    lock(editor);
    QTest::keyClick(editor, Qt::Key_Escape);
    QVERIFY(editor->templateAuthoringLocked());
  }
  void mixedNestingHistoryAndPersistence() {
    Fixture f;
    auto *editor = f.editor;
    QTRY_VERIFY(editor->hasFocus());
    UserTemplateRecord step;
    step.id = "step";
    step.commandToken = step.label = "@step";
    step.insertText = "hits += 1; $display(\"%0d\", 1);\n";
    step.command = {{"kind", "snippet"}, {"content", "statements"}};
    step.templateSlots = {
        {"count", 8, 1, 1, true, {{"kind", "Value"}, {"integer", true}}},
        {"count",
         int(step.insertText.lastIndexOf('1')),
         1,
         1,
         true,
         {{"kind", "Value"}, {"integer", true}}}};
    const auto saved =
        UserTemplateService::getInstance()->saveRecord(step, true);
    QVERIFY2(saved.valid, qPrintable(saved.failureReason));
    lock(editor);
    insert(f, "process");
    const auto process = editor->selectedTemplateId();
    type("  // handwritten before\n  ");
    insert(f, "repeat", true);
    const auto outer = editor->selectedTemplateId();
    type("2");
    QTest::keyClick(editor, Qt::Key_Tab);
    insert(f, "repeat", true);
    const auto inner = editor->selectedTemplateId();
    type("2");
    QTest::keyClick(editor, Qt::Key_Tab);
    insert(f, "step", true);
    const auto leaf = editor->selectedTemplateId();
    type("12");
    const auto *leafValue = editor->templateModel()->instance(leaf);
    QVERIFY(leafValue);
    QCOMPARE(editor->templateModel()->depth(leaf), 3);
    for (const auto &slot : leafValue->inputs)
      QCOMPARE(editor->toPlainText().mid(slot.start, slot.length),
               QString("12"));
    const auto *outerValue = editor->templateModel()->instance(outer);
    QCOMPARE(editor->toPlainText().mid(outerValue->inputs.first().start,
                                       outerValue->inputs.first().length),
             QString("2"));
    returnToParentCode(editor, inner);
    QCOMPARE(editor->selectedTemplateId(), inner);
    QVERIFY(editor->templateInputEditing());
    type("\n  // inner tail\n");
    returnToParentCode(editor, outer);
    QCOMPARE(editor->selectedTemplateId(), outer);
    type("\n hits += 1;\n");
    returnToParentCode(editor, process);
    QCOMPARE(editor->selectedTemplateId(), process);
    type("\n hits += 3;\n");
    insert(f, "step", true);
    const auto sibling = editor->selectedTemplateId();
    type("5");
    returnToParentCode(editor, process);
    QCOMPARE(editor->selectedTemplateId(), process);
    const auto mixed = editor->toPlainText();
    const auto metadata = editor->templateModel()->serialize(f.path);
    const auto child = *editor->templateModel()->instance(leaf);
    cursor(editor, child.fragments.first().start,
           child.fragments.first().end());
    type("forbidden");
    QCOMPARE(editor->toPlainText(), mixed);
    editor->undo();
    editor->redo();
    QCOMPARE(editor->toPlainText(), mixed);
    QCOMPARE(editor->templateModel()->serialize(f.path), metadata);
    QVERIFY(f.window.tabManager->saveCurrentTab());
    const auto serialized = editor->templateModel()->serialize(f.path);
    auto *shared = f.window.tabManager->sharedDocumentForEditor(editor);
    MyCodeEditor other;
    shared->attachView(&other);
    QCOMPARE(other.templateModel(), editor->templateModel());
    other.setTemplateAuthoringLocked(true);
    QVERIFY(other.editTemplateInstance(sibling));
    type("");
    QVERIFY(other.replaceTemplateInput(other.textCursor(), "7"));
    QVERIFY(editor->toPlainText().contains("hits += 7;"));
    other.undo();
    QCOMPARE(editor->toPlainText(), mixed);
    shared->detachView(&other);
    editor->setTemplateAuthoringLocked(false);
    QVERIFY(f.window.tabManager->closeAllTabs());
    QVERIFY(f.window.tabManager->openFileInTab(f.path));
    f.editor = editor = f.window.tabManager->getCurrentEditor();
    QCOMPARE(editor->templateModel()->serialize(f.path), serialized);
    editor->setTemplateAuthoringLocked(true);
    QVERIFY(editor->editTemplateInstance(process));
    QCoreApplication::processEvents();
    QVERIFY(editor->editorModeSnapshot().displayText.contains("@process"));
    QVERIFY(f.window.findChild<QLabel *>("editorTemplateModeStatus")
                ->text()
                .contains("@process"));
    evidence("mixed.sv", editor->toPlainText().toUtf8());
    evidence("mixed-templates.json", QJsonDocument(serialized).toJson());
    const auto output = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR");
    if (!output.isEmpty())
      f.window.grab().save(QDir(output).filePath("mixed.png"));
    ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
    QCoreApplication::processEvents();
    if (!output.isEmpty()) f.window.grab().save(QDir(output).filePath("mixed-dark.png"));
    QCOMPARE(editor->templateModel()->serialize(f.path), serialized);
    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    auto invalid = serialized;
    invalid["textSha256"] = "stale";
    QVERIFY(!editor->templateModel()->restore(invalid, f.path));
    QCOMPARE(editor->toPlainText(), mixed);
    QVERIFY(editor->templateModel()->instances().isEmpty());
    editor->setReadOnly(true);
    QVERIFY(!editor->editTemplateInstance(process));
  }
  void nativeInputInContainer_data() { nativeSyntax_data(); }
  void nativeInputInContainer() {
    QFETCH(QString, text);
    Fixture f;
    auto *editor = f.editor;
    QTRY_VERIFY(editor->hasFocus());
    lock(editor);
    insert(f, "process");
    const auto before = editor->toPlainText();
    const int position = editor->textCursor().position();
    if (text == "@event_name;") {
      UserTemplateRecord named;
      named.id = "event_name";
      named.label = named.commandToken = "@event_name";
      named.insertText = "hits += 9;";
      named.command = {{"kind", "snippet"}};
      QVERIFY(
          UserTemplateService::getInstance()->saveRecord(named, true).valid);
    }
    const auto raw = "  " + text;
    type(raw);
    QTRY_VERIFY(!f.window.globalControlCoordinator->panel->isVisible());
    auto expected = before;
    expected.insert(position, raw);
    QCOMPARE(editor->toPlainText(), expected);
    QCOMPARE(editor->textCursor().position(), position + raw.size());
    QCOMPARE(editor->templateModel()->instances().size(), 1);
  }
  void boundariesHistoryAndMetadata() {
    Fixture f;
    auto *e = f.editor;
    QTRY_VERIFY(e->hasFocus());
    const auto original = e->toPlainText();
    lock(e);
    insert(f, "process");
    const auto parent = e->selectedTemplateId();
    const auto parentText = e->toPlainText();
    const auto parentData = e->templateModel()->serialize(f.path);
    insert(f, "repeat", true);
    const auto child = e->selectedTemplateId();
    const auto childText = e->toPlainText();
    const auto childData = e->templateModel()->serialize(f.path);
    e->undo();
    QCOMPARE(e->toPlainText(), parentText);
    QCOMPARE(e->templateModel()->serialize(f.path), parentData);
    e->undo();
    QCOMPARE(e->toPlainText(), original);
    QVERIFY(e->templateModel()->instances().isEmpty());
    e->redo();
    QCOMPARE(e->toPlainText(), parentText);
    QCOMPARE(e->templateModel()->serialize(f.path), parentData);
    e->redo();
    QCOMPARE(e->toPlainText(), childText);
    QCOMPARE(e->templateModel()->serialize(f.path), childData);
    QVERIFY(e->editTemplateInstance(child));
    type("bad");
    QTest::keyClick(e, Qt::Key_Escape);
    QVERIFY(e->templateAuthoringLocked());
    QVERIFY(e->toPlainText().contains("repeat (bad)"));
    e->undo();
    QCOMPARE(e->toPlainText(),
             childText.left(childText.indexOf("repeat (")) + "repeat (ba)" +
                 childText.mid(childText.indexOf("repeat (") + 10));
    e->redo();
    const auto snapshot = e->templateModel()->serialize(f.path);
    const auto source = e->toPlainText();
    auto array = snapshot["instances"].toArray();
    auto value = array.first().toObject();
    auto inputs = value["inputs"].toArray();
    auto input = inputs.first().toObject();
    input["start"] = value["fragments"].toArray().first().toObject()["start"];
    input["length"] = value["fragments"].toArray().first().toObject()["length"];
    inputs[0] = input;
    value["inputs"] = inputs;
    array[0] = value;
    auto invalid = snapshot;
    invalid["instances"] = array;
    QVERIFY(!e->templateModel()->restore(invalid, f.path));
    QCOMPARE(e->toPlainText(), source);
    QVERIFY(e->templateModel()->restore(snapshot, f.path));
    invalid = snapshot;
    array = snapshot["instances"].toArray();
    value = array.last().toObject();
    value["parent"] = value["id"];
    array[1] = value;
    invalid["instances"] = array;
    QVERIFY(!e->templateModel()->restore(invalid, f.path));
    QCOMPARE(e->toPlainText(), source);
    QVERIFY(e->templateModel()->restore(snapshot, f.path));
    // Ordinary edits to fixed code invalidate ownership, and undo restores it.
    lock(e); QVERIFY(!e->templateAuthoringLocked());
    cursor(e, source.indexOf("initial"), source.indexOf("initial") + 7);
    e->insertPlainText("always");
    QVERIFY(e->templateModel()->instances().isEmpty());
    e->undo();
    QCOMPARE(e->templateModel()->serialize(f.path), snapshot);
    // External replacement with absent metadata leaves the source intact and
    // unowned.
    e->setPlainText("module external; endmodule\n");
    QVERIFY(e->templateModel()->instances().isEmpty());
    lock(e);
    QVERIFY(!e->editTemplateInstance(parent));
    QCOMPARE(e->toPlainText(), QString("module external; endmodule\n"));
  }
  void contentTypesAndFreshSegments() {
    Fixture f;
    auto *e = f.editor;
    QTRY_VERIFY(e->hasFocus());
    UserTemplateRecord declaration;
    declaration.id = "decl";
    declaration.commandToken = declaration.label = "@decl";
    declaration.insertText = "logic user_signal;\n";
    declaration.command = {{"kind", "snippet"}};
    QVERIFY(UserTemplateService::getInstance()
                ->saveRecord(declaration, true)
                .valid);
    lock(e);
    insert(f, "definitions");
    const auto definitions = e->selectedTemplateId();
    insert(f, "decl", true);
    returnToParentCode(e, definitions);
    QCOMPARE(e->selectedTemplateId(), definitions);
    type("parameter int N = 2;\n");
    QVERIFY(e->toPlainText().contains("parameter int N = 2;"));
    GlobalControlQueryContext context;
    context.documentText = e->toPlainText();
    context.cursorPosition = e->textCursor().position();
    context.replacementStart = context.cursorPosition;
    QVERIFY(!SimulationCommandPlanner::placementError(
                 context, {{"kind", "snippet"}}, "declarations",
                 "initial begin end")
                 .isEmpty());
    QVERIFY(
        SimulationCommandPlanner::placementError(
            context, {{"kind", "snippet"}}, "declarations",
            "typedef logic [3:0] word_t;\nparameter int X = 1;\nwire value;")
            .isEmpty());
    e->finishTemplateInput();
    e->finishTemplateInput();
    e->finishTemplateInput();
    cursor(e, e->toPlainText().indexOf("\nendmodule"));
    insert(f, "process");
    const int start = e->textCursor().position();
    type("hits += 4;");
    cursor(e, start, e->textCursor().position());
    QTest::keyClick(e, Qt::Key_Backspace);
    QVERIFY(e->templateCodeSegmentEmpty());
    insert(f, "repeat", true);
    const auto loop = e->selectedTemplateId();
    // A scalar cannot acquire its ancestor's Code permission.
    QString parent, name;
    e->templateInsertionContext(&parent, &name);
    QVERIFY(parent.isEmpty());
    TemplateInsertionSpec spec;
    QVERIFY(!e->simulationTemplateInsertionError(
                  e->textCursor().selectionStart(), 1, spec, {}, {})
                 .isEmpty());
    QTest::keyClick(e, Qt::Key_Tab);
    context.documentText = e->toPlainText();
    context.cursorPosition = context.replacementStart =
        e->textCursor().position();
    QVERIFY(!SimulationCommandPlanner::placementError(
                 context, {{"kind", "snippet"}}, "statements",
                 "logic invalid_here;")
                 .isEmpty());
    const auto before = e->toPlainText();
    const auto data = e->templateModel()->serialize(f.path);
    const auto second = f.root.filePath("second.sv");
    put(second, "module second; endmodule\n");
    QVERIFY(f.window.tabManager->openFileInTab(second));
    auto *other = f.window.tabManager->getCurrentEditor();
    QVERIFY(other != e);
    QVERIFY(!other->templateInputEditing());
    other->setTemplateAuthoringLocked(true);
    QVERIFY(!other->editTemplateInstance(loop));
    QVERIFY(f.window.tabManager->openFileInTab(f.path));
    QCOMPARE(f.window.tabManager->getCurrentEditor(), e);
    QCOMPARE(e->toPlainText(), before);
    QCOMPARE(e->templateModel()->serialize(f.path), data);
    QVERIFY(e->editTemplateInstance(loop));
  }
  void focusCompositionAndChoices() {
    Fixture f;
    auto *e = f.editor;
    QTRY_VERIFY(e->hasFocus());
    QTest::keyPress(e, Qt::Key_F24);
    QEvent deactivate(QEvent::ApplicationDeactivate);
    QApplication::sendEvent(qApp, &deactivate);
    type(" ");
    QVERIFY(!f.window.globalControlCoordinator->panel->isVisible());
    QTest::keyRelease(e, Qt::Key_F24);
    lock(e);
    insert(f, "process");
    const auto before = e->toPlainText();
    QInputMethodQueryEvent enabled(Qt::ImEnabled);
    QApplication::sendEvent(e, &enabled);
    QVERIFY(enabled.value(Qt::ImEnabled).toBool());
    QInputMethodEvent preedit(QString::fromUtf8("拼"), {});
    QApplication::sendEvent(e, &preedit);
    QCOMPARE(e->toPlainText(), before);
    QCOMPARE(e->textCursor().block().layout()->preeditAreaText(),
             QString::fromUtf8("拼"));
    QTest::keyClick(e, Qt::Key_Escape);
    QVERIFY(e->templateAuthoringLocked()); QVERIFY(e->templateInputEditing());
    QVERIFY(e->textCursor().block().layout()->preeditAreaText().isEmpty());
    QCOMPARE(e->toPlainText(), before);
    QApplication::sendEvent(e, &preedit);
    QInputMethodEvent commit;
    commit.setCommitString("// composition\n");
    QApplication::sendEvent(e, &commit);
    QVERIFY(e->toPlainText().contains("// composition"));
    UserTemplateRecord choice;
    choice.id = "pick";
    choice.commandToken = choice.label = "@pick";
    choice.insertText = "hits += 1;\n";
    choice.command = {{"kind", "snippet"}};
    choice.templateSlots = {
        {"count",
         8,
         1,
         1,
         true,
         {{"kind", "Choice"}, {"choices", QStringList{"1", "2", "3"}}}}};
    QVERIFY(UserTemplateService::getInstance()->saveRecord(choice, true).valid);
    insert(f, "pick", true);
    QTest::keyClick(e, Qt::Key_Down);
    QVERIFY(e->toPlainText().contains("hits += 2;"));
    QTest::keyClick(e, Qt::Key_Up);
    QVERIFY(e->toPlainText().contains("hits += 1;"));
    const auto id = e->selectedTemplateId();
    e->finishTemplateInput();
    e->finishTemplateInput();
    QTest::keyClick(e, Qt::Key_Return);
    QVERIFY(e->templateInputEditing());
    QTest::keyClick(e, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(e->selectedTemplateId(), id);
    QTest::keyClick(e, Qt::Key_Return);
    QVERIFY(e->templateInputEditing());
    QDialog dialog(&f.window);
    auto *field = new QLineEdit(&dialog);
    dialog.show();
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    type("ordinary");
    QCOMPARE(field->text(), QString("ordinary"));
    dialog.close();
    e->setFocus();
    QTRY_VERIFY(e->hasFocus());
    QTest::keyClick(e, Qt::Key_Escape);
    QVERIFY(e->templateAuthoringLocked());
  }
  void metadataSaveFailureAndRecovery() {
    Fixture f;
    auto *e = f.editor;
    QTRY_VERIFY(e->hasFocus());
    lock(e);
    insert(f, "process");
    type("hits += 1;\n");
    const auto source = e->toPlainText();
    const auto metadata = e->templateModel()->serialize(f.path);
    QLockFile busy(qEnvironmentVariable("ZEROSLACK_SESSION_STORAGE_PATH") +
                   ".write.lock");
    QVERIFY(busy.tryLock(0));
    QSignalSpy failures(f.window.tabManager.get(), &TabManager::fileSaveFailed);
    QVERIFY(!f.window.tabManager->saveCurrentTab());
    QVERIFY(!failures.isEmpty());
    QVERIFY(e->document()->isModified());
    QCOMPARE(e->toPlainText(), source);
    QCOMPARE(e->templateModel()->serialize(f.path), metadata);
    busy.unlock();
    QVERIFY2(f.window.tabManager->saveCurrentTab(),
             qPrintable(failures.last().at(1).toString()));
    QVERIFY(!e->document()->isModified());
    QString error;
    QCOMPARE(
        WorkspaceSessionStateService().loadDocumentTemplates(f.path, &error),
        metadata);
    QVERIFY(error.isEmpty());
  }
};
int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QStandardPaths::setTestModeEnabled(true);
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  QCoreApplication::setOrganizationName("ZeroSlackTemplateAuthoringTest");
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
  TemplateAuthoringTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "template_authoring_test.moc"
