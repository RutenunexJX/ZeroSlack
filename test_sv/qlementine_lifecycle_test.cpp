#include "applicationthememanager.h"
#include "contextfloatingwindow.h"
#include "editorappearance.h"
#include "editorappearancesettings.h"
#include "customabbreviationservice.h"
#include "settingscenterkeys.h"
#include "workspacemanager.h"
#include "editorradialmenu.h"
#include "insightgraphview.h"
#include "insightvisualstyle.h"
#include "mycodeeditor.h"
#include "mainwindow.h"
#include "signalkernelgraphpanelcoordinator.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <memory>

namespace {
void settle() { QApplication::processEvents(); QTest::qWait(20); }
const QList<ThemeMode> themes{ThemeMode::Light, ThemeMode::Dark, ThemeMode::CatppuccinLatte,
    ThemeMode::CatppuccinFrappe, ThemeMode::CatppuccinMacchiato, ThemeMode::CatppuccinMocha};

void capture(QWidget* widget, const QString& name)
{
    const QString root = qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
    if (root.isEmpty()) return;
    QDir().mkpath(root);
    QVERIFY(widget->grab().save(root + '/' + name + ".png"));
}
}

class QlementineLifecycleTest final : public QObject {
    Q_OBJECT
private slots:
    void profileIsolation()
    {
        // The organization/application constructor silently selects NativeFormat.
        // Seed only the isolated INI profile and check the actual service paths.
        QSettings profile(QSettings::IniFormat, QSettings::UserScope, "ZeroSlack", "ZeroSlack");
        QTemporaryDir project;
        QVERIFY(project.isValid());
        profile.beginWriteArray("recentWorkspaces/items", 1);
        profile.setArrayIndex(0);
        profile.setValue("alias", "Isolated preview workspace");
        profile.setValue("path", project.path());
        profile.endArray();
        profile.setValue(SettingsCenterKeys::FontSizePt, 19);
        profile.beginWriteArray("customAbbreviations/items", 1);
        profile.setArrayIndex(0);
        profile.setValue("id", "preview-only");
        profile.setValue("abbreviation", "previewonly");
        profile.setValue("commandToken", "fl");
        profile.endArray();
        profile.sync();
        WorkspaceManager workspaces;
        QCOMPARE(workspaces.recentWorkspaceEntries().size(), 1);
        QCOMPARE(workspaces.recentWorkspaceEntries().first().path, project.path());
        QVERIFY(workspaces.removeRecentWorkspace(project.path()));
        profile.sync();
        QCOMPARE(profile.beginReadArray("recentWorkspaces/items"), 0);
        profile.endArray();
        EditorAppearanceSettings appearance;
        QCOMPARE(appearance.options().fontSizePt, 19);
        appearance.setFontSizePt(17);
        profile.sync();
        QCOMPARE(profile.value(SettingsCenterKeys::FontSizePt).toInt(), 17);
        CustomAbbreviationService abbreviations;
        QCOMPARE(abbreviations.records().size(), 1);
        QCOMPARE(abbreviations.records().first().id, QString("preview-only"));
        QVERIFY(abbreviations.removeRecord("preview-only"));
        profile.sync();
        QCOMPARE(profile.beginReadArray("customAbbreviations/items"), 0);
        profile.endArray();
        profile.clear();
    }

