#include <QtWidgets>
#include <QtTest>
#include <memory>
#include <functional>
#define private public
#include "navigationcommandcoordinator.h"
#undef private
#include "tabmanager.h"
#include "mycodeeditor.h"
#include "uidialogs.h"
#include "testuistyle.h"

namespace {
int failures = 0, checks = 0;
void check(bool ok, const char* label) {
    ++checks; if (!ok) ++failures;
    fprintf(stdout, "[%s] %s\n", ok ? "PASS" : "FAIL", label);
}
bool write(const QString& path, const QByteArray& text) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}
void exercise(bool forward) {
    QTemporaryDir fixture;
    const QString a = fixture.filePath("a.sv"), b = fixture.filePath("b.sv");
    check(write(a,"module a;\nlogic a;\nendmodule\n") && write(b,"module b;\nlogic b;\nendmodule\n"), "navigation fixture written");
    QTabWidget widget; TabManager tabs(&widget);
    tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(fixture.filePath("recovery")));
    NavigationCommandCoordinator navigation(&tabs,nullptr);
    check(tabs.openFileInTab(a), "open A");
    auto* editorA = tabs.getCurrentEditor();
    editorA->setHierarchyInstanceContext({{},"top","top.a"});
    QTextCursor cursor(editorA->document()); cursor.setPosition(13); editorA->setTextCursor(cursor);
    navigation.navigateToFileAndLineWithContext(b,2,3,{{},"top","top.b"});
    if (forward) navigation.navigateBack();
    auto* current = tabs.getCurrentEditor();
    const auto currentCursor = current->textCursor();
    const auto currentContext = current->hierarchyInstanceContext();
    const QString target = forward ? b : a;
    auto* targetEditor = tabs.getDocumentModel()->editorForFile(target);
    const int targetPosition = targetEditor->textCursor().position();
    const auto targetContext = targetEditor->hierarchyInstanceContext();
    tabs.closeTab(widget.indexOf(targetEditor));
    check(QFile::rename(target,target+".hidden"), "target temporarily missing after tab close");
    const auto back = navigation.backStack, next = navigation.forwardStack;
    QString statusMessage;
    QObject::connect(current,&MyCodeEditor::editorStatusMessageRequested,current,
        [&](const QString& message) {statusMessage=message;});
    if (forward) navigation.navigateForward(); else navigation.navigateBack();
    check(navigation.backStack == back && navigation.forwardStack == next, "failed jump preserves both history stacks");
    check(statusMessage.contains("history") && statusMessage.contains("retry"), "failed jump explains retained history and retry");
    check(tabs.getCurrentEditor()==current && current->textCursor().position()==currentCursor.position()
        && current->hierarchyInstanceContext()==currentContext, "failed jump preserves editor position and instance");
    check(QFile::rename(target+".hidden",target), "target restored");
    if (forward) navigation.navigateForward(); else navigation.navigateBack();
    check(tabs.getCurrentDocument().fileName==target, "retry reaches restored target");
    check(tabs.getCurrentEditor()->textCursor().position()==targetPosition
        && tabs.getCurrentEditor()->hierarchyInstanceContext()==targetContext,
        "retry restores line and instance context");
    check(forward ? navigation.forwardStack.size()==next.size()-1 && navigation.backStack.size()==back.size()+1
                  : navigation.backStack.size()==back.size()-1 && navigation.forwardStack.size()==next.size()+1,
          "only successful replay transfers the two stacks");
    if (forward) navigation.navigateBack(); else navigation.navigateForward();
    check(tabs.getCurrentEditor()==current && current->textCursor().position()==currentCursor.position()
        && current->hierarchyInstanceContext()==currentContext, "opposite replay returns to original location");
    targetEditor = tabs.getDocumentModel()->editorForFile(target);
    tabs.closeTab(widget.indexOf(targetEditor));
    bool redirected = false;
    const auto redirect = QObject::connect(&tabs,&TabManager::activeDocumentChanged,&tabs,
        [&](const DocumentSnapshot& document) {
            if (!redirected && document.fileName==target) {
                redirected=true;
                tabs.activateOpenFile(forward?a:b);
            }
        });
    const auto backBeforePartial=navigation.backStack, forwardBeforePartial=navigation.forwardStack;
    if(forward) navigation.navigateForward(); else navigation.navigateBack();
    QObject::disconnect(redirect);
    check(redirected && tabs.getCurrentEditor()==current && current->textCursor().position()==currentCursor.position()
        && current->hierarchyInstanceContext()==currentContext && navigation.backStack==backBeforePartial
        && navigation.forwardStack==forwardBeforePartial,
        "partial activation failure restores presentation without committing history");
    check(tabs.getDocumentModel()->editorForFile(target)!=nullptr,"partial failure does not close newly opened document");
    if(forward) navigation.navigateForward(); else navigation.navigateBack();
    check(tabs.getCurrentDocument().fileName==target,"partial activation can be retried successfully");
}
}

int main(int argc,char** argv) {
    setbuf(stdout, nullptr);
    QApplication app(argc,argv); QStandardPaths::setTestModeEnabled(true);
    if (!initializeUiStyleForTest()) return 2;
    QTimer dismiss; dismiss.setInterval(5);
    QObject::connect(&dismiss,&QTimer::timeout,[] {
        for (auto* widget: QApplication::allWidgets())
            if (auto* message=qobject_cast<UiMessageDialog*>(widget)) message->reject();
            else if (auto* message=qobject_cast<QMessageBox*>(widget)) message->reject();
    });
    dismiss.start(); exercise(false); exercise(true);
    fprintf(stdout,"%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
