#include "../src/integrations/native/nativecontextview.h"
#include "../src/integrations/xips/xipscontextprovider.h"
#include "../src/integrations/simdock/simdockcontextprovider.h"
#include "../src/integrations/simdock/simdockcontextview.h"
#include "applicationthememanager.h"
#include "contextworkspacecontroller.h"
#include "contextdockhost.h"
#include "contextfloatingwindow.h"
#include "mainwindow.h"
#include "tabmanager.h"
#include "navigationwidget.h"
#include "navigationpanecoordinator.h"
#include "panellayoutcontroller.h"
#include "linkedcontextcontroller.h"
#include "../src/simulation/simdock/ui/workbench.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "settingscenterservice.h"
#include "testuistyle.h"
#include "version.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDockWidget>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDrag>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QDropEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMenu>
#include <QPlainTextEdit>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTabBar>
#include <QTabWidget>
#include <QTextDocument>
#include <QTimer>
#include <QtTest>
#include <functional>
#include <cmath>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
constexpr auto projectId = "1456b0f8-d164-4e78-bb8f-448a3edb9740";
bool put(const QString &path, const QByteArray &data)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray get(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
void click(QWidget *owner, const char *name)
{
    auto *button = owner->findChild<QAbstractButton *>(QString::fromLatin1(name));
    QVERIFY2(button, name);
    QVERIFY2(button->isEnabled(), name);
    button->click();
}
void whenVisible(QWidget *owner, const QString &name, std::function<void(QWidget *)> action, bool rejectOnReturn = false)
{
    auto *timer = new QTimer(owner);
    QElapsedTimer elapsed;
    elapsed.start();
    QObject::connect(timer, &QTimer::timeout, owner, [owner, timer, name, action, elapsed, rejectOnReturn] {
        for (auto *widget : owner->findChildren<QWidget *>(name)) {
            if (!widget->isVisible()) continue;
            if (auto *dialog = qobject_cast<QDialog *>(widget->window())) {
                QTimer::singleShot(10000, dialog, [dialog, name] {
                    if (!dialog->isVisible()) return;
                    QTest::qFail(qPrintable("Dialog action did not finish: " + name), __FILE__, __LINE__);
                    dialog->reject();
                });
            }
            timer->stop(); timer->deleteLater();
            const QPointer<QDialog> dialog = qobject_cast<QDialog *>(widget->window());
            // Test assertions can return from the action before it accepts the form.
            const auto dismissUnfinished = qScopeGuard([dialog, rejectOnReturn] {
                if ((rejectOnReturn || QTest::currentTestFailed()) && dialog && dialog->isVisible()) dialog->reject();
            });
            action(widget); return;
        }
        if (elapsed.elapsed() > 10000) {
            timer->stop(); timer->deleteLater();
            QTest::qFail(qPrintable("Timed out waiting for " + name), __FILE__, __LINE__);
            for (auto *dialog : owner->findChildren<QDialog *>())
                if (dialog->isVisible()) dialog->reject();
        }
    });
    timer->start(10);
}
QModelIndex fileIndex(QAbstractItemModel *model, const QString &path, const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const auto index = model->index(row, 0, parent);
        if (index.data(Qt::UserRole).toString() == path) return index;
        const auto child = fileIndex(model, path, index);
        if (child.isValid()) return child;
    }
    return {};
}
bool busy(QWidget *panel)
{
    bool result = true;
    QMetaObject::invokeMethod(panel, "isCatalogBusy", Q_RETURN_ARG(bool, result));
    return result;
}
QJsonObject moduleEvidence(const QString &name)
{
    QString path;
#ifdef Q_OS_WIN
    const auto module = GetModuleHandleW(reinterpret_cast<LPCWSTR>(name.utf16()));
    wchar_t buffer[32768]{};
    if (module) {
        const DWORD count = GetModuleFileNameW(module, buffer, 32768);
        if (count && count < 32768) path = QFileInfo(QString::fromWCharArray(buffer, int(count))).canonicalFilePath();
    }
#endif
    return {{"name", name}, {"path", path}, {"sha256", path.isEmpty() ? QString() :
            QString::fromLatin1(QCryptographicHash::hash(get(path), QCryptographicHash::Sha256).toHex())}};
}

struct SimulationIdentity
{
    QPointer<SimDockContextView> workspace;
    QPointer<simdock::Workbench> panel;
    QPointer<simdock::WorkspaceSimulationController> controller;
    QPointer<simdock::QuestaSession> session;
    QPointer<QWidget> target, logs;
    QPointer<QPlainTextEdit> log;
    QPointer<QTextDocument> logDocument;
    explicit SimulationIdentity(SimDockContextView *view) : workspace(view), panel(view->workbench())
    {
        controller = panel->controller(); session = controller->session();
        target = panel->findChild<QWidget*>("startSimulation");
        logs = panel->runLogWidget(); log = logs->findChild<QPlainTextEdit*>();
        logDocument = log ? log->document() : nullptr;
    }
    void verify(MainWindow &window) const
    {
        QVERIFY(workspace && panel && controller && session && target && logs && log && logDocument);
        QCOMPARE(workspace->workbench(), panel.data()); QCOMPARE(panel->controller(), controller.data());
        QCOMPARE(controller->session(), session.data()); QCOMPARE(panel->runLogWidget(), logs.data());
        QCOMPARE(log->document(), logDocument.data()); QCOMPARE(logDocument.data(), controller->logDocument());
        QCOMPARE(panel->findChild<QWidget*>("startSimulation"), target.data());
        QVERIFY(window.isAncestorOf(workspace)); QVERIFY(window.isAncestorOf(logs)); QVERIFY(!panel->isAncestorOf(logs));
        QVERIFY(!window.findChild<QWidget*>("sourceList"));
    }
};
}