    void protectedSurfaces()
    {
        auto& manager = ApplicationThemeManager::instance();
        manager.setAnimationsEnabled(false);
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* editor = new MyCodeEditor;
        editor->setPlainText("module sample(input logic clk);\n  logic ready;\nendmodule\n");
        layout->addWidget(editor);
        host.resize(650, 300); host.show(); settle();
        editor->setFocus(); editor->setCursorWidth(0); settle();
        const auto font = editor->font();
        const auto documentFont = editor->document()->defaultFont();
        for (auto mode : themes) {
            manager.setMode(mode); settle();
            QCOMPARE(editor->font(), font);
            QCOMPARE(editor->document()->defaultFont(), documentFont);
            capture(editor, QString("editor-%1").arg(int(mode)));
            QMenu menu;
            auto* action = menu.addAction("Kernel");
            action->setObjectName("insight.signalKernelGraph");
            EditorRadialMenu radial(&menu);
            radial.show(); settle();
            capture(&radial, QString("radial-%1").arg(int(mode)));
            auto* group = radial.findChild<QToolButton*>("radialGroup.0");
            QVERIFY(group);
            if (manager.backend() == UiStyleBackend::Qlementine)
                QVERIFY(group->property("_zeroslackClassicChild").toBool());
        }
        manager.setMode(ThemeMode::Light); settle();
        const auto sheet = editor->styleSheet();
        for (int i = 0; i < 8; ++i) {
            manager.setMode(ThemeMode::Dark); settle();
            manager.setMode(ThemeMode::Light); settle();
        }
        QCOMPARE(editor->styleSheet(), sheet);
        if (manager.backend() == UiStyleBackend::Qlementine) {
            // A late child must inherit the boundary, and leave it when moved.
            auto* late = new QPushButton("Late", editor);
            late->show(); settle();
            QVERIFY(late->property("_zeroslackClassicChild").toBool());
            late->setParent(&host); late->show(); settle();
            QVERIFY(!late->property("_zeroslackClassicChild").toBool());
            late->setParent(editor); late->show(); settle();
            QVERIFY(late->property("_zeroslackClassicChild").toBool());
        }
    }

    void disabledContentRemainsVisible()
    {
        auto& manager = ApplicationThemeManager::instance();
        manager.setAnimationsEnabled(false);
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* primary = new QPushButton("Apply");
        InsightVisualStyle::applyPrimaryButton(primary);
        primary->setFixedSize(180, 38);
        primary->setEnabled(false);
        auto* check = new QCheckBox("Enabled");
        check->setTristate(true);
        check->setEnabled(false);
        layout->addWidget(primary); layout->addWidget(check);
        host.show(); settle();
        for (auto mode : themes) {
            manager.setMode(mode); settle();
            primary->setText("Apply"); settle();
            const QImage text = primary->grab().toImage();
            primary->setText(""); settle();
            QVERIFY2(primary->grab().toImage() != text, "Disabled action text must remain visible");
            check->setCheckState(Qt::Checked); settle();
            const QImage checked = check->grab().toImage();
            check->setCheckState(Qt::PartiallyChecked); settle();
            QVERIFY2(check->grab().toImage() != checked, "Disabled tick and partial mark must be distinguishable");
        }
        manager.setMode(ThemeMode::Light);
    }

    void animatedControlsAndPopups()
    {
        auto& manager = ApplicationThemeManager::instance();
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* combo = new QComboBox;
        combo->addItems({"Light", "Dark", "Latte"});
        layout->addWidget(combo);
        auto* apply = new QPushButton("Apply");
        auto* another = new QPushButton("Inspect");
        InsightVisualStyle::applyPrimaryButton(apply);
        layout->addWidget(apply); layout->addWidget(another);
        host.resize(280, 200); host.show(); host.activateWindow(); settle();
        QSignalSpy clicked(apply, &QPushButton::clicked);
        for (int i = 0; i < 16; ++i) {
            manager.setAnimationsEnabled(true);
            QTest::mouseMove(host.windowHandle(), apply->mapTo(&host, apply->rect().center()));
            QTest::mouseClick(apply, Qt::LeftButton);
            apply->grab(); another->grab(); combo->grab(); settle();
            manager.setAnimationsEnabled(false);
            QCOMPARE(clicked.count(), i + 1);
            combo->showPopup(); settle();
            QTest::keyClick(combo->view(), i % 2 ? Qt::Key_Up : Qt::Key_Down);
            QTest::keyClick(combo->view(), Qt::Key_Return); settle();
            QVERIFY(!combo->view()->isVisible());
            QCOMPARE(combo->currentIndex(), i % 2 ? 0 : 1);
        }
        // Sampling describes event-loop render transitions, not DWM frame rate.
        manager.setAnimationsEnabled(true);
        QTest::mouseMove(host.windowHandle(), QPoint(1, 1)); QTest::qWait(250);
        const auto before = apply->grab().toImage();
        QTest::mouseMove(host.windowHandle(), apply->mapTo(&host, apply->rect().center()));
        QList<QImage> distinct;
        QElapsedTimer timer; timer.start();
        for (int i = 0; i < 16; ++i) {
            QTest::qWait(16);
            const auto frame = apply->grab().toImage();
            if (!distinct.contains(frame)) distinct.append(frame);
        }
        qInfo() << "render sample ms" << timer.elapsed() << "distinct frames" << distinct.size();
        if (manager.backend() == UiStyleBackend::Qlementine) QVERIFY(distinct.size() > 2);
        QVERIFY(apply->grab().toImage() != before);
        manager.setAnimationsEnabled(false);
    }

