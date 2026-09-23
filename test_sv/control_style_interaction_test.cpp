#include "applicationthememanager.h"
#include "contextfloatingwindow.h"
#include "contextrail.h"
#include "editorsplitcontroller.h"
#include "insightvisualstyle.h"
#include "mainwindow.h"
#include "settingscenterpanel.h"
#include "signalkernelgraphpanelcoordinator.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QGraphicsView>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalSpy>
#include <QStyleOptionButton>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
const QList<ThemeMode> modes{ThemeMode::Light, ThemeMode::Dark, ThemeMode::CatppuccinLatte,
    ThemeMode::CatppuccinFrappe, ThemeMode::CatppuccinMacchiato, ThemeMode::CatppuccinMocha};

void settle() { QApplication::processEvents(); QTest::qWait(10); }

void moveMouse(QWidget* widget, const QPoint& position)
{
    // QWindow coordinates are logical at every DPI; the QWidget QtTest helper
    // goes through the offscreen platform's synthetic global cursor instead.
    auto* window = widget->window();
    QTest::mouseMove(window->windowHandle(), widget->mapTo(window, position));
}

void capture(QWidget* widget, const QString& name)
{
    const QString root = qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
    if (root.isEmpty()) return;
    const QString directory = root + "/" + qEnvironmentVariable("QT_SCALE_FACTOR", "1");
    QDir().mkpath(directory);
    QVERIFY(widget->grab().save(directory + "/" + name + ".png"));
}

void reveal(QWidget* widget)
{
    for (auto* parent = widget->parentWidget(); parent; parent = parent->parentWidget())
        if (auto* scroll = qobject_cast<QScrollArea*>(parent)) scroll->ensureWidgetVisible(widget);
    widget->setFocus(Qt::TabFocusReason);
    settle();
}

int colorPixels(const QImage& image, const QRect& logicalRect, const QColor& color)
{
    const qreal dpr = image.devicePixelRatio();
    const QRect physical = QRectF(logicalRect.x() * dpr, logicalRect.y() * dpr,
                                  logicalRect.width() * dpr, logicalRect.height() * dpr).toAlignedRect();
    int count = 0;
    for (int y = physical.top(); y <= physical.bottom(); ++y)
        for (int x = physical.left(); x <= physical.right(); ++x) {
            if (!image.rect().contains(x, y)) continue;
            const auto pixel = image.pixelColor(x, y);
            if (qAbs(pixel.red() - color.red()) + qAbs(pixel.green() - color.green())
                + qAbs(pixel.blue() - color.blue()) < 35) ++count;
        }
    return count;
}
}

class ControlStyleInteractionTest final : public QObject {
    Q_OBJECT
private slots:
    void states_data()
    {
        QTest::addColumn<ThemeMode>("mode");
        for (auto mode : modes) QTest::newRow(qPrintable(QString::number(int(mode)))) << mode;
    }

