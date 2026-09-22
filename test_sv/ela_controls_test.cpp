#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "mycodeeditor.h"
#include "navigationwidget.h"
#include "settingscenterpanel.h"
#include "settingscenterservice.h"
#include "uicontrols.h"
#include "uitypography.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QRadioButton>
#include <QButtonGroup>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <memory>

namespace {
void settle() { QApplication::processEvents(); QTest::qWait(20); }
const QList<ThemeMode> modes{ThemeMode::Light, ThemeMode::Dark, ThemeMode::CatppuccinLatte,
    ThemeMode::CatppuccinFrappe, ThemeMode::CatppuccinMacchiato, ThemeMode::CatppuccinMocha};
}

class ElaControlsTest final : public QObject {
    Q_OBJECT
private slots:
    void actualWidgetsAndKeyboardContracts() {
        QDialog host;
        auto* layout = new QVBoxLayout(&host);
        auto* button = UiControls::pushButton("&Apply", &host);
        auto* tool = UiControls::toolButton(&host);
        auto* edit = UiControls::lineEdit(&host);
        auto* combo = UiControls::comboBox(&host);
        auto* check = UiControls::checkBox("Enabled", &host);
        auto* spin = UiControls::spinBox(&host);
        auto* decimal = UiControls::doubleSpinBox(&host);
        auto* slider = UiControls::slider(Qt::Horizontal, &host);
        const QList<QWidget*> controls{button, tool, edit, combo, check, spin, decimal, slider};
        const QList<const char*> types{"ElaPushButton", "ElaToolButton", "ElaLineEdit", "ElaComboBox",
            "ElaCheckBox", "ElaSpinBox", "ElaDoubleSpinBox", "ElaSlider"};
        for (int i = 0; i < controls.size(); ++i) {
            QVERIFY(controls[i]->inherits(types[i]));
            layout->addWidget(controls[i]);
            QCOMPARE(controls[i]->font().pixelSize(), UiTypography::font().pixelSize());
            QVERIFY(controls[i]->maximumHeight() > controls[i]->minimumHeight());
        }
        combo->addItems({"Light", "Dark", "Latte"});
        button->setDefault(true);
        tool->setText("Toggle"); tool->setCheckable(true);
        host.resize(320, 440); host.show(); host.activateWindow(); settle();
        QSignalSpy clicks(button, &QPushButton::clicked);
        QTest::keyClick(button, Qt::Key_Return); QCOMPARE(clicks.count(), 1);
        button->setFocus(Qt::TabFocusReason); settle();
        const auto normal = button->grab().toImage();
        QTest::keyPress(button, Qt::Key_Space); settle();
        QVERIFY(button->isDown()); QVERIFY(normal != button->grab().toImage());
        QTest::keyRelease(button, Qt::Key_Space); QCOMPARE(clicks.count(), 2);
        button->setCheckable(true);
        QTest::mouseClick(button, Qt::LeftButton); QVERIFY(button->isChecked());
        button->setEnabled(false);
        const int disabledCount = clicks.count();
        QTest::keyClick(button, Qt::Key_Space); QCOMPARE(clicks.count(), disabledCount);
        button->setEnabled(true);
        QTest::keyClick(tool, Qt::Key_Space); QVERIFY(tool->isChecked());
        check->setTristate(true);
        check->setCheckState(Qt::PartiallyChecked); settle();
        const auto partial = check->grab().toImage();
        check->setCheckState(Qt::Checked); settle();
        QVERIFY(partial != check->grab().toImage());
        QTest::keyClicks(edit, "filter.sv"); QCOMPARE(edit->text(), QString("filter.sv"));
        spin->setValue(12); QTest::keyClick(spin, Qt::Key_Up); QCOMPARE(spin->value(), 13);
        decimal->setValue(1.5); QTest::keyClick(decimal, Qt::Key_Up); QCOMPARE(decimal->value(), 2.5);
        for (int i = 0; i < 12; ++i) {
            combo->showPopup(); QApplication::processEvents();
            QVERIFY(combo->view()->isVisible());
            // Dismiss before the old upstream 400 ms opening animation ended.
            QTest::keyClick(combo->view(), Qt::Key_Escape); settle();
            QVERIFY(!combo->view()->isVisible());
        }
        combo->setFocus();
        QTest::keyClick(combo, Qt::Key_Down); QCOMPARE(combo->currentIndex(), 1);
        for (auto* control : controls) {
            QVERIFY2(control->height() >= control->minimumSizeHint().height(), control->metaObject()->className());
            QVERIFY(host.rect().contains(QRect(control->mapTo(&host, QPoint()), control->size())));
        }
    }

