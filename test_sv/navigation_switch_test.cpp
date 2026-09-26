#include "applicationthememanager.h"
#include "navigationwidget.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPersistentModelIndex>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidgetItemIterator>
#include <QScrollBar>

static int fileCount(QTreeWidget* tree) {
    int count = 0;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        count += (*it)->data(0, NavigationWidget::FileTreeKindRole).toInt() == NavigationWidget::FileItem;
    return count;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir profile;
    if (!profile.isValid()) return 1;
    QCoreApplication::setApplicationName("ZeroSlack-Navigation-Switch-Test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    auto& theme = ApplicationThemeManager::instance();
    if (!theme.selectBackend(UiStyleBackend::Ela)) return 2;
    theme.setMode(ThemeMode::Light);
    theme.applyToApplication();
    NavigationWidget navigation;
    navigation.resize(400, 740);
    navigation.setWorkspaceRoot(profile.path());
    QStringList files;
    for (int i = 0; i < 5000; ++i)
        files.append(profile.filePath(QString("rtl/group_%1/module_%2.sv").arg(i / 100).arg(i)));
    navigation.updateFileHierarchy(files);
    navigation.show();
    auto* tree = navigation.findChild<QTreeWidget*>("navigationFileTree");
    if (!tree) return 3;
    const auto waitForFiles = [&] {
        QElapsedTimer timer; timer.start();
        while (fileCount(tree) != files.size() && timer.elapsed() < 15000) QTest::qWait(5);
        return fileCount(tree) == files.size();
    };
    if (!waitForFiles()) return 4;
    navigation.highlightFile(files[2450]);
    tree->verticalScrollBar()->setValue(tree->verticalScrollBar()->value() + 10);
    const int scroll = tree->verticalScrollBar()->value();
    const QPersistentModelIndex selected = tree->currentIndex();
    QJsonArray actions;
    bool preserved = true;
    for (int i = 0; i < 12; ++i) {
        navigation.setActiveTab(NavigationWidget::DesignTab);
        QTest::qWait(20);
        QSignalSpy resets(tree->model(), &QAbstractItemModel::modelReset);
        bool interactionDelivered = false;
        double interactionMs = 0;
        QElapsedTimer timer; timer.start();
        navigation.setActiveTab(NavigationWidget::FileTab);
        const double switchMs = timer.nsecsElapsed() / 1e6;
        const int immediateFiles = fileCount(tree);
        QTimer::singleShot(0, &navigation, [&] {
            navigation.focusSearch();
            interactionMs = timer.nsecsElapsed() / 1e6;
            interactionDelivered = true;
        });
        while (!interactionDelivered && timer.elapsed() < 15000) QTest::qWait(1);
        if (!waitForFiles()) return 5;
        const bool selectionPreserved = selected.isValid() && tree->currentIndex() == selected;
        preserved &= selectionPreserved && resets.isEmpty() && immediateFiles == files.size()
            && tree->verticalScrollBar()->value() == scroll;
        actions.append(QJsonObject{{"switchMs", switchMs}, {"interactionMs", interactionMs},
            {"settledMs", timer.nsecsElapsed()/1e6}, {"modelResets", resets.size()},
            {"immediateFiles", immediateFiles}, {"selectionPreserved", selectionPreserved}});
    }
    bool invalidationVerified = true;
    if (argc > 2 && QByteArray(argv[2]) == "--verify") {
        const auto waitForCount = [&](int count) {
            QElapsedTimer timer; timer.start();
            while (fileCount(tree) != count && timer.elapsed() < 15000) QTest::qWait(5);
            return fileCount(tree) == count;
        };
        navigation.setActiveTab(NavigationWidget::DesignTab);
        navigation.setSearchFilter(NavigationWidget::FileTab, "module_2450");
        navigation.setActiveTab(NavigationWidget::FileTab);
        invalidationVerified &= waitForCount(1);
        navigation.setSearchFilter(NavigationWidget::FileTab, "");
        navigation.setActiveTab(NavigationWidget::DesignTab);
        // Switch while asynchronous population is active; it must finish once.
        QSignalSpy pendingResets(tree->model(), &QAbstractItemModel::modelReset);
        navigation.setActiveTab(NavigationWidget::FileTab);
        invalidationVerified &= pendingResets.isEmpty();
        // The first population chunk removes its loading placeholder once.
        invalidationVerified &= waitForCount(files.size()) && pendingResets.size() == 1;
        navigation.setActiveTab(NavigationWidget::DesignTab);
        navigation.updateFileHierarchy({profile.filePath("changed.sv")});
        navigation.setActiveTab(NavigationWidget::FileTab);
        invalidationVerified &= waitForCount(1);
        navigation.setWorkspaceRoot(profile.filePath("another-workspace"));
        navigation.updateFileHierarchy({});
        invalidationVerified &= waitForCount(0);
    }
    const QString outputPath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : "navigation-switch.json";
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)) return 6;
    output.write(QJsonDocument(QJsonObject{{"platform", QApplication::platformName()},
        {"dpr", navigation.devicePixelRatioF()}, {"files", files.size()}, {"actions", actions},
        {"preserved", preserved}, {"invalidationVerified", invalidationVerified}}).toJson());
    return argc > 2 && QByteArray(argv[2]) == "--verify" && (!preserved || !invalidationVerified) ? 7 : 0;
}
