#include "applicationthememanager.h"
#include "editorcolumnmodecontroller.h"
#include "editorsplitcontroller.h"
#include "externaldocumentsynccontroller.h"
#include "mycodeeditor.h"
#include "shareddocument.h"
#include "tabmanager.h"

#include <QtTest>
#include <QtWidgets>

#include <cstdio>
#include <functional>
#include <memory>

namespace {
int checks = 0;
int failures = 0;
const QString source = QStringLiteral("alpha\nbeta\ngamma\n");

void check(bool ok, const QString& label)
{
    ++checks;
    failures += !ok;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    std::fflush(stdout);
}

void key(MyCodeEditor* editor, int code, const QString& text = {},
         Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent event(QEvent::KeyPress, code, modifiers, text);
    QApplication::sendEvent(editor, &event);
}

void select(MyCodeEditor* editor, int start, int end = -1)
{
    QTextCursor cursor(editor->document());
    cursor.setPosition(start);
    if (end >= 0)
        cursor.setPosition(end, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
}

void rectangle(MyCodeEditor* editor)
{
    key(editor, Qt::Key_Escape);
    select(editor, 0);
    QTest::keyClick(editor, Qt::Key_Down, Qt::ShiftModifier | Qt::AltModifier);
    QTest::keyClick(editor, Qt::Key_Right, Qt::ShiftModifier | Qt::AltModifier);
}

struct Snapshot {
    QString text;
    int revision, undo, redo;
    bool modified;
    explicit Snapshot(MyCodeEditor* editor)
        : text(editor->toPlainText()), revision(editor->document()->revision()),
          undo(editor->document()->availableUndoSteps()),
          redo(editor->document()->availableRedoSteps()),
          modified(editor->document()->isModified()) {}
    bool unchanged(MyCodeEditor* editor) const
    {
        return text == editor->toPlainText()
            && revision == editor->document()->revision()
            && undo == editor->document()->availableUndoSteps()
            && redo == editor->document()->availableRedoSteps()
            && modified == editor->document()->isModified();
    }
};

struct Views {
    QWidget host;
    QWidget auxiliaryWindow;
    QTabWidget* tabs = nullptr;
    std::unique_ptr<TabManager> manager;
    MyCodeEditor* primary = nullptr;
    MyCodeEditor* auxiliary = nullptr;
    SharedDocument* shared = nullptr;

    explicit Views(const QString& fileName = {})
    {
        auto* layout = new QVBoxLayout(&host);
        tabs = new QTabWidget(&host);
        layout->addWidget(tabs);
        manager = std::make_unique<TabManager>(tabs);
        manager->enableSplitLayout(&host);
        if (fileName.isEmpty())
            manager->createNewTab();
        else
            check(manager->openFileInTab(fileName), "real TabManager opens disk fixture");
        primary = manager->getCurrentEditor();
        if (!primary)
            qFatal("No primary editor");
        if (fileName.isEmpty())
            primary->setPlainText(source);
        shared = manager->sharedDocumentForEditor(primary);
        auto* auxLayout = new QVBoxLayout(&auxiliaryWindow);
        auxiliary = manager->createAuxiliaryView(shared->documentId(), {}, &auxiliaryWindow);
        if (!auxiliary)
            qFatal("No auxiliary editor");
        auxLayout->addWidget(auxiliary);
        host.resize(800, 360);
        auxiliaryWindow.resize(500, 280);
        host.show();
        auxiliaryWindow.show();
        host.activateWindow();
        primary->setFocus();
        QApplication::processEvents();
    }

    ~Views()
    {
        if (auxiliary)
            manager->closeAuxiliaryView(auxiliary);
        for (MyCodeEditor* editor : manager->openEditors())
            editor->document()->setModified(false);
    }
};

bool writeFile(const QString& path, const QString& text)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(text.toUtf8()) == text.toUtf8().size();
}

bool readonlyReason(const QString& reason)
{
    return reason.contains(QStringLiteral("read-only"), Qt::CaseInsensitive);
}

void readonlyDiskAndDynamic()
{
    QTemporaryDir temporary;
    const QString fileName = temporary.filePath("readonly.sv");
    check(temporary.isValid() && writeFile(fileName, source), "create readonly disk fixture");
    const auto permissions = QFile::permissions(fileName);
    check(QFile::setPermissions(fileName, QFileDevice::ReadOwner | QFileDevice::ReadGroup
                                            | QFileDevice::ReadOther)
              && !QFileInfo(fileName).isWritable(),
          "fixture is readonly according to the real filesystem");
    {
        Views views(fileName);
        check(views.primary->isReadOnly() && views.auxiliary->isReadOnly()
                  && views.manager->externalDocumentSyncController()->isFileWatchedForTesting(fileName),
              "disk readonly flows through ExternalDocumentSync to primary and auxiliary");
        const Snapshot before(views.primary);
        const auto revision = views.shared->textRevision();
        key(views.primary, Qt::Key_Tab, "\t");
        rectangle(views.auxiliary);
        key(views.auxiliary, Qt::Key_Z, "z");
        check(before.unchanged(views.primary) && revision == views.shared->textRevision(),
              "disk readonly blocks both real view input paths without revision or undo changes");
    }
    check(QFile::setPermissions(fileName, permissions), "restore fixture permissions");

    Views views;
    auto* editor = views.primary;
    editor->setPlainText("    alpha_one\n    beta_two\nassign out = in;\n");
    select(editor, 4);
    editor->insertPlainText("seed_");
    views.shared->setReadOnly(true);
    check(editor->isReadOnly() && views.auxiliary->isReadOnly(),
          "runtime SharedDocument readonly change reaches all attached views");
    struct Input { const char* label; int code; QString text; Qt::KeyboardModifiers modifiers; };
    const Input inputs[] = {
        {"Tab", Qt::Key_Tab, "\t", Qt::NoModifier},
        {"Backtab", Qt::Key_Backtab, {}, Qt::ShiftModifier},
        {"Enter", Qt::Key_Return, "\r", Qt::NoModifier},
        {"pair", Qt::Key_ParenLeft, "(", Qt::NoModifier},
        {"plain text", Qt::Key_Z, "z", Qt::NoModifier},
        {"lexical backspace", Qt::Key_Backspace, {}, Qt::ControlModifier},
        {"lexical delete", Qt::Key_Delete, {}, Qt::ControlModifier},
    };
    for (const auto& input : inputs) {
        select(editor, 9);
        const Snapshot before(editor);
        key(editor, input.code, input.text, input.modifiers);
        check(before.unchanged(editor), QString("readonly %1 preserves content/revision/undo").arg(input.label));
    }
    for (const auto& input : inputs) {
        rectangle(views.auxiliary);
        const Snapshot before(editor);
        key(views.auxiliary, input.code, input.text, input.modifiers);
        check(before.unchanged(editor), QString("readonly column %1 preserves shared content/revision/undo").arg(input.label));
    }
    for (int code : {Qt::Key_Delete, Qt::Key_Backspace}) {
        rectangle(editor);
        const Snapshot before(editor);
        key(editor, code);
        check(before.unchanged(editor), QString("readonly column deletion %1 is blocked").arg(code));
    }

    rectangle(editor);
    const Snapshot beforePublic(editor);
    QString reason;
    check(!editor->applyColumnSelectionTexts({"x", "y"}, true, &reason) && readonlyReason(reason),
          "public column tool reports readonly failure");
    key(editor, Qt::Key_Escape);
    select(editor, 4, 9);
    check(!editor->deleteSelectedContent(&reason) && readonlyReason(reason),
          "public selection deletion reports readonly failure");
    check(!editor->clearSelectedAssignmentRhs(&reason) && readonlyReason(reason),
          "public assignment deletion reports readonly failure");
    check(!editor->createAssignmentQueueAt(0, &reason) && readonlyReason(reason),
          "public assignment queue reports readonly failure");
    check(!editor->confirmSignalDefinitionForTest("logic new_signal;", &reason) && readonlyReason(reason),
          "pending declaration confirmation reports readonly failure");
    check(!editor->insertCompletionText("new", -1, 0, {}, &reason) && readonlyReason(reason),
          "completion insertion retains its existing readonly capability");
    check(!editor->replaceNextText("alpha", "new", true)
              && editor->replaceAllText("alpha", "new", true) == 0,
          "public replace returns failure and zero replacements");
    editor->commentSelectionOrLine();
    editor->uncommentSelectionOrLine();
    editor->indentSelectionOrLine();
    editor->unindentSelectionOrLine();
    editor->insertPlainText("new");
    editor->undo();
    editor->redo();
    editor->cut();
    editor->paste();
    check(beforePublic.unchanged(editor), "all public readonly editing calls preserve source and history");
    const auto readonlyFormat = editor->formatDocument();
    check(!readonlyFormat.accepted() && readonlyReason(readonlyFormat.diagnostic)
              && beforePublic.unchanged(editor), "public formatting reports readonly failure without mutation");

    select(editor, 4, 9);
    editor->copy();
    check(QApplication::clipboard()->text() == "seed_", "readonly normal selection copy works");
    select(editor, 4);
    key(editor, Qt::Key_Right, {}, Qt::ControlModifier | Qt::ShiftModifier);
    check(editor->textCursor().hasSelection(), "readonly lexical navigation and selection work");
    rectangle(editor);
    editor->copy();
    check(QApplication::clipboard()->text() == " \n ", "readonly rectangle copy works");

    // Host loading/reset remains legal, even when user editing is disabled.
    editor->setPlainText("[3]\n[3:0]\n");
    const Snapshot rangeBefore(editor);
    select(editor, 2);
    key(editor, Qt::Key_Tab, "\t");
    select(editor, 5, 8);
    key(editor, Qt::Key_Up);
    check(rangeBefore.unchanged(editor), "readonly bracket Tab and selected range adjustment cannot write");
    editor->clear();
    editor->setPlainText(source);
    check(editor->isReadOnly() && editor->toPlainText() == source
              && views.auxiliary->toPlainText() == source,
          "readonly host setters still load and reset shared text");

    views.shared->setReadOnly(false);
    check(!editor->isReadOnly() && !views.auxiliary->isReadOnly(), "writable capability recovers in both views");
    editor->setPlainText("");
    key(editor, Qt::Key_ParenLeft, "(");
    check(editor->toPlainText() == "()", "writable pair insertion recovers");
    editor->undo();
    editor->redo();
    check(editor->toPlainText() == "()", "pair undo and redo remain usable");
    editor->setPlainText(source);
    rectangle(editor);
    key(editor, Qt::Key_Z, "z");
    check(editor->toPlainText() == "zlpha\nzeta\ngamma\n", "writable column input recovers");
    editor->undo();
    const Snapshot undoBefore(editor);
    views.shared->setReadOnly(true);
    editor->redo();
    check(undoBefore.unchanged(editor), "readonly redo preserves an existing redo branch");
    views.shared->setReadOnly(false);
    editor->redo();
    check(editor->toPlainText() == "zlpha\nzeta\ngamma\n", "redo branch survives the capability transition");
}

void virtualIme()
{
    Views views;
    auto* editor = views.primary;
    select(editor, 5);
    const QPoint beyond = editor->cursorRect().center() + QPoint(60, 0);
    QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::AltModifier, beyond);
    check(editor->virtualCursorActiveForTest(), "real Alt click enters virtual cursor beyond EOL");
    views.shared->setReadOnly(true);
    const Snapshot before(editor);
    QInputMethodEvent preedit(QString::fromUtf8("测"), {});
    QApplication::sendEvent(editor, &preedit);
    check(before.unchanged(editor), "simulated readonly IME preedit cannot insert virtual padding");
    QInputMethodEvent commit;
    commit.setCommitString(QString::fromUtf8("测"));
    QApplication::sendEvent(editor, &commit);
    check(before.unchanged(editor), "simulated readonly IME commit cannot change text or history");
    views.shared->setReadOnly(false);
    QApplication::sendEvent(editor, &commit);
    check(editor->toPlainText().contains(QString::fromUtf8("测")),
          "simulated writable IME commit works after capability recovery");
}

