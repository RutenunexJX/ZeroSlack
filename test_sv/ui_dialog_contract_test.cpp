#include "applicationthememanager.h"
#include "mycodeeditor.h"
#include "shareddocument.h"
#include "testuistyle.h"
#include "uidialogs.h"
#include "unsaveddocumentmanager.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

namespace {
bool usesEla() { return ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela; }

template<class Action> void whenModal(Action action)
{
    QTimer::singleShot(0, qApp, [action] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) qFatal("Expected an active input/confirmation dialog");
        action(dialog);
    });
}

template<class Control> Control* input(QDialog* dialog)
{
    auto* control = dialog->findChild<Control*>();
    if (!control) qFatal("Expected dialog input control");
    return control;
}
}

class UiDialogContractTest final : public QObject {
    Q_OBJECT
private slots:
    void textKeyboardAcceptAndCancel() {
        bool accepted = false;
        whenModal([](QDialog* dialog) {
            auto* edit = input<QLineEdit>(dialog);
            QCOMPARE(edit->text(), QString("original"));
            if (usesEla()) QVERIFY(edit->inherits("ElaLineEdit"));
            edit->setText(QString::fromUtf8("workspace_中文"));
            QTest::keyClick(edit, Qt::Key_Return);
        });
        QCOMPARE(UiDialogs::getText(nullptr, "Rename", "Workspace name", QLineEdit::Normal,
                                    "original", &accepted), QString::fromUtf8("workspace_中文"));
        QVERIFY(accepted);
        whenModal([](QDialog* dialog) {
            auto* edit = input<QLineEdit>(dialog);
            edit->setText("must not apply");
            QTest::keyClick(edit, Qt::Key_Escape);
        });
        QVERIFY(UiDialogs::getText(nullptr, "Rename", "Workspace name", QLineEdit::Normal,
                                  "original", &accepted).isEmpty());
        QVERIFY(!accepted);
    }

    void integerBoundsAndCancelledValue() {
        bool accepted = false;
        whenModal([](QDialog* dialog) {
            auto* spin = input<QSpinBox>(dialog);
            if (usesEla()) QVERIFY(spin->inherits("ElaSpinBox"));
            spin->setValue(1000);
            QTest::keyClick(spin, Qt::Key_Return);
        });
        QCOMPARE(UiDialogs::getInt(nullptr, "Go to Line", "Line:", 7, 1, 40, 1, &accepted), 40);
        QVERIFY(accepted);
        whenModal([](QDialog* dialog) {
            input<QSpinBox>(dialog)->setValue(15);
            dialog->close();
        });
        QCOMPARE(UiDialogs::getInt(nullptr, "Go to Line", "Line:", 7, 1, 40, 1, &accepted), 7);
        QVERIFY(!accepted);
    }

    void itemPopupDismissalKeepsDialogOpen() {
        bool accepted = false;
        whenModal([](QDialog* dialog) {
            auto* combo = input<QComboBox>(dialog);
            if (usesEla()) QVERIFY(combo->inherits("ElaComboBox"));
            combo->setCurrentIndex(1);
            combo->showPopup();
            QApplication::processEvents();
            QTest::keyClick(combo->view(), Qt::Key_Escape);
            QVERIFY(!combo->view()->isVisible());
            QVERIFY(dialog->isVisible());
            QTest::keyClick(combo, Qt::Key_Return);
        });
        QCOMPARE(UiDialogs::getItem(nullptr, "Select instance", "Instance:",
                                    {"top.a", "top.b", "top.c"}, 0, false, &accepted), QString("top.b"));
        QVERIFY(accepted);
        whenModal([](QDialog* dialog) {
            input<QComboBox>(dialog)->setCurrentIndex(1);
            QTest::keyClick(dialog, Qt::Key_Escape);
        });
        QCOMPARE(UiDialogs::getItem(nullptr, "Select instance", "Instance:",
                                   {"top.a", "top.b"}, 0, false, &accepted), QString("top.a"));
        QVERIFY(!accepted);
    }

    void buttonRolesAndDisabledActions() {
        QDialog dialog;
        auto* layout = new QVBoxLayout(&dialog);
        auto* box = UiDialogs::buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        auto* discard = UiDialogs::addButton(box, "Discard", QDialogButtonBox::DestructiveRole);
        layout->addWidget(box);
        QSignalSpy accepted(box, &QDialogButtonBox::accepted);
        QSignalSpy rejected(box, &QDialogButtonBox::rejected);
        dialog.show();
        QApplication::processEvents();
        QCOMPARE(box->buttonRole(discard), QDialogButtonBox::DestructiveRole);
        for (auto* button : box->buttons()) {
            if (usesEla()) QVERIFY(button->inherits("ElaPushButton"));
            QVERIFY(button->width() >= button->minimumSizeHint().width());
            QVERIFY(button->height() >= button->minimumSizeHint().height());
            QVERIFY(dialog.rect().contains(QRect(button->mapTo(&dialog, QPoint()), button->size())));
            button->setEnabled(false);
            button->click();
            QCOMPARE(accepted.count(), 0);
            QCOMPARE(rejected.count(), 0);
        }
        for (auto* button : box->buttons()) button->setEnabled(true);
        for (auto* button : box->buttons()) button->click();
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(rejected.count(), 1);
    }

