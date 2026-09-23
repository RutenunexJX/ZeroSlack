#include "applicationthememanager.h"
#include "completionmodel.h"
#include "editorcompletionui.h"
#include "editorsemanticcontextservice.h"
#include "mycodeeditor.h"
#include "testuistyle.h"
#include "uicontrols.h"
#include "uitooltips.h"

#include <QApplication>
#include <QCompleter>
#include <QDir>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPointer>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

namespace {
QWidget* tooltip() {
    for (auto* widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "uiToolTip") return widget;
    return nullptr;
}
void capture(QWidget* widget, const QString& name) {
    const QString path = qEnvironmentVariable("ZEROSLACK_SURFACE_SCREENSHOTS");
    if (path.isEmpty()) return;
    QDir().mkpath(path);
    QVERIFY(widget->grab().save(path + '/' + name + ".png"));
}
}

class UiPopupContractTest final : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { UiToolTips::hideText(); }

    void completionPopupRetainsModelAndSelection() {
        MyCodeEditor editor;
        editor.setPlainText("module top; endmodule\n");
        editor.resize(820, 550);
        editor.show();
        // Exercise the presentation controller without invoking an editing command.
        for (auto* completer : editor.findChildren<QCompleter*>()) completer->setWidget(nullptr);
        EditorCompletionUi completion;
        QString activated;
        completion.attachToEditor(&editor, [&](const QModelIndex& index) {
            activated = index.data().toString();
        });
        QStringList files;
        for (int i = 0; i < 60; ++i) files << QString("rtl/channel_%1.svh").arg(i, 2, 10, QChar('0'));
        completion.updateIncludeFileCompletions(files, {});
        auto* popup = completion.popup();
        QVERIFY(popup->inherits("ElaListView"));
        QVERIFY(popup->verticalScrollBar()->inherits("ElaScrollBar"));
        completion.showForCursor(editor.cursorRect(), true);
        QApplication::processEvents();
        QVERIFY(popup->isVisible());
        const QModelIndex current = popup->currentIndex();
        QVERIFY(current.isValid());
        QVERIFY(!current.data(Qt::ToolTipRole).toString().isEmpty());
        QCOMPARE(current.model(), popup->model());
        QCOMPARE_GE(popup->visualRect(current).height(), 28);
        QVERIFY(completion.activationContextForIndex(current).selectable);
        QVERIFY(!completion.activationContextForIndex(popup->model()->index(0, 0)).selectable);
        const auto* model = popup->model();
        const int count = model->rowCount();
        QVERIFY(count >= 60);
        QTest::keyClick(popup, Qt::Key_Down);
        QCOMPARE(popup->currentIndex().row(), current.row() + 1);
        const QString selected = popup->currentIndex().data().toString();
        completion.activateIndex(popup->currentIndex());
        QCOMPARE(activated, selected);
        completion.hidePopup();
        QCOMPARE(editor.toPlainText(), QString("module top; endmodule\n"));
        completion.showForCursor(editor.cursorRect(), true);
        QApplication::processEvents();
        capture(popup, "completion");
        const auto before = ApplicationThemeManager::instance().mode();
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        QCOMPARE(popup->model(), model);
        QCOMPARE(popup->model()->rowCount(), count);
        QTest::keyClick(popup, Qt::Key_Escape);
        QVERIFY(!popup->isVisible());
        ApplicationThemeManager::instance().setMode(before);
        completion.detach();
    }

    void editorDispatchesCompletionKeys() {
        MyCodeEditor editor;
        editor.resize(820, 550);
        editor.show();
        editor.setFocus();
        auto* completer = editor.findChild<QCompleter*>();
        QVERIFY(completer);
        auto* model = qobject_cast<CompletionModel*>(completer->model());
        QVERIFY(model);
        auto* popup = completer->popup();
        QSignalSpy activation(completer, QOverload<const QModelIndex&>::of(&QCompleter::activated));
        for (const auto key : {Qt::Key_Return, Qt::Key_Tab}) {
            editor.setPlainText({});
            model->updateIncludeFileCompletions({"rtl/alpha.svh", "rtl/beta.svh"}, {});
            completer->complete(editor.cursorRect());
            popup->setCurrentIndex(popup->model()->index(1, 0));
            QTest::keyClick(&editor, Qt::Key_Down);
            QCOMPARE(popup->currentIndex().row(), 2);
            const auto selected = popup->currentIndex();
            activation.clear();
            QTest::keyClick(&editor, key);
            QCOMPARE(activation.size(), 1);
            QCOMPARE(activation.first().first().value<QModelIndex>(), selected);
            popup->hide();
        }
        model->updateIncludeFileCompletions({"rtl/alpha.svh"}, {});
        completer->complete(editor.cursorRect());
        QTest::keyClick(&editor, Qt::Key_Escape);
        QVERIFY(!popup->isVisible());
    }

    void tooltipsPreserveFocusAndStayInsideScreen() {
        QWidget window;
        auto* layout = new QVBoxLayout(&window);
        auto* edit = UiControls::lineEdit(&window);
        auto* button = UiControls::pushButton("Open", &window);
        layout->addWidget(edit);
        layout->addWidget(button);
        window.resize(450, 160);
        window.show();
        window.activateWindow();
        edit->setFocus();
        QApplication::processEvents();
        const QPointer<QWidget> focused = QApplication::focusWidget();
        const auto area = window.screen()->availableGeometry();
        const QString text = "rtl/top.sv\nLong path: " + QString(220, 'w');
        UiToolTips::showText(area.bottomRight() - QPoint(2, 2), text, button);
        QApplication::processEvents();
        auto* tip = tooltip();
        QVERIFY(tip && tip->inherits("ElaToolTip") && tip->isVisible());
        QVERIFY(tip->testAttribute(Qt::WA_TransparentForMouseEvents));
        QVERIFY(tip->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QCOMPARE(QApplication::focusWidget(), focused.data());
        QVERIFY(area.contains(tip->geometry()));
        auto* caption = tip->findChild<QLabel*>("uiToolTipText");
        QCOMPARE(tip->accessibleName(), text);
        QCOMPARE(QString(caption->text()).remove('\n'), QString(text).remove('\n'));
        QVERIFY(caption->height() >= caption->fontMetrics().height() * 5);
        capture(tip, "tooltip");
        QTest::keyClick(edit, Qt::Key_A);
        QVERIFY(!tip->isVisible());
        UiToolTips::showText(button->mapToGlobal(QPoint()), "brief", button, {}, 30);
        QTRY_VERIFY(!tip->isVisible());
        UiToolTips::showText(button->mapToGlobal(QPoint()), "hide with owner", button);
        window.hide();
        QVERIFY(!tip->isVisible());
    }

    void itemAndWidgetHelpEventsUseEla() {
        QWidget window;
        auto* layout = new QVBoxLayout(&window);
        auto* button = UiControls::pushButton("Project", &window);
        button->setToolTip("Project commands");
        auto* list = UiControls::listWidget(&window);
        auto* first = new QListWidgetItem("top.sv", list);
        first->setToolTip("rtl/top.sv");
        new QListWidgetItem("empty tooltip", list);
        layout->addWidget(button);
        layout->addWidget(list);
        window.resize(450, 240);
        window.show();
        QApplication::processEvents();
        QHelpEvent buttonHelp(QEvent::ToolTip, button->rect().center(), button->mapToGlobal(button->rect().center()));
        QApplication::sendEvent(button, &buttonHelp);
        QVERIFY(tooltip() && tooltip()->isVisible());
        QCOMPARE(tooltip()->findChild<QLabel*>("uiToolTipText")->text(), QString("Project commands"));
        const auto itemRect = list->visualItemRect(first);
        QHelpEvent itemHelp(QEvent::ToolTip, itemRect.center(), list->viewport()->mapToGlobal(itemRect.center()));
        QApplication::sendEvent(list->viewport(), &itemHelp);
        QCOMPARE(tooltip()->findChild<QLabel*>("uiToolTipText")->text(), QString("rtl/top.sv"));
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(list->viewport(), &leave);
        QVERIFY(!tooltip()->isVisible());
        QApplication::processEvents();
        QVERIFY(!tooltip()->isVisible());
    }

    void deletingOwnerAndThemeChangeClearTooltip() {
        auto* owner = new QWidget;
        owner->show();
        UiToolTips::showText({20, 20}, "temporary owner", owner);
        QPointer<QWidget> previous = tooltip();
        QVERIFY(previous && previous->isVisible());
        delete owner;
        QVERIFY(previous.isNull());
        QWidget replacement;
        replacement.show();
        UiToolTips::showText({20, 20}, "replacement", &replacement);
        QVERIFY(tooltip() && tooltip()->isVisible());
        const auto before = ApplicationThemeManager::instance().mode();
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        QVERIFY(!tooltip()->isVisible());
        ApplicationThemeManager::instance().setMode(before);
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir profile;
    if (!profile.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    if (!initializeUiStyleForTest()) return 3;
    UiPopupContractTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_popup_contract_test.moc"