void ownColumnEdits()
{
    Views views;
    auto* editor = views.primary;
    rectangle(editor);
    key(editor, Qt::Key_Z, "z");
    key(editor, Qt::Key_X, "x");
    check(editor->columnSelectionActive() && editor->toPlainText() == "zxlpha\nzxeta\ngamma\n",
          "consecutive own column keys retain their intended rows");
    editor->undo();
    check(editor->toPlainText() == "zlpha\nzeta\ngamma\n" && editor->columnSelectionActive(),
          "one undo restores the previous column edit and logical caret");
    editor->redo();
    check(editor->toPlainText() == "zxlpha\nzxeta\ngamma\n" && editor->columnSelectionActive(),
          "redo restores the column edit and logical caret");
    QApplication::clipboard()->setText("p");
    const QString beforePaste = editor->toPlainText();
    editor->paste();
    const QString pasted = editor->toPlainText();
    check(pasted == "zxplpha\nzxpeta\ngamma\n" && editor->columnSelectionActive(),
          "column paste delivers contentsChange within its owning edit block");
    editor->undo();
    check(editor->toPlainText() == beforePaste && editor->columnSelectionActive(), "column paste undoes once");
    editor->redo();
    check(editor->toPlainText() == pasted && editor->columnSelectionActive(), "column paste redoes once");
    QString message;
    check(editor->applyColumnSelectionTexts({"1", "2"}, true, &message) && message.isEmpty()
              && editor->toPlainText() == "zxp1lpha\nzxp2eta\ngamma\n" && editor->columnSelectionActive(),
          "column tool uses the same ownership boundary and retains continuous intent");
    editor->undo();
    check(editor->toPlainText() == pasted && editor->columnSelectionActive(), "column tool is one undo step");
    editor->redo();
    check(editor->toPlainText() == "zxp1lpha\nzxp2eta\ngamma\n" && editor->columnSelectionActive(),
          "column tool redo restores text and mode");

    QMenu menu;
    auto* action = menu.addAction("Apply column rows");
    QObject::connect(action, &QAction::triggered, editor, [editor] {
        editor->applyColumnSelectionTexts({"m", "n"}, true);
    });
    menu.popup(editor->mapToGlobal(QPoint(40, 40)));
    QApplication::processEvents();
    check(editor->columnSelectionActive(), "popup focus alone preserves column intent");
    action->trigger();
    menu.close();
    check(editor->toPlainText() == "zxp1mlpha\nzxp2neta\ngamma\n" && editor->columnSelectionActive(),
          "menu-triggered column tool still applies to the selected rows");
}

