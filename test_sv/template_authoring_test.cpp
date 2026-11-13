#include "applicationthememanager.h"
#include "documenttemplatemodel.h"
#include "editorfileidentity.h"
#include "mycodeeditor.h"
#include "simulationcommandplanner.h"
#include "simulationcommandservice.h"
#include "testuistyle.h"
#include "templatefixture.h"
#include "tsdocument.h"
#include "usertemplateservice.h"
#include <QtTest>
#include <QtWidgets>
#include <QScopeGuard>
#include <QElapsedTimer>
#include <QDeadlineTimer>
#include <cstdio>
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
    std::printf("KEY_TARGET_BEFORE_WAIT code=%u focus=%p active=%p\n",
                unsigned(character.unicode()), static_cast<void*>(QApplication::focusWidget()),
                static_cast<void*>(QApplication::activeWindow()));
    std::fflush(stdout);
    QTRY_VERIFY(QApplication::focusWidget() != nullptr);
    if (character == '\n')
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    else
      QTest::keyClicks(QApplication::focusWidget(), QString(character));
    QCoreApplication::processEvents();
  }
}
void cursor(MyCodeEditor *editor, int start, int end = -1) {
  QTextCursor position(editor->document());
  position.setPosition(start);
  if (end >= 0)
    position.setPosition(end, QTextCursor::KeepAnchor);
  editor->setTextCursor(position);
}
void returnToParentCode(MyCodeEditor* editor, const QString& parent) {
  editor->finishTemplateInput();
  const auto* item = editor->templateModel()->instance(parent);
  QVERIFY(item);
  for (int index = 0; index < item->inputs.size(); ++index) {
    const auto input = item->inputs.at(index);
    if (input.name != "body") continue;
    const int end = input.start + input.length;
    const auto firstLine = editor->document()->findBlock(input.start).text();
    int columns = 0;
    while (columns < firstLine.size() && (firstLine[columns] == ' ' || firstLine[columns] == '\t')) ++columns;
    const auto indent = firstLine.left(columns);
    QVERIFY(editor->editTemplateInstance(parent, input.name, end));
    const auto segment = editor->templateModel()->inputSegment(parent, index, end);
    QVERIFY(segment.first >= 0);
    QTextCursor target(editor->document()); target.setPosition(end);
    const int start = qMax(segment.first, target.block().position());
    const bool blank = editor->toPlainText().mid(start, end - start).trimmed().isEmpty();
    if (blank) target.setPosition(start, QTextCursor::KeepAnchor);
    const int insertion = target.selectionStart();
    const QString prefix = blank ? QString() : QString("\n");
    QVERIFY(editor->replaceTemplateInput(target, prefix + indent + "\n"));
    target.setPosition(insertion + prefix.size() + indent.size());
    editor->setTextCursor(target);
    return;
  }
  QFAIL("The parent Code input is missing");
}