    void iconAndCheckRendering() {
        auto* button = UiControls::pushButton("", nullptr);
        std::unique_ptr<QPushButton> owner(button);
        button->resize(100, 40); button->show(); settle();
        const auto blank = button->grab().toImage();
        QPixmap icon(16, 16); icon.fill(Qt::red); button->setIcon(QIcon(icon)); settle();
        QVERIFY(blank != button->grab().toImage());
        button->setCheckable(true);
        const auto unchecked = button->grab().toImage();
        button->setChecked(true); settle();
        QVERIFY(unchecked != button->grab().toImage());
        std::unique_ptr<QToolButton> arrow(UiControls::toolButton());
        arrow->resize(40, 40); arrow->show(); settle();
        const auto empty = arrow->grab().toImage();
        arrow->setArrowType(Qt::DownArrow); settle();
        const auto down = arrow->grab().toImage();
        QVERIFY(empty != down);
        arrow->setArrowType(Qt::RightArrow); settle();
        QVERIFY(down != arrow->grab().toImage());
    }

    void themeAndEditorIsolation() {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* editor = new MyCodeEditor(&host);
        editor->setPlainText("module sample(input logic clk);\nendmodule\n");
        auto* button = UiControls::pushButton("Apply", &host);
        auto* check = UiControls::checkBox("Enabled", &host);
        layout->addWidget(editor); layout->addWidget(button); layout->addWidget(check);
        host.resize(620, 360); host.show(); settle();
        const auto editorFont = editor->font();
        const auto documentFont = editor->document()->defaultFont();
        const auto controlFont = button->font();
        auto* editorStyle = editor->style();
        for (int repeat = 0; repeat < 2; ++repeat) for (auto mode : modes) {
            ApplicationThemeManager::instance().setMode(mode); settle();
            QCOMPARE(editor->font(), editorFont);
            QCOMPARE(editor->document()->defaultFont(), documentFont);
            QCOMPARE(editor->style(), editorStyle);
            QCOMPARE(button->font(), controlFont);
            QVERIFY(!editor->property("_zeroslackClassicChild").toBool());
            std::unique_ptr<QLineEdit> late(UiControls::lineEdit("late control"));
            QCOMPARE(late->font(), controlFont);
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        QVERIFY(!ApplicationThemeManager::instance().selectBackend(UiStyleBackend::Classic));
    }

    void visibleControlDestruction() {
        const QList<QWidget*> controls{UiControls::pushButton("Close"), UiControls::toolButton(),
            UiControls::lineEdit("Close"), UiControls::comboBox(), UiControls::checkBox("Close"),
            UiControls::spinBox(), UiControls::doubleSpinBox(), UiControls::slider(Qt::Horizontal),
            UiControls::radioButton("Choice")};
        for (auto* control : controls) {
            qInfo() << "destroy-visible" << control->metaObject()->className();
            control->resize(180, 40); control->show(); control->activateWindow(); settle();
            delete control;
            settle();
        }
    }

    void radioExclusiveKeyboardAndDisabledState() {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        QButtonGroup group(&host);
        auto* first = UiControls::radioButton("&Decimal", &host);
        auto* second = UiControls::radioButton("&Hexadecimal", &host);
        for (auto* radio : {first, second}) {
            QVERIFY(radio->inherits("ElaRadioButton"));
            group.addButton(radio); layout->addWidget(radio);
            QCOMPARE(radio->font(), UiTypography::font());
        }
        host.show(); host.activateWindow(); first->setChecked(true); settle();
        QSignalSpy toggled(second, &QRadioButton::toggled);
        second->setFocus(); QTest::keyClick(second, Qt::Key_Space); settle();
        QVERIFY(second->isChecked()); QVERIFY(!first->isChecked()); QCOMPARE(toggled.count(), 1);
        const auto checked = second->grab().toImage();
        second->setEnabled(false); settle();
        QVERIFY(checked != second->grab().toImage());
        QTest::keyClick(second, Qt::Key_Space); QCOMPARE(toggled.count(), 1);
        for (auto mode : modes) {
            ApplicationThemeManager::instance().setMode(mode); settle();
            QVERIFY(second->isChecked());
            QCOMPARE(second->palette().color(QPalette::Disabled, QPalette::WindowText),
                     InsightVisualStyle::theme().button.textDisabled);
            QVERIFY(first->height() >= first->minimumSizeHint().height());
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }

    void comboPopupStyleOutlivesPopup() {
        for (bool editable : {false, true}) {
            QWidget host;
            auto* layout = new QVBoxLayout(&host);
            auto* combo = UiControls::comboBox(&host);
            combo->setEditable(editable);
            combo->addItems({"top", "counter"});
            layout->addWidget(combo);
            host.show(); settle();
            combo->showPopup(); settle();
            QPointer<QWidget> popup = combo->view()->window();
            QVERIFY(popup && popup->isVisible());
            combo->hidePopup();
            QPointer<QStyle> localStyle;
            for (auto* style : qApp->findChildren<QStyle*>(QString(), Qt::FindDirectChildrenOnly))
                if (style->inherits("ElaComboBoxStyle")) localStyle = style;
            QVERIFY(localStyle);
            bool styleAliveAtPopupDestruction = false;
            connect(popup, &QObject::destroyed, &host, [&] {
                styleAliveAtPopupDestruction = !localStyle.isNull();
            });
            combo->hide();
            combo->setParent(nullptr);
            delete combo;
            QVERIFY(popup.isNull());
            QVERIFY(styleAliveAtPopupDestruction);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(localStyle.isNull());
        }
    }

    void realSettingsAndNavigation() {
        SettingsCenterService service;
        SettingsCenterPanel panel(&service);
        panel.resize(820, 640); panel.show(); panel.selectCategory("analysis"); settle();
        auto* enabled = qobject_cast<QCheckBox*>(panel.fieldEditor("analysis.enabled"));
        QVERIFY(enabled); QVERIFY(enabled->inherits("ElaCheckBox"));
        auto* overrideCheck = panel.findChild<QCheckBox*>(
            SettingsCenterPanel::fieldOverrideObjectName("analysis.enabled"));
        QVERIFY(overrideCheck);
        if (!enabled->isEnabled()) {
            overrideCheck->setFocus(); QTest::keyClick(overrideCheck, Qt::Key_Space); settle();
        }
        QVERIFY(enabled->isEnabled());
        auto* apply = panel.findChild<QPushButton*>("settingsCenterApplyButton");
        auto* revert = panel.findChild<QPushButton*>("settingsCenterRevertButton");
        QVERIFY(apply && revert); QVERIFY(apply->inherits("ElaPushButton"));
        const bool initial = enabled->isChecked();
        enabled->setFocus(); QTest::keyClick(enabled, Qt::Key_Space); settle();
        QVERIFY(enabled->isChecked() != initial); QVERIFY(apply->isEnabled());
        QTest::mouseClick(revert, Qt::LeftButton); settle();
        QCOMPARE(enabled->isChecked(), initial); QVERIFY(!apply->isEnabled());
        NavigationWidget navigation;
        auto* search = navigation.findChild<QLineEdit*>();
        QVERIFY(search && search->inherits("ElaLineEdit"));
        navigation.setSearchFilter(NavigationWidget::FileTab, "uart");
        QCOMPARE(navigation.searchFilter(NavigationWidget::FileTab), QString("uart"));
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir profile;
    if (!profile.isValid()) return 2;
    QCoreApplication::setApplicationName("ZeroSlack-Ela-Test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", (profile.path() + "/sessions.ini").toUtf8());
    auto& manager = ApplicationThemeManager::instance();
    if (!manager.selectBackend(UiStyleBackend::Ela)) return 3;
    manager.applyToApplication();
    ElaControlsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ela_controls_test.moc"