void foreignColumnEdits()
{
    Views views;
    auto* editor = views.primary;
    auto* auxiliary = views.auxiliary;
    struct Mutation { const char* label; std::function<void()> apply; };
    const Mutation mutations[] = {
        {"auxiliary prefix insertion", [&] { select(auxiliary, 0); auxiliary->insertPlainText("prefix\n"); }},
        {"auxiliary line deletion", [&] { select(auxiliary, 0, 6); auxiliary->deleteSelectedContent(); }},
        {"auxiliary same-line edit", [&] { select(auxiliary, 2); auxiliary->insertPlainText("long"); }},
        {"unowned document transaction", [&] {
            QTextCursor cursor(editor->document()); cursor.beginEditBlock();
            cursor.insertText("prefix\n"); cursor.endEditBlock();
        }},
        {"transaction spanning both views", [&] {
            auto primaryEdit = editor->beginSynchronousEditTransaction();
            auto auxiliaryEdit = auxiliary->beginSynchronousEditTransaction();
            QTextCursor cursor(editor->document()); cursor.insertText("prefix\n");
        }},
        {"same-view model setter", [&] { editor->setPlainText("prefix\n" + source); }},
    };
    for (const auto& mutation : mutations) {
        editor->setPlainText(source);
        rectangle(editor);
        check(editor->columnSelectionActive(), QString("rectangle setup for %1").arg(mutation.label));
        // Intentionally no focusOut: mutation ownership, not focus, is the boundary.
        mutation.apply();
        check(!editor->columnSelectionActive(), QString("%1 ends stale rectangle synchronously").arg(mutation.label));
        QString expected = editor->toPlainText();
        expected.insert(editor->textCursor().position(), 'z');
        key(editor, Qt::Key_Z, "z");
        check(editor->toPlainText() == expected,
              QString("next input after %1 edits only the current physical caret").arg(mutation.label));
    }
    editor->setPlainText(source);
    rectangle(editor);
    key(editor, Qt::Key_Z, "z");
    auxiliary->undo();
    check(editor->toPlainText() == source && !editor->columnSelectionActive(),
          "foreign undo ends local column intent");
    auxiliary->redo();
    check(editor->toPlainText() == "zlpha\nzeta\ngamma\n" && !editor->columnSelectionActive(),
          "foreign redo cannot resurrect local rectangle");

    // Undo-step counts are per document and may repeat after external reset.
    editor->setPlainText(source);
    rectangle(editor);
    key(editor, Qt::Key_Z, "z");
    auxiliary->setPlainText("new\ncontent\n");
    auxiliary->insertPlainText("other");
    editor->undo();
    check(!editor->columnSelectionActive(), "foreign reset discards stale local undo rectangle snapshots");

    editor->setPlainText(source);
    check(views.manager->splitCurrentView(EditorSplitDirection::Right), "create real TabManager split");
    auto* split = views.manager->getCurrentEditor();
    check(split != editor && split->document() == editor->document(), "split uses the same shared QTextDocument");
    rectangle(editor);
    select(split, 0);
    split->insertPlainText("prefix\n");
    check(!editor->columnSelectionActive(), "split edit invalidates primary rectangle");
    rectangle(split);
    select(editor, 0);
    editor->insertPlainText("other\n");
    check(!split->columnSelectionActive(), "primary edit invalidates split rectangle");

    editor->setPlainText("module top;\nlogic a;\nalways_comb begin\na = 1'b0;\nend\nendmodule\n");
    rectangle(editor);
    rectangle(auxiliary);
    const auto report = split->formatDocument();
    std::printf("format accepted=%d changed=%d diagnostic=%s\n", report.accepted(), report.changed,
                qPrintable(report.diagnostic));
    check(report.accepted() && report.changed && !editor->columnSelectionActive()
              && !auxiliary->columnSelectionActive(),
          "real formatting transaction invalidates rectangle in other shared views");
}