// Uses the product's actual MainWindow and providers, never component C++
// internals or test factories. Keep this separate from controllable fake-DLL tests.
class NativeMainWindowIntegrationTest : public QObject
{
    Q_OBJECT
    QString report;
    QString uiCase;
    QJsonArray pointerEvidence, captures, uiLayouts;
    QJsonObject finalUi;
    bool finalUiRequested() const { return qEnvironmentVariableIsSet("ZEROSLACK_FINAL_NATIVE_UI"); }
    void screenshot(QWidget &window, const QString &name)
    {
        if (report.isEmpty()) return;
        QTest::qWait(100);
        const bool native = QGuiApplication::platformName() == "windows";
        const auto pixels = window.grab();
        QVERIFY(!pixels.isNull());
        QVERIFY(pixels.save(QDir(report).filePath(name + ".png")));
        if (native) {
            const auto origin = window.mapToGlobal(QPoint());
            const auto desktopPixels = window.screen()->grabWindow(0, origin.x(), origin.y(), window.width(), window.height());
            QVERIFY(desktopPixels.save(QDir(report).filePath(name + "-native.png")));
        }
        captures.append(QJsonObject{{"file", name + ".png"}, {"capture", "QWidget::grab on live platform"},
            {"platform", QGuiApplication::platformName()}, {"nativeCompanion", native ? name + "-native.png" : QString()},
            {"logicalWidth", window.width()}, {"logicalHeight", window.height()},
            {"pixelWidth", pixels.width()}, {"pixelHeight", pixels.height()}, {"dpr", window.devicePixelRatioF()}});
        if (finalUiRequested())
            QVERIFY(put(QDir(report).filePath("ui-observations.json"), QJsonDocument(QJsonObject{
                {"finalUiIntegration", finalUi}, {"layouts", uiLayouts}, {"pointerChecks", pointerEvidence}, {"screenshots", captures}}).toJson()));
    }
    bool reachable(QWidget *widget)
    {
        if (!widget || !widget->isVisible()) return false;
        for (auto *parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
            if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
                scroll->ensureWidgetVisible(widget, 4, 4);
                QCoreApplication::processEvents();
                // ensureWidgetVisible can prefer an input's cursor rectangle.
                // Scroll to the whole control before checking every boundary.
                const QRect target(widget->mapTo(scroll->viewport(), QPoint()), widget->size());
                const auto viewport = scroll->viewport()->rect();
                if (target.bottom() > viewport.bottom())
                    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->value() + target.bottom() - viewport.bottom() + 4);
                else if (target.top() < viewport.top())
                    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->value() + target.top() - 4);
                QCoreApplication::processEvents();
            }
            if (parent->isWindow()) break;
        }
        QTest::qWait(30);
        const QRect rect(widget->mapToGlobal(QPoint()), widget->size());
        bool inside = true;
        QString clippedBy;
        for (auto *parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
            if (!QRect(parent->mapToGlobal(QPoint()), parent->size()).contains(rect)) {
                inside = false; clippedBy = QString::fromLatin1(parent->metaObject()->className()) + '/' + parent->objectName()
                    + QString(" [%1,%2 %3x%4]").arg(parent->mapToGlobal(QPoint()).x()).arg(parent->mapToGlobal(QPoint()).y())
                    .arg(parent->width()).arg(parent->height()); break;
            }
            if (parent->isWindow()) break;
        }
        auto *hit = QApplication::widgetAt(rect.center());
        const bool pointerHit = hit == widget || (hit && widget->isAncestorOf(hit));
        const bool onScreen = widget->screen()->availableGeometry().contains(rect);
        pointerEvidence.append(QJsonObject{{"case", uiCase}, {"control", widget->objectName()},
            {"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()},
            {"enabled", widget->isEnabled()}, {"insideAncestors", inside}, {"clippedBy", clippedBy}, {"onScreen", onScreen}, {"pointerHit", pointerHit}});
        if (!inside || !onScreen || !pointerHit) {
            qWarning() << "Unreachable" << uiCase << widget->objectName() << rect << "clippedBy" << clippedBy
                       << "hit" << hit << "screen" << widget->screen()->availableGeometry();
            screenshot(*widget->window(), "failure-" + uiCase + '-' + widget->objectName());
        }
        return inside && onScreen && pointerHit;
    }
    void pointerClick(QWidget *owner, const char *name)
    {
        auto *button = owner->findChild<QAbstractButton *>(QLatin1String(name));
        QVERIFY2(reachable(button), name);
        QVERIFY2(button->isEnabled(), name);
        QTest::mouseClick(button, Qt::LeftButton);
    }
    void cancelForm(QWidget *owner, const char *button, const QString &field)
    {
        const auto opened = std::make_shared<bool>(false);
        whenVisible(owner->window(), field, [opened](QWidget *widget) {
            *opened = true;
            auto *dialog = qobject_cast<QDialog *>(widget->window()); QVERIFY(dialog);
            dialog->reject();
        });
        pointerClick(owner, button);
        QTRY_VERIFY_WITH_TIMEOUT(*opened, 10000);
    }
    void xipsReachability(QWidget *panel)
    {
        QVERIFY(reachable(panel->findChild<QWidget *>("assetSearch")));
        pointerClick(panel, "filterButton");
        QVERIFY(reachable(panel->findChild<QWidget *>("typeCombo")));
        QVERIFY(reachable(panel->findChild<QWidget *>("indexCombo")));
        pointerClick(panel, "clearFiltersButton"); pointerClick(panel, "filterButton");
        QVERIFY(!QTest::currentTestFailed());
        bool menuOpened = false;
        QTimer closer;
        connect(&closer, &QTimer::timeout, panel, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *menu = qobject_cast<QMenu *>(widget); menu && menu->isVisible()) {
                    menuOpened = true; QTest::keyClick(menu, Qt::Key_Escape);
                }
        });
        closer.start(10); pointerClick(panel, "collectButton"); closer.stop(); QVERIFY(menuOpened);
        cancelForm(panel, "folderButton", "xipsFilePicker");
        cancelForm(panel, "newGroupButton", "groupName");
        cancelForm(panel, "newAssetButton", "newAssetName");
        pointerClick(panel, "refreshButton"); QTRY_VERIFY_WITH_TIMEOUT(!busy(panel), 10000);
        QVERIFY(!QTest::currentTestFailed());
        auto *pages = panel->findChild<QTabWidget *>("assetPages"); QVERIFY(pages);
        auto *bar = pages->tabBar();
        for (int page : {0, 1}) {
            QVERIFY(reachable(bar)); QVERIFY(bar->rect().contains(bar->tabRect(page)));
            QTest::mouseClick(bar, Qt::LeftButton, {}, bar->tabRect(page).center());
            QCOMPARE(pages->currentIndex(), page);
            if (page == 0) {
                cancelForm(panel, "addFilesButton", "xipsFilePicker");
                cancelForm(panel, "addFolderButton", "xipsFilePicker");
                QVERIFY(reachable(panel->findChild<QWidget *>("checkAllFiles")));
                cancelForm(panel, "updateButton", "payloadReviewForm");
            } else {
                cancelForm(panel, "takeButton", "exportDestination");
                cancelForm(panel, "deleteRevisionButton", "deleteRevisionForm");
            }
            QVERIFY(!QTest::currentTestFailed());
            screenshot(*panel->window(), uiCase + (page ? "-versions" : "-working"));
        }
    }
    void simdockReachability(QWidget *panel)
    {
        for (const auto* name : {"startSimulation", "openSettings"})
            QVERIFY2(reachable(panel->findChild<QWidget*>(QLatin1String(name))), name);
        for (const auto* name : {"sourceList", "projectList", "simulationInputMode", "dutSelector", "editStimulus", "graphicalDuration"})
            QVERIFY(!panel->findChild<QWidget*>(QLatin1String(name)));
        cancelForm(panel, "openSettings", "settingsDialog");
        QVERIFY(!QTest::currentTestFailed());
        screenshot(*panel->window(), uiCase + "-simulation");
    }
    void verifyThemes(NativeContextView *xhost, SimDockContextView *shost, bool dark)
    {
        for (auto *host : {static_cast<QWidget*>(xhost), static_cast<QWidget*>(shost)}) {
            auto *panel = host == xhost ? xhost->component() : shost->component();
            QCOMPARE(host->property("nativeComponentDarkTheme").toBool(), dark);
            qInfo() << "Theme check" << host->objectName() << dark << panel->palette().color(QPalette::Window);
            QTRY_VERIFY2_WITH_TIMEOUT((panel->palette().color(QPalette::Window).lightness() < 128) == dark,
                qPrintable(host->objectName() + " visible palette does not follow host theme"), 2000);
        }
        auto luminance = [](const QColor &color) {
            auto linear = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
            return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
        };
        for (auto *panel : {xhost->component(), shost->component()})
            for (auto *view : panel->findChildren<QAbstractItemView *>()) {
                if (!QStringList{"assetList", "workingFiles", "revisionTable", "projectList", "sourceList"}.contains(view->objectName())) continue;
                for (auto group : {QPalette::Active, QPalette::Inactive}) {
                    const auto palette = view->palette();
                    const double text = luminance(palette.color(group, QPalette::HighlightedText));
                    const double background = luminance(palette.color(group, QPalette::Highlight));
                    QVERIFY2((qMax(text, background) + 0.05) / (qMin(text, background) + 0.05) >= 4.5,
                             qPrintable(view->objectName() + " selected text contrast < 4.5"));
                }
            }
    }
    void finalNativeUi(MainWindow &window, ContextWorkspaceController *controller,
                       NativeContextView *xhost, SimDockContextView *shost, const QString &xkey,
                       const QPalette &darkPalette, const QPalette &lightPalette)
    {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
        QCOMPARE(window.devicePixelRatioF(), window.screen()->devicePixelRatio());
        finalUi["applicationScaleFactor"] = qEnvironmentVariable("QT_SCALE_FACTOR", "1").toDouble();
        finalUi["screenDevicePixelRatio"] = window.screen()->devicePixelRatio();
        const auto available = window.screen()->availableGeometry();
        finalUi["availableLogicalWidth"] = available.width();
        finalUi["availableLogicalHeight"] = available.height();
#ifdef Q_OS_WIN
        finalUi["systemDpi"] = int(GetDpiForSystem());
        finalUi["windowNativeDpi"] = int(GetDpiForWindow(reinterpret_cast<HWND>(window.winId())));
#endif
        verifyThemes(xhost, shost, true); QVERIFY(!QTest::currentTestFailed()); finalUi["initialTheme"] = true;
        whenVisible(&window, "payloadReviewForm", [](QWidget *form) { click(form, "formAccept"); });
        click(xhost->component(), "updateButton"); QTRY_VERIFY_WITH_TIMEOUT(!busy(xhost->component()), 10000);
        auto *dock = controller->dockHost();
        const SimulationIdentity simulation(shost);
        simulation.verify(window); QVERIFY(!QTest::currentTestFailed());
        const auto xstate = xhost->saveState(), sstate = shost->saveState();
        window.resizeDocks({controller->dockWidget()}, {window.width()}, Qt::Horizontal);
        QTest::qWait(100);
        const int maximumDockWidth = controller->dockWidget()->width();
        finalUi["maximumDockWidth"] = maximumDockWidth;
        for (const auto &layout : QList<QPair<int, int>>{{280, 690}, {520, 690}, {920, 690}, {280, 380}}) {
            window.resize(window.width(), qMin(layout.second == 380 ? 460 : 860, available.height() - 24));
            for (bool dark : {true, false}) {
                ApplicationThemeManager::instance().setMode(dark ? ThemeMode::Dark : ThemeMode::Light);
                verifyThemes(xhost, shost, dark); QVERIFY(!QTest::currentTestFailed());
                QCOMPARE(qApp->palette(), dark ? darkPalette : lightPalette);
                auto stale = sstate; auto preferences = stale.value("preferences").toMap(); preferences["theme/dark"] = !dark;
                stale["preferences"] = preferences;
                shost->restoreState(stale); xhost->restoreState(xstate);
                QTRY_VERIFY_WITH_TIMEOUT(!busy(xhost->component()), 10000);
                verifyThemes(xhost, shost, dark); QVERIFY(!QTest::currentTestFailed());
                finalUi["themeSwitch"] = true; finalUi["afterRestore"] = true;
                QVERIFY(dock->setSectionCollapsed(xkey, false, false));
                QVERIFY(dock->setSectionHeight(xkey, layout.second));
                QVERIFY(controller->focusResource(xkey));
                const int effectiveWidth = qMin(layout.first, maximumDockWidth);
                window.resizeDocks({controller->dockWidget()}, {effectiveWidth}, Qt::Horizontal);
                window.raise(); window.activateWindow();
                QVERIFY(QTest::qWaitForWindowActive(&window)); QTest::qWait(150);
                uiCase = QString("xips-%1x%2-%3").arg(layout.first).arg(layout.second).arg(dark ? "dark" : "light");
                auto *panel = xhost->component();
                uiLayouts.append(QJsonObject{{"case", uiCase}, {"requestedWidth", layout.first},
                    {"screenConstrainedWidth", effectiveWidth}, {"dockWidth", controller->dockWidget()->width()},
                    {"panelWidth", panel->width()}, {"panelHeight", panel->height()}});
                if (effectiveWidth > 900) QVERIFY2(panel->width() >= 900, qPrintable(uiCase));
                else QVERIFY2(qAbs(controller->dockWidget()->width() - effectiveWidth) <= 12, qPrintable(uiCase));
                if (layout.second == 380) QVERIFY(panel->height() <= 380);
                xipsReachability(panel); QVERIFY(!QTest::currentTestFailed());
                simulation.verify(window); QVERIFY(!QTest::currentTestFailed());
                QVERIFY(dock->setSectionCollapsed(xkey, true, false));
                uiCase = QString("compact-simulation-%1-%2").arg(layout.first).arg(dark ? "dark" : "light");
                simdockReachability(shost->component()); QVERIFY(!QTest::currentTestFailed());
            }
            finalUi[layout.first == 280 ? (layout.second == 380 ? "narrowShort" : "280") : layout.first == 520 ? "520" : "expanded"] = true;
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        window.resize(window.width(), qMin(860, available.height() - 24));
        QPointer<QWidget> surface = controller->viewForResource(xkey), panel = xhost->component();
        QVERIFY(surface && panel);
        QCOMPARE(surface.data(), static_cast<QWidget*>(xhost));
        const auto saved = xhost->saveState();
        QVERIFY(controller->unpinResource(xkey));
        QPointer<ContextFloatingWindow> floating = controller->floatingWindow();
        QVERIFY(floating && surface && panel);
        QCOMPARE(floating->view(), surface.data()); QCOMPARE(controller->viewForResource(xkey), surface.data());
        QVERIFY(!dock->containsResource(xkey)); QCOMPARE(xhost->component(), panel.data());
        floating->resize(qMin(920, available.width() - 24), qMin(760, available.height() - 24));
        floating->move(available.topLeft() + QPoint(12, 12));
        floating->show(); floating->raise(); floating->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(floating)); QTest::qWait(150);
        uiCase = "xips-floating"; xipsReachability(panel); QVERIFY(!QTest::currentTestFailed());
        QVERIFY(controller->pinFloatingResource(xkey));
        QVERIFY(surface && panel && floating);
        QCOMPARE(controller->viewForResource(xkey), surface.data()); QCOMPARE(dock->viewForResource(xkey), surface.data());
        QVERIFY(!floating->view()); QVERIFY(dock->isAncestorOf(surface)); QCOMPARE(xhost->component(), panel.data());
        QCOMPARE(xhost->saveState().value("assetId"), saved.value("assetId"));
        simulation.verify(window); QVERIFY(!QTest::currentTestFailed());
        finalUi["floating"] = true; finalUi["xipsSurfaceRoundTrip"] = true; finalUi["sharedSimulationOwnership"] = true;
        QVERIFY(dock->setSectionCollapsed(xkey, true, false));
        shost->restoreState(sstate); verifyThemes(xhost, shost, true);
    }
    void xipsWorkflow(MainWindow &window, NativeContextView *host, const QString &root,
                      const QString &workspace, const QString &library)
    {
        QWidget *panel = host->component();
        QVERIFY(panel);
        QTRY_VERIFY_WITH_TIMEOUT(!busy(panel), 10000);
        QCOMPARE(host->saveState().value("library").toString(), library);
        whenVisible(&window, "groupName", [](QWidget *field) {
            qobject_cast<QLineEdit *>(field)->setText("AXI"); click(field->window(), "formAccept");
        });
        click(panel, "newGroupButton");
        QTRY_VERIFY_WITH_TIMEOUT(!busy(panel), 10000);
        const auto group = host->saveState();
        QVERIFY(!group.value("groupId").toString().isEmpty());
        for (const auto &kind : {QStringLiteral("module"), QStringLiteral("ip")}) {
            host->restoreState(group);
            whenVisible(&window, "newAssetName", [kind](QWidget *field) {
                qobject_cast<QLineEdit *>(field)->setText(kind + "_empty");
                auto *type = field->window()->findChild<QComboBox *>("newAssetType");
                QVERIFY(type); type->setCurrentIndex(type->findData(kind));
                click(field->window(), "formAccept");
            });
            click(panel, "newAssetButton");
            QTRY_VERIFY_WITH_TIMEOUT(!busy(panel), 10000);
            auto *versions = panel->findChild<QAbstractItemView *>("revisionTable");
            auto *working = panel->findChild<QAbstractItemView *>("workingFiles");
            QVERIFY(versions && working);
            QCOMPARE(versions->model()->rowCount(), 0);
            QCOMPARE(working->model()->rowCount(), 0);
        }
        const auto asset = host->saveState();
        host->restoreState(group);
        auto *members = panel->findChild<QAbstractItemView *>("groupMembers");
        QVERIFY(members);
        QCOMPARE(members->model()->rowCount(members->rootIndex()), 2);
        host->restoreState(asset);
        QVERIFY(put(root + "/external/top.sv", "module top; endmodule\n"));
        QVERIFY(put(root + "/external/rtl/sub/helper.sv", "module helper; endmodule\n"));
        whenVisible(&window, "xipsFilePicker", [root](QWidget *form) {
            auto *path = form->findChild<QLineEdit *>("pickerPath"); QVERIFY(path);
            path->selectAll(); QTest::keyClicks(path, root + "/external/top.sv");
            auto *accept = form->findChild<QAbstractButton *>("pickerAccept"); QVERIFY(accept);
            QTRY_VERIFY_WITH_TIMEOUT(accept->isEnabled(), 3000);
            click(form, "pickerAccept");
        });
        click(panel, "addFilesButton");
        QTRY_VERIFY_WITH_TIMEOUT(!busy(panel), 10000);
        auto *working = panel->findChild<QAbstractItemView *>("workingFiles");
        QVERIFY(working);
        // Ordinary file drops collect working files without creating a revision.
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(root + "/external/rtl/sub/helper.sv")});
        QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(working->viewport(), &enter); QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(working->viewport(), &drop); QVERIFY(drop.isAccepted());
        QCOMPARE(drop.dropAction(), Qt::CopyAction);
        QTRY_VERIFY_WITH_TIMEOUT(!busy(panel), 10000);
        auto *model = working->model();
        auto *versions = panel->findChild<QAbstractItemView *>("revisionTable");
        QVERIFY(versions);
        QCOMPARE(versions->model()->rowCount(), 0);
        const auto top = fileIndex(model, "top.sv"), helper = fileIndex(model, "helper.sv");
        QVERIFY(top.isValid() && helper.isValid());
        QVERIFY(model->setData(top, Qt::Unchecked, Qt::CheckStateRole));
        QVERIFY(model->setData(helper, Qt::Checked, Qt::CheckStateRole));
        const auto checked = host->saveState();
        host->restoreState(checked);
        QTRY_VERIFY_WITH_TIMEOUT(!busy(panel), 10000);
        QCOMPARE(host->saveState().value("workingChecks"), checked.value("workingChecks"));
        whenVisible(&window, "payloadReviewForm", [](QWidget *form) { click(form, "formAccept"); });
        click(panel, "updateButton");
        QTRY_VERIFY_WITH_TIMEOUT(!busy(panel), 10000);
        QCOMPARE(versions->model()->rowCount(), 1);
        const auto revision = host->saveState().value("revision").toString();
        QVERIFY(!revision.isEmpty());
        QDirIterator records(library, {revision + ".json"}, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        QString manifestPath;
        while (records.hasNext()) {
            const auto path = records.next();
            if (path.contains("/.xips/revisions/")) { manifestPath = path; break; }
        }
        QVERIFY(!manifestPath.isEmpty());
        const auto manifest = QJsonDocument::fromJson(get(manifestPath)).object();
        QCOMPARE(manifest.value("files").toArray(), QJsonArray{QStringLiteral("helper.sv")});
        QVERIFY(fileIndex(model, "helper.sv").isValid() && fileIndex(model, "top.sv").isValid());
        QCOMPARE(host->saveState().value("workingChecks"), checked.value("workingChecks"));
        const auto destination = workspace + "/subset.sv";
        whenVisible(&window, "exportDestination", [destination](QWidget *field) {
            qobject_cast<QLineEdit *>(field)->setText(destination); click(field->window(), "formAccept");
        });
        click(panel, "takeButton");
        QTRY_VERIFY_WITH_TIMEOUT(!busy(panel), 10000);
        QCOMPARE(get(destination), get(root + "/external/rtl/sub/helper.sv"));
        const auto receipts = QJsonDocument::fromJson(get(workspace + "/.zeroslack/xips-references.json"))
                                  .object().value("assets").toArray();
        QCOMPARE(receipts.size(), 1);
        QCOMPARE(receipts.first().toObject().value("revision").toString(), revision);
        whenVisible(&window, "deleteRevisionForm", [](QWidget *form) { click(form, "formAccept"); });
        click(panel, "deleteRevisionButton");
        QTRY_VERIFY_WITH_TIMEOUT(!busy(panel), 10000);
        QCOMPARE(versions->model()->rowCount(), 0);
        QVERIFY(fileIndex(model, "top.sv").isValid());
        QVERIFY(fileIndex(model, "helper.sv").isValid());
        QCOMPARE(host->saveState().value("workingChecks"), checked.value("workingChecks"));
        const QString definitionRoot = QFileInfo(manifestPath).dir().absoluteFilePath("../..");
        const auto definition = QJsonDocument::fromJson(get(definitionRoot + "/.xips.json")).object();
        const QString source = definition.value("source").toObject().value("path").toString();
        QVERIFY(!source.isEmpty());
        const QString assetRoot = QDir(library).filePath(source);
        QCOMPARE(get(assetRoot + "/top.sv"), get(root + "/external/top.sv"));
        QCOMPARE(get(assetRoot + "/helper.sv"), get(root + "/external/rtl/sub/helper.sv"));

        // Folder drops review a selected payload and archive it immediately in 2.13.2.
        QVERIFY(put(root + "/external/rtl/unused.sv", "module not_imported; endmodule\n"));
        const auto beforeImport = host->saveState();
        const auto dropFolder = [&] {
            QMimeData folderMime; folderMime.setUrls({QUrl::fromLocalFile(root + "/external/rtl")});
            QDragEnterEvent folderEnter(QPoint(10, 10), Qt::CopyAction, &folderMime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(working->viewport(), &folderEnter); QVERIFY(folderEnter.isAccepted());
            QDropEvent folderDrop(QPointF(10, 10), Qt::CopyAction, &folderMime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(working->viewport(), &folderDrop); QVERIFY(folderDrop.isAccepted());
            QCOMPARE(folderDrop.dropAction(), Qt::CopyAction);
        };
        bool cancelled = false;
        whenVisible(&window, "folderImportForm", [&](QWidget *form) {
            QCOMPARE(form->parentWidget(), &window);
            QCOMPARE(QApplication::activeModalWidget(), form);
            QCOMPARE(form->windowModality(), Qt::ApplicationModal);
            auto *transfer = form->findChild<QComboBox *>("importTransfer"); QVERIFY(transfer);
            transfer->setCurrentIndex(transfer->findData(true));
            cancelled = true;
            // Deliberately return without accepting: independent cleanup must reject it.
        }, true);
        dropFolder();
        QTRY_VERIFY_WITH_TIMEOUT(cancelled && !busy(panel) && host->canClose(), 12000);
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(versions->model()->rowCount(), 0);
        QCOMPARE(host->saveState().value("assetId"), beforeImport.value("assetId"));
        QCOMPARE(host->saveState().value("workingChecks"), beforeImport.value("workingChecks"));
        QVERIFY(!QFileInfo::exists(assetRoot + "/rtl/sub/helper.sv"));
        QCOMPARE(get(root + "/external/rtl/sub/helper.sv"), get(assetRoot + "/helper.sv"));
        QVERIFY(QFileInfo::exists(root + "/external/rtl/unused.sv"));

        bool imported = false;
        whenVisible(&window, "folderImportForm", [&](QWidget *form) {
            QCOMPARE(form->parentWidget(), &window);
            QCOMPARE(QApplication::activeModalWidget(), form);
            QCOMPARE(form->windowModality(), Qt::ApplicationModal);
            auto *destination = form->findChild<QComboBox *>("importDestination"); QVERIFY(destination);
            QCOMPARE(destination->currentData().toString(), asset.value("assetId").toString());
            auto *tree = form->findChild<QAbstractItemView *>("importFiles"); QVERIFY(tree);
            const auto selected = fileIndex(tree->model(), "rtl/sub/helper.sv");
            const auto excluded = fileIndex(tree->model(), "rtl/unused.sv");
            QVERIFY(selected.isValid() && excluded.isValid());
            QVERIFY(tree->model()->setData(excluded, Qt::Unchecked, Qt::CheckStateRole));
            QCOMPARE(selected.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
            auto *transfer = form->findChild<QComboBox *>("importTransfer"); QVERIFY(transfer);
            QCOMPARE(transfer->currentData().toBool(), false);
            auto *version = form->findChild<QLineEdit *>("importVersion"); QVERIFY(version);
            version->setText("folder-v1");
            auto *note = form->findChild<QLineEdit *>("importNote"); QVERIFY(note);
            note->setText("selected nested file through ZeroSlack");
            if (!report.isEmpty()) {
                QTest::qWait(350);
                QVERIFY(form->grab().save(QDir(report).filePath("folder-import.png")));
            }
            imported = true;
            click(form, "formAccept");
        });
        dropFolder();
        QTRY_VERIFY_WITH_TIMEOUT(imported && !busy(panel) && host->canClose(), 12000);
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(versions->model()->rowCount(), 1);
        const auto importedRevision = versions->model()->index(0, 0).data(Qt::UserRole).toString();
        QVERIFY(!importedRevision.isEmpty() && importedRevision != revision);
        const auto importedManifest = QJsonDocument::fromJson(get(QFileInfo(manifestPath).dir()
            .filePath(importedRevision + ".json"))).object();
        QCOMPARE(importedManifest.value("files").toArray(), QJsonArray{QStringLiteral("rtl/sub/helper.sv")});
        QCOMPARE(importedManifest.value("note").toString(), QStringLiteral("selected nested file through ZeroSlack"));
        QCOMPARE(versions->model()->index(0, 0).data().toString(), QStringLiteral("folder-v1"));
        QCOMPARE(get(assetRoot + "/rtl/sub/helper.sv"), get(root + "/external/rtl/sub/helper.sv"));
        QVERIFY(!QFileInfo::exists(assetRoot + "/rtl/unused.sv"));
        QVERIFY(QFileInfo::exists(root + "/external/rtl/unused.sv"));
        QCOMPARE(get(assetRoot + "/top.sv"), get(root + "/external/top.sv"));
        QCOMPARE(get(assetRoot + "/helper.sv"), get(root + "/external/rtl/sub/helper.sv"));
        auto *pages = panel->findChild<QTabWidget *>("assetPages"); QVERIFY(pages);
        pages->setCurrentIndex(1);
        versions->setCurrentIndex(versions->model()->index(0, 0));
        const auto folderDestination = workspace + "/folder-subset.sv";
        whenVisible(&window, "exportDestination", [folderDestination](QWidget *field) {
            qobject_cast<QLineEdit *>(field)->setText(folderDestination); click(field->window(), "formAccept");
        });
        click(panel, "takeButton");
        QTRY_VERIFY_WITH_TIMEOUT(!busy(panel), 10000);
        QCOMPARE(get(folderDestination), get(root + "/external/rtl/sub/helper.sv"));
        const auto updatedReceipts = QJsonDocument::fromJson(get(workspace + "/.zeroslack/xips-references.json"))
                                         .object().value("assets").toArray();
        QCOMPARE(updatedReceipts.size(), 2);
        QCOMPARE(updatedReceipts.first(), receipts.first());
        QCOMPARE(updatedReceipts.last().toObject().value("revision").toString(), importedRevision);
        QCOMPARE(updatedReceipts.last().toObject().value("path").toString(), QStringLiteral("folder-subset.sv"));
        QCOMPARE(QFileInfo(window.tabManager->getCurrentDocumentMetadata().fileName).canonicalFilePath(),
                 QFileInfo(folderDestination).canonicalFilePath());
        if (!report.isEmpty()) QVERIFY(put(QDir(report).filePath("xips-workflow.json"), QJsonDocument(QJsonObject{
            {"ordinaryFileRevision", manifest}, {"folderImportRevision", importedManifest},
            {"modalEarlyReturnCleaned", cancelled}, {"folderImportReviewed", imported},
            {"receipts", updatedReceipts}}).toJson()));
        screenshot(window, "mainwindow-xips-dark");
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        QCOMPARE(host->property("nativeComponentDarkTheme").toBool(), false);
        screenshot(window, "mainwindow-xips-light");
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        QCOMPARE(host->property("nativeComponentDarkTheme").toBool(), true);
    }

private slots:
    void cleanupTestCase()
    {
        if (finalUiRequested() && !report.isEmpty())
            QVERIFY(put(QDir(report).filePath("ui-observations.json"), QJsonDocument(QJsonObject{
                {"finalUiIntegration", finalUi}, {"layouts", uiLayouts}, {"pointerChecks", pointerEvidence}, {"screenshots", captures}}).toJson()));
    }
    void initTestCase()
    {
        QVERIFY2(!qEnvironmentVariableIsEmpty("XIPS_BROWSER_LIBRARY"), "Real xIPs DLL required");
        QVERIFY2(QFileInfo::exists(qEnvironmentVariable("SIMDOCK_TEST_QUESTA")), "Actual Questa executable required");
        report = qEnvironmentVariable("ZEROSLACK_NATIVE_REPORT");
        if (!report.isEmpty()) QVERIFY(QDir().mkpath(report));
    }
    void unifiedTabRegionsInActualMainWindow()
    {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto file=fixture.filePath("regions.sv");
        QVERIFY(put(file,"module regions;\nendmodule\n"));
        MainWindow window;
        const auto screen=window.screen()->availableGeometry();
        window.resize(qMin(1450,screen.width()-24),qMin(900,screen.height()-48));
        window.move(screen.topLeft()+QPoint(12,12)); window.show();
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        auto* sessions=window.findChild<WorkspaceSessionCoordinator*>();
        QVERIFY(sessions && sessions->openWorkspace(fixture.path()));
        auto* manager=window.tabManager.get(); auto* registry=manager->editorSplitController();
        QCOMPARE(registry->regions(),QStringList({"bottom","left","main","right"}));
        QVERIFY(manager->openFileInTab(file));
        auto* editor=manager->getCurrentEditor(); QVERIFY(editor);
        auto* document=manager->sharedDocumentForEditor(editor); QVERIFY(document);
        const auto identity=document->documentId(); const auto pageId=registry->pageId(editor);
        const auto viewId=editor->property("editorViewId");
        auto* files=registry->pageById("tool:files"); auto* design=registry->pageById("tool:design");
        auto* log=registry->pageById("tool:simulationLog"); QVERIFY(files && design && log);
        auto* simulation=window.simulationWorkbench()->controller();
        for (const auto& area : {QStringLiteral("left"),QStringLiteral("bottom"),QStringLiteral("right"),QStringLiteral("main")}) {
            QVERIFY(registry->movePage(editor,registry->regionGroup(area)));
            QTRY_VERIFY(editor->isVisible());
            QCOMPARE(registry->pageId(editor),pageId); QCOMPARE(editor->property("editorViewId"),viewId);
            QCOMPARE(document->documentId(),identity); QCOMPARE(document->viewCount(),1);
            editor->setFocus(); QTRY_COMPARE(manager->editorActionTarget(),editor);
            QTest::keyClicks(editor,"x"); editor->undo();
            QCOMPARE(editor->toPlainText(),QStringLiteral("module regions;\nendmodule\n"));
            screenshot(window,"code-"+area);
            if (area!=QStringLiteral("main")) {
                auto* source=registry->groupForPage(editor);
                auto* target=registry->regionGroup(QStringLiteral("main"));
                bool started=false,entered=false,moved=false,dropped=false;
                const auto connection=connect(registry,&EditorSplitController::tabDragStarted,&window,[&](QDrag* drag) {
                    started=true;
                    const QPoint position=target->rect().center();
                    QDragEnterEvent enter(position,Qt::MoveAction,drag->mimeData(),Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(target,&enter);entered=enter.isAccepted();
                    QDragMoveEvent moving(position,Qt::MoveAction,drag->mimeData(),Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(target,&moving);moved=moving.isAccepted();
                    QDropEvent drop(position,Qt::MoveAction,drag->mimeData(),Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(target,&drop);dropped=drop.isAccepted();
                });
                auto* sourceBar=source->tabBar();
                const QPoint press=sourceBar->tabRect(source->indexOf(editor)).center();
                QTest::mousePress(sourceBar,Qt::LeftButton,Qt::NoModifier,press);
                const QPoint end=press+QPoint(0,100);
                QMouseEvent move(QEvent::MouseMove,end,sourceBar->mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(sourceBar,&move);
                QTest::mouseRelease(sourceBar,Qt::LeftButton,Qt::NoModifier,press);
                disconnect(connection);
                QVERIFY(started && entered && moved && dropped);
                QCOMPARE(registry->groupForPage(editor),target);
                QTRY_COMPARE(manager->editorActionTarget(),editor);
                QCOMPARE(registry->pageId(editor),pageId);
                QCOMPARE(document->viewCount(),1);
                screenshot(window,"drop-back-from-"+area);
            }
        }
        auto* pane=window.findChild<NavigationPaneCoordinator*>(); QVERIFY(pane);
        auto* linked=window.findChild<LinkedContextController*>(); QVERIFY(linked);
        auto* linkedSource=registry->createSplit(registry->regionGroup("left"),EditorSplitDirection::Below);
        QVERIFY(linkedSource && registry->movePage(editor,linkedSource));
        const auto linkedGroupId=registry->groupId(linkedSource);
        QVERIFY(registry->activatePage(editor));
        QVERIFY(linked->setActive(true));
        QTRY_VERIFY(editor->isVisibleTo(&window));
        QCOMPARE(registry->regionForGroup(registry->groupForPage(editor)),QStringLiteral("main"));
        QCOMPARE(registry->groupById(linkedGroupId),linkedSource);
        editor->insertPlainText("// linked\n"); editor->undo();
        QCOMPARE(editor->toPlainText(),QStringLiteral("module regions;\nendmodule\n"));
        screenshot(window,"linked-from-left");
        QVERIFY(linked->setActive(false));
        QCOMPARE(registry->groupForPage(editor),linkedSource);
        QCOMPARE(registry->pageId(editor),pageId);
        QCOMPARE(document->viewCount(),1);
        QVERIFY(registry->movePage(editor,registry->regionGroup("main")));
        pane->setExpanded(false,false);
        QTest::qWait(100);
        auto* origin=registry->groupForPage(editor);
        bool dragStarted=false,hoverOpened=false,enteredLeft=false;
        const auto dragConnection=connect(registry,&EditorSplitController::tabDragStarted,&window,[&](QDrag* drag) {
            dragStarted=true;
            QCursor::setPos(window.centralWidget()->mapToGlobal(QPoint(2,window.centralWidget()->height()/2)));
            for (int wait=0;wait<75 && !pane->isExpanded();++wait) QTest::qWait(20);
            hoverOpened=pane->isExpanded();
            for (int wait=0;wait<50 && pane->isAnimating();++wait) QTest::qWait(20);
            auto* target=registry->regionGroup(QStringLiteral("left"));
            const QPoint position=target->tabBar()->tabRect(0).center();
            QDragEnterEvent enter(position,Qt::MoveAction,drag->mimeData(),Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(target->tabBar(),&enter);
            enteredLeft=enter.isAccepted() && registry->regionForGroup(registry->groupForPage(editor))==QStringLiteral("left");
            if (!enteredLeft) qWarning() << "Left drag entry" << enter.isAccepted() << target->size()
                << registry->regionForGroup(registry->groupForPage(editor));
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
            QApplication::sendEvent(target->tabBar(),&escape);
        });
        auto* bar=origin->tabBar();
        const QPoint press=bar->tabRect(origin->indexOf(editor)).center();
        QTest::mousePress(bar,Qt::LeftButton,Qt::NoModifier,press);
        const QPoint end=press+QPoint(0,100);
        QMouseEvent move(QEvent::MouseMove,end,bar->mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(bar,&move);
        QTest::mouseRelease(bar,Qt::LeftButton,Qt::NoModifier,press);
        disconnect(dragConnection);
        QVERIFY(dragStarted);
        QVERIFY(hoverOpened);
        QVERIFY(enteredLeft);
        QCOMPARE(registry->groupForPage(editor),origin);
        QCOMPARE(registry->pageId(editor),pageId);
        QTRY_VERIFY(!pane->isExpanded());
        QVERIFY(manager->duplicateCurrentView()); auto* duplicate=manager->getCurrentEditor(); QVERIFY(duplicate!=editor);
        QVERIFY(registry->movePage(duplicate,registry->regionGroup("left")));
        duplicate->insertPlainText("// shared\n"); QCOMPARE(editor->toPlainText(),duplicate->toPlainText());
        editor->undo(); QVERIFY(!document->dirty()); duplicate->redo(); QVERIFY(document->dirty());
        QVERIFY(registry->movePage(files,registry->regionGroup("bottom")));
        QVERIFY(registry->movePage(design,registry->regionGroup("right")));
        auto* navigation=window.findChild<NavigationWidget*>(); QVERIFY(navigation);
        navigation->setSearchFilter(NavigationWidget::FileTab,"regions");
        navigation->setSearchFilter(NavigationWidget::DesignTab,"other");
        QCOMPARE(navigation->searchFilter(NavigationWidget::FileTab),QStringLiteral("regions"));
        QCOMPARE(navigation->searchFilter(NavigationWidget::DesignTab),QStringLiteral("other"));
        navigation->setActiveTab(NavigationWidget::FileTab); QTRY_VERIFY(files->isVisible());
        QCOMPARE(registry->regionForGroup(registry->groupForPage(files)),QStringLiteral("bottom"));
        QVERIFY(manager->editorActionTarget()==nullptr);
        QVERIFY(registry->requestPageClose(files)); QVERIFY(!registry->groupForPage(files));
        navigation->setActiveTab(NavigationWidget::FileTab); QCOMPARE(registry->pageById("tool:files"),files);
        QVERIFY(registry->movePage(log,registry->regionGroup("left")));
        QVERIFY(registry->requestPageClose(log)); QVERIFY(!registry->groupForPage(log));
        QCOMPARE(window.simulationWorkbench()->controller(),simulation);
        PanelLayoutController* panels=nullptr;
        for (auto* child : window.children()) if (auto* candidate=dynamic_cast<PanelLayoutController*>(child)) panels=candidate;
        QVERIFY(panels && panels->restorePanel("simulationLog"));
        QCOMPARE(registry->pageById("tool:simulationLog"),log);
        QCOMPARE(registry->regionForGroup(registry->groupForPage(log)),QStringLiteral("left"));
        pane->setExpanded(false,false); QVERIFY(!pane->isExpanded());
        QVERIFY(registry->activatePage(duplicate)); QTRY_VERIFY(pane->isExpanded());
        screenshot(window,"all-regions-wide");
        const int wideWidth=window.width();
        window.resize(qMin(wideWidth*2/3,qMax(680,screen.width()/3)),window.height());
        QTest::qWait(150);
        screenshot(window,"all-regions-narrow");
        if (!screen.contains(window.frameGeometry())) {
            qWarning() << "Screen" << screen << "window" << window.frameGeometry()
                       << "minimum" << window.minimumSize() << window.minimumSizeHint();
            for (auto* child : window.findChildren<QWidget*>())
                if (child->isVisible() && (child->minimumSizeHint().width() > 400 || child->minimumWidth() > 180))
                    qWarning() << child->metaObject()->className() << child->objectName() << child->size()
                               << "minimum" << child->minimumSize() << child->minimumSizeHint();
        }
        QVERIFY(screen.contains(window.frameGeometry()));
        QVERIFY2(window.width()<wideWidth,"Narrow capture must have a genuinely smaller window");
        QVERIFY(manager->saveEditorView(duplicate,false,file));
        registry->activatePage(editor); manager->closeTab(registry->groupForPage(editor)->indexOf(editor));
        QCOMPARE(document->viewCount(),1); QVERIFY(manager->openEditors().contains(duplicate));
        QVERIFY(manager->closeAllTabs());
        if (!report.isEmpty()) QVERIFY(put(QDir(report).filePath("unified-tabs.json"),QJsonDocument(QJsonObject{
            {"platform",QGuiApplication::platformName()},{"dpr",window.devicePixelRatioF()},
            {"width",window.width()},{"height",window.height()},{"sharedDocument",true},{"toolLifetime",true},{"snapshots",captures}}).toJson()));
    }
    void simultaneousComponentsUseActualMainWindow()
    {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto a = fixture.filePath("project"), b = fixture.filePath("other"), library = fixture.filePath("library");
        QVERIFY(QDir().mkpath(b) && QDir().mkpath(library));
        QVERIFY(put(a + "/dut.sv", "module checked(input logic clk,rst,byte_valid,out_ready, input logic [7:0] byte_data, "
                    "output logic byte_ready,uart_tx,out_valid,output logic [7:0] out_data); "
                    "assign byte_ready=1'b1; assign uart_tx=1'b1; assign out_valid=byte_valid; assign out_data=byte_data; endmodule\n"));
        QVERIFY(put(a + "/unused.sv", "module unused; endmodule\n"));
        const QByteArray positiveTb =
            "module tb_checked; timeunit 1ns; timeprecision 1ps;\n"
            "logic clk=0,rst=0,byte_valid=0,out_ready=1; logic [7:0] byte_data=0;\n"
            "wire byte_ready,uart_tx,out_valid; wire [7:0] out_data; checked dut(.*);\n"
            "always #5 clk=~clk; initial begin #10; byte_valid=1; byte_data=8'ha5; #1;\n"
            "if(!out_valid || out_data!==8'ha5) $fatal(1,\"HANDWRITTEN_CHECK_FAILED\");\n"
            "$display(\"HANDWRITTEN_CHECK_PASS\"); #9; $finish; end endmodule\n";
        auto negativeTb = positiveTb;
        negativeTb.replace("out_data!==8'ha5", "out_data!==8'h3c");
        QVERIFY(put(a + "/tb_checked.sv", negativeTb));
        const QString projectFile = a + QStringLiteral("/.simdock/projects/%1.json").arg(projectId);
        const QJsonObject project{{"schema", "simdock.project/v1"}, {"id", projectId}, {"name", "ZeroSlack integration"},
            {"sources", QJsonArray{"dut.sv"}}, {"dutFile", "dut.sv"}, {"dutName", "checked"}, {"durationNs", 10000}, {"tbFile", "tb_checked.sv"}, {"tbName", "tb_checked"}};
        QVERIFY(put(projectFile, QJsonDocument(project).toJson()));
        const auto oldPath = qgetenv("PATH"), oldLibrary = qgetenv("XIPS_LIBRARY");
        const auto oldDirectory = QDir::currentPath();
        const auto oldTheme = ApplicationThemeManager::instance().mode();
        const auto restore = qScopeGuard([=] {
            qputenv("PATH", oldPath); QDir::setCurrent(oldDirectory);
            if (oldLibrary.isNull()) qunsetenv("XIPS_LIBRARY"); else qputenv("XIPS_LIBRARY", oldLibrary);
            ApplicationThemeManager::instance().setMode(oldTheme);
        });
        for (int i = 0; i < 4 && !qEnvironmentVariableIsEmpty("PATH"); ++i)
            QVERIFY(qunsetenv("PATH"));
        QVERIFY(qEnvironmentVariableIsEmpty("PATH"));
        qputenv("XIPS_LIBRARY", library.toUtf8());
        QVERIFY(QDir::setCurrent(fixture.path()));
        QVERIFY(SettingsCenterService().saveGlobal({{"appearance.theme", "Dark"}}).saved);
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        MainWindow window;
        const auto screen = window.screen()->availableGeometry();
        window.resize(qMin(1500, screen.width() - 24), qMin(860, screen.height() - 60));
        window.move(screen.topLeft() + QPoint(12, 12)); window.show();
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        auto *sessions = window.findChild<WorkspaceSessionCoordinator *>();
        auto *controller = window.findChild<ContextWorkspaceController *>();
        QVERIFY(sessions && controller);
        QVERIFY(sessions->openWorkspace(a));
        QVERIFY(window.tabManager->openFileInTab(a + "/dut.sv"));
        QCOMPARE(ApplicationThemeManager::instance().mode(), ThemeMode::Dark);
        auto *title = window.findChild<QLabel *>("workspaceFilePath");
        QVERIFY(title);
        const QString expectedTitle = QStringLiteral("ZeroSlack v%1").arg(QLatin1String(APP_VERSION));
        QCOMPARE(title->text(), expectedTitle);
        const auto font = qApp->font(), hostFont = window.font();
        const auto palette = qApp->palette();
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        const auto lightPalette = qApp->palette();
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        auto *style = qApp->style();
        const auto sheet = qApp->styleSheet(), identity = qApp->applicationName(), organization = qApp->organizationName();
        const auto version = qApp->applicationVersion();
        const auto pluginPaths = qApp->libraryPaths();
        QSettings().setValue("host/sentinel", "preserved");
        auto settingsKeys = QSettings().allKeys();
        settingsKeys.removeIf([](const QString& key) { return key.startsWith("integrations/simdock/"); });
        XipsContextProvider xips(nullptr, nullptr);
        SimDockContextProvider simdock;
        const auto xresource = xips.activationResource(a);
        QVERIFY(controller->openResource(xresource, {ContextSurface::Docked, ContextPersistence::Kept}));
        QPointer<NativeContextView> xhost = qobject_cast<NativeContextView *>(controller->viewForResource(xresource.stableKey()));
        QVERIFY(xhost);
        QVERIFY2(xhost->isReady(), qPrintable(xhost->property("nativeComponentError").toString()));
        QCOMPARE(xhost->property("nativeComponentDarkTheme").toBool(), true);
        QPointer<SimDockContextView> shost = window.findChild<SimDockContextView*>();
        QVERIFY(shost && shost->isReady());
        shost->workbench()->setSimulator(qEnvironmentVariable("SIMDOCK_TEST_QUESTA"));
        QVERIFY(shost->workbench()->openProject(projectId).isEmpty());
        QCOMPARE(shost->property("nativeComponentDarkTheme").toBool(), true);
        if (finalUiRequested()) { verifyThemes(xhost, shost, true); QVERIFY(!QTest::currentTestFailed()); }
        QCOMPARE(qApp->font(), font); QCOMPARE(window.font(), hostFont);
        QCOMPARE(qApp->palette(), palette); QCOMPARE(qApp->style(), style); QCOMPARE(qApp->styleSheet(), sheet);
        QCOMPARE(qApp->applicationName(), identity); QCOMPARE(qApp->organizationName(), organization);
        QCOMPARE(qApp->applicationVersion(), version); QCOMPARE(qApp->libraryPaths(), pluginPaths);
        auto remainingKeys = QSettings().allKeys();
        remainingKeys.removeIf([](const QString& key) { return key.startsWith("integrations/simdock/"); });
        QCOMPARE(remainingKeys, settingsKeys); QCOMPARE(title->text(), expectedTitle);
        QJsonArray xipsModules;
        const auto xipsDirectory = QFileInfo(qEnvironmentVariable("XIPS_BROWSER_LIBRARY")).canonicalPath();
        for (const QString &name : {"xips-browser.dll", "xips-browser-impl.dll", "XipsEla.dll"}) {
            const auto entry = moduleEvidence(name);
            QVERIFY2(!entry.value("path").toString().isEmpty(), qPrintable(name));
            QCOMPARE(QFileInfo(entry.value("path").toString()).canonicalPath(), xipsDirectory);
            xipsModules.append(entry);
        }
        if (!report.isEmpty()) QVERIFY(put(QDir(report).filePath("xips-loaded-modules.json"),
            QJsonDocument(QJsonObject{{"modules", xipsModules}}).toJson()));
        QVERIFY(controller->focusResource(xresource.stableKey()));
        xipsWorkflow(window, xhost, fixture.path(), a, library);
        QVERIFY(!QTest::currentTestFailed());
        auto* panel = shost->workbench();
        auto* simulationController = panel->controller();
        auto* session = simulationController->session();
        session->setInteractive(false);
        auto* log = window.findChild<QPlainTextEdit*>("simulationLog"); QVERIFY(log);
        const SimulationIdentity simulation(shost); simulation.verify(window); QVERIFY(!QTest::currentTestFailed());
        const auto stopOnFailure = qScopeGuard([session] { session->stop(); });
        QSignalSpy finished(simulationController, &simdock::WorkspaceSimulationController::finished);
        click(&window, "bottomPanelButton_simulationLog");
        if (finalUiRequested()) {
            finalNativeUi(window, controller, xhost, shost, xresource.stableKey(), palette, lightPalette);
            QVERIFY(!QTest::currentTestFailed());
        }
        click(panel, "startSimulation");
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 45000);
        QVERIFY2(!finished.takeFirst()[0].toBool(), qPrintable(log->toPlainText()));
        QCOMPARE(session->outcome(), QString("fatal"));
        QTRY_COMPARE(panel->findChild<QAbstractButton*>("startSimulation")->text(),QString("Run"));
        QVERIFY(log->toPlainText().contains("HANDWRITTEN_CHECK_FAILED"));
        if (!report.isEmpty()) QVERIFY(put(QDir(report).filePath("questa-negative-check.log"), log->toPlainText().toUtf8()));
        const auto pid = session->processId();
        QVERIFY(put(a + "/tb_checked.sv", positiveTb));
        click(panel, "startSimulation");
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 45000);
        QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(log->toPlainText()));
        QCOMPARE(session->processId(), pid); QCOMPARE(session->outcome(), QString("finished"));
        QTRY_COMPARE(panel->findChild<QAbstractButton*>("startSimulation")->text(),QString("Run"));
        QVERIFY(log->toPlainText().contains("HANDWRITTEN_CHECK_PASS"));
        const auto plan = simulationController->lastPlan(); QVERIFY(plan && plan->valid());
        QCOMPARE(plan->compilation.files.size(), 2);
        QVERIFY(!plan->compilation.files.contains(a + "/unused.sv"));
        const auto firstRun = session->lastRunDirectory();
        click(panel, "startSimulation");
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 45000);
        QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(log->toPlainText()));
        QCOMPARE(session->processId(), pid); QVERIFY(!QFileInfo::exists(firstRun));
        screenshot(window, "mainwindow-simdock-run");
        if (!report.isEmpty()) QVERIFY(put(QDir(report).filePath("questa.log"), log->toPlainText().toUtf8()));
        auto infiniteTb = positiveTb; infiniteTb.replace("#9; $finish;", "#9;");
        QVERIFY(put(a + "/tb_checked.sv", infiniteTb));
        // A failed run exposes the log; restore the editor before checking
        // that a rejected workspace transition preserves the active editor.
        QVERIFY(window.tabManager->openFileInTab(a + "/dut.sv"));
        QPointer<MyCodeEditor> activeEditor = window.tabManager->getCurrentEditor();
        QVERIFY(activeEditor);
        click(panel, "startSimulation");
        QTRY_VERIFY_WITH_TIMEOUT(simulationController->state() == "Running", 15000);
        QVERIFY(!sessions->openWorkspace(b)); QVERIFY(!sessions->closeWorkspace(0));
        QCOMPARE(window.workspaceManager->getWorkspacePath(), a);
        QVERIFY(window.tabManager->getCurrentEditor());
        QCOMPARE(window.tabManager->getCurrentEditor(), activeEditor.data());
        QCloseEvent close; QApplication::sendEvent(&window, &close); QVERIFY(!close.isAccepted());
        shost->hide(); QTest::qWait(100); QVERIFY(session->busy());
        shost->show(); simulation.verify(window); QVERIFY(!QTest::currentTestFailed());
        const auto owned = QFileInfo(session->lastRunDirectory()).absolutePath();
        click(panel, "startSimulation");
        QTRY_VERIFY_WITH_TIMEOUT(!session->alive(), 10000);
        QCOMPARE(session->outcome(), QString("cancelled")); QVERIFY(!QFileInfo::exists(owned));
        QTRY_COMPARE(panel->findChild<QAbstractButton*>("startSimulation")->text(),QString("Run"));
        QCOMPARE(QJsonDocument::fromJson(get(projectFile)).object(), project);
        QVERIFY(put(a + "/tb_checked.sv", positiveTb));
        QJsonArray modules;
        for (const QString &name : {"libzeroslack_core.dll", "ElaWidgetTools.dll", "xips-browser.dll", "xips-browser-impl.dll", "XipsEla.dll",
                                    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll",
                                    "Qt6Network.dll", "Qt6Sql.dll", "Qt6Svg.dll", "qwindows.dll", "libstdc++-6.dll", "libwinpthread-1.dll"}) {
            const auto entry = moduleEvidence(name);
            QVERIFY2(!entry.value("path").toString().isEmpty(), qPrintable(name)); modules.append(entry);
        }
        const QJsonObject evidence{{"host", "ZeroSlack MainWindow"}, {"version", version}, {"title", title->text()},
            {"platform", QGuiApplication::platformName()}, {"path", qEnvironmentVariable("PATH")},
            {"cwd", QDir::currentPath()}, {"modules", modules},
            {"xipsAbi", xhost->property("nativeComponentAbi").toString()},
            {"simdockSourceVersion", shost->property("simdockSourceVersion").toString()},
            {"xipsCapabilities", QJsonObject::fromVariantMap(xhost->property("nativeComponentCapabilities").toMap())},
            {"simdockCapabilities", QJsonObject::fromVariantMap(shost->property("nativeComponentCapabilities").toMap())}};
        if (!report.isEmpty()) QVERIFY(put(QDir(report).filePath("runtime-evidence.json"), QJsonDocument(evidence).toJson()));
        QVERIFY(moduleEvidence("simdock-workbench.dll").value("path").toString().isEmpty());
        QVERIFY(moduleEvidence("SimDockEla.dll").value("path").toString().isEmpty());
        QVERIFY(moduleEvidence("wavewidgets.dll").value("path").toString().isEmpty());
        QVERIFY(moduleEvidence("WaveWorkbenchEla.dll").value("path").toString().isEmpty());
        const auto originalSimDock = qgetenv("SIMDOCK_WORKBENCH_LIBRARY");
        const auto restoreSimDock = qScopeGuard([originalSimDock] { qputenv("SIMDOCK_WORKBENCH_LIBRARY", originalSimDock); });
        qputenv("SIMDOCK_WORKBENCH_LIBRARY", fixture.filePath("missing-retired-component.dll").toUtf8());
        {
            std::unique_ptr<QWidget> independent(simdock.createView(simdock.activationResource(a), nullptr));
            auto *other = qobject_cast<SimDockContextView *>(independent.get());
            QVERIFY(other && other->isReady());
            QVERIFY(!other->property("nativeComponentLibrary").isValid());
        }
        const auto sstate = shost->saveState(), xstate = xhost->saveState();
        shost->hide(); simulation.verify(window); QVERIFY(!QTest::currentTestFailed());
        shost->show(); shost->restoreState(sstate);
        QCOMPARE(shost->saveState(), sstate);
        QVERIFY(sessions->openWorkspace(b)); QVERIFY(sessions->switchWorkspace(0));
        QVERIFY(controller->openTool("xips"));
        xhost = qobject_cast<NativeContextView*>(controller->viewForResource(xresource.stableKey()));
        QVERIFY(xhost && xhost->isReady()); QTRY_VERIFY_WITH_TIMEOUT(xhost->canClose(), 10000);
        QCOMPARE(xhost->saveState().value("library"), xstate.value("library"));
        QCOMPARE(xhost->saveState().value("assetId"), xstate.value("assetId"));
        shost = window.findChild<SimDockContextView*>(); QVERIFY(shost && shost->isReady());
        QCOMPARE(shost.data(), simulation.workspace.data());
        simulation.verify(window); QVERIFY(!QTest::currentTestFailed());
        QCOMPARE(shost->workbench()->controller()->target().top, QString("tb_checked"));
        QCOMPARE(shost->workbench()->controller()->legacyProjectId(), QString::fromLatin1(projectId));
        QCOMPARE(qApp->font(), font); QCOMPARE(qApp->palette(), palette); QCOMPARE(qApp->style(), style);
        QCOMPARE(qApp->styleSheet(), sheet); QCOMPARE(qApp->applicationName(), identity);
        QCOMPARE(qApp->organizationName(), organization); QCOMPARE(qApp->applicationVersion(), version);
        QCOMPARE(QSettings().value("host/sentinel").toString(), QStringLiteral("preserved"));
        QCOMPARE(title->text(), expectedTitle); QCOMPARE(qgetenv("PATH"), QByteArray());
        QCOMPARE(QDir::currentPath(), fixture.path());
        if (finalUiRequested()) {
            verifyThemes(xhost, shost, true); QVERIFY(!QTest::currentTestFailed());
            finalUi["globalPurity"] = true; finalUi["closeReopenContext"] = true;
            finalUi["simulationControllerAndLogDocumentRetained"] = true;
            QVERIFY(put(QDir(report).filePath("final-ui-evidence.json"), QJsonDocument(QJsonObject{
                {"host", "ZeroSlack MainWindow"}, {"platform", QGuiApplication::platformName()}, {"dpr", window.devicePixelRatioF()},
                {"finalUiIntegration", finalUi}, {"layouts", uiLayouts}, {"pointerChecks", pointerEvidence}, {"screenshots", captures}}).toJson()));
        }
        QVERIFY(window.close());
        QVERIFY(!window.isVisible());
    }
};

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir profile;
    if (!profile.isValid()) return 2;
    QCoreApplication::setApplicationName("ZeroSlack");
    QCoreApplication::setOrganizationName("ZeroSlack");
    QCoreApplication::setApplicationVersion(QLatin1String(APP_VERSION));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", profile.filePath("sessions.ini").toUtf8());
    qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH", profile.filePath("legacy.ini").toUtf8());
    qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH", profile.filePath("editing-time.json").toUtf8());
    if (!initializeUiStyleForTest()) return 3;
    ApplicationThemeManager::instance().applyToApplication();
    NativeMainWindowIntegrationTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "native_mainwindow_integration_test.moc"