class Fixture {
public:
  QTemporaryDir root;
  MainWindow window;
  MyCodeEditor *editor = nullptr;
  QString path;
  QObject focusContext;
  QElapsedTimer focusClock;
  QJsonArray focusEvents;
  int focusTraceId = 0;
  void recordFocus(const char* phase) {
    const auto label = [](QObject* object) {
      return object ? QString::fromLatin1(object->metaObject()->className()) + ":" + object->objectName()
                    : QStringLiteral("none");
    };
    focusEvents.append(QJsonObject{{"phase", QString::fromLatin1(phase)},
        {"microseconds", focusClock.nsecsElapsed() / 1000},
        {"visible", window.isVisible()}, {"active", window.isActiveWindow()},
        {"applicationActive", qApp->applicationState() == Qt::ApplicationActive},
        {"focusWindowIsOwner", window.windowHandle() && QGuiApplication::focusWindow() == window.windowHandle()},
        {"editorHasFocus", editor && editor->hasFocus()},
        {"focusWidget", label(QApplication::focusWidget())},
        {"activeWindow", label(QApplication::activeWindow())},
        {"activePopup", label(QApplication::activePopupWidget())}});
  }
  Fixture() {
    static int nextFocusTraceId = 0;
    focusTraceId = ++nextFocusTraceId;
    focusClock.start();
    QObject::connect(qApp, &QApplication::focusChanged, &focusContext,
                     [this](QWidget*, QWidget*) { recordFocus("focusChanged"); });
    QObject::connect(qApp, &QGuiApplication::applicationStateChanged, &focusContext,
                     [this](Qt::ApplicationState) { recordFocus("applicationStateChanged"); });
    recordFocus("before-setup");
    path = root.filePath("tb.sv");
    put(path, "module tb;\n  int hits = 0;\n\nendmodule\n");
    window.resize(1180, 850);
    window.show();
    if (window.windowHandle())
      QObject::connect(window.windowHandle(), &QWindow::activeChanged, &focusContext,
                       [this] { recordFocus("owner-activeChanged"); });
    window.activateWindow();
    recordFocus("after-initial-activation-request");
    window.workspaceSessionCoordinator->setRestoreOnActivation(false);
    window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(
        false);
    window.workspaceSessionCoordinator->openWorkspace(root.path());
    window.tabManager->openFileInTab(path);
    editor = window.tabManager->getCurrentEditor();
    cursor(editor, editor->toPlainText().indexOf("\nendmodule"));
    editor->setFocus();
    recordFocus("before-editor-focus-preparation");
    const bool focusReady = QTest::qWaitFor([&] {
      if (!window.isVisible() || window.isMinimized() || QApplication::activePopupWidget())
        return false;
      if (!window.isActiveWindow() || qApp->applicationState() != Qt::ApplicationActive
          || QGuiApplication::focusWindow() != window.windowHandle()) {
        recordFocus("owner-activation-request");
        window.raise();
        window.activateWindow();
        return false;
      }
      if (!editor->hasFocus()) {
        editor->setFocus();
        recordFocus("editor-focus-request");
      }
      return editor->hasFocus() && QApplication::focusWidget() == editor;
    }, QDeadlineTimer(QTest::Internal::defaultTryTimeout));
    recordFocus(focusReady ? "focus-preparation-complete" : "focus-preparation-failed");
    recordFocus("constructor-return");
  }
  ~Fixture() {
    recordFocus("fixture-destruction");
    const QJsonObject report{{"test", QString::fromLatin1(QTest::currentTestFunction())},
        {"data", QString::fromLatin1(QTest::currentDataTag())}, {"fixture", focusTraceId},
        {"failed", QTest::currentTestFailed()}, {"events", focusEvents}};
    std::printf("FIXTURE_FOCUS_TRACE %s\n",
                QJsonDocument(report).toJson(QJsonDocument::Compact).constData());
    std::fflush(stdout);
    const auto directory = qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
    if (!directory.isEmpty()) {
      QFile output(QDir(directory).filePath(QString("fixture-focus-%1.json").arg(focusTraceId)));
      if (output.open(QIODevice::WriteOnly)) output.write(QJsonDocument(report).toJson());
    }
    for (auto *view : window.tabManager->openEditors())
      view->document()->setModified(false);
  }
};
} // namespace