void lifecycleAndReload()
{
    QTemporaryDir temporary;
    const QString fileName = temporary.filePath("reload.sv");
    check(writeFile(fileName, source), "create reload fixture");
    Views views(fileName);
    auto* editor = views.primary;
    rectangle(editor);
    views.auxiliaryWindow.activateWindow();
    views.auxiliary->setFocus();
    QApplication::processEvents();
    check(editor->columnSelectionActive(), "auxiliary focus alone preserves column selection");
    views.manager->createNewTab();
    auto* other = views.manager->getCurrentEditor();
    other->setPlainText("new\ndocument\n");
    views.tabs->setCurrentWidget(editor);
    check(!editor->columnSelectionActive(), "tab switch retains the existing mode exit policy");
    rectangle(editor);
    views.manager->setWorkspaceScope({temporary.path()}, temporary.path());
    views.manager->setWorkspaceScope({temporary.path()}, {});
    check(!editor->columnSelectionActive(), "workspace switch retains the existing mode exit policy");
    rectangle(editor);
    check(editor->columnSelectionActive(), "re-arm a real rectangle before external reload");
    check(writeFile(fileName, "prefix\n" + source), "replace disk generation for real external reload");
    const auto result = views.manager->externalDocumentSyncController()->processFileChange(fileName);
    check(result.outcome == ExternalDocumentSyncOutcome::Reloaded
              && editor->toPlainText() == "prefix\n" + source && !editor->columnSelectionActive(),
          "real external clean reload invalidates rectangle");
    key(editor, Qt::Key_Z, "z");
    check(editor->toPlainText().startsWith("prefix\n"), "post-reload input preserves new unselected prefix");

    rectangle(views.auxiliary);
    key(views.auxiliary, Qt::Key_X, "x");
    const auto* otherDocument = views.manager->sharedDocumentForEditor(other);
    check(views.manager->rebindAuxiliaryView(views.auxiliary, otherDocument->documentId(), {})
              && !views.auxiliary->columnSelectionActive() && views.auxiliary->document() == other->document(),
          "rebind exits old document rectangle");
    views.auxiliary->insertPlainText("fresh");
    views.auxiliary->undo();
    check(!views.auxiliary->columnSelectionActive() && other->toPlainText() == "new\ndocument\n",
          "rebound document undo cannot restore old rectangle intent");
    rectangle(views.auxiliary);
    QPointer<MyCodeEditor> closed = views.auxiliary;
    check(views.manager->closeAuxiliaryView(views.auxiliary), "close auxiliary view with active rectangle");
    views.auxiliary = nullptr;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(closed.isNull(), "closed rectangle owner is destroyed");
    other->insertPlainText("alive");
    check(other->toPlainText().contains("alive"), "shared document remains editable after auxiliary close");
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("ZeroSlackInputBoundaryContract");
    QCoreApplication::setApplicationName("R4");
    auto& theme = ApplicationThemeManager::instance();
    if (!theme.selectBackend(UiStyleBackend::Ela))
        return 2;
    theme.applyToApplication();
    readonlyDiskAndDynamic();
    virtualIme();
    ownColumnEdits();
    foreignColumnEdits();
    lifecycleAndReload();
    std::printf("checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