    void unsavedCancelDiscardAndSaveFailure() {
        SharedDocument document("pending", "pending.sv", "module pending; endmodule\n");
        QTextCursor cursor(document.textDocument());
        cursor.insertText("// changed\n");
        UnsavedDocumentManager manager;
        int saveCalls = 0;
        auto save = [&](SharedDocument*) { ++saveCalls; return false; };
        whenModal([](QDialog* dialog) {
            auto* box = qobject_cast<QMessageBox*>(dialog);
            QVERIFY(box);
            QVERIFY(box->defaultButton());
            QVERIFY(box->escapeButton() == box->defaultButton());
            if (usesEla()) QVERIFY(box->defaultButton()->inherits("ElaPushButton"));
            QTest::keyClick(dialog, Qt::Key_Return);
        });
        QVERIFY(!manager.resolve({&document}, nullptr, save));
        QCOMPARE(saveCalls, 0);
        whenModal([](QDialog* dialog) {
            auto* box = qobject_cast<QMessageBox*>(dialog);
            for (auto* button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::DestructiveRole) button->click();
        });
        QVERIFY(manager.resolve({&document}, nullptr, save));
        QCOMPARE(saveCalls, 0);
        whenModal([](QDialog* dialog) {
            auto* box = qobject_cast<QMessageBox*>(dialog);
            for (auto* button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::AcceptRole) button->click();
        });
        QVERIFY(!manager.resolve({&document}, nullptr, save));
        QCOMPARE(saveCalls, 1);
        document.setExternalState(SharedDocumentExternalState::Conflict);
        whenModal([](QDialog* dialog) {
            auto* box = qobject_cast<QMessageBox*>(dialog);
            for (auto* button : box->buttons()) {
                if (box->buttonRole(button) == QMessageBox::AcceptRole) QVERIFY(!button->isEnabled());
            }
            QTest::keyClick(dialog, Qt::Key_Escape);
        });
        QVERIFY(!manager.resolve({&document}, nullptr, save));
        QCOMPARE(saveCalls, 1);
    }

    void warningEscapeAndEditorSearch() {
        whenModal([](QDialog* dialog) {
            auto* box = qobject_cast<QMessageBox*>(dialog);
            QVERIFY(box);
            QCOMPARE(box->text(), QString("Could not save."));
            if (usesEla()) QVERIFY(box->defaultButton()->inherits("ElaPushButton"));
            QTest::keyClick(dialog, Qt::Key_Escape);
        });
        UiDialogs::warning(nullptr, "Save", "Could not save.");
        MyCodeEditor editor;
        editor.resize(900, 500);
        editor.setPlainText("one\ntwo\nthree\n");
        editor.show();
        const QFont original = editor.font();
        whenModal([](QDialog* dialog) {
            input<QSpinBox>(dialog)->setValue(3);
            dialog->accept();
        });
        QCOMPARE(editor.showGotoLineDialog(), 3);
        QCOMPARE(editor.textCursor().blockNumber(), 2);
        editor.activateWindow();
        editor.showReplaceDialog();
        QApplication::processEvents();
        auto* bar = editor.findChild<QWidget*>("editorFindBar");
        QVERIFY(bar && bar->isVisible());
        auto* find = bar->findChild<QLineEdit*>("editorFindInput");
        QVERIFY(find);
        if (usesEla()) QVERIFY(find->inherits("ElaLineEdit"));
        for (auto* button : bar->findChildren<QPushButton*>()) {
            if (usesEla()) QVERIFY(button->inherits("ElaPushButton"));
            QVERIFY(button->width() >= button->minimumSizeHint().width());
        }
        find->setFocus();
        QTRY_VERIFY(find->hasFocus());
        find->setText("two");
        QSignalSpy searches(find, &QLineEdit::returnPressed);
        QTest::keyClick(find, Qt::Key_Return);
        QCOMPARE(searches.count(), 1);
        QCOMPARE(editor.textCursor().selectedText(), QString("two"));
        QCOMPARE(editor.toPlainText(), QString("one\ntwo\nthree\n"));
        auto* replacement = bar->findChild<QLineEdit*>("editorReplaceInput");
        QVERIFY(replacement);
        replacement->setFocus();
        QTRY_VERIFY(replacement->hasFocus());
        replacement->setText("TWO");
        QTest::keyClick(replacement, Qt::Key_Enter);
        QCOMPARE(editor.toPlainText(), QString("one\nTWO\nthree\n"));
        QTest::keyClick(replacement, Qt::Key_Escape);
        QTRY_VERIFY(!editor.findChild<QWidget*>("editorFindBar"));
        editor.undo();
        QCOMPARE(editor.toPlainText(), QString("one\ntwo\nthree\n"));
        QCOMPARE(editor.font(), original);
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir profile;
    if (!profile.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    if (!initializeUiStyleForTest()) return 3;
    UiDialogContractTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_dialog_contract_test.moc"