class TemplateInputTest : public QObject {
  Q_OBJECT
  void insert(Fixture &f, const QString &token) {
    insertTemplateFixture(f.editor, f.window.simulationCommands->currentRequest(), token);
  }
private slots:
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
    for (auto category : {static_cast<GlobalControlCategory>(1), GlobalControlCategory::Symbols,
                          GlobalControlCategory::Commands}) {
      panel.setCategory(category);
      panel.setItems({item});
      QTRY_VERIFY(search->hasFocus());
      {
        QCOMPARE(panel.category(), category == GlobalControlCategory::Commands
            ? GlobalControlCategory::Commands : GlobalControlCategory::Symbols);
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
      QTest::newRow(qPrintable(entry.first)) << entry.second;
  }
  void codeNewlineMatchesOrdinary() {
    QFETCH(QString, body);
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
    insert(f, "process");
    const auto parent = e->selectedTemplateId();
    const int parentColumn = e->textCursor().positionInBlock();
    QVERIFY(parentColumn > 0);
    type("hits = 1;\n");
    QCOMPARE(e->textCursor().positionInBlock(), parentColumn);
    QStringList children;
    for (int i = 0; i < 2; ++i) {
      insert(f, "repeat");
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
      const int continuationPosition = e->textCursor().position();
      if (continued != before) {
        e->undo();
        QCOMPARE(e->toPlainText(), before);
        QCOMPARE(e->templateModel()->serialize(f.path), beforeData);
        e->redo();
        QCOMPARE(e->toPlainText(), continued);
        QCOMPARE(e->templateModel()->serialize(f.path), continuedData);
        QVERIFY(e->editTemplateInstance(parent, {}, continuationPosition));
        QCOMPARE(e->textCursor().positionInBlock(), parentColumn);
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
    QVERIFY(f.window.tabManager->saveCurrentTab());
    
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
    QVERIFY(!e->replaceTemplateInput(e->textCursor(), "\n"));
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
  void nativeSyntax_data() {
    QTest::addColumn<QString>("text");
    for (const auto &value :
         {"@(posedge clk)", "@ (negedge clk)", "@*", "@(*)", "@event_name;"})
      QTest::newRow(value) << QString(value);
  }
  void nativeSyntax() {
    QFETCH(QString, text);
    Fixture f;
    f.editor->setPlainText("module tb; initial begin " + text + " end endmodule");
    cursor(f.editor, f.editor->toPlainText().indexOf(text) + text.size());
    const auto original = f.editor->toPlainText();
    f.editor->setFocus();
    QTRY_VERIFY(f.editor->hasFocus());
    QTest::keyClick(f.editor, Qt::Key_Space, Qt::ControlModifier);
    auto *panel = f.window.globalControlCoordinator->panel.get();
    QTRY_VERIFY(panel->isVisible());
    QCOMPARE(panel->category(), GlobalControlCategory::Symbols);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QCOMPARE(f.editor->toPlainText(), original);
  }
  void lifecycleAndProtection() {
    Fixture f;
    auto *editor = f.editor;
    QTRY_VERIFY(editor->hasFocus());
    const auto original = editor->toPlainText();
    const int undo = editor->document()->availableUndoSteps();
    QTest::keyClick(editor, Qt::Key_Space, Qt::ControlModifier);
    QVERIFY(f.window.globalControlCoordinator->panel->isVisible());
    QCOMPARE(editor->toPlainText(), original);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QTRY_VERIFY(editor->hasFocus());
    QCOMPARE(editor->document()->availableUndoSteps(), undo);
    insert(f, "process");
    QVERIFY(editor->templateInputEditing());
    type("hits += 1;\n");
    QVERIFY(editor->toPlainText().contains("hits += 1;"));
    const auto before = editor->toPlainText();
    editor->finishTemplateInput();
    editor->setReadOnly(true);
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
    editor->setReadOnly(false);
    QVERIFY(editor->editTemplateInstance());
    auto* clipboard = QApplication::clipboard();
    QElapsedTimer clipboardClock;
    clipboardClock.start();
    const QDeadlineTimer clipboardDeadline(QTest::Internal::defaultTryTimeout);
    QSignalSpy clipboardChanges(clipboard, &QClipboard::dataChanged);
    QJsonArray clipboardEvents;
    int clipboardWrites = 0;
    const auto recordClipboard = [&](const char* phase) {
      const auto text = clipboard->text();
      const QJsonObject event{{"phase", QString::fromLatin1(phase)},
          {"microseconds", clipboardClock.nsecsElapsed() / 1000},
          {"writeAttempts", clipboardWrites}, {"changeSignals", clipboardChanges.size()},
          {"ownsClipboard", clipboard->ownsClipboard()}, {"empty", text.isEmpty()},
          {"expectedMatches", text == "hits += 2;"}};
      clipboardEvents.append(event);
      std::printf("CLIPBOARD_PRECONDITION %s\n",
                  QJsonDocument(event).toJson(QJsonDocument::Compact).constData());
      std::fflush(stdout);
    };
    QTimer clipboardRetry;
    clipboardRetry.setInterval(50);
    const auto writeClipboard = [&] {
      ++clipboardWrites;
      clipboard->setText("hits += 2;");
      recordClipboard("write-return");
    };
    QObject::connect(&clipboardRetry, &QTimer::timeout, &clipboardRetry, [&] {
      const auto current = clipboard->text();
      if (clipboardDeadline.hasExpired()) {
        clipboardRetry.stop();
        recordClipboard("retry-budget-expired");
        return;
      }
      if (current != "hits += 2;")
        writeClipboard();
    });
    const auto recordClipboardResult = qScopeGuard([&] {
      clipboardRetry.stop();
      recordClipboard("scope-exit");
      const auto directory = qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
      if (!directory.isEmpty()) {
        QFile output(QDir(directory).filePath("clipboard-precondition.json"));
        if (output.open(QIODevice::WriteOnly))
          output.write(QJsonDocument(QJsonObject{{"events", clipboardEvents},
              {"platform", QGuiApplication::platformName()},
              {"testFailed", QTest::currentTestFailed()}}).toJson());
      }
    });
    // Retry only fixture seeding within the original assertion's wait budget.
    clipboardRetry.start();
    writeClipboard();
    QTRY_COMPARE(QApplication::clipboard()->text(), QString("hits += 2;"));
    clipboardRetry.stop();
    recordClipboard("ready-before-paste");
    editor->paste();
    QVERIFY(editor->toPlainText().contains("hits += 2;"));
    QInputMethodEvent commit;
    commit.setCommitString(" // input");
    QApplication::sendEvent(editor, &commit);
    QVERIFY(editor->toPlainText().contains("// input"));
    
    editor->finishTemplateInput();
    cursor(editor, 0);
    type("// free\n");
    QVERIFY(editor->toPlainText().startsWith("// free"));
    QTest::keyClick(editor, Qt::Key_Escape);
    
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
    insert(f, "process");
    const auto process = editor->selectedTemplateId();
    type("  // handwritten before\n  ");
    insert(f, "repeat");
    const auto outer = editor->selectedTemplateId();
    type("2");
    QTest::keyClick(editor, Qt::Key_Tab);
    insert(f, "repeat");
    const auto inner = editor->selectedTemplateId();
    type("2");
    QTest::keyClick(editor, Qt::Key_Tab);
    insert(f, "step");
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
    insert(f, "step");
    const auto sibling = editor->selectedTemplateId();
    type("5");
    returnToParentCode(editor, process);
    QCOMPARE(editor->selectedTemplateId(), process);
    const auto mixed = editor->toPlainText();
    const auto metadata = editor->templateModel()->serialize(f.path);
    const auto child = *editor->templateModel()->instance(leaf);
    cursor(editor, child.fragments.first().start,
           child.fragments.first().end());
    QVERIFY(!editor->replaceTemplateInput(editor->textCursor(), "forbidden"));
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
    
    QVERIFY(other.editTemplateInstance(sibling));
    type("");
    QVERIFY(other.replaceTemplateInput(other.textCursor(), "7"));
    QVERIFY(editor->toPlainText().contains("hits += 7;"));
    other.undo();
    QCOMPARE(editor->toPlainText(), mixed);
    shared->detachView(&other);
    
    QVERIFY(f.window.tabManager->closeAllTabs());
    QVERIFY(f.window.tabManager->openFileInTab(f.path));
    f.editor = editor = f.window.tabManager->getCurrentEditor();
    QCOMPARE(editor->templateModel()->serialize(f.path), serialized);
    
    QVERIFY(editor->editTemplateInstance(process));
    QCoreApplication::processEvents();
    QVERIFY(editor->templateInputFeedback().contains("@process"));
    auto* feedback = f.window.findChild<QLabel*>("templateInputStatus");
    QVERIFY(feedback && feedback->isVisible());
    QVERIFY(feedback->toolTip().contains("@process"));
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
    insert(f, "process");
    const auto parent = e->selectedTemplateId();
    const auto parentText = e->toPlainText();
    const auto parentData = e->templateModel()->serialize(f.path);
    insert(f, "repeat");
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
    e->finishTemplateInput();
    cursor(e, source.indexOf("initial"), source.indexOf("initial") + 7);
    e->insertPlainText("always");
    QVERIFY(e->templateModel()->instances().isEmpty());
    e->undo();
    QCOMPARE(e->templateModel()->serialize(f.path), snapshot);
    // External replacement with absent metadata leaves the source intact and
    // unowned.
    e->setPlainText("module external; endmodule\n");
    QVERIFY(e->templateModel()->instances().isEmpty());
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
    insert(f, "definitions");
    const auto definitions = e->selectedTemplateId();
    insert(f, "decl");
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
    const auto* processInput = e->templateModel()->instance(e->selectedTemplateId());
    QVERIFY(processInput && !processInput->inputs.isEmpty());
    const auto bodyInput = processInput->inputs.first();
    QVERIFY(e->toPlainText().mid(bodyInput.start, bodyInput.length).trimmed().isEmpty());
    insert(f, "repeat");
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
    QVERIFY(e->templateInputEditing());
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
    insert(f, "pick");
    QTest::keyClick(e, Qt::Key_Down);
    QVERIFY(e->toPlainText().contains("hits += 2;"));
    QTest::keyClick(e, Qt::Key_Up);
    QVERIFY(e->toPlainText().contains("hits += 1;"));
    const auto id = e->selectedTemplateId();
    e->finishTemplateInput();
    e->finishTemplateInput();
    QVERIFY(e->editTemplateInstance(id));
    QVERIFY(e->templateInputEditing());
    QTest::keyClick(e, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(e->selectedTemplateId(), id);
    QVERIFY(e->editTemplateInstance(id));
    QVERIFY(e->templateInputEditing());
    QDialog dialog(&f.window);
    auto *field = new QLineEdit(&dialog);
    dialog.show();
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    type("ordinary");
    QCOMPARE(field->text(), QString("ordinary"));
    dialog.close();
    f.window.activateWindow();
    e->setFocus();
    QTRY_VERIFY(e->hasFocus());
    QTest::keyClick(e, Qt::Key_Escape);
    QVERIFY(!e->templateInputEditing());
  }
  void inputUndoRedoIdentity() {
    Fixture f; auto* e = f.editor;
    insert(f, "process"); insert(f, "repeat");
    QVERIFY(!e->documentReadOnly());
    QVERIFY(e->templateInputEditing());
    const auto id = e->selectedTemplateId();
    const auto before = e->toPlainText();
    const auto beforeData = e->templateModel()->serialize(f.path);
    QVERIFY(e->replaceTemplateInput(e->textCursor(), "12"));
    const auto after = e->toPlainText();
    const auto afterData = e->templateModel()->serialize(f.path);
    QVERIFY(after != before);
    QTest::keyClick(e, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(e->toPlainText(), before);
    QCOMPARE(e->templateModel()->serialize(f.path), beforeData);
    QVERIFY(e->templateModel()->instance(id));
    QTest::keyClick(e, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(e->toPlainText(), after);
    QCOMPARE(e->templateModel()->serialize(f.path), afterData);
    QVERIFY(e->templateModel()->instance(id));
  }
  void inputTabFocus() {
    Fixture f; auto* e = f.editor;
    insert(f, "process"); insert(f, "repeat");
    QCOMPARE(e->templateSlotModeActiveIndex(), 0);
    type("3");
    QTest::keyClick(e, Qt::Key_Tab);
    QVERIFY(e->hasFocus()); QCOMPARE(e->templateSlotModeActiveIndex(), 1);
    QTest::keyClick(e, Qt::Key_Tab, Qt::ShiftModifier);
    QVERIFY(e->hasFocus()); QCOMPARE(e->templateSlotModeActiveIndex(), 0);
    QTest::keyClick(e, Qt::Key_Tab);
    QTest::keyClick(e, Qt::Key_Backtab);
    QVERIFY(e->hasFocus()); QCOMPARE(e->templateSlotModeActiveIndex(), 0);
    const auto source = e->toPlainText();
    QTest::keyClick(e, Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(e->hasFocus()); QVERIFY(!e->templateInputEditing());
    QCOMPARE(e->toPlainText(), source);
    QVERIFY(source.contains("repeat (3)"));
  }
  void fixedTextInvalidation() {
    Fixture f; auto* e = f.editor;
    insert(f, "process"); insert(f, "repeat");
    const auto id = e->selectedTemplateId();
    const auto before = e->toPlainText();
    const auto metadata = e->templateModel()->serialize(f.path);
    e->finishTemplateInput();
    QVERIFY(!e->documentReadOnly());
    const int start = before.indexOf("initial");
    QVERIFY(start >= 0);
    cursor(e, start, start + 7);
    e->insertPlainText("always");
    const auto after = QString(before).replace(start, 7, "always");
    QCOMPARE(e->toPlainText(), after);
    QVERIFY(e->templateModel()->instances().isEmpty());
    QCOMPARE(e->templateModel()->issue(),
        QString("Template boundaries changed; affected instances are read-only in template mode until inserted again."));
    e->undo();
    QCOMPARE(e->toPlainText(), before);
    QCOMPARE(e->templateModel()->serialize(f.path), metadata);
    QVERIFY(e->templateModel()->instance(id));
    e->redo();
    QCOMPARE(e->toPlainText(), after);
    QVERIFY(e->templateModel()->instances().isEmpty());
  }
  void legacyMetadataKeepsSource() {
    Fixture f;
    const auto path = f.root.filePath("legacy-tb.sv");
    // This source and metadata restored successfully before retirement in stage 0.
    const QByteArray source = R"legacy(module tb_evidence;
  timeunit 1ns;
  timeprecision 1ps;

  // Definitions
  
  // Instances
core unit();
  
  // Behaviors
  
endmodule
)legacy";
    auto metadata = QJsonDocument::fromJson(R"legacy({
    "file": "c:/users/14971/appdata/local/temp/zeroslack-tb-before-supg0o69/workspace/tb.sv",
    "instances": [
        {
            "fixedHash": "f8fe8946cb54310ec6183248fdb003ca7f550be3240b6a1a1faf426e7e5ccb08",
            "fragments": [
                {
                    "auxiliary": false,
                    "length": 137,
                    "start": 0
                }
            ],
            "id": "80634cee-bf70-4fca-8857-94de58de501e",
            "inputs": [
                {
                    "input": {
                    },
                    "length": 11,
                    "name": "name",
                    "start": 7,
                    "tabStop": 1,
                    "visible": true
                },
                {
                    "input": {
                        "allowEmpty": true,
                        "content": "declarations",
                        "kind": "Code"
                    },
                    "length": 3,
                    "name": "definitions",
                    "start": 75,
                    "tabStop": 2,
                    "visible": true
                },
                {
                    "input": {
                        "allowEmpty": true,
                        "content": "instances",
                        "kind": "Code"
                    },
                    "length": 16,
                    "name": "instances",
                    "start": 93,
                    "tabStop": 3,
                    "visible": true
                },
                {
                    "input": {
                        "allowEmpty": true,
                        "content": "behaviors",
                        "kind": "Code"
                    },
                    "length": 3,
                    "name": "behaviors",
                    "start": 124,
                    "tabStop": 4,
                    "visible": true
                }
            ],
            "label": "@tb",
            "parent": "",
            "parentSlot": "",
            "snapshot": {
                "activation": {
                    "mode": "exactLockedFileGap",
                    "query": "tb"
                },
                "capability": "moduleRoot",
                "capabilityVersion": 1,
                "kind": "snippet",
                "nameSlot": "name",
                "placement": "root",
                "regions": {
                    "behaviors": "behaviors",
                    "declarations": "definitions",
                    "instances": "instances"
                },
                "role": "tbRoot",
                "summary": "name · definitions · instances · behaviors"
            }
        },
        {
            "fixedHash": "b3c3bf4ba7b6c6aefc22d820748578b09945e3aa7e95d7510608ebffb7af5a96",
            "fragments": [
                {
                    "auxiliary": false,
                    "length": 13,
                    "start": 93
                }
            ],
            "id": "8b82a03c-2afc-4031-b64c-5aaeeaf44d18",
            "inputs": [
            ],
            "label": "@dut",
            "parent": "80634cee-bf70-4fca-8857-94de58de501e",
            "parentSlot": "instances",
            "snapshot": {
                "capability": "moduleInstance",
                "capabilityVersion": 1,
                "content": "instances",
                "kind": "operations",
                "operations": [
                    {
                        "label": "DUT signals",
                        "namePattern": "${instance}_${port}",
                        "op": "declareSignals",
                        "reuse": "explicit",
                        "target": "definitions"
                    },
                    {
                        "bindings": "named",
                        "op": "instantiateModule",
                        "target": "instances"
                    }
                ],
                "placement": "module",
                "semanticInputs": {
                    "moduleSource": "compilation",
                    "parameters": "selectedModule",
                    "ports": "elaboratedPorts"
                },
                "summary": "module · parameters · explicit connections"
            }
        }
    ],
    "textSha256": "1ed219bd066cb68f586dce2cd9ae2296fb7fc8809adf76cde0515f6306f8a2e1",
    "version": 1
}
)legacy").object();
    metadata["file"] = EditorFileIdentity::lookupKey(path);
    QCOMPARE(metadata.value("version").toInt(), 1);
    QCOMPARE(metadata.value("textSha256").toString().toLatin1(),
             QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex());
    const auto instances = metadata.value("instances").toArray();
    QCOMPARE(instances.size(), 2);
    QVERIFY(instances.first().toObject().value("snapshot").toObject().contains("regions"));
    QCOMPARE(instances.last().toObject().value("parentSlot").toString(), QString("instances"));
    put(path, source);
    QString error;
    QVERIFY2(WorkspaceSessionStateService().saveDocumentTemplates(path, metadata, &error),
             qPrintable(error));
    QCOMPARE(WorkspaceSessionStateService().loadDocumentTemplates(path, &error), metadata);
    QVERIFY(f.window.tabManager->openFileInTab(path));
    auto* e = f.window.tabManager->getCurrentEditor();
    QVERIFY(e);
    QCOMPARE(e->toPlainText().toUtf8(), source);
    QCOMPARE(e->templateModel()->issue(),
        QString("Template metadata does not match this document; source text was preserved and template editing is disabled for unrecognized ranges."));
    QVERIFY(e->templateModel()->instances().isEmpty());
    QVERIFY(!e->documentReadOnly());
    QVERIFY(!e->templateInputEditing());
    QVERIFY(!QApplication::activeModalWidget());
    QFile disk(path); QVERIFY(disk.open(QIODevice::ReadOnly));
    QCOMPARE(disk.readAll(), source); disk.close();
    QCOMPARE(WorkspaceSessionStateService().loadDocumentTemplates(path, &error), metadata);
    f.window.activateWindow(); e->setFocus(); QTRY_VERIFY(e->hasFocus());
    cursor(e, 0);
    QTest::keyClicks(e, "// editable ");
    QCOMPARE(e->toPlainText().toUtf8(), QByteArray("// editable ") + source);
    QVERIFY(e->document()->isModified());
    QVERIFY(disk.open(QIODevice::ReadOnly)); QCOMPARE(disk.readAll(), source);
    QCOMPARE(WorkspaceSessionStateService().loadDocumentTemplates(path, &error), metadata);
  }

  void metadataSaveFailureAndRecovery() {
    const auto variable = "ZEROSLACK_SESSION_STORAGE_PATH";
    const bool callerSet = qEnvironmentVariableIsSet(variable);
    const QByteArray callerValue = qgetenv(variable);
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    const auto privatePath = QDir::cleanPath(storage.filePath("sessions.ini"));
    QVERIFY(QFileInfo(privatePath).isAbsolute());
    bool restored = false, windowDestroyedBeforeRestore = false;
    {
      QPointer<MainWindow> observedWindow;
      const auto restoreEnvironment = qScopeGuard([&] {
        windowDestroyedBeforeRestore = observedWindow.isNull();
        restored = callerSet ? qputenv(variable, callerValue) : qunsetenv(variable);
        qInfo().noquote() << "METADATA_ENV_RESTORED" << QJsonDocument(QJsonObject{
            {"callerWasSet", callerSet}, {"environmentSet", qEnvironmentVariableIsSet(variable)},
            {"valueRestored", qgetenv(variable) == callerValue}, {"restoreSucceeded", restored},
            {"windowDestroyedBeforeRestore", windowDestroyedBeforeRestore}}).toJson(QJsonDocument::Compact);
      });
      QVERIFY(qputenv(variable, privatePath.toUtf8()));
      const auto resolvedPath = WorkspaceSessionStateService().localStoragePath();
      QCOMPARE(resolvedPath, privatePath);
      qInfo().noquote() << "METADATA_PRIVATE_STORAGE" << QJsonDocument(QJsonObject{
          {"callerWasSet", callerSet}, {"callerValue", QString::fromUtf8(callerValue)},
          {"privatePath", privatePath}, {"localStoragePath", resolvedPath}}).toJson(QJsonDocument::Compact);
      Fixture f;
      observedWindow = &f.window;
      auto *e = f.editor;
      QTRY_VERIFY(e->hasFocus());
      insert(f, "process");
      type("hits += 1;\n");
      const auto source = e->toPlainText();
      const auto metadata = e->templateModel()->serialize(f.path);
      QCOMPARE(WorkspaceSessionStateService().localStoragePath(), resolvedPath);
      const auto lockPath = resolvedPath + ".write.lock";
      qInfo().noquote() << "METADATA_LOCK_BEFORE_SAVE" << QJsonDocument(QJsonObject{
          {"localStoragePath", resolvedPath}, {"testLockPath", lockPath}}).toJson(QJsonDocument::Compact);
      QLockFile busy(lockPath);
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
    QVERIFY(windowDestroyedBeforeRestore);
    QVERIFY(restored);
    QCOMPARE(qEnvironmentVariableIsSet(variable), callerSet);
    QCOMPARE(qgetenv(variable), callerValue);
  }
};
int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QStandardPaths::setTestModeEnabled(true);
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  QCoreApplication::setOrganizationName("ZeroSlackTemplateInputTest");
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
  if (!UserTemplateService::getInstance()->setRecords(simulationTemplateFixtures()).valid) return 3;
  if (!initializeUiStyleForTest())
    return 2;
  ApplicationThemeManager::instance().setAnimationsEnabled(false);
  TemplateInputTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "template_authoring_test.moc"