    void windowAndFloatingLifecycle()
    {
        MainWindow window;
        window.resize(1080, 780); window.show(); settle();
        auto* brand = window.findChild<QToolButton*>("welcomeOpenProjectButton");
        QVERIFY(brand);
        brand->clearFocus();
        QTest::mouseMove(window.windowHandle(), QPoint(1, 1)); settle();
        const QImage brandImage = brand->grab().toImage();
        const QColor accent = InsightVisualStyle::theme().accent;
        int accentPixels = 0;
        for (int y = 0; y < brandImage.height(); ++y)
            for (int x = 0; x < brandImage.width(); ++x) {
                const QColor pixel = brandImage.pixelColor(x, y);
                if (qAbs(pixel.red() - accent.red()) + qAbs(pixel.green() - accent.green())
                    + qAbs(pixel.blue() - accent.blue()) < 20) ++accentPixels;
            }
        QVERIFY2(accentPixels > 20, "Brand accent must not be recolored to monochrome by QStyle");
        for (auto* screen : QGuiApplication::screens())
            qInfo() << "screen" << screen->name() << screen->geometry() << screen->devicePixelRatio();
        for (int i = 0; i < 3; ++i) {
            window.showMaximized(); settle(); QVERIFY(window.isMaximized());
            window.showNormal(); settle(); QVERIFY(!window.isMaximized());
            window.showMinimized(); settle(); QVERIFY(window.isMinimized());
            window.showNormal(); settle(); QVERIFY(!window.isMinimized());
        }
        SignalKernelGraphPanelCoordinator kernel(&window);
        SignalKernelGraphReport report;
        report.found = true; report.kernel.id = 0; report.kernel.displayName = "ready";
        SignalKernelGraphNode input;
        input.id = 1; input.role = SignalKernelGraphNodeRole::Input; input.displayName = "start";
        report.inputs.append(input); report.edges.append({1, 0, "data"});
        kernel.renderReportForTest(report);
        auto* panel = kernel.dock()->widget();
        auto* inputs = panel->findChild<QCheckBox*>("signalKernelGraphShowInputsCheck");
        QVERIFY(inputs);
        ContextResource resource; resource.providerId = "qlementine-product-test";
        resource.resourceId = "kernel"; resource.title = "Kernel";
        for (auto mode : themes) {
            ApplicationThemeManager::instance().setMode(mode); settle();
            for (int i = 0; i < 3; ++i) {
                auto floating = std::make_unique<ContextFloatingWindow>(&window, window.centralWidget());
                floating->setView(resource, panel); floating->show(); settle();
                const auto transform = kernel.view()->transform();
                inputs->setFocus(); QTest::keyClick(inputs, Qt::Key_Space); settle();
                QCOMPARE(kernel.visibleGraphNodeCountForTest(), 1);
                QTest::keyClick(inputs, Qt::Key_Space); settle();
                QCOMPARE(kernel.visibleGraphNodeCountForTest(), 2);
                QCOMPARE(kernel.view()->transform(), transform);
                QVERIFY(inputs->height() >= inputs->minimumSizeHint().height());
                if (i == 0) capture(floating.get(), QString("floating-%1").arg(int(mode)));
                QCOMPARE(floating->takeView(), panel);
                kernel.dock()->setWidget(panel); panel->show();
                floating.reset(); settle();
            }
        }
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir storage;
    if (!storage.isValid()) return 2;
    QCoreApplication::setApplicationName("ZeroSlack-Qlementine-Test");
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storage.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, storage.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", (storage.path() + "/sessions.ini").toUtf8());
    auto& manager = ApplicationThemeManager::instance();
    const bool classic = qEnvironmentVariable("ZEROSLACK_TEST_UI_STYLE") == "classic";
    if (!manager.selectBackend(classic ? UiStyleBackend::Classic : UiStyleBackend::Qlementine)) return 3;
    manager.applyToApplication();
    if (manager.selectBackend(classic ? UiStyleBackend::Qlementine : UiStyleBackend::Classic)) return 4;
    QlementineLifecycleTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "qlementine_lifecycle_test.moc"