    void states()
    {
        QFETCH(ThemeMode, mode);
        ApplicationThemeManager::instance().setMode(mode);
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        QList<QAbstractButton*> controls;
        auto* normal = new QPushButton("Open", &host);
        auto* toolbar = new QPushButton("Inspect", &host);
        InsightVisualStyle::applyToolbarButton(toolbar);
        auto* primary = new QPushButton("Apply", &host);
        InsightVisualStyle::applyPrimaryButton(primary);
        auto* tool = new QToolButton(&host);
        tool->setText("Export"); tool->setToolButtonStyle(Qt::ToolButtonTextOnly);
        auto* check = new QCheckBox("Enabled", &host);
        auto* segment = new QCheckBox("Inputs", &host);
        InsightVisualStyle::applySegmentedCheckBox(segment);
        controls = {normal, toolbar, primary, tool, check, segment};
        for (auto* button : controls) { layout->addWidget(button); button->setFocusPolicy(Qt::StrongFocus); }
        host.show(); host.activateWindow(); settle();
        int index = 0;
        for (auto* button : controls) {
            QVERIFY(button->styleSheet().isEmpty());
            QSignalSpy clicked(button, &QAbstractButton::clicked);
            const QSize hint = button->sizeHint();
            const QSize size = button->size();
            button->clearFocus();
            moveMouse(&host, QPoint(host.width() - 1, host.height() - 1)); settle();
            const QImage ordinary = button->grab().toImage();
            moveMouse(button, button->rect().center()); settle();
            const QImage hover = button->grab().toImage();
            QVERIFY(hover != ordinary);
            QTest::mousePress(button, Qt::LeftButton); settle();
            QVERIFY(button->isDown());
            const QImage pressed = button->grab().toImage();
            QVERIFY(pressed != hover);
            QCOMPARE(button->sizeHint(), hint);
            QCOMPARE(button->size(), size);
            // Drag outside cancels activation; there must be no delayed click.
            moveMouse(button, QPoint(-8, -8));
            QTest::mouseRelease(button, Qt::LeftButton, Qt::NoModifier, QPoint(-8, -8));
            QCOMPARE(clicked.count(), 0);
            QVERIFY(!button->isDown());
            moveMouse(&host, QPoint(host.width() - 1, host.height() - 1));
            button->setFocus(Qt::TabFocusReason); settle();
            QVERIFY(button->hasFocus());
            QVERIFY(button->grab().toImage() != ordinary);
            QCOMPARE(button->sizeHint(), hint);
            QTest::keyClick(button, Qt::Key_Space);
            QCOMPARE(clicked.count(), 1);
            for (int n = 0; n < 12; ++n) QTest::mouseClick(button, Qt::LeftButton);
            QCOMPARE(clicked.count(), 13);
            const QImage enabled = button->grab().toImage();
            QTest::mousePress(button, Qt::LeftButton);
            button->setEnabled(false);
            QTest::mouseRelease(button, Qt::LeftButton);
            QTest::keyClick(button, Qt::Key_Space);
            QCOMPARE(clicked.count(), 13);
            QVERIFY(!button->isDown());
            QVERIFY(button->grab().toImage() != enabled);
            QCOMPARE(button->sizeHint(), hint);
            button->setEnabled(true);
            QTest::mousePress(button, Qt::LeftButton);
            button->hide(); button->show(); settle();
            QTest::mouseRelease(button, Qt::LeftButton);
            QCOMPARE(clicked.count(), 13);
            QVERIFY(!button->isDown());
            button->clearFocus();

            if (auto* box = qobject_cast<QCheckBox*>(button)) {
                box->setCheckState(Qt::Checked); settle();
                QStyleOptionButton option;
                option.initFrom(box); option.state |= QStyle::State_On;
                const QRect indicator = box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, box);
                const QImage checked = box->grab().toImage();
                QVERIFY2(colorPixels(checked, indicator.adjusted(3, 3, -3, -3),
                    InsightVisualStyle::theme().button.textChecked) >= 3, "Checked indicator must contain a visible tick");
                box->setCheckState(Qt::PartiallyChecked); settle();
                QVERIFY(box->grab().toImage() != checked);
                capture(box, QString("state-%1-%2-mixed").arg(int(mode)).arg(index));
                box->setCheckState(Qt::Checked);
            } else {
                button->setCheckable(true); button->setChecked(true);
                QVERIFY2(button->grab().toImage() != ordinary, qPrintable(button->text()));
            }
            capture(button, QString("state-%1-%2-checked").arg(int(mode)).arg(index++));
            QCOMPARE(button->size(), size);
        }
        // The style must not change QWidget keyboard navigation or dialog defaults.
        normal->setFocus(Qt::TabFocusReason); settle();
        QTest::keyClick(normal, Qt::Key_Tab); QCOMPARE(host.focusWidget(), toolbar);
        QTest::keyClick(toolbar, Qt::Key_Tab, Qt::ShiftModifier); QCOMPARE(host.focusWidget(), normal);
        QDialog dialog;
        auto* defaultButton = new QPushButton("Apply", &dialog);
        defaultButton->setDefault(true); InsightVisualStyle::applyPrimaryButton(defaultButton);
        auto* dialogLayout = new QVBoxLayout(&dialog); dialogLayout->addWidget(defaultButton);
        QSignalSpy accepted(defaultButton, &QAbstractButton::clicked);
        dialog.show(); dialog.activateWindow(); settle();
        QTest::keyClick(defaultButton, Qt::Key_Return); QCOMPARE(accepted.count(), 1);
    }

    void mainWindowAndFloating()
    {
        MainWindow window;
        window.resize(1080, 780); window.show(); settle();
        auto* contextRail = window.findChild<ContextRail*>();
        QVERIFY(contextRail);
        const auto contextEntries = contextRail->entryIds();
        QCOMPARE(contextEntries.size(), 6);
        QVERIFY(!contextEntries.contains(QStringLiteral("workspaceHub")));
        for (const auto& id : {"temporaryEditor", "rtlInsight.kernel", "rtlInsight.block",
                               "rtlInsight.hotspot", "rtlInsight.state", "pinloom"})
            QVERIFY2(contextEntries.contains(QString::fromLatin1(id)), id);
        if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela) {
            auto* splits = window.findChild<EditorSplitController*>();
            QVERIFY(splits);
            auto* initialTabs = splits->initialGroup();
            QVERIFY(initialTabs);
            QVERIFY(initialTabs->tabBar()->inherits("ElaTabBar"));
        }
        auto* expandSidebar = window.findChild<QToolButton*>("expandProjectSidebarButton");
        QVERIFY(expandSidebar);
        QTest::mouseClick(expandSidebar, Qt::LeftButton);
        auto* settingsButton = window.findChild<QToolButton*>("settingsRailButton");
        QVERIFY(settingsButton);
        QTRY_VERIFY(settingsButton->isVisible());
        if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela)
            QVERIFY(settingsButton->inherits("ElaToolButton"));
        QTest::mouseClick(settingsButton, Qt::LeftButton); settle();
        auto* settings = window.findChild<SettingsCenterPanel*>(); QVERIFY(settings);
        settings->selectCategory("analysis"); settle();
        auto* enabled = qobject_cast<QCheckBox*>(settings->fieldEditor("analysis.enabled")); QVERIFY(enabled);
        auto* overrideCheck = settings->findChild<QCheckBox*>(
            SettingsCenterPanel::fieldOverrideObjectName("analysis.enabled")); QVERIFY(overrideCheck);
        auto* apply = settings->findChild<QPushButton*>("settingsCenterApplyButton"); QVERIFY(apply);
        auto* revert = settings->findChild<QPushButton*>("settingsCenterRevertButton"); QVERIFY(revert);
        const auto font = enabled->font();
        SignalKernelGraphPanelCoordinator kernel(&window);
        SignalKernelGraphReport report;
        report.found = true; report.kernel.id = 0; report.kernel.displayName = "ready";
        SignalKernelGraphNode input;
        input.id = 1; input.role = SignalKernelGraphNodeRole::Input; input.displayName = "start";
        report.inputs.append(input); report.edges.append({1, 0, "data"});
        kernel.renderReportForTest(report);
        auto* panel = kernel.dock()->widget();
        auto* inputs = panel->findChild<QCheckBox*>("signalKernelGraphShowInputsCheck"); QVERIFY(inputs);
        ContextFloatingWindow floating(&window, window.centralWidget());
        ContextResource resource; resource.providerId = "control-style-test"; resource.resourceId = "kernel";
        resource.title = "Kernel";
        for (auto mode : modes) {
            ApplicationThemeManager::instance().setMode(mode); settle();
            window.activateWindow(); settle();
            for (auto* button : window.findChildren<QToolButton*>()) {
                auto* parent = button->parentWidget();
                if (!button->isVisible() || !parent
                    || (parent->objectName() != QStringLiteral("projectSidebarHeader")
                        && parent->objectName() != QStringLiteral("contextRail"))) continue;
                QVERIFY2(button->width() >= button->minimumSizeHint().width(), qPrintable(button->objectName()));
                QVERIFY2(button->height() >= button->minimumSizeHint().height(), qPrintable(button->objectName()));
            }
            reveal(overrideCheck);
            QTest::keyClick(overrideCheck, Qt::Key_Space); settle();
            QVERIFY(enabled->isEnabled());
            reveal(enabled);
            const bool original = enabled->isChecked();
            QTest::keyClick(enabled, Qt::Key_Space); settle();
            QCOMPARE(enabled->isChecked(), !original);
            QVERIFY(apply->isEnabled()); QVERIFY(revert->isEnabled());
            capture(&window, QString("main-settings-%1").arg(int(mode)));
            QTest::mouseClick(revert, Qt::LeftButton); settle();
            QCOMPARE(enabled->isChecked(), original);
            QVERIFY(!apply->isEnabled()); QCOMPARE(enabled->font(), font);

            floating.setView(resource, panel); floating.show(); settle();
            QVERIFY(inputs->isVisible());
            const auto transform = kernel.view()->transform();
            inputs->setFocus(Qt::TabFocusReason);
            QTest::keyClick(inputs, Qt::Key_Space); settle();
            QVERIFY(!inputs->isChecked()); QCOMPARE(kernel.visibleGraphNodeCountForTest(), 1);
            QTest::keyClick(inputs, Qt::Key_Space); settle();
            QCOMPARE(kernel.visibleGraphNodeCountForTest(), 2);
            QCOMPARE(kernel.view()->transform(), transform);
            capture(&floating, QString("floating-kernel-%1").arg(int(mode)));
            QCOMPARE(floating.takeView(), panel);
            kernel.dock()->setWidget(panel); panel->show(); floating.hide(); settle();
            QVERIFY(inputs->styleSheet().isEmpty());
            capture(panel, QString("docked-kernel-%1").arg(int(mode)));
        }
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settings;
    if (!settings.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    if (qEnvironmentVariable("ZEROSLACK_TEST_UI_STYLE") == "ela"
        && !ApplicationThemeManager::instance().selectBackend(UiStyleBackend::Ela)) return 3;
    if (qEnvironmentVariable("ZEROSLACK_TEST_UI_STYLE") == "qlementine") {
        if (!ApplicationThemeManager::instance().selectBackend(UiStyleBackend::Qlementine)) return 3;
        ApplicationThemeManager::instance().setAnimationsEnabled(false);
    }
    ApplicationThemeManager::instance().applyToApplication();
    ControlStyleInteractionTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "control_style_interaction_test.moc"
