#include <QMenuBar>
#include <QToolBar>
#include "editorgutter.h"
#include "fixture_names.h"
// Offscreen GUI smoke test for the real MainWindow/TabManager/MyCodeEditor path.
// It keeps the assertions coarse on purpose: this target is a repeatable guard that
// the GUI workflow is alive, while detailed semantic behavior stays in the focused
// headless tests.
#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QCompleter>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QImage>
#include <QIODevice>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStatusBar>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPointer>
#include <QRegularExpression>
#include <QRadioButton>
#include <QScrollBar>
#include <QScreen>
#include <QSet>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFragment>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>
#include <QtTest/QTest>

#include "version.h"
#include "uitypography.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iostream>
#include <functional>
#include <memory>
#include <utility>

#define private public
#include "mainwindow.h"
#include "insightvisualstyle.h"
#include "panellayoutcontroller.h"
#include "analysiscoordinator.h"
#include "analysisprogresscoordinator.h"
#include "analysisscheduler.h"
#include "activitylogservice.h"
#include "documentmodel.h"
#include "definitionpreviewservice.h"
#include "diagnosticservice.h"
#include "editorappearance.h"
#include "editorappearancesettings.h"
#include "editorcoordinator.h"
#include "editorgeometry.h"
#include "editorhoverpopup.h"
#include "editorruntime.h"
#include "editorsemanticcontextservice.h"
#include "effectivevalueservice.h"
#include "filecommandcoordinator.h"
#include "foldblockshelfmodel.h"
#include "foldblockshelfpanel.h"
#include "columnnumbertool.h"
#include "commandlayercommandregistry.h"
#include "commandlayercoordinator.h"
#include "commandlayerservice.h"
#include "completionservice.h"
#include "codetemplateservice.h"
#include "crashrecoveryservice.h"
#include "globalcontrolcoordinator.h"
#include "globalcontrolpanel.h"
#include "globalcontrolservice.h"
#include "hierarchyservice.h"
#include "liveinsightsession.h"
#include "instancepairconnectionpanel.h"
#include "multisignalpropagationpanel.h"
#include "navigationwidget.h"
#include "navigationpanecoordinator.h"
#include "semantic_fixture_records.h"
#include "sourcenavigationservice.h"
#include "symbolanalyzer.h"
#include "symbolhoverservice.h"
#include "navigationmanager.h"
#include "navigationservice.h"
#include "navigationcommandcoordinator.h"
#include "notificationcenter.h"
#include "problemspanelcoordinator.h"
#include "rtlhighriskeditpanel.h"
#include "rtlinsightspanelcoordinator.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "semanticdockcoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "semanticruntimecoordinator.h"
#include "settingscenterpanel.h"
#include "signalkernelgraphpanelcoordinator.h"
#include "signalusagehotspotpanel.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "tabmanager.h"
#include "tsdocument.h"
#include "usertemplateservice.h"
#include "wavepreviewpanelcoordinator.h"
#include "wavesimulationcoordinator.h"
#include "workspaceanalysisplanservice.h"
#include "workspaceanalysisrequestqueue.h"
#include "workspacemanager.h"
#undef private
#include "applicationthememanager.h"
#include "contextdockhost.h"
#include "contextpeekhost.h"
#include "contextrail.h"
#include "contextworkspacecontroller.h"
#include "editordroppreviewoverlay.h"
#include "liveinsightscontextprovider.h"
#include "liveinsightscontextview.h"
#include "liveinsighttoolpage.h"
#include "rtlinsightworkbench.h"
#include "temporaryeditorcontextprovider.h"
#include "temporaryeditorcontextview.h"

static int g_checks = 0;
static int g_fails = 0;

class ScopedGuiTestSettingsRoot final
{
public:
    ScopedGuiTestSettingsRoot()
        : previousFormat(QSettings::defaultFormat())
    {
        if (!settingsRoot.isValid())
            return;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,
                           QSettings::UserScope,
                           settingsRoot.path());
    }

    ~ScopedGuiTestSettingsRoot()
    {
        QSettings::setDefaultFormat(previousFormat);
        // QSettings has no path getter. The IniFormat path override remains
        // process-local and the test process exits after this guard.
    }

    bool isValid() const
    {
        return settingsRoot.isValid();
    }

    QString path() const
    {
        return settingsRoot.path();
    }

private:
    QTemporaryDir settingsRoot;
    QSettings::Format previousFormat;
};

static bool waitUntil(const std::function<bool()>& predicate, int timeoutMs);

static QString sourceFixturePath(const QString& relativePath)
{
    const QString normalizedRelative =
        QDir::fromNativeSeparators(relativePath);
    QStringList roots;
    const QString sourceRoot =
        qEnvironmentVariable("ZEROSLACK_SOURCE_DIR");
    if (!sourceRoot.isEmpty())
        roots << sourceRoot;
    roots << QDir::currentPath() << QCoreApplication::applicationDirPath();
    for (const QString& root : std::as_const(roots)) {
        QDir dir(root);
        for (int depth = 0; depth < 8; ++depth) {
            const QString candidate = dir.absoluteFilePath(normalizedRelative);
            if (QFileInfo(candidate).exists())
                return candidate;
            if (!dir.cdUp())
                break;
        }
    }
    return QDir::current().absoluteFilePath(normalizedRelative);
}

static std::shared_ptr<const SemanticIndexSnapshot> snapshotFromRecords(
    const QList<SemanticSymbolRecord>& records,
    QList<SemanticRelationship> relationships = {},
    QList<SemanticDiagnostic> diagnostics = {},
    QHash<QString, QString> fileContents = {})
{
    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            records,
            std::move(relationships),
            std::move(diagnostics),
            std::move(fileContents)));
}

static void expectBool(const char* what, bool got, bool want)
{
    ++g_checks;
    const bool ok = (got == want);
    if (!ok)
        ++g_fails;
    printf("[%s] %-48s got=%s want=%s\n",
           ok ? "PASS" : "FAIL", what, got ? "true" : "false", want ? "true" : "false");
    fflush(stdout);
}

static bool writeTextFile(const QString& fileName, const QString& text)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QFile::Text))
        return false;
    QTextStream out(&file);
    out << text;
    return true;
}

static bool copyFixtureTree(const QString& sourceRoot,
                            const QString& destinationRoot)
{
    const QDir source(sourceRoot);
    if (!source.exists()
        || !QDir().mkpath(destinationRoot)) {
        return false;
    }

    QDirIterator iterator(
        sourceRoot,
        QDir::AllEntries
            | QDir::Hidden
            | QDir::System
            | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourcePath = iterator.next();
        const QFileInfo sourceInfo(sourcePath);
        const QString relativePath =
            source.relativeFilePath(sourcePath);
        const QString destinationPath =
            QDir(destinationRoot)
                .absoluteFilePath(relativePath);
        if (sourceInfo.isDir()) {
            if (!QDir().mkpath(destinationPath))
                return false;
            continue;
        }
        if (!QDir().mkpath(
                QFileInfo(destinationPath)
                    .absolutePath())
            || !QFile::copy(sourcePath,
                            destinationPath)) {
            return false;
        }
    }
    return true;
}

static bool saveFullAppSignalUsageHotspotScreenshot(MainWindow& window,
                                                    const QString& fixturePath)
{
    SemanticPanelRefreshCoordinator* refresh =
        window.semanticDocks
        ? window.semanticDocks->refreshCoordinator()
        : nullptr;
    if (!refresh || !window.contextWorkspaceController) {
        return false;
    }

    refresh->showSignalUsageHotspotForSymbol(
        QStringLiteral("data_q"),
        fixturePath,
        QStringLiteral("insight_top"));
    if (window.panelLayoutController)
        window.panelLayoutController->setBottomCollapsed(true);
    if (QDockWidget* navigationDock = window.dockForPanelId(QStringLiteral("navigation")))
        navigationDock->show();

    window.resize(1900, 1040);
    window.showNormal();
    window.raise();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    QTest::qWait(300);
    expectBool("full screenshot has an active main-window layout",
               window.layout()->geometry() == window.contentsRect(), true);
    auto* contextHost = window.contextWorkspaceController->dockHost();
    const QRect contextRect(contextHost->mapTo(&window, QPoint()), contextHost->size());
    expectBool("full screenshot center does not overlap Context",
               window.centralWidget()->geometry().intersected(contextRect).isEmpty(), true);

    QString artifactRoot =
        qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
    if (artifactRoot.isEmpty()) {
        artifactRoot = QDir::temp().absoluteFilePath(
            QStringLiteral("zeroslack-gui-smoke"));
    }
    if (!QDir().mkpath(artifactRoot))
        return false;
    QFile geometryFile(QDir(artifactRoot).filePath(QStringLiteral("geometry.txt")));
    if (geometryFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&geometryFile);
        stream << "window " << window.width() << "x" << window.height() << " layout " << window.layout()->geometry().width() << "x" << window.layout()->geometry().height() << '\n';
        for (QWidget* w : window.findChildren<QWidget*>()) {
            if (!w->isVisible()) continue;
            const QPoint at=w->mapTo(&window,QPoint());
            stream << w->metaObject()->className() << ':' << w->objectName() << " at " << at.x() << ',' << at.y() << " size " << w->width() << 'x' << w->height() << " min " << w->minimumSizeHint().width() << " parent " << w->parentWidget()->metaObject()->className() << ':' << w->parentWidget()->objectName() << " size " << w->parentWidget()->width() << 'x' << w->parentWidget()->height() << '\n';
        }
    }
    const QString outputPath =
        QDir(artifactRoot).absoluteFilePath(
            QStringLiteral(
                "live_insights_signal_usage_hotspot_full_after.png"));
    const bool saved = window.grab().save(outputPath);
    if (!saved)
        qWarning() << "Failed to save full app screenshot" << outputPath;
    else
        qInfo() << "Saved full app screenshot" << outputPath;
    const ContextResource activeResource =
        window.contextWorkspaceController->dockHost()
            ->currentResource();
    if (activeResource.isValid()) {
        window.contextWorkspaceController->closePinnedResource(
            activeResource.stableKey());
    }
    if (window.tabManager) {
        QWidget* fullView = window.tabManager->toolPage(
            QStringLiteral("live-insight:hotspot"));
        auto* group = fullView
            ? qobject_cast<QTabWidget*>(
                  fullView->parentWidget())
            : nullptr;
        if (group) {
            const int index = group->indexOf(fullView);
            if (index >= 0)
                window.tabManager->closePage(group, index);
        }
        window.liveInsightToolPages.remove(
            static_cast<int>(LiveInsightKind::Hotspot));
        window.tabManager->activateOpenFile(fixturePath);
    }
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    return saved;
}

static QString editorLayoutArtifactPath(const QString& fileName)
{
    QString artifactRoot =
        qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
    if (artifactRoot.isEmpty()) {
        artifactRoot = QDir::temp().absoluteFilePath(
            QStringLiteral("zeroslack-editor-layout"));
    }
    if (!QDir().mkpath(artifactRoot))
        return QString();
    return QDir(artifactRoot).absoluteFilePath(fileName);
}

static bool saveEditorLayoutScreenshot(MainWindow& window,
                                       const QString& fileName)
{
    const QString path = editorLayoutArtifactPath(fileName);
    return !path.isEmpty() && window.grab().save(path);
}

static int visibleBottomPanelContentHeight(MainWindow& window,
                                           QTabBar* tabBar)
{
    QWidget* central = window.centralWidget();
    if (!central || !tabBar || !tabBar->isVisible())
        return -1;
    const int centralBottom =
        central->mapTo(&window, QPoint(0, 0)).y()
        + central->height();
    const int tabTop =
        tabBar->mapTo(&window, QPoint(0, 0)).y();
    const int separatorExtent = window.style()->pixelMetric(
        QStyle::PM_DockWidgetSeparatorExtent,
        nullptr,
        &window);
    return qMax(0,
                tabTop - centralBottom
                    - qMax(0, separatorExtent));
}

static QString peekLabelText(const QLabel* label)
{
    return label->objectName() == QStringLiteral("peekFieldValue")
        ? label->accessibleName() : label->text();
}

static QString visibleEditorHoverPopupText(bool* visible = nullptr)
{
    bool found = false;
    QString text;
    for (QWidget* widget : QApplication::allWidgets()) {
        if (!widget || widget->objectName() != QStringLiteral("editorHoverPopup")
            || !widget->isVisible()) {
            continue;
        }
        found = true;
        const QList<QLabel*> labels = widget->findChildren<QLabel*>();
        for (const QLabel* label : labels)
            text += peekLabelText(label) + QLatin1Char('\n');
    }
    if (visible)
        *visible = found;
    return text;
}

static void hideEditorHoverPopups()
{
    for (QWidget* widget : QApplication::allWidgets()) {
        if (widget && widget->objectName() == QStringLiteral("editorHoverPopup"))
            widget->hide();
    }
}

static QWidget* visibleEditorHoverPopupWidget()
{
    for (QWidget* widget : QApplication::allWidgets()) {
        if (widget
            && widget->objectName() == QStringLiteral("editorHoverPopup")
            && widget->isVisible()) {
            return widget;
        }
    }
    return nullptr;
}

static bool fontMatchesEditor(const QFont& candidate,
                              const QFont& editorFont)
{
    const QFontInfo candidateInfo(candidate);
    const QFontInfo editorInfo(editorFont);
    if (candidate.families() != editorFont.families()
        || candidateInfo.family() != editorInfo.family()
        || candidate.styleHint() != editorFont.styleHint()
        || candidate.fixedPitch() != editorFont.fixedPitch()) {
        return false;
    }
    if (editorInfo.pointSizeF() > 0.0
        && !qFuzzyCompare(candidateInfo.pointSizeF(),
                          editorInfo.pointSizeF())) {
        return false;
    }
    return true;
}

static bool popupTextUsesSemanticTypography(QWidget* popup,
                                    const QFont& editorFont)
{
    if (!popup)
        return false;
    popup->ensurePolished();
    if (popup->property("symbolInspector").toBool()) {
        bool hasCode = false;
        bool hasUi = false;
        for (const auto* label : popup->findChildren<QLabel*>()) {
            if (label->objectName() == QStringLiteral("peekSymbolIcon")) continue;
            const bool code = label->objectName() == QStringLiteral("peekTitle")
                || label->objectName() == QStringLiteral("peekFieldValue");
            if (code) {
                hasCode = true;
                if (label->font().families() != editorFont.families()
                    || label->font().pixelSize() < 13) return false;
            } else {
                hasUi = true;
                if (label->font().families() != UiTypography::font().families()
                    && label->font().families() != editorFont.families()) return false;
            }
        }
        return hasCode && hasUi;
    }

    const QList<QLabel*> labels = popup->findChildren<QLabel*>();
    if (labels.isEmpty())
        return false;
    for (QLabel* label : labels) {
        if (!label || label->textFormat() != Qt::PlainText
            || !fontMatchesEditor(label->font(), editorFont)) {
            return false;
        }
    }

    for (QAbstractButton* button :
         popup->findChildren<QAbstractButton*>()) {
        if (!fontMatchesEditor(button->font(), editorFont))
            return false;
    }
    for (QLineEdit* lineEdit : popup->findChildren<QLineEdit*>()) {
        if (!fontMatchesEditor(lineEdit->font(), editorFont))
            return false;
    }

    const auto documentUsesEditorFont = [&](QTextDocument* document) {
        if (!document
            || !fontMatchesEditor(document->defaultFont(), editorFont)) {
            return false;
        }
        for (QTextBlock block = document->begin(); block.isValid();
             block = block.next()) {
            for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                if (!fragment.isValid())
                    continue;
                const QFont resolved = fragment.charFormat().font().resolve(
                    document->defaultFont());
                if (!fontMatchesEditor(resolved, editorFont))
                    return false;
            }
        }
        return true;
    };
    for (QTextEdit* textEdit : popup->findChildren<QTextEdit*>()) {
        if (!documentUsesEditorFont(textEdit->document()))
            return false;
    }
    for (QPlainTextEdit* textEdit :
         popup->findChildren<QPlainTextEdit*>()) {
        if (!documentUsesEditorFont(textEdit->document()))
            return false;
    }
    return true;
}

static QRect differentPixelBounds(const QImage& before,
                                  const QImage& after)
{
    if (before.size() != after.size() || before.isNull() || after.isNull())
        return {};
    QRect bounds;
    for (int y = 0; y < before.height(); ++y) {
        for (int x = 0; x < before.width(); ++x) {
            if (before.pixel(x, y) == after.pixel(x, y))
                continue;
            const QRect pixel(x, y, 1, 1);
            bounds = bounds.isNull() ? pixel : bounds.united(pixel);
        }
    }
    return bounds;
}

static QImage renderWidgetImage(QWidget* widget)
{
    if (!widget || widget->size().isEmpty())
        return {};
    QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget->render(&image);
    return image;
}

static bool sendEditorWheel(MyCodeEditor* editor,
                            Qt::KeyboardModifiers modifiers,
                            int angleY)
{
    if (!editor || !editor->viewport())
        return false;

    const QPoint point(16, 16);
    const QPoint globalPoint = editor->viewport()->mapToGlobal(point);
    QWheelEvent event(QPointF(point),
                      QPointF(globalPoint),
                      QPoint(),
                      QPoint(0, angleY),
                      Qt::NoButton,
                      modifiers,
                      Qt::NoScrollPhase,
                      false);
    event.ignore();
    QCoreApplication::sendEvent(editor->viewport(), &event);
    return event.isAccepted();
}

static void acceptNextMessageBoxOk()
{
    auto action = std::make_shared<std::function<void(int)>>();
    *action = [action](int attempts) {
        QMessageBox* box =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) {
            if (attempts > 0)
                QTimer::singleShot(
                    10,
                    [action, attempts]() { (*action)(attempts - 1); });
            return;
        }
        if (QAbstractButton* ok = box->button(QMessageBox::Ok))
            ok->click();
    };
    QTimer::singleShot(0, [action]() { (*action)(50); });
}

static std::unique_ptr<QSettings> makeTemporarySettings(
    const QString& fileName)
{
    return std::make_unique<QSettings>(fileName, QSettings::IniFormat);
}

static void runEditorAppearanceSettingsRegression()
{
    QTemporaryDir settingsDir;
    expectBool("appearance settings temp dir valid",
               settingsDir.isValid(),
               true);
    if (!settingsDir.isValid())
        return;

    const QString settingsFile =
        settingsDir.filePath(QStringLiteral("appearance.ini"));
    EditorAppearanceOptions options = EditorAppearance::defaultOptions();
    options.fontFamily = EditorAppearance::fallbackFontFamily();
    options.fontSizePt = 13;
    options.lineHeight = 1.5;
    options.ligaturesEnabled = true;

    {
        EditorAppearanceSettings settings(
            makeTemporarySettings(settingsFile));
        QSignalSpy spy(&settings,
                       &EditorAppearanceSettings::settingsChanged);
        settings.setOptions(options);
        expectBool("appearance settings emits change",
                   spy.count() == 1,
                   true);
    }

    EditorAppearanceSettings reloaded(
        makeTemporarySettings(settingsFile));
    const EditorAppearanceOptions persisted = reloaded.options();
    expectBool("appearance settings persists font",
               persisted.fontFamily == options.fontFamily,
               true);
    expectBool("appearance settings persists size",
               persisted.fontSizePt == options.fontSizePt,
               true);
    expectBool("appearance settings persists line height",
               qAbs(persisted.lineHeight - options.lineHeight) < 0.001,
               true);
    expectBool("appearance settings persists ligatures",
               persisted.ligaturesEnabled == options.ligaturesEnabled,
               true);

    MyCodeEditor editor;
    editor.setPlainText(QStringLiteral("module appearance_probe;\nendmodule\n"));
    editor.applyAppearanceSettings(options);
    expectBool("editor applies appearance font size",
               editor.font().pointSize() == options.fontSizePt,
               true);
    expectBool("editor applies appearance family",
               editor.font().family() == options.fontFamily,
               true);
    expectBool("editor applies appearance line height",
               editor.document()->firstBlock().blockFormat().lineHeight()
                   == qRound(options.lineHeight * 100.0),
               true);
    expectBool("editor applies appearance ligatures",
               editor.font().featureValue(QFont::Tag("liga")) == 1U,
               true);

    expectBool("appearance filters cjk latin alias",
               EditorAppearance::isCjkFontFamily(QStringLiteral("Microsoft YaHei")),
               true);
    expectBool("appearance filters cjk family name",
               EditorAppearance::isCjkFontFamily(QStringLiteral("微软雅黑")),
               true);
    expectBool("appearance keeps latin monospace family",
               !EditorAppearance::isCjkFontFamily(QStringLiteral("Consolas")),
               true);

    const QStringList recommended =
        EditorAppearance::recommendedFontFamilies();
    const QStringList expectedRecommended{
        QStringLiteral("Cascadia Code"),
        QStringLiteral("Maple Mono"),
        QStringLiteral("Iosevka"),
        QStringLiteral("Monaspace Neon"),
        QStringLiteral("Intel One Mono"),
        QStringLiteral("Geist Mono"),
        QStringLiteral("0xProto"),
    };
    expectBool("appearance keeps selected editor fonts",
               recommended == expectedRecommended,
               true);
    expectBool("appearance loads bundled editor fonts",
               EditorAppearance::ensureApplicationFontsLoaded(),
               true);

    const QString fallback = EditorAppearance::fallbackFontFamily();
    expectBool("appearance fallback is Maple Mono",
               fallback == QStringLiteral("Maple Mono"),
               true);
    expectBool("appearance default font size is 15pt",
               EditorAppearance::defaultOptions().fontSizePt == 15,
               true);
    expectBool("appearance accepts Maple Mono",
               EditorAppearance::resolveFontFamily(QStringLiteral("Maple Mono"))
                   == QStringLiteral("Maple Mono"),
               true);
    expectBool("appearance accepts Iosevka",
               EditorAppearance::resolveFontFamily(QStringLiteral("Iosevka"))
                   == QStringLiteral("Iosevka"),
               true);
    expectBool("appearance accepts Monaspace Neon",
               EditorAppearance::resolveFontFamily(QStringLiteral("Monaspace Neon"))
                   == QStringLiteral("Monaspace Neon"),
               true);
    expectBool("appearance accepts Intel One Mono",
               EditorAppearance::resolveFontFamily(QStringLiteral("Intel One Mono"))
                   == QStringLiteral("Intel One Mono"),
               true);
    expectBool("appearance accepts Geist Mono",
               EditorAppearance::resolveFontFamily(QStringLiteral("Geist Mono"))
                   == QStringLiteral("Geist Mono"),
               true);
    expectBool("appearance accepts 0xProto",
               EditorAppearance::resolveFontFamily(QStringLiteral("0xProto"))
                   == QStringLiteral("0xProto"),
               true);
    expectBool("appearance rejects non selected saved font",
               EditorAppearance::resolveFontFamily(QStringLiteral("Consolas"))
                   == QStringLiteral("Maple Mono"),
               true);
    expectBool("appearance rejects commercial saved font",
               EditorAppearance::resolveFontFamily(QStringLiteral("Berkeley Mono"))
                   == QStringLiteral("Maple Mono"),
               true);

    {
        const QString cjkSettingsFile =
            settingsDir.filePath(QStringLiteral("appearance_cjk.ini"));
        {
            QSettings writer(cjkSettingsFile, QSettings::IniFormat);
            writer.setValue(QStringLiteral("editorAppearance/fontFamily"),
                            QStringLiteral("Microsoft YaHei"));
            writer.setValue(QStringLiteral("editorAppearance/fontSizePt"), 14);
            writer.sync();
        }
        EditorAppearanceSettings cjkReloaded(
            makeTemporarySettings(cjkSettingsFile));
        expectBool("appearance cjk saved font falls back",
                   cjkReloaded.options().fontFamily
                       == EditorAppearance::fallbackFontFamily(),
                   true);
    }

}

static void runEditorAppearanceCoordinatorRegression()
{
    QTemporaryDir settingsDir;
    expectBool("appearance coordinator temp dir valid",
               settingsDir.isValid(),
               true);
    if (!settingsDir.isValid())
        return;

    QTabWidget tabsWidget;
    TabManager tabs(&tabsWidget);
    EditorCoordinator coordinator(&tabs);
    EditorAppearanceSettings settings(
        makeTemporarySettings(
            settingsDir.filePath(QStringLiteral("appearance.ini"))));

    coordinator.setAppearanceSettings(&settings);
    coordinator.connectSignals();

    tabs.createNewTab();
    tabs.createNewTab();
    expectBool("appearance coordinator has editors",
               tabs.editorCount() == 2,
               true);

    EditorAppearanceOptions options = EditorAppearance::defaultOptions();
    options.fontFamily = EditorAppearance::fallbackFontFamily();
    options.fontSizePt = 15;
    options.lineHeight = 1.35;
    options.ligaturesEnabled = false;
    settings.setOptions(options);

    bool allOpenEditorsUpdated = true;
    for (int i = 0; i < tabs.editorCount(); ++i) {
        MyCodeEditor* editor = tabs.getEditorAt(i);
        allOpenEditorsUpdated = allOpenEditorsUpdated
            && editor
            && editor->font().pointSize() == options.fontSizePt
            && editor->font().featureValue(QFont::Tag("liga")) == 0U;
    }
    expectBool("appearance coordinator updates open editors",
               allOpenEditorsUpdated,
               true);

    tabs.createNewTab();
    MyCodeEditor* newEditor = tabs.getCurrentEditor();
    expectBool("appearance coordinator applies new editor",
               newEditor && newEditor->font().pointSize() == options.fontSizePt,
               true);
    if (newEditor) {
        QStringList scrollLines;
        for (int i = 0; i < 200; ++i)
            scrollLines.append(QStringLiteral("line %1").arg(i));
        newEditor->setPlainText(scrollLines.join(QLatin1Char('\n')));
        newEditor->resize(420, 180);
        newEditor->show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        expectBool("appearance wheel zoom event accepted",
                   sendEditorWheel(newEditor,
                                   Qt::ControlModifier | Qt::ShiftModifier,
                                   120),
                   true);
        expectBool("appearance wheel zoom increases font size",
                   settings.options().fontSizePt == options.fontSizePt + 1
                       && newEditor->font().pointSize()
                              == options.fontSizePt + 1,
                   true);

        const int fontSizeAfterZoom = settings.options().fontSizePt;
        const int scrollBefore = newEditor->verticalScrollBar()->value();
        expectBool("appearance ctrl wheel fast scroll accepted",
                   sendEditorWheel(newEditor, Qt::ControlModifier, -120),
                   true);
        expectBool("appearance ctrl wheel keeps font size",
                   settings.options().fontSizePt == fontSizeAfterZoom
                       && newEditor->font().pointSize() == fontSizeAfterZoom,
                   true);
        expectBool("appearance ctrl wheel scrolls editor",
                   newEditor->verticalScrollBar()->value() >= scrollBefore,
                   true);
    }
}

static void runTabOpenDedupRegression()
{
    QTemporaryDir dir;
    expectBool("tab open dedup temp dir valid", dir.isValid(), true);
    if (!dir.isValid())
        return;

    const QString filePath = dir.filePath(QStringLiteral("defs.svh"));
    QFile file(filePath);
    expectBool("tab open dedup fixture writable",
               file.open(QIODevice::WriteOnly | QIODevice::Text),
               true);
    if (!file.isOpen())
        return;
    file.write("`define WIDTH 8\n");
    file.close();

    QTabWidget tabsWidget;
    TabManager tabs(&tabsWidget);

    expectBool("tab open first file succeeds",
               tabs.openFileInTab(filePath),
               true);
    MyCodeEditor* firstEditor = tabs.getCurrentEditor();
    expectBool("tab open first file creates one tab",
               firstEditor && tabs.editorCount() == 1,
               true);

    const QString nativePath = QDir::toNativeSeparators(filePath);
    expectBool("tab open duplicate file succeeds",
               tabs.openFileInTab(nativePath),
               true);
    expectBool("tab open duplicate activates existing tab",
               tabs.editorCount() == 1
                   && tabs.getCurrentEditor() == firstEditor,
               true);

    tabs.createNewTab();
    expectBool("tab open dedup has scratch tab",
               tabs.editorCount() == 2
                   && tabs.getCurrentEditor() != firstEditor,
               true);
    expectBool("tab open existing from scratch succeeds",
               tabs.openFileInTab(filePath),
               true);
    expectBool("tab open existing from scratch reuses tab",
               tabs.editorCount() == 2
                   && tabs.getCurrentEditor() == firstEditor,
               true);

    tabs.setWorkspaceScope({dir.path()}, dir.path());
    HierarchyInstanceContext boundTabContext;
    boundTabContext.workspacePath = dir.path();
    boundTabContext.activeTopModule = QStringLiteral("top");
    boundTabContext.instancePath = QStringLiteral("top.u0");
    firstEditor->setHierarchyInstanceContext(boundTabContext);
    const int firstEditorIndex = tabsWidget.indexOf(firstEditor);
    const int otherEditorIndex = firstEditorIndex == 0 ? 1 : 0;
    tabsWidget.setCurrentIndex(otherEditorIndex);
    tabs.activateOpenFile(filePath);
    expectBool("tab switch preserves hierarchy instance context",
               firstEditor->hierarchyInstanceContext() == boundTabContext,
               true);
    tabs.openFileInTab(filePath);
    expectBool("Files-style direct open clears instance binding",
               !firstEditor->hierarchyInstanceContext().isBound()
                   && !firstEditor->hierarchyInstanceContext()
                           .workspacePath.isEmpty(),
               true);
    firstEditor->setHierarchyInstanceContext(boundTabContext);
    HierarchyInstanceContext secondInstanceContext = boundTabContext;
    secondInstanceContext.instancePath = QStringLiteral("top.u1");
    NavigationCommandCoordinator contextHistory(&tabs, nullptr);
    contextHistory.navigateToFileAndLineWithContext(
        filePath, 1, 1, secondInstanceContext);
    expectBool("source navigation applies requested instance context",
               firstEditor->hierarchyInstanceContext()
                   == secondInstanceContext,
               true);
    contextHistory.navigateBack();
    expectBool("navigation history restores instance context",
               firstEditor->hierarchyInstanceContext()
                   == boundTabContext,
               true);

    QTemporaryDir workspaceA;
    QTemporaryDir workspaceB;
    QTemporaryDir externalDir;
    expectBool("tab workspace scope dirs valid",
               workspaceA.isValid()
                   && workspaceB.isValid()
                   && externalDir.isValid(),
               true);
    if (!workspaceA.isValid()
        || !workspaceB.isValid()
        || !externalDir.isValid()) {
        return;
    }

    const QString fileA =
        QDir(workspaceA.path()).absoluteFilePath(QStringLiteral("a.sv"));
    const QString fileB =
        QDir(workspaceB.path()).absoluteFilePath(QStringLiteral("b.sv"));
    const QString externalFile =
        QDir(externalDir.path()).absoluteFilePath(QStringLiteral("external.sv"));
    auto writeProbeFile = [](const QString& path, const QByteArray& text) {
        QFile probe(path);
        if (!probe.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        probe.write(text);
        return true;
    };
    expectBool("tab workspace scope files writable",
               writeProbeFile(fileA, "module a; endmodule\n")
                   && writeProbeFile(fileB, "module b; endmodule\n")
                   && writeProbeFile(externalFile, "module ext; endmodule\n"),
               true);

    QTabWidget scopedWidget;
    TabManager scopedTabs(&scopedWidget);
    expectBool("tab workspace scope open A",
               scopedTabs.openFileInTab(fileA),
               true);
    MyCodeEditor* editorA = scopedTabs.getCurrentEditor();
    expectBool("tab workspace scope open B",
               scopedTabs.openFileInTab(fileB),
               true);
    MyCodeEditor* editorB = scopedTabs.getCurrentEditor();
    expectBool("tab workspace scope open external",
               scopedTabs.openFileInTab(externalFile),
               true);
    MyCodeEditor* externalEditor = scopedTabs.getCurrentEditor();
    scopedTabs.createNewTab();
    MyCodeEditor* scratchEditor = scopedTabs.getCurrentEditor();

    auto tabVisible = [&scopedWidget](MyCodeEditor* editor) {
        const int index = scopedWidget.indexOf(editor);
        return index >= 0 && scopedWidget.tabBar()->isTabVisible(index);
    };

    scopedTabs.setWorkspaceScope(
        {workspaceA.path(), workspaceB.path()},
        workspaceA.path());
    expectBool("workspace A scope shows A scratch external",
               tabVisible(editorA)
                   && !tabVisible(editorB)
                   && tabVisible(externalEditor)
                   && tabVisible(scratchEditor),
               true);

    HierarchyInstanceContext workspaceAContext;
    workspaceAContext.workspacePath = workspaceA.path();
    workspaceAContext.activeTopModule = QStringLiteral("a");
    workspaceAContext.instancePath = QStringLiteral("a.u_a");
    editorA->setHierarchyInstanceContext(workspaceAContext);
    HierarchyInstanceContext workspaceBContext;
    workspaceBContext.workspacePath = workspaceB.path();
    workspaceBContext.activeTopModule = QStringLiteral("b");
    workspaceBContext.instancePath = QStringLiteral("b.u_b");
    editorB->setHierarchyInstanceContext(workspaceBContext);

    scopedTabs.setWorkspaceScope(
        {workspaceA.path(), workspaceB.path()},
        workspaceB.path());
    expectBool("workspace B scope shows B scratch external",
               !tabVisible(editorA)
                   && tabVisible(editorB)
                   && tabVisible(externalEditor)
                   && tabVisible(scratchEditor),
               true);
    expectBool("workspace switch preserves per-tab instance contexts",
               editorA->hierarchyInstanceContext() == workspaceAContext
                   && editorB->hierarchyInstanceContext()
                          == workspaceBContext,
               true);

    scopedTabs.setWorkspaceScope({}, {});
    expectBool("workspace scope cleared shows all tabs",
               tabVisible(editorA)
                   && tabVisible(editorB)
                   && tabVisible(externalEditor)
                   && tabVisible(scratchEditor),
               true);
    expectBool("closed workspaces clear stale instance bindings",
               !editorA->hierarchyInstanceContext().isBound()
                   && !editorB->hierarchyInstanceContext().isBound(),
               true);

    QSignalSpy scopedCloseSpy(&scopedTabs, &TabManager::tabClosed);
    expectBool("close workspace tabs succeeds",
               scopedTabs.closeTabsInWorkspace(workspaceB.path()),
               true);
    expectBool("close workspace tabs removes only workspace file",
               scopedWidget.indexOf(editorA) >= 0
                   && scopedWidget.indexOf(editorB) < 0
                   && scopedWidget.indexOf(externalEditor) >= 0
                   && scopedWidget.indexOf(scratchEditor) >= 0
                   && scopedTabs.editorCount() == 3
                   && scopedCloseSpy.size() == 1,
               true);

    QTemporaryDir sessionTabsWorkspace;
    expectBool("tab session temp dir valid",
               sessionTabsWorkspace.isValid(),
               true);
    if (!sessionTabsWorkspace.isValid())
        return;

    const QString sessionTopFile =
        QDir(sessionTabsWorkspace.path()).absoluteFilePath(
            QStringLiteral("session_top.sv"));
    const QString sessionHelperFile =
        QDir(sessionTabsWorkspace.path()).absoluteFilePath(
            QStringLiteral("session_helper.svh"));
    const QString sessionMissingFile =
        QDir(sessionTabsWorkspace.path()).absoluteFilePath(
            QStringLiteral("missing.sv"));
    QString longHelperText = QStringLiteral("`define SESSION_HELPER\n");
    for (int i = 0; i < 80; ++i)
        longHelperText += QStringLiteral("// helper line %1\n").arg(i);
    expectBool("tab session files writable",
               writeTextFile(sessionTopFile,
                             QStringLiteral("module session_top;\n"
                                            "  logic a;\n"
                                            "endmodule\n"))
                   && writeTextFile(sessionHelperFile, longHelperText),
               true);

    QTabWidget captureWidget;
    captureWidget.resize(480, 160);
    captureWidget.show();
    TabManager captureTabs(&captureWidget);
    expectBool("tab session opens top",
               captureTabs.openFileInTab(sessionTopFile),
               true);
    MyCodeEditor* sessionTopEditor = captureTabs.getCurrentEditor();
    if (sessionTopEditor) {
        QTextBlock topBlock =
            sessionTopEditor->document()->findBlockByNumber(1);
        QTextCursor topCursor(topBlock);
        topCursor.setPosition(topBlock.position() + 4);
        sessionTopEditor->setTextCursor(topCursor);
    }
    expectBool("tab session opens helper",
               captureTabs.openFileInTab(sessionHelperFile),
               true);
    MyCodeEditor* sessionHelperEditor = captureTabs.getCurrentEditor();
    int capturedHelperScroll = 0;
    if (sessionHelperEditor) {
        sessionHelperEditor->resize(480, 120);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTextBlock helperBlock =
            sessionHelperEditor->document()->findBlockByNumber(20);
        QTextCursor helperCursor(helperBlock);
        helperCursor.setPosition(helperBlock.position() + 3);
        sessionHelperEditor->setTextCursor(helperCursor);
        if (QScrollBar* bar = sessionHelperEditor->verticalScrollBar()) {
            bar->setValue(qMin(7, bar->maximum()));
            capturedHelperScroll = bar->value();
        }
    }
    const QList<WorkspaceSessionTabState> capturedTabs =
        captureTabs.workspaceSessionTabs(sessionTabsWorkspace.path());
    bool capturedTop = false;
    bool capturedActiveHelper = false;
    for (const WorkspaceSessionTabState& state : capturedTabs) {
        capturedTop =
            capturedTop
            || (QFileInfo(state.filePath).fileName()
                    == QStringLiteral("session_top.sv")
                && state.cursorLine == 2
                && state.cursorColumn == 5
                && !state.active);
        capturedActiveHelper =
            capturedActiveHelper
            || (QFileInfo(state.filePath).fileName()
                    == QStringLiteral("session_helper.svh")
                && state.cursorLine == 21
                && state.cursorColumn == 4
                && state.verticalScrollValue == capturedHelperScroll
                && state.active);
    }
    expectBool("tab session captures tabs cursor scroll active",
               capturedTabs.size() == 2
                   && capturedTop
                   && capturedActiveHelper,
               true);

    QList<WorkspaceSessionTabState> restoreStates = capturedTabs;
    WorkspaceSessionTabState missingState;
    missingState.filePath = sessionMissingFile;
    restoreStates.append(missingState);
    QTabWidget restoreWidget;
    restoreWidget.resize(480, 160);
    restoreWidget.show();
    TabManager restoreTabs(&restoreWidget);
    QStringList skippedSessionTabs;
    const QStringList restoredSessionTabs =
        restoreTabs.restoreWorkspaceSessionTabs(sessionTabsWorkspace.path(),
                                                restoreStates,
                                                &skippedSessionTabs);
    MyCodeEditor* restoredHelper = restoreTabs.getCurrentEditor();
    const DocumentSnapshot restoredDocument =
        restoreTabs.getCurrentDocument();
    const QTextCursor restoredCursor =
        restoredHelper ? restoredHelper->textCursor() : QTextCursor();
    expectBool("tab session restores existing tabs skips missing",
               restoredSessionTabs.size() == 2
                   && skippedSessionTabs.size() == 1
                   && QFileInfo(restoredDocument.fileName).fileName()
                          == QStringLiteral("session_helper.svh"),
               true);
    expectBool("tab session restores active cursor scroll",
               restoredHelper
                   && restoredCursor.blockNumber() == 20
                   && restoredCursor.positionInBlock() == 3
                   && (!restoredHelper->verticalScrollBar()
                       || restoredHelper->verticalScrollBar()->value()
                              == capturedHelperScroll),
               true);
}

static void runWorkspaceCloseRegression()
{
    QTemporaryDir workspaceA;
    QTemporaryDir workspaceB;
    QTemporaryDir workspaceC;
    expectBool("workspace close temp dirs valid",
               workspaceA.isValid()
                   && workspaceB.isValid()
                   && workspaceC.isValid(),
               true);
    if (!workspaceA.isValid()
        || !workspaceB.isValid()
        || !workspaceC.isValid()) {
        return;
    }

    WorkspaceManager workspace;
    QSignalSpy closedSpy(&workspace, &WorkspaceManager::workspaceClosed);
    expectBool("workspace close open A",
               workspace.openWorkspace(workspaceA.path()),
               true);
    expectBool("workspace close open B",
               workspace.openWorkspace(workspaceB.path()),
               true);
    expectBool("workspace close open C",
               workspace.openWorkspace(workspaceC.path()),
               true);
    expectBool("workspace close switch to B",
               workspace.switchWorkspace(1),
               true);

    expectBool("workspace close removes inactive before active",
               workspace.closeWorkspace(0),
               true);
    expectBool("workspace close keeps active path after inactive close",
               workspace.workspaceEntries().size() == 2
                   && workspace.activeWorkspaceIndex() == 0
                   && workspace.getWorkspacePath()
                       == QDir::cleanPath(QDir::fromNativeSeparators(
                              QFileInfo(workspaceB.path()).absoluteFilePath()))
                   && closedSpy.size() == 0,
               true);

    expectBool("workspace close active switches next workspace",
               workspace.closeWorkspace(0),
               true);
    expectBool("workspace close active selects remaining workspace",
               workspace.workspaceEntries().size() == 1
                   && workspace.activeWorkspaceIndex() == 0
                   && workspace.getWorkspacePath()
                       == QDir::cleanPath(QDir::fromNativeSeparators(
                              QFileInfo(workspaceC.path()).absoluteFilePath()))
                   && closedSpy.size() == 1,
               true);

    expectBool("workspace close final workspace succeeds",
               workspace.closeWorkspace(0),
               true);
    expectBool("workspace close final clears active workspace",
               workspace.workspaceEntries().isEmpty()
                   && workspace.activeWorkspaceIndex() == -1
                   && !workspace.isWorkspaceOpen()
                   && closedSpy.size() == 2,
               true);
}

static bool editorGhostCacheContains(
    const MyCodeEditor* editor,
    GhostAnnotationKind kind,
    const QString& text = QString())
{
    if (!editor || !editor->state)
        return false;
    for (const GhostAnnotation& annotation : editor->state->ghostAnnotations) {
        if (annotation.kind == kind
            && (text.isEmpty() || annotation.text == text)) {
            return true;
        }
    }
    return false;
}

static void runTabOpenGhostLifecycleRegression()
{
    QTemporaryDir dir;
    expectBool("tab Ghost lifecycle temp dir valid", dir.isValid(), true);
    if (!dir.isValid())
        return;

    const QString filePath =
        dir.filePath(QStringLiteral("ghost_lifecycle.sv"));
    const QString source = QStringLiteral(
        "module leaf #(parameter int P = 1) (\n"
        "  input logic [P-1:0] data_i\n"
        ");\n"
        "endmodule\n"
        "module parent #(parameter int BASE = 4);\n"
        "  parameter logic [7:0] HEX = 8'h2a;\n"
        "  localparam int EXPANDED = BASE + 2;\n"
        "  logic [BASE:0] bus;\n"
        "  leaf #(.P(BASE + 1)) u_leaf (\n"
        "    .data_i                                             (bus)\n"
        "  );\n"
        "endmodule\n"
        "module text_leaf #(parameter string S = \"default\");\n"
        "endmodule\n"
        "module top;\n"
        "  parent #(.BASE(8)) p0();\n"
        "  parent #(.BASE(11)) p1();\n"
        "  text_leaf #(.S(\"bound\")) text0();\n"
        "endmodule\n");
    expectBool("tab Ghost lifecycle fixture writable",
               writeTextFile(filePath, source),
               true);

    SlangManager slang;
    QList<EffectiveValueFact> initialFacts;
    QList<SemanticSymbolRecord> initialRecords =
        slang.extractSymbolRecords(filePath, source, {}, {}, &initialFacts);
    expectBool("tab Ghost lifecycle Slang records available",
               !initialRecords.isEmpty(),
               true);
    expectBool("tab Ghost lifecycle Slang facts available",
               !initialFacts.isEmpty(),
               true);
    if (initialRecords.isEmpty())
        return;

    auto appendDuplicateFormalPort = [](QList<SemanticSymbolRecord>* records) {
        if (!records)
            return;
        SemanticSymbolRecord duplicate;
        for (const SemanticSymbolRecord& record :
             std::as_const(*records)) {
            if (record.name == QStringLiteral("data_i")
                && record.collectorKind
                       == SymbolTaxonomy::CollectorKind::PortInput) {
                duplicate = record;
                break;
            }
        }
        if (duplicate.isValid())
            records->append(std::move(duplicate));
    };
    appendDuplicateFormalPort(&initialRecords);

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    EffectiveValueService* values = EffectiveValueService::getInstance();
    const auto previousSnapshot = semanticIndex->snapshot();
    values->clearPublishedFacts();
    const std::uint64_t initialComputation =
        values->beginComputation({filePath});
    for (SemanticSymbolRecord& record : initialRecords) {
        record.presentation.computationRevision = initialComputation;
        record.presentation.documentRevision = 0;
    }
    semanticIndex->setSnapshot(snapshotFromRecords(
        initialRecords, {}, {}, {{filePath, source}}));
    values->publishDocumentFacts(filePath,
                                 source,
                                 initialFacts,
                                 initialComputation,
                                 0);

    GhostAnnotationQuery initialGhostQuery;
    initialGhostQuery.fileName = filePath;
    initialGhostQuery.documentText = source;
    const GhostAnnotationReport initialGhostReport =
        GhostAnnotationService::getInstance()->annotationsForDocument(
            initialGhostQuery);
    bool formalPortUsesVisiblePlacement = false;
    bool formalPortKeepsCompleteDeclaration = false;
    int formalPortLine = 0;
    GhostAnnotation formalPortAnnotation;
    bool sawDirectLiteralParameterValue = false;
    bool sawDerivedParameterValue = false;
    bool sawDirectLiteralOverride = false;
    for (const GhostAnnotation& annotation :
         initialGhostReport.annotations) {
        if (annotation.kind == GhostAnnotationKind::FormalPort) {
            formalPortUsesVisiblePlacement =
                formalPortUsesVisiblePlacement
                || annotation.placement
                       == GhostAnnotationPlacement::RightOfLine;
            formalPortKeepsCompleteDeclaration =
                formalPortKeepsCompleteDeclaration
                || (annotation.text.contains(QStringLiteral("input"))
                    && annotation.text.contains(
                        QStringLiteral("logic"))
                    && annotation.text.contains(
                        QStringLiteral("data_i")));
            if (annotation.text.contains(QStringLiteral("data_i")))
                formalPortLine = annotation.line;
            if (annotation.text.contains(QStringLiteral("data_i")))
                formalPortAnnotation = annotation;
        }
        if (annotation.kind == GhostAnnotationKind::ParameterOverride) {
            sawDirectLiteralOverride =
                sawDirectLiteralOverride
                || annotation.line == 16
                || annotation.line == 17
                || annotation.line == 18;
        }
        if (annotation.kind != GhostAnnotationKind::ParameterValue) {
            continue;
        }
        sawDirectLiteralParameterValue =
            sawDirectLiteralParameterValue
            || annotation.line == 1 || annotation.line == 5
            || annotation.line == 6 || annotation.line == 13;
        sawDerivedParameterValue =
            sawDerivedParameterValue || annotation.line == 7;
    }
    expectBool("FormalPort Ghost uses visible line-tail placement",
               formalPortUsesVisiblePlacement,
               true);
    expectBool("FormalPort Ghost keeps complete declaration in service",
               formalPortKeepsCompleteDeclaration,
               true);
    expectBool("direct literal parameter omits redundant Ghost",
               sawDirectLiteralParameterValue,
               false);
    expectBool("derived parameter keeps computed Ghost",
               sawDerivedParameterValue,
               true);
    expectBool("direct literal parameter override omits redundant Ghost",
               sawDirectLiteralOverride,
               false);

    GhostAnnotationQuery boundOverrideQuery = initialGhostQuery;
    boundOverrideQuery.instanceContext.workspacePath = dir.path();
    boundOverrideQuery.instanceContext.activeTopModule =
        QStringLiteral("top");
    boundOverrideQuery.instanceContext.instancePath =
        QStringLiteral("top.p0");
    const GhostAnnotationReport boundOverrideReport =
        GhostAnnotationService::getInstance()->annotationsForDocument(
            boundOverrideQuery);
    const bool sawDerivedOverride = std::any_of(
        boundOverrideReport.annotations.cbegin(),
        boundOverrideReport.annotations.cend(),
        [](const GhostAnnotation& annotation) {
            return annotation.kind
                       == GhostAnnotationKind::ParameterOverride
                && annotation.line == 9
                && annotation.text == QStringLiteral("= 9");
        });
    expectBool("expression parameter override keeps computed Ghost",
               sawDerivedOverride,
               true);

    QTabWidget tabWidget;
    TabManager tabs(&tabWidget);
    AnalysisScheduler scheduler;
    AnalysisCoordinator coordinator(&scheduler,
                                    nullptr,
                                    nullptr,
                                    &tabs,
                                    nullptr,
                                    nullptr);
    coordinator.connectSignals();
    tabs.setWorkspaceScope({dir.path()}, dir.path());
    expectBool("real tab open succeeds for Ghost lifecycle",
               tabs.openFileInTab(filePath),
               true);
    MyCodeEditor* editor = tabs.getCurrentEditor();
    expectBool("real tab open assigns file identity",
               editor && editor->documentFileName() == filePath,
               true);
    expectBool("file identity assignment refreshes FormalPort Ghost",
               waitUntil([&]() {
                   return editorGhostCacheContains(
                       editor,
                       GhostAnnotationKind::FormalPort);
               }, 5000),
               true);
    if (!editor) {
        values->clearPublishedFacts();
        semanticIndex->setSnapshot(previousSnapshot);
        return;
    }

    tabWidget.resize(360, 220);
    tabWidget.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTextBlock formalPortBlock = editor->document()->findBlockByNumber(
        qMax(0, formalPortLine - 1));
    QTextCursor formalPortCursor(formalPortBlock);
    editor->setTextCursor(formalPortCursor);
    editor->centerCursor();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("FormalPort render fixture scrolls anchor into viewport",
               formalPortLine > 0
                   && formalPortBlock.isValid()
                   && editor->viewport()->rect().intersects(
                       editor->cursorRect(formalPortCursor)),
               true);
    expectBool("FormalPort fixed annotation lane is removed",
               editor->findChild<QWidget*>(
                   QStringLiteral("FormalPortGhostLane")) == nullptr,
               true);

    MyCodeEditor geometryEditor;
    geometryEditor.setLineWrapMode(QPlainTextEdit::NoWrap);
    geometryEditor.setTabStopDistance(53.5);
    geometryEditor.resize(1100, 180);
    geometryEditor.setPlainText(QStringLiteral(
        "\tleaf u_geometry (\t.data_i\t\t\t\t(bus));   \t  \n"));
    geometryEditor.setReadOnly(true);
    geometryEditor.show();
    geometryEditor.clearFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    GhostAnnotation geometryAnnotation = formalPortAnnotation;
    geometryAnnotation.kind = GhostAnnotationKind::FormalPort;
    geometryAnnotation.placement = GhostAnnotationPlacement::RightOfLine;
    geometryAnnotation.line = 1;
    geometryAnnotation.anchorPosition = geometryEditor.toPlainText().indexOf(
        QStringLiteral("data_i"));
    geometryAnnotation.anchorLength = QStringLiteral("data_i").size();
    geometryAnnotation.text = QStringLiteral(
        "input var logic signed [P_1 * 2 - 1:0] data_i "
        "[0:P_1 - 1] /* deliberately long declaration */");

    EditorDocumentGeometry documentGeometry;
    const QTextBlock visualGeometryBlock =
        geometryEditor.document()->firstBlock();
    bool visualGeometryMatchesEditBoundaries =
        visualGeometryBlock.isValid();
    for (int offset = 0;
         visualGeometryMatchesEditBoundaries
         && offset <= visualGeometryBlock.text().size();
         ++offset) {
        const int visualColumn =
            EditorVisualColumnGeometry::visualColumnForOffset(
                &geometryEditor, visualGeometryBlock, offset);
        const int resolvedOffset =
            EditorVisualColumnGeometry::offsetForVisualColumn(
                &geometryEditor,
                visualGeometryBlock,
                visualColumn,
                EditorVisualBoundary::Start);
        QTextCursor resolvedCursor(visualGeometryBlock);
        resolvedCursor.setPosition(
            visualGeometryBlock.position() + resolvedOffset);
        visualGeometryMatchesEditBoundaries =
            EditorVisualColumnGeometry::viewportXForVisualColumn(
                &geometryEditor,
                visualGeometryBlock,
                visualColumn,
                EditorVisualBoundary::Start)
            == geometryEditor.cursorRect(resolvedCursor).left();
    }
    expectBool("column caret geometry matches edit boundaries",
               visualGeometryMatchesEditBoundaries,
               true);
    auto renderFormalPortDifference = [&]() {
        geometryEditor.setGhostAnnotations({});
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        const QImage withoutGhost = renderWidgetImage(
            geometryEditor.viewport());
        geometryEditor.setGhostAnnotations({geometryAnnotation});
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        const QImage withGhost = renderWidgetImage(
            geometryEditor.viewport());
        return differentPixelBounds(withoutGhost, withGhost);
    };
    auto expectRenderedAfterTail = [&](const char* label) {
        const EditorCodeLineTailGeometry tail =
            documentGeometry.codeLineTailGeometry(&geometryEditor, 0);
        const QRect difference = renderFormalPortDifference();
        const bool followsTail = tail.valid && !difference.isNull()
            && difference.left() >= qFloor(tail.textRight + 4.0)
            && difference.left() <= qCeil(tail.textRight + 20.0)
            && difference.right() > difference.left()
            && difference.bottom() >= qFloor(tail.top)
            && difference.top() <= qCeil(tail.top + tail.height);
        expectBool(label, followsTail, true);
        return qMakePair(tail, difference);
    };

    const int viewportWidthWithoutGhost = geometryEditor.viewport()->width();
    geometryEditor.setGhostAnnotations({geometryAnnotation});
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    expectBool("FormalPort does not reserve a right viewport margin",
               geometryEditor.viewport()->width()
                   == viewportWidthWithoutGhost,
               true);

    QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    fixedFont.setPointSize(11);
    geometryEditor.setFont(fixedFont);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const auto fixedGeometry = expectRenderedAfterTail(
        "FormalPort pixels follow tab-expanded monospace line tail");

    QTextBlock geometryBlock = geometryEditor.document()->firstBlock();
    QString geometryText = geometryBlock.text();
    int visibleEnd = geometryText.size();
    while (visibleEnd > 0
           && (geometryText.at(visibleEnd - 1) == QLatin1Char(' ')
               || geometryText.at(visibleEnd - 1) == QLatin1Char('\t'))) {
        --visibleEnd;
    }
    QTextCursor visibleEndCursor(geometryEditor.document());
    visibleEndCursor.setPosition(geometryBlock.position() + visibleEnd);
    expectBool("FormalPort tail excludes trailing invisible whitespace",
               fixedGeometry.first.valid
                   && qAbs(fixedGeometry.first.textRight
                           - geometryEditor.cursorRect(visibleEndCursor).left())
                          <= 2.0,
               true);

    QFont proportionalFont = QFontDatabase::systemFont(
        QFontDatabase::GeneralFont);
    proportionalFont.setPointSize(13);
    proportionalFont.setFixedPitch(false);
    geometryEditor.setFont(proportionalFont);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const auto proportionalGeometry = expectRenderedAfterTail(
        "FormalPort pixels follow proportional-font line tail");
    expectBool("FormalPort geometry reacts to real font layout",
               proportionalGeometry.first.valid
                   && fixedGeometry.first.valid
                   && qAbs(proportionalGeometry.first.textRight
                           - fixedGeometry.first.textRight) > 2.0,
               true);

    const qreal tailBeforeEdit = proportionalGeometry.first.textRight;
    QTextCursor lineEndCursor(geometryEditor.document());
    lineEndCursor.setPosition(geometryBlock.position() + visibleEnd);
    lineEndCursor.insertText(QStringLiteral(" visible_tail"));
    geometryEditor.setGhostAnnotations({geometryAnnotation});
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const auto editedGeometry = expectRenderedAfterTail(
        "FormalPort moves immediately after a line-tail edit");
    expectBool("FormalPort edited tail moves right",
               editedGeometry.first.valid
                   && editedGeometry.first.textRight > tailBeforeEdit,
               true);

    geometryEditor.setLineWrapMode(QPlainTextEdit::NoWrap);
    geometryEditor.resize(360, 180);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    geometryEditor.horizontalScrollBar()->setValue(0);
    const EditorCodeLineTailGeometry unscrolledTail =
        documentGeometry.codeLineTailGeometry(&geometryEditor, 0);
    const int horizontalMaximum =
        geometryEditor.horizontalScrollBar()->maximum();
    const int scrollValue = qMin(horizontalMaximum,
                                 qMax(1, horizontalMaximum / 2));
    geometryEditor.horizontalScrollBar()->setValue(scrollValue);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const EditorCodeLineTailGeometry scrolledTail =
        documentGeometry.codeLineTailGeometry(&geometryEditor, 0);
    expectBool("FormalPort line tail follows normal horizontal scrolling",
               horizontalMaximum > 0 && unscrolledTail.valid
                   && scrolledTail.valid
                   && qAbs((unscrolledTail.textRight
                            - scrolledTail.textRight)
                           - scrollValue) <= 2.0,
               true);

    geometryEditor.horizontalScrollBar()->setValue(0);
    geometryEditor.setLineWrapMode(QPlainTextEdit::WidgetWidth);
    geometryEditor.resize(280, 240);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const EditorCodeLineTailGeometry wrappedTail =
        documentGeometry.codeLineTailGeometry(&geometryEditor, 0);
    const EditorBlockGeometry wrappedBlock = geometryEditor.blockGeometry(0);
    const QRect wrappedDifference = renderFormalPortDifference();
    expectBool("FormalPort follows the final QTextLine when wrapping",
               wrappedTail.valid
                   && wrappedTail.top > wrappedBlock.top + 1.0
                   && !wrappedDifference.isNull()
                   && wrappedDifference.left()
                          >= qFloor(wrappedTail.textRight + 4.0)
                   && wrappedDifference.top()
                          <= qCeil(wrappedTail.top + wrappedTail.height)
                   && wrappedDifference.bottom()
                          >= qFloor(wrappedTail.top),
               true);

    HierarchyInstanceContext p0Context;
    p0Context.workspacePath = dir.path();
    p0Context.activeTopModule = QStringLiteral("top");
    p0Context.instancePath = QStringLiteral("top.p0");
    editor->setHierarchyInstanceContext(p0Context);

    QList<EffectiveValueFact> currentFacts;
    QList<SemanticSymbolRecord> currentRecords =
        slang.extractSymbolRecords(filePath, source, {}, {}, &currentFacts);
    appendDuplicateFormalPort(&currentRecords);
    const std::uint64_t currentComputation =
        values->beginComputation({filePath});
    const std::uint64_t documentRevision =
        editor->semanticDocumentRevision();
    for (SemanticSymbolRecord& record : currentRecords) {
        record.presentation.computationRevision = currentComputation;
        record.presentation.documentRevision = documentRevision;
    }
    semanticIndex->setSnapshot(snapshotFromRecords(
        currentRecords, {}, {}, {{filePath, source}}));
    values->publishDocumentFacts(filePath,
                                 source,
                                 currentFacts,
                                 currentComputation,
                                 documentRevision);

    editor->setGhostAnnotations({});
    emit scheduler.documentRefreshRequested(filePath);
    const bool documentGhostRefreshFinished = waitUntil([&]() {
        return editorGhostCacheContains(
                   editor,
                   GhostAnnotationKind::FormalPort)
            && editorGhostCacheContains(
                   editor,
                   GhostAnnotationKind::ParameterOverride,
                   QStringLiteral("= 9"))
            && editorGhostCacheContains(
                   editor,
                   GhostAnnotationKind::ParameterValue,
                   QStringLiteral("= 8"))
            && editorGhostCacheContains(
                   editor,
                   GhostAnnotationKind::SignalWidth);
    }, 5000);
    expectBool("document refresh repopulates FormalPort Ghost",
               documentGhostRefreshFinished
                   && editorGhostCacheContains(
                       editor,
                       GhostAnnotationKind::FormalPort),
               true);
    expectBool("document refresh publishes bound override Ghost",
               editorGhostCacheContains(editor,
                                        GhostAnnotationKind::ParameterOverride,
                                        QStringLiteral("= 9")),
               true);
    expectBool("bound parameter differing from source literal keeps Ghost",
               editorGhostCacheContains(editor,
                                        GhostAnnotationKind::ParameterValue,
                                        QStringLiteral("= 8")),
               true);
    expectBool("document refresh publishes effective width Ghost",
               editorGhostCacheContains(editor,
                                        GhostAnnotationKind::SignalWidth),
               true);

    editor->setGhostAnnotations({});
    emit scheduler.fileSymbolAnalysisFinished(filePath,
                                              currentRecords.size());
    expectBool("analysis completion repopulates all Ghost annotations",
               waitUntil([&]() {
                   return editorGhostCacheContains(
                              editor,
                              GhostAnnotationKind::FormalPort)
                       && editorGhostCacheContains(
                              editor,
                              GhostAnnotationKind::ParameterOverride,
                              QStringLiteral("= 9"))
                       && editorGhostCacheContains(
                              editor,
                              GhostAnnotationKind::SignalWidth);
               }, 5000),
               true);

    HierarchyInstanceContext p1Context = p0Context;
    p1Context.instancePath = QStringLiteral("top.p1");
    editor->setHierarchyInstanceContext(p1Context);
    expectBool("instance context change refreshes effective Ghost",
               waitUntil([&]() {
                   return editorGhostCacheContains(
                              editor,
                              GhostAnnotationKind::ParameterOverride,
                              QStringLiteral("= 12"))
                       && !editorGhostCacheContains(
                              editor,
                              GhostAnnotationKind::ParameterOverride,
                              QStringLiteral("= 9"));
               }, 5000),
               true);

    scheduler.shutdown();
    values->clearPublishedFacts();
    semanticIndex->setSnapshot(previousSnapshot);
}

static bool workspaceManagerHasActiveScanTimer(WorkspaceManager* workspace)
{
    if (!workspace)
        return false;
    const QList<QTimer*> timers =
        workspace->findChildren<QTimer*>(QString(),
                                         Qt::FindDirectChildrenOnly);
    return std::any_of(timers.cbegin(),
                       timers.cend(),
                       [](QTimer* timer) {
                           return timer && timer->isActive();
                       });
}

static void runWorkspaceScanReentrancyRegression()
{
    QTemporaryDir startedWorkspace;
    QTemporaryDir progressWorkspace;
    QTemporaryDir finishWorkspace;
    expectBool("workspace scan reentrancy temp dirs valid",
               startedWorkspace.isValid()
                   && progressWorkspace.isValid()
                   && finishWorkspace.isValid(),
               true);
    if (!startedWorkspace.isValid()
        || !progressWorkspace.isValid()
        || !finishWorkspace.isValid()) {
        return;
    }

    auto writeModule = [](const QString& root, const QString& name) {
        QFile file(QDir(root).absoluteFilePath(name + QStringLiteral(".sv")));
        return file.open(QIODevice::WriteOnly | QIODevice::Text)
            && file.write(
                   QStringLiteral("module %1; endmodule\n").arg(name)
                       .toUtf8()) > 0;
    };
    expectBool("workspace scan reentrancy fixtures written",
               writeModule(progressWorkspace.path(),
                           QStringLiteral("progress_top"))
                   && writeModule(finishWorkspace.path(),
                                  QStringLiteral("finish_top")),
               true);

    WorkspaceManager startedManager;
    bool closedFromStarted = false;
    QObject::connect(
        &startedManager,
        &WorkspaceManager::workspaceScanStarted,
        &startedManager,
        [&](const QString&) {
            closedFromStarted = true;
            startedManager.closeWorkspace();
        });
    startedManager.openWorkspace(startedWorkspace.path());
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("scan-started synchronous close does not restart old timer",
               closedFromStarted
                   && !startedManager.isWorkspaceOpen()
                   && !workspaceManagerHasActiveScanTimer(&startedManager),
               true);

    WorkspaceManager progressManager;
    bool closedFromProgress = false;
    QSignalSpy progressFinishedSpy(
        &progressManager,
        &WorkspaceManager::workspaceScanFinished);
    QObject::connect(
        &progressManager,
        &WorkspaceManager::workspaceScanProgress,
        &progressManager,
        [&](const QString&, int) {
            closedFromProgress = true;
            progressManager.closeWorkspace();
        });
    progressManager.openWorkspace(progressWorkspace.path());
    const bool progressSignalHandled = waitUntil(
        [&]() { return closedFromProgress; }, 2000);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("scan-progress synchronous close invalidates iterator safely",
               progressSignalHandled
                   && !progressManager.isWorkspaceOpen()
                   && progressFinishedSpy.isEmpty()
                   && !workspaceManagerHasActiveScanTimer(&progressManager),
               true);

    WorkspaceManager finishManager;
    bool closedFromProjectPublication = false;
    QSignalSpy finishSignalSpy(
        &finishManager,
        &WorkspaceManager::workspaceScanFinished);
    QMetaObject::Connection finishProjectConnection;
    QObject::connect(
        &finishManager,
        &WorkspaceManager::workspaceScanStarted,
        &finishManager,
        [&](const QString&) {
            finishProjectConnection = QObject::connect(
                finishManager.getProjectModel(),
                &ProjectModel::projectChanged,
                &finishManager,
                [&](const ProjectSnapshot&) {
                    QObject::disconnect(finishProjectConnection);
                    closedFromProjectPublication = true;
                    finishManager.closeWorkspace();
                });
        });
    finishManager.openWorkspace(finishWorkspace.path());
    const bool projectPublicationHandled = waitUntil(
        [&]() { return closedFromProjectPublication; }, 2000);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("scan-finish project publication cannot commit after close",
               projectPublicationHandled
                   && !finishManager.isWorkspaceOpen()
                   && finishSignalSpy.isEmpty()
                   && !workspaceManagerHasActiveScanTimer(&finishManager),
               true);
}

static void runWorkspaceCachedSwitchRegression()
{
    QTemporaryDir workspaceA;
    QTemporaryDir workspaceB;
    expectBool("workspace cached switch temp dirs valid",
               workspaceA.isValid() && workspaceB.isValid(),
               true);
    if (!workspaceA.isValid() || !workspaceB.isValid())
        return;

    const QString fileA =
        QDir(workspaceA.path()).absoluteFilePath(QStringLiteral("a_top.sv"));
    const QString fileB =
        QDir(workspaceB.path()).absoluteFilePath(QStringLiteral("b_top.sv"));
    {
        QFile out(fileA);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write("module a_top; endmodule\n");
    }
    {
        QFile out(fileB);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write("module b_top; endmodule\n");
    }

    WorkspaceManager workspace;
    QSignalSpy openedSpy(&workspace, &WorkspaceManager::workspaceOpened);
    QSignalSpy activatedSpy(&workspace, &WorkspaceManager::workspaceActivated);
    QSignalSpy filesScannedSpy(&workspace, &WorkspaceManager::filesScanned);
    QSignalSpy scanStartedSpy(&workspace, &WorkspaceManager::workspaceScanStarted);

    expectBool("workspace cached switch open A",
               workspace.openWorkspace(workspaceA.path()),
               true);
    expectBool("workspace cached switch scans A",
               waitUntil([&]() {
                   return workspace.getSystemVerilogFiles().contains(
                       QDir::cleanPath(QDir::fromNativeSeparators(
                           QFileInfo(fileA).absoluteFilePath())));
               }, 2000),
               true);

    expectBool("workspace cached switch open B",
               workspace.openWorkspace(workspaceB.path()),
               true);
    expectBool("workspace cached switch scans B",
               waitUntil([&]() {
                   return workspace.getSystemVerilogFiles().contains(
                       QDir::cleanPath(QDir::fromNativeSeparators(
                           QFileInfo(fileB).absoluteFilePath())));
               }, 2000),
               true);

    const QString addedFileA =
        QDir(workspaceA.path()).absoluteFilePath(
            QStringLiteral("a_added.sv"));
    {
        QFile out(addedFileA);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write("module a_added; endmodule\n");
    }
    const QString normalizedAddedA =
        QDir::cleanPath(QDir::fromNativeSeparators(
            QFileInfo(addedFileA).absoluteFilePath()));

    const int openedBeforeSwitch = openedSpy.count();
    const int filesScannedBeforeSwitch = filesScannedSpy.count();
    const int scanStartedBeforeSwitch = scanStartedSpy.count();
    const int activatedBeforeSwitch = activatedSpy.count();

    expectBool("workspace cached switch activates A",
               workspace.switchWorkspace(0),
               true);
    const QString normalizedA =
        QDir::cleanPath(QDir::fromNativeSeparators(
            QFileInfo(fileA).absoluteFilePath()));
    expectBool("workspace cached switch restores A cache immediately",
               workspace.getSystemVerilogFiles() == QStringList{normalizedA}
                   && scanStartedSpy.count() == scanStartedBeforeSwitch + 1,
               true);
    expectBool("workspace cached switch reconciles inactive additions",
               waitUntil([&]() {
                   return workspace.getSystemVerilogFiles().contains(
                       normalizedAddedA);
               }, 2000),
               true);
    expectBool("workspace cached switch publishes changed membership",
               activatedSpy.count() == activatedBeforeSwitch + 1
                   && openedSpy.count() == openedBeforeSwitch
                   && filesScannedSpy.count() == filesScannedBeforeSwitch + 1
                   && scanStartedSpy.count() == scanStartedBeforeSwitch + 1,
               true);

    const int filesScannedAfterASwitch = filesScannedSpy.count();
    expectBool("workspace cached switch activates B",
               workspace.switchWorkspace(1),
               true);
    const QString normalizedB =
        QDir::cleanPath(QDir::fromNativeSeparators(
            QFileInfo(fileB).absoluteFilePath()));
    expectBool("workspace cached switch restores B cache immediately",
               workspace.getSystemVerilogFiles() == QStringList{normalizedB}
                   && scanStartedSpy.count() == scanStartedBeforeSwitch + 2,
               true);
    expectBool("workspace unchanged reconciliation stays passive",
               waitUntil([&]() {
                   return !workspace.scanIterator
                       && workspace.scanningPath.isEmpty();
               }, 2000)
                   && workspace.getSystemVerilogFiles()
                          == QStringList{normalizedB}
                   && filesScannedSpy.count() == filesScannedAfterASwitch
                   && openedSpy.count() == openedBeforeSwitch
                   && scanStartedSpy.count() == scanStartedBeforeSwitch + 2,
               true);
}

static void runWorkspaceScanSignalReentrancyRegression()
{
    const auto normalizedPath = [](const QString& path) {
        return QDir::cleanPath(QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
    };
    const auto createSource = [](QTemporaryDir& directory,
                                 const QString& fileName) {
        const QString path =
            QDir(directory.path()).absoluteFilePath(fileName);
        return writeTextFile(path,
                             QStringLiteral("module scan_probe; endmodule\n"))
            ? path
            : QString();
    };

    {
        QTemporaryDir directory;
        const QString source =
            createSource(
                directory,
                QStringLiteral("activation_restore.sv"));
        expectBool(
            "workspace activation restore fixture valid",
            directory.isValid() && !source.isEmpty(),
            true);
        if (!directory.isValid() || source.isEmpty())
            return;

        WorkspaceManager workspace;
        workspace.setRecentWorkspacePersistenceEnabledForTesting(
            false);
        const QString expectedPath =
            normalizedPath(directory.path());
        const QString expectedSource =
            normalizedPath(source);
        int restoredActivations = 0;
        QObject::connect(
            &workspace,
            &WorkspaceManager::workspaceActivated,
            &workspace,
            [&](int,
                const QString&,
                const QString& path) {
                if (normalizedPath(path)
                    != expectedPath) {
                    return;
                }
                if (workspace.restoreSessionScanState(
                        QStringList{source}, true)) {
                    ++restoredActivations;
                }
            });

        expectBool(
            "workspace activation restore reports successful open",
            workspace.openWorkspace(directory.path()),
            true);
        expectBool(
            "workspace activation restore starts reconciliation scan",
            restoredActivations == 1
                && workspace.isWorkspaceOpen()
                && workspace.getWorkspacePath()
                       == expectedPath
                && workspace.getSystemVerilogFiles()
                       == QStringList{expectedSource}
                && workspace.scanIterator
                && workspace.scanningPath == expectedPath,
            true);
        expectBool(
            "workspace activation reconciliation completes",
            waitUntil([&]() {
                return !workspace.scanIterator
                    && workspace.scanningPath.isEmpty();
            }, 2000),
            true);

        workspace.closeWorkspace();
        expectBool(
            "workspace activation restore reports successful reopen",
            workspace.openWorkspace(directory.path()),
            true);
        expectBool(
            "workspace activation restore restarts reconciliation after close",
            restoredActivations == 2
                && workspace.isWorkspaceOpen()
                && workspace.workspaceEntries().size() == 1
                && workspace.getSystemVerilogFiles()
                       == QStringList{expectedSource}
                && workspace.scanIterator
                && workspace.scanningPath == expectedPath,
            true);
        expectBool(
            "workspace reopened reconciliation completes",
            waitUntil([&]() {
                return !workspace.scanIterator
                    && workspace.scanningPath.isEmpty();
            }, 2000),
            true);
    }

    {
        QTemporaryDir directory;
        const QString source = createSource(directory,
                                            QStringLiteral("started_close.sv"));
        expectBool("workspace scan-start close fixture valid",
                   directory.isValid() && !source.isEmpty(),
                   true);
        if (!directory.isValid() || source.isEmpty())
            return;

        WorkspaceManager workspace;
        workspace.setRecentWorkspacePersistenceEnabledForTesting(false);
        const QString expectedPath = normalizedPath(directory.path());
        bool closedFromStarted = false;
        QObject::connect(&workspace,
                         &WorkspaceManager::workspaceScanStarted,
                         &workspace,
                         [&](const QString& path) {
                             if (normalizedPath(path) != expectedPath)
                                 return;
                             closedFromStarted = true;
                             workspace.closeWorkspace();
                         });

        expectBool("workspace scan-start close reports synchronous override",
                   workspace.openWorkspace(directory.path()),
                   false);
        expectBool("workspace scan-start close cancels synchronously",
                   closedFromStarted
                       && !workspace.isWorkspaceOpen()
                       && !workspace.scanIterator
                       && workspace.scanningPath.isEmpty()
                       && workspace.scanTimer
                       && !workspace.scanTimer->isActive(),
                   true);
    }

    {
        QTemporaryDir cachedDirectory;
        QTemporaryDir scanningDirectory;
        const QString cachedSource =
            createSource(cachedDirectory, QStringLiteral("cached_started.sv"));
        const QString scanningSource =
            createSource(scanningDirectory, QStringLiteral("switch_started.sv"));
        expectBool("workspace scan-start switch fixtures valid",
                   cachedDirectory.isValid()
                       && scanningDirectory.isValid()
                       && !cachedSource.isEmpty()
                       && !scanningSource.isEmpty(),
                   true);
        if (!cachedDirectory.isValid()
            || !scanningDirectory.isValid()
            || cachedSource.isEmpty()
            || scanningSource.isEmpty()) {
            return;
        }

        WorkspaceManager workspace;
        workspace.setRecentWorkspacePersistenceEnabledForTesting(false);
        expectBool("workspace scan-start switch opens cached workspace",
                   workspace.openWorkspace(cachedDirectory.path())
                       && workspace.restoreSessionScanState(
                           QStringList{cachedSource}, true),
                   true);
        const QString cachedPath = normalizedPath(cachedDirectory.path());
        const QString scanningPath = normalizedPath(scanningDirectory.path());
        bool switchedFromStarted = false;
        QObject::connect(&workspace,
                         &WorkspaceManager::workspaceScanStarted,
                         &workspace,
                         [&](const QString& path) {
                             if (normalizedPath(path) != scanningPath)
                                 return;
                             switchedFromStarted = workspace.switchWorkspace(0);
                         });

        expectBool("workspace scan-start switch reports synchronous override",
                   workspace.openWorkspace(scanningDirectory.path()),
                   false);
        expectBool("workspace scan-start switch reconciles cached workspace",
                   switchedFromStarted
                       && workspace.getWorkspacePath() == cachedPath
                       && workspace.scanIterator
                       && workspace.scanningPath == cachedPath
                       && workspace.scanTimer
                       && workspace.scanTimer->isActive(),
                   true);
        expectBool("workspace switched reconciliation completes",
                   waitUntil([&]() {
                       return !workspace.scanIterator
                           && workspace.scanningPath.isEmpty()
                           && workspace.scanTimer
                           && !workspace.scanTimer->isActive();
                   }, 2000),
                   true);
    }

    {
        QTemporaryDir directory;
        const QString source = createSource(directory,
                                            QStringLiteral("progress_close.sv"));
        expectBool("workspace scan-progress close fixture valid",
                   directory.isValid() && !source.isEmpty(),
                   true);
        if (!directory.isValid() || source.isEmpty())
            return;

        WorkspaceManager workspace;
        workspace.setRecentWorkspacePersistenceEnabledForTesting(false);
        const QString expectedPath = normalizedPath(directory.path());
        bool closedFromProgress = false;
        QObject::connect(&workspace,
                         &WorkspaceManager::workspaceScanProgress,
                         &workspace,
                         [&](const QString& path, int) {
                             if (normalizedPath(path) != expectedPath)
                                 return;
                             closedFromProgress = true;
                             workspace.closeWorkspace();
                         });

        expectBool("workspace scan-progress close starts scan",
                   workspace.openWorkspace(directory.path()),
                   true);
        expectBool("workspace scan-progress close survives callback",
                   waitUntil([&]() { return closedFromProgress; }, 2000)
                       && !workspace.isWorkspaceOpen()
                       && !workspace.scanIterator
                       && workspace.scanningPath.isEmpty()
                       && workspace.scanTimer
                       && !workspace.scanTimer->isActive(),
                   true);
    }

    {
        QTemporaryDir cachedDirectory;
        QTemporaryDir scanningDirectory;
        const QString cachedSource =
            createSource(cachedDirectory, QStringLiteral("cached_progress.sv"));
        const QString scanningSource =
            createSource(scanningDirectory, QStringLiteral("switch_progress.sv"));
        expectBool("workspace scan-progress switch fixtures valid",
                   cachedDirectory.isValid()
                       && scanningDirectory.isValid()
                       && !cachedSource.isEmpty()
                       && !scanningSource.isEmpty(),
                   true);
        if (!cachedDirectory.isValid()
            || !scanningDirectory.isValid()
            || cachedSource.isEmpty()
            || scanningSource.isEmpty()) {
            return;
        }

        WorkspaceManager workspace;
        workspace.setRecentWorkspacePersistenceEnabledForTesting(false);
        expectBool("workspace scan-progress switch opens cached workspace",
                   workspace.openWorkspace(cachedDirectory.path())
                       && workspace.restoreSessionScanState(
                           QStringList{cachedSource}, true),
                   true);
        const QString cachedPath = normalizedPath(cachedDirectory.path());
        const QString scanningPath = normalizedPath(scanningDirectory.path());
        bool switchedFromProgress = false;
        QObject::connect(&workspace,
                         &WorkspaceManager::workspaceScanProgress,
                         &workspace,
                         [&](const QString& path, int) {
                             if (normalizedPath(path) != scanningPath)
                                 return;
                             switchedFromProgress = workspace.switchWorkspace(0);
                         });

        expectBool("workspace scan-progress switch opens second workspace",
                   workspace.openWorkspace(scanningDirectory.path()),
                   true);
        expectBool("workspace scan-progress switch survives callback",
                   waitUntil([&]() { return switchedFromProgress; }, 2000)
                       && workspace.getWorkspacePath() == cachedPath
                       && workspace.getSystemVerilogFiles()
                              == QStringList{normalizedPath(cachedSource)}
                       && !workspace.scanIterator
                       && workspace.scanningPath.isEmpty()
                       && workspace.scanTimer
                       && !workspace.scanTimer->isActive(),
                   true);
    }
}

static void runWorkspaceAliasRenameRegression()
{
    QTemporaryDir workspaceA;
    QTemporaryDir workspaceB;
    expectBool("workspace rename temp dirs valid",
               workspaceA.isValid() && workspaceB.isValid(),
               true);
    if (!workspaceA.isValid() || !workspaceB.isValid())
        return;

    WorkspaceManager workspace;
    QSignalSpy listSpy(&workspace, &WorkspaceManager::workspaceListChanged);
    expectBool("workspace rename open A",
               workspace.openWorkspace(workspaceA.path()),
               true);
    expectBool("workspace rename open B",
               workspace.openWorkspace(workspaceB.path()),
               true);
    const QString pathA = workspace.workspaceEntries().at(0).path;
    const QString pathB = workspace.workspaceEntries().at(1).path;
    const QString originalAliasA = workspace.workspaceEntries().at(0).alias;

    QString errorMessage;
    expectBool("workspace rename active succeeds",
               workspace.renameWorkspaceAlias(
                   1,
                   QStringLiteral("SharedName"),
                   &errorMessage),
               true);
    expectBool("workspace rename active updates current alias",
               workspace.getWorkspaceAlias() == QStringLiteral("SharedName")
                   && workspace.workspaceEntries().at(1).path == pathB,
               true);

    expectBool("workspace rename switch to A",
               workspace.switchWorkspace(0),
               true);
    expectBool("workspace rename rejects empty alias",
               workspace.renameWorkspaceAlias(0, QStringLiteral("   "),
                                             &errorMessage),
               false);
    expectBool("workspace rename rejects duplicate alias",
               workspace.renameWorkspaceAlias(
                   0,
                   QStringLiteral("sharedname"),
                   &errorMessage),
               false);
    expectBool("workspace rename failed attempts keep alias",
               workspace.workspaceEntries().at(0).alias == originalAliasA,
               true);

    expectBool("workspace rename active workspace succeeds",
               workspace.renameWorkspaceAlias(
                   0,
                   QStringLiteral("RTL Core"),
                   &errorMessage),
               true);
    expectBool("workspace rename updates active alias and preserves path",
               workspace.getWorkspaceAlias() == QStringLiteral("RTL Core")
                   && workspace.workspaceEntries().at(0).alias
                          == QStringLiteral("RTL Core")
                   && workspace.workspaceEntries().at(0).path == pathA,
               true);

    bool sawRecentA = false;
    bool sawRecentB = false;
    const QList<WorkspaceManager::WorkspaceEntry> recentEntries =
        workspace.recentWorkspaceEntries();
    for (const WorkspaceManager::WorkspaceEntry& entry : recentEntries) {
        sawRecentA = sawRecentA
            || (entry.path == pathA
                && entry.alias == QStringLiteral("RTL Core"));
        sawRecentB = sawRecentB
            || (entry.path == pathB
                && entry.alias == QStringLiteral("SharedName"));
    }
    expectBool("workspace rename updates recent metadata",
               sawRecentA && sawRecentB,
               true);
    expectBool("workspace recent removal succeeds",
               workspace.removeRecentWorkspace(pathA),
               true);
    const QList<WorkspaceManager::WorkspaceEntry> recentAfterRemoval =
        workspace.recentWorkspaceEntries();
    expectBool("workspace recent removal keeps open workspace",
               workspace.workspaceEntries().size() == 2
                   && workspace.getWorkspacePath() == pathA
                   && std::none_of(
                       recentAfterRemoval.cbegin(),
                       recentAfterRemoval.cend(),
                       [&pathA](const WorkspaceManager::WorkspaceEntry& entry) {
                           return entry.path == pathA;
                       })
                   && std::any_of(
                       recentAfterRemoval.cbegin(),
                       recentAfterRemoval.cend(),
                       [&pathB](const WorkspaceManager::WorkspaceEntry& entry) {
                           return entry.path == pathB;
                       }),
               true);
    expectBool("workspace recent removal rejects missing entry",
               workspace.removeRecentWorkspace(pathA),
               false);
    expectBool("workspace rename emits list changes",
               listSpy.size() >= 4,
               true);
}

static void runWorkspaceSessionCloseSaveOrderRegression()
{
    QTemporaryDir workspaceDir;
    QTemporaryDir localStateDir;
    expectBool("workspace session close temp dir valid",
               workspaceDir.isValid()
                   && localStateDir.isValid(),
               true);
    if (!workspaceDir.isValid()
        || !localStateDir.isValid())
        return;

    const QByteArray previousSessionStorage =
        qgetenv(
            "ZEROSLACK_SESSION_STORAGE_PATH");
    const QString localSessionStorage =
        QDir(localStateDir.path())
            .absoluteFilePath(
                QStringLiteral(
                    "workspace-sessions.ini"));
    qputenv(
        "ZEROSLACK_SESSION_STORAGE_PATH",
        localSessionStorage.toUtf8());

    const QString sessionFile =
        QDir(workspaceDir.path()).absoluteFilePath(
            QStringLiteral("session_close_top.sv"));
    expectBool("workspace session close file writable",
               writeTextFile(sessionFile,
                             QStringLiteral("module session_close_top;\n"
                                            "endmodule\n")),
               true);

    {
        MainWindow window;
        expectBool("workspace session close opens workspace",
                   window.workspaceManager
                       && window.workspaceManager->openWorkspace(
                           workspaceDir.path()),
                   true);
        expectBool("workspace session close scan completes",
                   waitUntil([&]() {
                       return window.workspaceManager
                           && window.workspaceManager->workspaceEntries().size()
                                  == 1
                           && window.workspaceManager->workspaceEntries()
                                  .first()
                                  .scanComplete;
                   },
                             3000),
                   true);
        expectBool("workspace session close opens tab",
                   window.tabManager
                       && window.tabManager->openFileInTab(sessionFile),
                   true);

        const QList<WorkspaceSessionTabState> capturedBeforeClose =
            window.tabManager->workspaceSessionTabs(
                workspaceDir.path());
        expectBool("workspace session close pre-capture has tab",
                   capturedBeforeClose.size() == 1
                       && QFileInfo(capturedBeforeClose.first().filePath)
                              .fileName()
                              == QStringLiteral("session_close_top.sv"),
                   true);

        window.closeActiveWorkspace();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        WorkspaceSessionStateService service;
        const WorkspaceSessionRestoreResult restored =
            service.load(workspaceDir.path());
        expectBool("workspace session close saved before tab close",
                   restored.loaded
                       && restored.state.tabs.size() == 1
                       && QFileInfo(restored.state.tabs.first().filePath)
                              .fileName()
                              == QStringLiteral("session_close_top.sv")
                       && QFileInfo(localSessionStorage).isFile()
                       && !QFileInfo(
                               QDir(workspaceDir.path())
                                   .absoluteFilePath(
                                       QStringLiteral(".zs")))
                               .exists(),
                   true);
    }

    if (previousSessionStorage.isEmpty()) {
        qunsetenv(
            "ZEROSLACK_SESSION_STORAGE_PATH");
    } else {
        qputenv(
            "ZEROSLACK_SESSION_STORAGE_PATH",
            previousSessionStorage);
    }
}

static void runWorkspaceWatcherIncrementalRegression()
{
    QTemporaryDir directory;
    expectBool("workspace watcher temp directory valid",
               directory.isValid(),
               true);
    if (!directory.isValid())
        return;

    const QString source = QDir(directory.path()).absoluteFilePath(
        QStringLiteral("watch_top.sv"));
    expectBool("workspace watcher source written",
               writeTextFile(
                   source,
                   QStringLiteral("module watch_top; endmodule\n")),
               true);

    WorkspaceManager workspace;
    workspace.setRecentWorkspacePersistenceEnabledForTesting(false);
    QSignalSpy scanStartedSpy(
        &workspace,
        &WorkspaceManager::workspaceScanStarted);
    QSignalSpy fileChangedSpy(
        &workspace,
        &WorkspaceManager::fileChanged);
    expectBool("workspace watcher opens fixture",
               workspace.openWorkspace(directory.path()),
               true);
    expectBool("workspace watcher initial scan completes",
               waitUntil([&]() {
                   return workspace.getSystemVerilogFiles().contains(
                       QDir::cleanPath(QFileInfo(source).absoluteFilePath()));
               }, 3000),
               true);

    const int scansBeforeSave = scanStartedSpy.count();
    expectBool("workspace watcher updates existing source",
               writeTextFile(
                   source,
                   QStringLiteral(
                       "module watch_top; logic changed; endmodule\n")),
               true);
    expectBool("workspace watcher publishes one-file content change",
               waitUntil([&]() {
                   return fileChangedSpy.count() == 1;
               }, 3000),
               true);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
    expectBool("workspace watcher emits one content notification per save",
               fileChangedSpy.count() == 1,
               true);
    expectBool("workspace watcher save avoids workspace rescan",
               scanStartedSpy.count() == scansBeforeSave,
               true);

    const QString added = QDir(directory.path()).absoluteFilePath(
        QStringLiteral("watch_added.sv"));
    expectBool("workspace watcher added source written",
               writeTextFile(
                   added,
                   QStringLiteral("module watch_added; endmodule\n")),
               true);
    expectBool("workspace watcher membership change rescans",
               waitUntil([&]() {
                   return scanStartedSpy.count() > scansBeforeSave
                       && workspace.getSystemVerilogFiles().contains(
                           QDir::cleanPath(
                               QFileInfo(added).absoluteFilePath()));
               }, 4000),
               true);
}

static void runExternalConflictReviewRegression()
{
    printf("\n-- external conflict review regression --\n");
    QTemporaryDir directory;
    const QString fileName =
        directory.filePath(
            QStringLiteral("external_conflict_ui.sv"));
    const QString initialText =
        QStringLiteral(
            "module external_conflict_ui;\n"
            "  logic initial_signal;\n"
            "endmodule\n");
    const QString firstExternalText =
        QStringLiteral(
            "module external_conflict_ui;\n"
            "  logic first_external_signal;\n"
            "endmodule\n");
    const QString secondExternalText =
        QStringLiteral(
            "module external_conflict_ui;\n"
            "  logic second_external_signal;\n"
            "endmodule\n");
    expectBool("external conflict UI fixture writable",
               directory.isValid()
                   && writeTextFile(fileName, initialText),
               true);
    if (!directory.isValid())
        return;

    MainWindow window;
    window.resize(900, 640);
    window.show();
    expectBool("external conflict UI opens source",
               window.tabManager
                   && window.tabManager->openFileInTab(
                       fileName),
               true);
    MyCodeEditor* editor =
        window.tabManager
        ? window.tabManager->getCurrentEditor()
        : nullptr;
    SharedDocument* document =
        window.tabManager && editor
        ? window.tabManager
              ->sharedDocumentForEditor(editor)
        : nullptr;
    if (!editor || !document)
        return;

    QTextCursor localEdit(editor->document());
    localEdit.movePosition(QTextCursor::End);
    localEdit.insertText(
        QStringLiteral("// local dirty text\n"));
    editor->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        50);
    QWidget* originalFocus =
        QApplication::focusWidget();
    const PanelLayoutState before =
        window.panelLayoutController->layoutState();

    expectBool("external conflict UI external replacement writable",
               writeTextFile(fileName,
                             firstExternalText),
               true);
    const ExternalDocumentSyncResult conflict =
        window.tabManager
            ->externalDocumentSyncController()
            ->processFileChange(fileName);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        50);

    NotificationItem notification;
    const bool hasNotification =
        window.notificationCenterForTesting()
        && window.notificationCenterForTesting()
               ->notificationByKey(
                   QStringLiteral("external:%1")
                       .arg(fileName),
                   &notification);
    const PanelLayoutState afterDetection =
        window.panelLayoutController->layoutState();
    expectBool("dirty external change posts actionable notification",
               conflict.outcome
                       == ExternalDocumentSyncOutcome::Conflict
                   && hasNotification
                   && notification.actions.size() == 4,
               true);
    expectBool("conflict detection is non-blocking and layout neutral",
               window.externalConflictReviewBar
                   && !window.externalConflictReviewBar
                           ->isVisible()
                   && QApplication::focusWidget()
                          == originalFocus
                   && afterDetection.closedBottomPanels
                          == before.closedBottomPanels
                   && afterDetection.bottomPanelOrder
                          == before.bottomPanelOrder
                   && afterDetection.pinnedBottomPanels
                          == before.pinnedBottomPanels
                   && afterDetection.activeBottomPanel
                          == before.activeBottomPanel
                   && afterDetection.bottomCollapsed
                          == before.bottomCollapsed
                   && afterDetection.expandedBottomHeight
                          == before.expandedBottomHeight,
               true);

    expectBool("compare action is accepted",
               window.notificationCenterForTesting()
                   ->requestAction(
                       notification.id,
                       QStringLiteral(
                           "external.review")),
               true);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        50);
    expectBool("comparison is embedded and preserves focus",
               window.externalConflictReviewBar
                   && window.externalConflictReviewBar
                          ->isVisible()
                   && window.externalConflictReviewBar
                          ->window()
                          == &window
                   && window.externalConflictLocalText
                          ->toPlainText()
                          .contains(
                              QStringLiteral(
                                  "local dirty text"))
                   && window.externalConflictDiskText
                          ->toPlainText()
                          == firstExternalText
                   && QApplication::focusWidget()
                          == originalFocus,
               true);

    expectBool("second external generation is writable",
               writeTextFile(fileName,
                             secondExternalText),
               true);
    window.tabManager
        ->externalDocumentSyncController()
        ->processFileChange(fileName);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        50);
    expectBool("open comparison refreshes to the latest generation",
               window.externalConflictDiskText
                   ->toPlainText()
                   == secondExternalText
                   && document->dirty()
                   && editor->toPlainText().contains(
                       QStringLiteral(
                           "local dirty text")),
               true);

    QPushButton* closeButton =
        window.findChild<QPushButton*>(
            QStringLiteral(
                "externalConflictCloseButton"));
    if (closeButton)
        closeButton->click();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        50);
    expectBool("comparison cancellation preserves dirty text and focus",
               closeButton
                   && !window.externalConflictReviewBar
                           ->isVisible()
                   && document->dirty()
                   && editor->toPlainText().contains(
                       QStringLiteral(
                           "local dirty text"))
                   && QApplication::focusWidget()
                          == originalFocus,
               true);

    expectBool("reload notification action is accepted",
               window.notificationCenterForTesting()
                   ->requestAction(
                       notification.id,
                       QStringLiteral(
                           "external.reload")),
               true);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        50);
    const PanelLayoutState afterReload =
        window.panelLayoutController->layoutState();
    expectBool("explicit reload resolves conflict without panel mutation",
               !document->dirty()
                   && document->externalState()
                          == SharedDocumentExternalState::Current
                   && editor->toPlainText()
                          == secondExternalText
                   && !window.externalConflictReviewBar
                           ->isVisible()
                   && QApplication::focusWidget()
                          == originalFocus
                   && afterReload.closedBottomPanels
                          == before.closedBottomPanels
                   && afterReload.bottomPanelOrder
                          == before.bottomPanelOrder
                   && afterReload.pinnedBottomPanels
                          == before.pinnedBottomPanels
                   && afterReload.activeBottomPanel
                          == before.activeBottomPanel
                   && afterReload.bottomCollapsed
                          == before.bottomCollapsed
                   && afterReload.expandedBottomHeight
                          == before.expandedBottomHeight,
               true);
}

static void runCrashRecoveryReviewRegression()
{
    printf("\n-- crash recovery review regression --\n");
    QTemporaryDir workspaceDir;
    QTemporaryDir recoveryStorageDir;
    expectBool("crash recovery UI temp dirs valid",
               workspaceDir.isValid()
                   && recoveryStorageDir.isValid(),
               true);
    if (!workspaceDir.isValid()
        || !recoveryStorageDir.isValid()) {
        return;
    }

    const QString firstFile =
        workspaceDir.filePath(
            QStringLiteral("recover_first.sv"));
    const QString secondFile =
        workspaceDir.filePath(
            QStringLiteral("recover_second.sv"));
    const QString firstSource =
        QStringLiteral("module recover_first;\nendmodule\n");
    const QString secondSource =
        QStringLiteral("module recover_second;\nendmodule\n");
    const QString firstRecovered =
        QStringLiteral("module recover_first;\n"
                       "  logic recovered_a;\n"
                       "endmodule\n");
    const QString firstRecoveredUpdated =
        QStringLiteral("module recover_first;\n"
                       "  logic recovered_after_review;\n"
                       "endmodule\n");
    const QString secondRecovered =
        QStringLiteral("module recover_second;\n"
                       "  logic discard_me;\n"
                       "endmodule\n");
    expectBool("crash recovery UI sources writable",
               writeTextFile(firstFile, firstSource)
                   && writeTextFile(secondFile,
                                    secondSource),
               true);

    const auto fileBytes =
        [](const QString& path) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return QByteArray();
            return file.readAll();
        };
    const auto recoveryRequest =
        [&](const QString& fileName,
            const QString& recoveredText,
            quint64 revision) {
            CrashRecoverySnapshotRequest request;
            request.document.workspacePath =
                workspaceDir.path();
            request.document.originalFilePath =
                fileName;
            request.text = recoveredText;
            request.documentRevision = revision;
            request.savedBaselineSha256 =
                CrashRecoveryService::sha256(
                    fileBytes(fileName));
            request.savedBaselineModifiedUtc =
                QFileInfo(fileName)
                    .lastModified()
                    .toUTC();
            return request;
        };

    MainWindow window;
    auto recoveryService =
        std::make_unique<CrashRecoveryService>(
            recoveryStorageDir.path());
    CrashRecoveryService* recoveryServiceRaw =
        recoveryService.get();
    window.tabManager->setCrashRecoveryService(
        std::move(recoveryService));

    CrashRecoverySnapshotRequest firstRequest =
        recoveryRequest(firstFile,
                        firstRecovered,
                        4);
    CrashRecoverySnapshotRequest secondRequest =
        recoveryRequest(secondFile,
                        secondRecovered,
                        7);
    const CrashRecoveryWriteResult firstWrite =
        recoveryServiceRaw->writeSnapshot(
            firstRequest);
    const CrashRecoveryWriteResult secondWrite =
        recoveryServiceRaw->writeSnapshot(
            secondRequest);
    expectBool("crash recovery UI fixtures persisted",
               firstWrite.succeeded()
                   && secondWrite.succeeded(),
               true);
    if (!firstWrite.succeeded()
        || !secondWrite.succeeded()) {
        return;
    }

    window.resize(900, 640);
    window.show();
    auto* focusProbe = new QLineEdit(&window);
    focusProbe->setObjectName(
        QStringLiteral("crashRecoveryFocusProbe"));
    focusProbe->setGeometry(8, 8, 160, 24);
    focusProbe->show();
    focusProbe->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        50);
    QDockWidget* activityDock =
        window.findChild<QDockWidget*>(
            QStringLiteral("activityDock"));
    if (activityDock)
        activityDock->hide();
    const QWidget* focusBeforeNotification =
        QApplication::focusWidget();

    window.tabManager
        ->crashRecoveryCandidatesAvailable(
            workspaceDir.path(),
            2,
            0);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        50);

    NotificationItem availability;
    const bool availabilityPosted =
        window.notificationCenter
        && window.notificationCenter
               ->notificationByKey(
                   window.crashRecoveryNotificationKey(
                       workspaceDir.path()),
                   &availability);
    expectBool("recovery discovery posts actionable notification",
               availabilityPosted
                   && availability.source
                          == QStringLiteral("CrashRecovery")
                   && availability.actions.size() == 1
                   && availability.actions.first().id
                          == QString::fromLatin1(
                              ActionIds::
                                  ReviewCrashRecovery),
               true);
    expectBool("recovery discovery does not create or show review UI",
               window.crashRecoveryReviewDialog == nullptr
                   && activityDock
                   && !activityDock->isVisible(),
               true);
    expectBool("recovery discovery does not steal focus",
               QApplication::focusWidget()
                   == focusBeforeNotification,
               true);

    const bool reviewRequested =
        availabilityPosted
        && window.notificationCenter->requestAction(
            availability.id,
            QString::fromLatin1(
                ActionIds::
                    ReviewCrashRecovery));
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        50);
    expectBool("notification action opens centralized review",
               reviewRequested
                   && window.crashRecoveryReviewDialog
                   && window.crashRecoveryReviewDialog
                          ->isVisible()
                   && window.crashRecoveryCandidateList
                   && window.crashRecoveryCandidateList
                          ->topLevelItemCount() == 2,
               true);
    expectBool("recovery review is nonmodal non-topmost",
               window.crashRecoveryReviewDialog
                   && !window.crashRecoveryReviewDialog
                           ->isModal()
                   && window.crashRecoveryReviewDialog
                          ->windowModality()
                          == Qt::NonModal
                   && !window.crashRecoveryReviewDialog
                           ->windowFlags()
                           .testFlag(
                               Qt::WindowStaysOnTopHint)
                   && window.crashRecoveryReviewDialog
                          ->testAttribute(
                              Qt::WA_ShowWithoutActivating),
               true);
    expectBool("opening recovery review leaves editor focus active",
               QApplication::focusWidget()
                   == focusBeforeNotification,
               true);

    const auto candidateItem =
        [&](const QString& recoveryId) {
            if (!window.crashRecoveryCandidateList)
                return static_cast<QTreeWidgetItem*>(
                    nullptr);
            for (int row = 0;
                 row < window.crashRecoveryCandidateList
                           ->topLevelItemCount();
                 ++row) {
                QTreeWidgetItem* item =
                    window.crashRecoveryCandidateList
                        ->topLevelItem(row);
                if (item
                    && item->data(
                           0,
                           Qt::UserRole)
                           .toString()
                           == recoveryId) {
                    return item;
                }
            }
            return static_cast<QTreeWidgetItem*>(
                nullptr);
        };

    QTreeWidgetItem* firstItem =
        candidateItem(firstWrite.recoveryId);
    if (firstItem) {
        window.crashRecoveryCandidateList
            ->setCurrentItem(firstItem);
    }
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        20);
    expectBool("review shows source and recovered snapshot side by side",
               firstItem
                   && window.crashRecoverySourceText
                   && window.crashRecoverySourceText
                          ->toPlainText()
                          == firstSource
                   && window.crashRecoveryRecoveredText
                   && window.crashRecoveryRecoveredText
                          ->toPlainText()
                          == firstRecovered
                   && window.crashRecoveryRestoreButton
                          ->isEnabled()
                   && window.crashRecoveryDiscardButton
                          ->isEnabled(),
               true);

    firstRequest.text =
        firstRecoveredUpdated;
    firstRequest.documentRevision = 8;
    const CrashRecoveryWriteResult changedAfterReview =
        recoveryServiceRaw->writeSnapshot(
            firstRequest);
    expectBool("recovery fixture can change after review",
               changedAfterReview.succeeded(),
               true);
    window.crashRecoveryRestoreButton->click();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        50);
    NotificationItem staleReviewError;
    expectBool("stale reviewed token is rejected atomically",
               window.notificationCenter
                   ->notificationByKey(
                       QStringLiteral(
                           "crash-recovery-error:%1")
                           .arg(firstWrite.recoveryId),
                       &staleReviewError)
                   && staleReviewError.message.contains(
                       QStringLiteral(
                           "changed after review"))
                   && window.tabManager
                          ->getPlainTextFromOpenFile(
                              firstFile)
                          .isEmpty(),
               true);
    expectBool("failed restore refreshes comparison before retry",
               window.reviewedCrashRecoveryCandidate
                   && window.reviewedCrashRecoveryCandidate
                          ->documentRevision == 8
                   && window.crashRecoveryRecoveredText
                          ->toPlainText()
                          == firstRecoveredUpdated,
               true);

    window.crashRecoveryRestoreButton->click();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        50);
    expectBool("fresh reviewed token restores document text",
               window.tabManager
                       ->getPlainTextFromOpenFile(
                           firstFile)
                       == firstRecoveredUpdated,
               true);
    expectBool("fresh reviewed token leaves document unsaved",
               window.tabManager
                   ->hasUnsavedChanges(),
               true);
    expectBool("restored recovery candidate leaves the pending review",
               candidateItem(
                   firstWrite.recoveryId)
                   == nullptr,
               true);

    QTreeWidgetItem* secondItem =
        candidateItem(secondWrite.recoveryId);
    if (secondItem) {
        window.crashRecoveryCandidateList
            ->setCurrentItem(secondItem);
    }
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        20);
    expectBool("second recovery candidate is independently reviewed",
               secondItem
                   && window.crashRecoverySourceText
                          ->toPlainText()
                          == secondSource
                   && window.crashRecoveryRecoveredText
                          ->toPlainText()
                          == secondRecovered,
               true);
    window.crashRecoveryDiscardButton->click();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        50);

    const CrashRecoveryRecoverResult discarded =
        recoveryServiceRaw->recoverText(
            workspaceDir.path(),
            secondWrite.recoveryId);
    NotificationItem remainingAvailability;
    expectBool("discard removes only the selected snapshot",
               discarded.status
                       == CrashRecoveryStatus::NotFound
                   && window.crashRecoveryCandidateList
                          ->topLevelItemCount() == 0,
               true);
    expectBool("handled recovery notification is dismissed",
               !window.notificationCenter
                    ->notificationByKey(
                        window.crashRecoveryNotificationKey(
                            workspaceDir.path()),
                        &remainingAvailability),
               true);
}

static void runNoImplicitCompletionRegression()
{
    const QString completionFile =
        QStringLiteral("C:/fixture/no_implicit_completion.sv");
    const SemanticSymbolRecord macroPrefixCandidate =
        SemanticFixtureRecordBuilder(
            QStringLiteral("FOO_BAR"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(completionFile)
            .withLocalHandle(91001)
            .withLine(1)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    const SemanticSymbolRecord memberPrefixCandidate =
        SemanticFixtureRecordBuilder(
            QStringLiteral("member_item"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(completionFile)
            .withLocalHandle(91002)
            .withLine(2)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    const SemanticSymbolRecord visibleModule =
        SemanticFixtureRecordBuilder(
            QStringLiteral("visible_top"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(completionFile)
            .withLocalHandle(91003)
            .withLine(3)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    const SemanticSymbolRecord visibleCandidate =
        SemanticFixtureRecordBuilder(
            QStringLiteral("local_signal"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(completionFile)
            .withLocalHandle(91004)
            .withLine(4)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .inModule(QStringLiteral("visible_top"))
            .withType(QStringLiteral("logic"))
            .record();
    SemanticIndex completionIndex;
    completionIndex.setSnapshot(
        snapshotFromRecords(
            {macroPrefixCandidate,
             memberPrefixCandidate,
             visibleModule,
             visibleCandidate}));
    CompletionService::getInstance()->setSemanticIndex(&completionIndex);

    CommandCompletionQuery macroQuery;
    macroQuery.prefix = QStringLiteral("FOO");
    macroQuery.commandKind = CompletionCommandKind::Module;
    CommandCompletionQuery memberQuery;
    memberQuery.prefix = QStringLiteral("member");
    memberQuery.commandKind = CompletionCommandKind::Module;
    const auto hasExplicitCandidate = [](const CommandCompletionQuery& query,
                                         const QString& name) {
        const QList<SemanticSymbolRecord> records =
            CompletionService::getInstance()
                ->findCommandCompletionSymbolRecords(query);
        return std::any_of(
            records.cbegin(),
            records.cend(),
            [&](const SemanticSymbolRecord& record) {
                return record.name == name;
            });
    };
    expectBool("negative completion fixture has macro candidate",
               hasExplicitCandidate(macroQuery,
                                    QStringLiteral("FOO_BAR")),
               true);
    expectBool("negative completion fixture has member candidate",
               hasExplicitCandidate(memberQuery,
                                    QStringLiteral("member_item")),
               true);

    MyCodeEditor editor;
    editor.resize(560, 160);
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCompleter* completer = editor.findChild<QCompleter*>();
    expectBool("explicit-command completer remains attached",
               completer != nullptr,
               true);

    auto expectNeverAutoOpens = [&](const char* what, const QString& text) {
        editor.clear();
        if (completer)
            completer->popup()->hide();
        QTest::keyClicks(&editor, text);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        expectBool(what,
                   completer && !completer->popup()->isVisible(),
                   true);
    };

    expectNeverAutoOpens("two ordinary characters never auto-open completion",
                         QStringLiteral("ab"));
    expectNeverAutoOpens("ordinary identifier never auto-opens completion",
                         QStringLiteral("FOO"));
    expectNeverAutoOpens("macro text never auto-opens completion",
                         QStringLiteral("`FOO"));
    expectNeverAutoOpens("long macro text never auto-opens completion",
                         QStringLiteral("`FOO_BAR"));
    expectNeverAutoOpens("member access never auto-opens completion",
                         QStringLiteral("obj.member"));
    expectNeverAutoOpens("package access never auto-opens completion",
                         QStringLiteral("pkg::member"));

    MyCodeEditor legacyTriggerEditor;
    legacyTriggerEditor.setDocumentFileName(completionFile);
    legacyTriggerEditor.resize(560, 160);
    legacyTriggerEditor.show();
    legacyTriggerEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::keyClicks(&legacyTriggerEditor, ";v local_s");
    QTest::keyClick(&legacyTriggerEditor, Qt::Key_Tab);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("legacy semicolon completion trigger is inactive",
               legacyTriggerEditor.toPlainText().startsWith(
                   QStringLiteral(";v local_s"))
                   && !legacyTriggerEditor.toPlainText().contains(
                       QStringLiteral("local_signal")),
               true);

    CompletionService::getInstance()->setSemanticIndex(
        SemanticIndex::getInstance());
}

static void runIncludeCompletionRegression()
{
    auto includeProvider = [](const QString&) {
        return QStringList{
            QStringLiteral("defs.svh"),
            QStringLiteral("rtl/top_defs.svh")
        };
    };

    MyCodeEditor editor;
    editor.setIncludeFileCompletionProvider(includeProvider);
    editor.setPlainText(QString());
    expectBool("Ctrl+Space include candidates use workspace provider",
               editor.includeFileCompletionCandidates()
                   == QStringList({QStringLiteral("defs.svh"),
                                   QStringLiteral("rtl/top_defs.svh")}),
               true);
    QString insertionFailure;
    expectBool("structured header include inserts at cursor",
               editor.insertHeaderInclude(QStringLiteral("defs.svh"),
                                          &insertionFailure)
                   && editor.toPlainText()
                          == QStringLiteral("`include \"defs.svh\""),
               true);

    MyCodeEditor newHeaderEditor;
    newHeaderEditor.setPlainText(QString());
    IncludeNewHeaderRequest createdRequest;
    int createCalls = 0;
    newHeaderEditor.setIncludeNewHeaderCreator(
        [&](const IncludeNewHeaderRequest& request) {
            createdRequest = request;
            ++createCalls;
            IncludeNewHeaderResult result;
            result.success = true;
            result.includePath =
                request.fileStem + QLatin1Char('.') + request.extension;
            return result;
        });
    QString createFailure;
    expectBool("Ctrl+Space new-header route creates and inserts include",
               newHeaderEditor.createAndInsertHeader(
                   QStringLiteral("new_defs.svh"), &createFailure)
                   && createCalls == 1
                   && createdRequest.fileStem == QStringLiteral("new_defs")
                   && createdRequest.extension == QStringLiteral("svh")
                   && createdRequest.templateName == QStringLiteral("empty")
                   && newHeaderEditor.toPlainText()
                          == QStringLiteral("`include \"new_defs.svh\""),
               true);

    QTemporaryDir workspaceDir;
    expectBool("include new header workspace temp dir valid",
               workspaceDir.isValid(),
               true);
    if (!workspaceDir.isValid())
        return;
    QTabWidget tabsWidget;
    TabManager tabs(&tabsWidget);
    WorkspaceManager workspace;
    EditorCoordinator coordinator(&tabs);
    coordinator.setWorkflowDependencies(&workspace, nullptr, nullptr, nullptr);
    coordinator.connectSignals();
    expectBool("include new header opens test workspace",
               workspace.openWorkspace(workspaceDir.path()),
               true);
    IncludeNewHeaderRequest request;
    request.fileStem = QStringLiteral("created_defs");
    request.extension = QStringLiteral("svh");
    request.templateName = QStringLiteral("guard");
    request.cursorToken = QStringLiteral("__CURSOR__");
    request.templateBody =
        QStringLiteral("`ifndef CREATED_DEFS_SVH_\n"
                       "`define CREATED_DEFS_SVH_\n\n"
                       "__CURSOR__\n\n"
                       "`endif\n");
    const IncludeNewHeaderResult result =
        coordinator.createIncludeNewHeader(request);
    const QString createdPath =
        QDir(workspaceDir.path()).absoluteFilePath(
            QStringLiteral("created_defs.svh"));
    expectBool("include new header coordinator creates file",
               result.success && QFileInfo::exists(createdPath),
               true);
    expectBool("include new header coordinator opens file",
               tabs.editorCount() == 1
                   && tabs.getCurrentEditor()
                   && QDir::cleanPath(QDir::fromNativeSeparators(
                          tabs.getCurrentEditor()->documentFileName()))
                       == QDir::cleanPath(QDir::fromNativeSeparators(
                          createdPath)),
               true);
    expectBool("include new header coordinator places cursor",
               tabs.getCurrentEditor()
                   && tabs.getCurrentEditor()->textCursor().position()
                       == result.cursorPosition,
               true);

    const QByteArray createdBodyBefore = [&]() {
        QFile file(createdPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return QByteArray();
        return file.readAll();
    }();
    IncludeNewHeaderResult duplicateResult =
        coordinator.createIncludeNewHeader(request);
    const QByteArray createdBodyAfter = [&]() {
        QFile file(createdPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return QByteArray();
        return file.readAll();
    }();
    expectBool("include new header coordinator refuses overwrite",
               !duplicateResult.success
                   && duplicateResult.errorMessage.contains(
                       QStringLiteral("already exists"))
                   && !createdBodyBefore.isEmpty()
                   && createdBodyBefore == createdBodyAfter,
               true);

    const QString rtlDirPath =
        QDir(workspaceDir.path()).absoluteFilePath(QStringLiteral("rtl"));
    QDir().mkpath(rtlDirPath);
    const QString currentSourcePath =
        QDir(rtlDirPath).absoluteFilePath(QStringLiteral("current.sv"));
    QFile currentSource(currentSourcePath);
    expectBool("include new header current source fixture created",
               currentSource.open(QIODevice::WriteOnly | QIODevice::Text),
               true);
    if (currentSource.isOpen()) {
        currentSource.write("module current; endmodule\n");
        currentSource.close();
    }
    IncludeNewHeaderRequest siblingRequest;
    siblingRequest.fileStem = QStringLiteral("sibling_defs");
    siblingRequest.extension = QStringLiteral("svh");
    siblingRequest.currentFileName = currentSourcePath;
    siblingRequest.templateName = QStringLiteral("empty");
    siblingRequest.cursorToken = QStringLiteral("__CURSOR__");
    siblingRequest.templateBody = QStringLiteral("__CURSOR__\n");
    const IncludeNewHeaderResult siblingResult =
        coordinator.createIncludeNewHeader(siblingRequest);
    expectBool("include new header prefers current file directory",
               siblingResult.success
                   && QFileInfo::exists(
                       QDir(rtlDirPath).absoluteFilePath(
                           QStringLiteral("sibling_defs.svh")))
                   && !QFileInfo::exists(
                       QDir(workspaceDir.path()).absoluteFilePath(
                           QStringLiteral("sibling_defs.svh"))),
               true);
}

static void runEditorBracketRangeRegression()
{
    MyCodeEditor editor;
    editor.resize(320, 120);
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTest::keyClick(&editor, Qt::Key_BracketLeft);
    expectBool("editor inserts bracket pair",
               editor.toPlainText() == QStringLiteral("[]")
                   && editor.textCursor().position() == 1,
               true);
    QTest::keyClicks(&editor, "8");
    QTest::keyClick(&editor,
                    Qt::Key_Tab,
                    Qt::ShiftModifier);
    expectBool("editor reserves bracket expansion for plain Tab",
               editor.toPlainText() != QStringLiteral("[7:0]"),
               true);
    editor.setPlainText(QStringLiteral("[8]"));
    QTextCursor numericCursor = editor.textCursor();
    numericCursor.setPosition(2);
    editor.setTextCursor(numericCursor);
    QTest::keyClick(&editor, Qt::Key_Tab);
    expectBool("editor expands numeric bracket range",
               editor.toPlainText() == QStringLiteral("[7:0]")
                   && editor.textCursor().position()
                       == editor.toPlainText().size(),
               true);

    MyCodeEditor paramEditor;
    paramEditor.resize(320, 120);
    paramEditor.setPlainText(QStringLiteral("[P_W]"));
    QTextCursor paramCursor = paramEditor.textCursor();
    paramCursor.setPosition(QStringLiteral("[P_W").size());
    paramEditor.setTextCursor(paramCursor);
    paramEditor.show();
    paramEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::keyClick(&paramEditor, Qt::Key_Tab);
    expectBool("editor expands parameter bracket range",
               paramEditor.toPlainText()
                   == QStringLiteral("[P_W - 1:0]"),
               true);

    MyCodeEditor exactEditor;
    exactEditor.resize(320, 120);
    exactEditor.show();
    exactEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::keyClick(&exactEditor, Qt::Key_BracketLeft);
    QTest::keyClicks(&exactEditor, "PW+DW:0");
    QTest::keyClick(&exactEditor, Qt::Key_Tab);
    expectBool("editor preserves exact bracket range",
               exactEditor.toPlainText()
                       == QStringLiteral("[PW+DW:0]")
                   && exactEditor.textCursor().position()
                       == exactEditor.toPlainText().size(),
               true);

    MyCodeEditor stepEditor;
    stepEditor.resize(320, 120);
    stepEditor.setPlainText(QStringLiteral("[7:0]"));
    stepEditor.show();
    stepEditor.setFocus();
    QTextCursor stepCursor = stepEditor.textCursor();
    stepCursor.setPosition(2);
    stepEditor.setTextCursor(stepCursor);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QRect clickRect = stepEditor.cursorRect(stepCursor);
    QTest::mouseClick(stepEditor.viewport(),
                      Qt::LeftButton,
                      Qt::ControlModifier,
                      clickRect.center());
    expectBool("editor ctrl-click leaves range body unselected",
               stepEditor.textCursor().selectedText().isEmpty(),
               true);
    QTest::mouseClick(stepEditor.viewport(),
                      Qt::LeftButton,
                      Qt::AltModifier,
                      clickRect.center());
    expectBool("editor alt-click selects range body",
               stepEditor.textCursor().selectedText()
                   == QStringLiteral("7:0"),
               true);
    QTest::keyClick(&stepEditor, Qt::Key_Up);
    expectBool("editor range up increments left bound",
               stepEditor.toPlainText() == QStringLiteral("[8:0]")
                   && stepEditor.textCursor().selectedText()
                       == QStringLiteral("8:0"),
               true);
    QTest::keyClick(&stepEditor, Qt::Key_Up, Qt::ShiftModifier);
    expectBool("editor shift range up increments right bound",
               stepEditor.toPlainText() == QStringLiteral("[8:1]")
                   && stepEditor.textCursor().selectedText()
                       == QStringLiteral("8:1"),
               true);
    QTest::keyClick(&stepEditor, Qt::Key_Down, Qt::ShiftModifier);
    expectBool("editor shift range down decrements right bound",
               stepEditor.toPlainText() == QStringLiteral("[8:0]")
                   && stepEditor.textCursor().selectedText()
                       == QStringLiteral("8:0"),
               true);

    auto selectRangeBody = [](MyCodeEditor& rangeEditor) {
        QTextCursor cursor = rangeEditor.textCursor();
        cursor.setPosition(1);
        cursor.setPosition(rangeEditor.toPlainText().size() - 1,
                           QTextCursor::KeepAnchor);
        rangeEditor.setTextCursor(cursor);
    };
    auto showRangeEditor = [](MyCodeEditor& rangeEditor,
                              const QString& text) {
        rangeEditor.resize(360, 120);
        rangeEditor.setPlainText(text);
        rangeEditor.show();
        rangeEditor.setFocus();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    };

    MyCodeEditor parameterUpEditor;
    showRangeEditor(parameterUpEditor, QStringLiteral("[P_TEST:0]"));
    selectRangeBody(parameterUpEditor);
    QTest::keyClick(&parameterUpEditor, Qt::Key_Up);
    expectBool("editor parameter range up adds one",
               parameterUpEditor.toPlainText()
                   == QStringLiteral("[P_TEST+1:0]"),
               true);

    MyCodeEditor parameterDownEditor;
    showRangeEditor(parameterDownEditor, QStringLiteral("[P_TEST:0]"));
    selectRangeBody(parameterDownEditor);
    QTest::keyClick(&parameterDownEditor, Qt::Key_Down);
    expectBool("editor parameter range down subtracts one",
               parameterDownEditor.toPlainText()
                   == QStringLiteral("[P_TEST-1:0]"),
               true);

    MyCodeEditor complexDownEditor;
    showRangeEditor(complexDownEditor,
                    QStringLiteral("[P_TEST0*P_TEST1:0]"));
    selectRangeBody(complexDownEditor);
    QTest::keyClick(&complexDownEditor, Qt::Key_Down);
    expectBool("editor complex range down parenthesizes expression",
               complexDownEditor.toPlainText()
                   == QStringLiteral("[(P_TEST0*P_TEST1)-1:0]"),
               true);

    MyCodeEditor numericGuardEditor;
    showRangeEditor(numericGuardEditor, QStringLiteral("[0:0]"));
    selectRangeBody(numericGuardEditor);
    QTest::keyClick(&numericGuardEditor, Qt::Key_Down);
    expectBool("editor numeric range down respects right bound",
               numericGuardEditor.toPlainText() == QStringLiteral("[0:0]"),
               true);
}

static void runEditorSmartSelectionRegression()
{
    MyCodeEditor editor;
    editor.resize(520, 140);
    editor.setPlainText(
        QStringLiteral("assign y = ((test_s.test_m == 1) & (test_data == 1));\n"));
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(editor.toPlainText().indexOf(QStringLiteral("test_m")) + 2);
    editor.setTextCursor(cursor);
    QTest::keyClick(&editor, Qt::Key_W, Qt::ControlModifier);
    expectBool("smart selection starts with symbol",
               editor.textCursor().selectedText() == QStringLiteral("test_m"),
               true);
    QTest::keyClick(&editor, Qt::Key_W, Qt::ControlModifier);
    expectBool("smart selection expands to hierarchical expression",
               editor.textCursor().selectedText()
                   == QStringLiteral("test_s.test_m"),
               true);
    QTest::keyClick(&editor, Qt::Key_W, Qt::ControlModifier);
    expectBool("smart selection expands to inner parentheses",
               editor.textCursor().selectedText()
                   == QStringLiteral("test_s.test_m == 1"),
               true);
    QTest::keyClick(&editor, Qt::Key_W, Qt::ControlModifier);
    expectBool("smart selection expands to outer parentheses",
               editor.textCursor().selectedText()
                   == QStringLiteral(
                       "(test_s.test_m == 1) & (test_data == 1)"),
               true);

    QTextCursor numberCursor = editor.textCursor();
    numberCursor.clearSelection();
    numberCursor.setPosition(editor.toPlainText().lastIndexOf(QLatin1Char('1')));
    editor.setTextCursor(numberCursor);
    QTest::keyClick(&editor, Qt::Key_W, Qt::ControlModifier);
    expectBool("smart selection starts operator number at parentheses",
               editor.textCursor().selectedText()
                   == QStringLiteral("test_data == 1"),
               true);
}

static void runEditorOccurrenceNavigationRegression()
{
    MyCodeEditor editor;
    editor.resize(520, 140);
    editor.setPlainText(
        QStringLiteral("assign foo = foo + bar;\nassign bar = foo;\n"));
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    const QString text = editor.toPlainText();
    const int firstFoo = text.indexOf(QStringLiteral("foo"));
    const int secondFoo = text.indexOf(QStringLiteral("foo"), firstFoo + 1);
    const int thirdFoo = text.indexOf(QStringLiteral("foo"), secondFoo + 1);
    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(firstFoo);
    cursor.setPosition(firstFoo + 3, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);

    QTest::keyClick(&editor, Qt::Key_E, Qt::ControlModifier);
    expectBool("occurrence navigation next selects second occurrence",
               editor.textCursor().selectionStart() == secondFoo
                   && editor.textCursor().selectedText()
                          == QStringLiteral("foo"),
               true);
    QTest::keyClick(&editor, Qt::Key_E, Qt::ControlModifier);
    expectBool("occurrence navigation next selects third occurrence",
               editor.textCursor().selectionStart() == thirdFoo
                   && editor.textCursor().selectedText()
                          == QStringLiteral("foo"),
               true);
    QTest::keyClick(&editor, Qt::Key_E, Qt::ControlModifier);
    expectBool("occurrence navigation next wraps",
               editor.textCursor().selectionStart() == firstFoo
                   && editor.textCursor().selectedText()
                          == QStringLiteral("foo"),
               true);
    QTest::keyClick(&editor, Qt::Key_Q, Qt::ControlModifier);
    expectBool("occurrence navigation previous wraps",
               editor.textCursor().selectionStart() == thirdFoo
                   && editor.textCursor().selectedText()
                          == QStringLiteral("foo"),
               true);
}

static void runEditorRegistryRenameAdapterRegression()
{
    MyCodeEditor editor;
    editor.resize(520, 160);
    editor.setPlainText(
        QStringLiteral("logic oldName;\n"
                       "assign oldName_next = oldName;\n"
                       "assign sink = oldName;\n"));
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextCursor cursor = editor.textCursor();
    const int firstOldName = editor.toPlainText().indexOf(QStringLiteral("oldName"));
    cursor.setPosition(firstOldName);
    cursor.setPosition(firstOldName + QStringLiteral("oldName").size(),
                       QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);

    const QString before = editor.toPlainText();
    QString statusMessage;
    QObject::connect(
        &editor,
        &MyCodeEditor::editorStatusMessageRequested,
        &editor,
        [&statusMessage](const QString& message) {
            statusMessage = message;
        });
    QTest::keyClick(&editor, Qt::Key_R, Qt::ControlModifier);
    expectBool("Ctrl+R rejects an editor without current semantic identity",
               !statusMessage.isEmpty()
                   && editor.findChild<QLineEdit*>(
                          QStringLiteral(
                              "semanticRenameInlineEditor"))
                          == nullptr,
               true);
    expectBool("failed Ctrl+R performs no editor-local rename",
               editor.toPlainText() == before
                   && QApplication::activeModalWidget()
                          == nullptr,
               true);
}

static void runEditorColumnEditRegression()
{
    MyCodeEditor boundaryEditor;
    EditorAppearance().apply(&boundaryEditor);
    boundaryEditor.resize(900, 180);
    const QString boundaryLine = QStringLiteral(
        "        rx_step_len[i]        <= 'd0;");
    boundaryEditor.setPlainText(
        boundaryLine + QLatin1Char('\n')
        + boundaryLine + QLatin1Char('\n'));
    boundaryEditor.show();
    boundaryEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QTextBlock boundaryFirst =
        boundaryEditor.document()->findBlockByNumber(0);
    const QTextBlock boundaryLast =
        boundaryEditor.document()->findBlockByNumber(1);
    bool asciiBoundariesRemainExact =
        boundaryFirst.isValid() && boundaryLast.isValid();
    for (int offset = 0;
         asciiBoundariesRemainExact
         && offset <= boundaryLine.size();
         ++offset) {
        asciiBoundariesRemainExact =
            EditorVisualColumnGeometry::visualColumnForOffset(
                &boundaryEditor, boundaryFirst, offset)
            == offset;
    }
    expectBool("column geometry preserves every ASCII insertion boundary",
               asciiBoundariesRemainExact,
               true);

    QTextCursor boundaryProbe(boundaryFirst);
    boundaryProbe.setPosition(
        boundaryFirst.position() + boundaryLine.size());
    const qreal boundaryEndX =
        boundaryEditor.cursorRect(boundaryProbe).left();
    const qreal boundaryCell =
        EditorVisualColumnGeometry::spaceAdvance(&boundaryEditor);
    expectBool("column mouse geometry snaps the final half-cell to EOL",
               EditorVisualColumnGeometry::visualColumnForViewportX(
                   &boundaryEditor,
                   boundaryFirst,
                   boundaryEndX - boundaryCell * 0.4)
                   == boundaryLine.size()
                   && EditorVisualColumnGeometry::visualColumnForViewportX(
                          &boundaryEditor,
                          boundaryFirst,
                          boundaryEndX + boundaryCell * 0.4)
                          == boundaryLine.size(),
               true);

    QTextCursor boundaryStart(boundaryFirst);
    boundaryStart.setPosition(
        boundaryFirst.position() + boundaryLine.size());
    QTextCursor boundaryEnd(boundaryLast);
    boundaryEnd.setPosition(
        boundaryLast.position() + boundaryLine.size());
    boundaryEditor.setTextCursor(boundaryStart);
    QTest::mouseClick(
        boundaryEditor.viewport(),
        Qt::LeftButton,
        Qt::ShiftModifier | Qt::AltModifier,
        boundaryEditor.cursorRect(boundaryEnd).center());
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const EditorColumnModeSnapshot boundarySnapshot =
        boundaryEditor.state->columnMode.snapshotForTest();
    expectBool("column caret after semicolon stays at the real EOL boundary",
               boundarySnapshot.selectionActive
                   && boundarySnapshot.anchorColumn
                          == boundaryLine.size()
                   && boundarySnapshot.currentColumn
                          == boundaryLine.size()
                   && EditorVisualColumnGeometry::viewportXForVisualColumn(
                          &boundaryEditor,
                          boundaryLast,
                          boundarySnapshot.currentColumn)
                          == boundaryEditor.cursorRect(boundaryEnd).left(),
               true);

    MyCodeEditor editor;
    editor.resize(480, 180);
    editor.setPlainText(QStringLiteral("abc\nabc\nabc\n"));
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextBlock firstBlock = editor.document()->findBlockByNumber(0);
    QTextBlock lastBlock = editor.document()->findBlockByNumber(2);
    QTextCursor startCursor(firstBlock);
    startCursor.setPosition(firstBlock.position() + 1);
    QTextCursor endCursor(lastBlock);
    endCursor.setPosition(lastBlock.position() + 1);
    const QPoint endPoint = editor.cursorRect(endCursor).center();
    const Qt::KeyboardModifiers columnModifiers =
        Qt::ShiftModifier | Qt::AltModifier;

    editor.setTextCursor(startCursor);
    QTest::mouseClick(editor.viewport(),
                      Qt::LeftButton,
                      columnModifiers,
                      endPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTest::keyClicks(&editor, "X");
    expectBool("editor column mode inserts on each selected line",
               editor.toPlainText() == QStringLiteral("aXbc\naXbc\naXbc\n"),
               true);

    QTest::keyClick(&editor, Qt::Key_Backspace);
    expectBool("editor column mode backspace edits each line",
               editor.toPlainText() == QStringLiteral("abc\nabc\nabc\n"),
               true);

    QTest::keyClicks(&editor, "Y");
    expectBool("editor column mode remains active after backspace",
               editor.toPlainText() == QStringLiteral("aYbc\naYbc\naYbc\n"),
               true);

    QTest::keyClick(&editor, Qt::Key_Left);
    QTest::keyClicks(&editor, "Z");
    expectBool("editor column mode plain arrow moves vertical cursor",
               editor.toPlainText()
                   == QStringLiteral("aZYbc\naZYbc\naZYbc\n"),
               true);

    QTest::keyClick(&editor, Qt::Key_Backspace);
    expectBool("editor column mode backspace remains multi-line after move",
               editor.toPlainText() == QStringLiteral("aYbc\naYbc\naYbc\n"),
               true);

    QTest::keyClick(&editor,
                    Qt::Key_Right,
                    columnModifiers);
    QTest::keyClick(&editor, Qt::Key_Right);
    QTest::keyClicks(&editor, "Q");
    expectBool("editor column mode adjusts then moves rectangular selection",
               editor.toPlainText() == QStringLiteral("aYQc\naYQc\naYQc\n"),
               true);

    const QPoint plainClickPoint =
        editor.cursorRect(editor.textCursor()).center();
    QTest::mouseClick(editor.viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      plainClickPoint);
    QTest::keyClicks(&editor, "Z");
    expectBool("editor plain click exits column mode",
               editor.toPlainText().count(QLatin1Char('Z')) == 1,
               true);

    MyCodeEditor virtualEditor;
    virtualEditor.resize(480, 180);
    virtualEditor.setPlainText(QStringLiteral("a\nab\nabc\n"));
    virtualEditor.show();
    virtualEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextBlock virtualFirstBlock =
        virtualEditor.document()->findBlockByNumber(0);
    QTextBlock virtualLastBlock =
        virtualEditor.document()->findBlockByNumber(2);
    QTextCursor virtualStartCursor(virtualFirstBlock);
    virtualStartCursor.setPosition(virtualFirstBlock.position() + 1);
    QTextCursor virtualEndCursor(virtualLastBlock);
    virtualEndCursor.setPosition(virtualLastBlock.position() + 1);
    const QPoint virtualEndPoint =
        virtualEditor.cursorRect(virtualEndCursor).center();

    virtualEditor.setTextCursor(virtualStartCursor);
    QTest::mouseClick(virtualEditor.viewport(),
                      Qt::LeftButton,
                      columnModifiers,
                      virtualEndPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    int broadColumnHighlights = 0;
    for (const QTextEdit::ExtraSelection& selection :
         virtualEditor.extraSelections()) {
        if (selection.format.background().style() != Qt::NoBrush
            && selection.format.background().color()
                   == QColor(37, 99, 235, 80)
            && selection.cursor.hasSelection()) {
            ++broadColumnHighlights;
        }
    }
    expectBool("editor zero-width column mode avoids broad highlight",
               broadColumnHighlights == 0,
               true);

    for (int i = 0; i < 4; ++i)
        QTest::keyClick(&virtualEditor, Qt::Key_Right);
    QTest::keyClicks(&virtualEditor, "X");
    expectBool("editor zero-width column mode pads virtual columns",
               virtualEditor.toPlainText()
                   == QStringLiteral("a    X\nab   X\nabc  X\n"),
               true);

    MyCodeEditor clipboardEditor;
    clipboardEditor.resize(480, 180);
    clipboardEditor.setPlainText(QStringLiteral("abc\nabc\nabc\n"));
    clipboardEditor.show();
    clipboardEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextBlock clipFirstBlock =
        clipboardEditor.document()->findBlockByNumber(0);
    QTextBlock clipLastBlock =
        clipboardEditor.document()->findBlockByNumber(2);
    QTextCursor clipStartCursor(clipFirstBlock);
    clipStartCursor.setPosition(clipFirstBlock.position() + 1);
    QTextCursor clipEndCursor(clipLastBlock);
    clipEndCursor.setPosition(clipLastBlock.position() + 2);
    clipboardEditor.setTextCursor(clipStartCursor);
    QTest::mouseClick(clipboardEditor.viewport(),
                      Qt::LeftButton,
                      columnModifiers,
                      clipboardEditor.cursorRect(clipEndCursor).center());
    QTest::keyClick(&clipboardEditor, Qt::Key_C, Qt::ControlModifier);
    expectBool("editor rectangular copy uses column rows",
               QApplication::clipboard()->text()
                   == QStringLiteral("b\nb\nb"),
               true);
    QTest::keyClick(&clipboardEditor, Qt::Key_X, Qt::ControlModifier);
    expectBool("editor rectangular cut edits each selected line",
               clipboardEditor.toPlainText()
                   == QStringLiteral("ac\nac\nac\n"),
               true);
    QApplication::clipboard()->setText(QStringLiteral("XY"));
    QTest::keyClick(&clipboardEditor, Qt::Key_V, Qt::ControlModifier);
    expectBool("editor rectangular paste repeats one clipboard row",
               clipboardEditor.toPlainText()
                   == QStringLiteral("aXYc\naXYc\naXYc\n"),
               true);

    MyCodeEditor tabColumnEditor;
    tabColumnEditor.resize(520, 180);
    const QFontMetrics tabMetrics(tabColumnEditor.font());
    tabColumnEditor.setTabStopDistance(
        tabMetrics.horizontalAdvance(QLatin1Char(' ')) * 4);
    tabColumnEditor.setPlainText(QStringLiteral("\tfoo\n    foo\n\tfoo\n"));
    tabColumnEditor.show();
    tabColumnEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextBlock tabFirstBlock =
        tabColumnEditor.document()->findBlockByNumber(0);
    QTextBlock tabLastBlock =
        tabColumnEditor.document()->findBlockByNumber(2);
    QTextCursor tabStartCursor(tabFirstBlock);
    tabStartCursor.setPosition(tabFirstBlock.position() + 1);
    QTextCursor tabEndCursor(tabLastBlock);
    tabEndCursor.setPosition(tabLastBlock.position() + 1);
    tabColumnEditor.setTextCursor(tabStartCursor);
    QTest::mouseClick(tabColumnEditor.viewport(),
                      Qt::LeftButton,
                      columnModifiers,
                      tabColumnEditor.cursorRect(tabEndCursor).center());
    QTest::keyClicks(&tabColumnEditor, "X");
    expectBool("editor column mode inserts by visual column through tabs",
               tabColumnEditor.toPlainText()
                   == QStringLiteral("\tXfoo\n    Xfoo\n\tXfoo\n"),
               true);

    MyCodeEditor tabKeyEditor;
    tabKeyEditor.resize(520, 160);
    tabKeyEditor.setTabStopDistance(
        tabMetrics.horizontalAdvance(QLatin1Char(' ')) * 4);
    tabKeyEditor.setPlainText(QStringLiteral("\tfoo\n    foo\n"));
    tabKeyEditor.show();
    tabKeyEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTextBlock tabKeyFirst = tabKeyEditor.document()->findBlockByNumber(0);
    QTextBlock tabKeyLast = tabKeyEditor.document()->findBlockByNumber(1);
    QTextCursor tabKeyStart(tabKeyFirst);
    tabKeyStart.setPosition(tabKeyFirst.position() + 1);
    QTextCursor tabKeyEnd(tabKeyLast);
    tabKeyEnd.setPosition(tabKeyLast.position() + 4);
    tabKeyEditor.setTextCursor(tabKeyStart);
    QTest::mouseClick(tabKeyEditor.viewport(),
                      Qt::LeftButton,
                      columnModifiers,
                      tabKeyEditor.cursorRect(tabKeyEnd).center());
    QTest::keyClick(&tabKeyEditor, Qt::Key_Tab);
    expectBool("editor column mode Tab inserts spaces to next visual tab stop",
               tabKeyEditor.toPlainText()
                   == QStringLiteral("\t    foo\n        foo\n"),
               true);
    QTest::keyClick(&tabKeyEditor,
                    Qt::Key_Tab,
                    Qt::ShiftModifier);
    expectBool("editor column mode Tab+Shift is captured as visual outdent",
               tabKeyEditor.toPlainText()
                   == QStringLiteral("\tfoo\n    foo\n"),
               true);

    const QFont fixedFont =
        QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const auto spaceAdvance = [](const MyCodeEditor& target) {
        return qMax<qreal>(
            1.0,
            QFontMetricsF(target.font())
                .horizontalAdvance(QLatin1Char(' ')));
    };
    const auto visualColumnAtLineEnd =
        [&](const MyCodeEditor& target, int line) {
            const QTextBlock block =
                target.document()->findBlockByNumber(line);
            QTextCursor start(block);
            start.setPosition(block.position());
            QTextCursor end(block);
            end.setPosition(
                block.position() + block.text().size());
            return qMax(
                0,
                qRound((target.cursorRect(end).left()
                        - target.cursorRect(start).left())
                       / spaceAdvance(target)));
        };
    const auto pointAtVisualColumn =
        [&](const MyCodeEditor& target, int line, int column) {
            const QTextBlock block =
                target.document()->findBlockByNumber(line);
            QTextCursor start(block);
            start.setPosition(block.position());
            const QRect rowRect = target.cursorRect(start);
            return QPoint(
                qRound(rowRect.left()
                       + qMax(0, column)
                             * spaceAdvance(target)),
                rowRect.center().y());
        };

    MyCodeEditor ordinaryVirtualEditor;
    ordinaryVirtualEditor.resize(520, 180);
    ordinaryVirtualEditor.setLineWrapMode(QPlainTextEdit::NoWrap);
    ordinaryVirtualEditor.setFont(fixedFont);
    ordinaryVirtualEditor.setPlainText(
        QStringLiteral("123456\n12345\n"));
    ordinaryVirtualEditor.document()->setModified(false);
    ordinaryVirtualEditor.show();
    ordinaryVirtualEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    const int firstEndVisual =
        visualColumnAtLineEnd(ordinaryVirtualEditor, 0);
    const int firstTargetVisual = firstEndVisual + 3;
    QTextBlock ordinaryFirstBlock =
        ordinaryVirtualEditor.document()->findBlockByNumber(0);
    QTextCursor ordinaryFirstEnd(ordinaryFirstBlock);
    ordinaryFirstEnd.setPosition(
        ordinaryFirstBlock.position()
        + ordinaryFirstBlock.text().size());
    ordinaryVirtualEditor.setTextCursor(ordinaryFirstEnd);
    const QPoint firstVirtualPoint =
        pointAtVisualColumn(
            ordinaryVirtualEditor, 0, firstTargetVisual);
    QTest::mouseClick(ordinaryVirtualEditor.viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      firstVirtualPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    expectBool("ordinary click beyond EOL clamps to the real EOL",
               !ordinaryVirtualEditor.virtualCursorActiveForTest()
                   && ordinaryVirtualEditor.textCursor().position()
                          == ordinaryFirstBlock.position()
                                 + ordinaryFirstBlock.text().size()
                   && ordinaryVirtualEditor.toPlainText()
                          == QStringLiteral("123456\n12345\n")
                   && !ordinaryVirtualEditor.document()->isModified(),
               true);

    ordinaryVirtualEditor.viewport()->repaint();
    const QImage beforeVirtualClick =
        renderWidgetImage(ordinaryVirtualEditor.viewport());
    QTest::mouseClick(ordinaryVirtualEditor.viewport(),
                      Qt::LeftButton,
                      Qt::AltModifier,
                      firstVirtualPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QImage afterVirtualClick =
        renderWidgetImage(ordinaryVirtualEditor.viewport());
    const QRect virtualOverlayDiff =
        differentPixelBounds(beforeVirtualClick, afterVirtualClick);
    const QRect firstVirtualBand(
        ordinaryVirtualEditor.cursorRect(ordinaryFirstEnd).left(),
        ordinaryVirtualEditor.cursorRect(ordinaryFirstEnd).top(),
        qMax(1,
             firstVirtualPoint.x()
                 - ordinaryVirtualEditor.cursorRect(
                       ordinaryFirstEnd).left() + 3),
        ordinaryVirtualEditor.cursorRect(ordinaryFirstEnd).height());

    expectBool("Alt+click beyond EOL records a virtual cursor",
               ordinaryVirtualEditor.virtualCursorActiveForTest()
                   && ordinaryVirtualEditor.virtualCursorLineForTest() == 0
                   && ordinaryVirtualEditor.virtualCursorColumnForTest()
                          == firstTargetVisual,
               true);
    expectBool("explicit virtual click does not modify the document",
               ordinaryVirtualEditor.toPlainText()
                       == QStringLiteral("123456\n12345\n")
                   && !ordinaryVirtualEditor.document()->isModified()
                   && ordinaryVirtualEditor.textCursor().position()
                          == ordinaryFirstBlock.position()
                                 + ordinaryFirstBlock.text().size(),
               true);
    expectBool("explicit virtual cursor paints only an overlay",
               !virtualOverlayDiff.isNull()
                   && virtualOverlayDiff.intersects(firstVirtualBand),
               true);

    QTest::keyClick(&ordinaryVirtualEditor, Qt::Key_Backspace);
    expectBool("Backspace moves left inside the virtual region",
               ordinaryVirtualEditor.virtualCursorActiveForTest()
                   && ordinaryVirtualEditor.virtualCursorColumnForTest()
                          == firstTargetVisual - 1
                   && ordinaryVirtualEditor.toPlainText()
                          == QStringLiteral("123456\n12345\n")
                   && !ordinaryVirtualEditor.document()->isModified(),
               true);
    QTest::keyClick(&ordinaryVirtualEditor, Qt::Key_Right);
    expectBool("Right moves right inside the virtual region",
               ordinaryVirtualEditor.virtualCursorActiveForTest()
                   && ordinaryVirtualEditor.virtualCursorColumnForTest()
                          == firstTargetVisual
                   && ordinaryVirtualEditor.toPlainText()
                          == QStringLiteral("123456\n12345\n")
                   && !ordinaryVirtualEditor.document()->isModified(),
               true);
    QApplication::clipboard()->setText(
        QStringLiteral("virtual-copy-sentinel"));
    QTest::keyClick(&ordinaryVirtualEditor,
                    Qt::Key_C,
                    Qt::ControlModifier);
    expectBool("copy excludes a standalone virtual region",
               ordinaryVirtualEditor.virtualCursorActiveForTest()
                   && ordinaryVirtualEditor.toPlainText()
                          == QStringLiteral("123456\n12345\n")
                   && !ordinaryVirtualEditor.document()->isModified(),
               true);

    QTest::keyClicks(&ordinaryVirtualEditor, "X");
    expectBool("typing materializes spaces only at the virtual cursor",
               ordinaryVirtualEditor.toPlainText()
                   == QStringLiteral("123456")
                          + QString(firstTargetVisual - firstEndVisual,
                                    QLatin1Char(' '))
                          + QStringLiteral("X\n12345\n")
                   && !ordinaryVirtualEditor.virtualCursorActiveForTest(),
               true);

    ordinaryVirtualEditor.setPlainText(
        QStringLiteral("123456\n12345\n"));
    ordinaryVirtualEditor.document()->setModified(false);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const int secondEndVisual =
        visualColumnAtLineEnd(ordinaryVirtualEditor, 1);
    const int secondTargetVisual = secondEndVisual + 4;
    QTest::mouseClick(
        ordinaryVirtualEditor.viewport(),
        Qt::LeftButton,
        Qt::AltModifier,
        pointAtVisualColumn(
            ordinaryVirtualEditor, 1, secondTargetVisual));
    QApplication::clipboard()->setText(QStringLiteral("YZ"));
    QTest::keyClick(&ordinaryVirtualEditor,
                    Qt::Key_V,
                    Qt::ControlModifier);
    expectBool("paste materializes virtual padding before clipboard text",
               ordinaryVirtualEditor.toPlainText()
                   == QStringLiteral("123456\n12345")
                          + QString(secondTargetVisual - secondEndVisual,
                                    QLatin1Char(' '))
                          + QStringLiteral("YZ\n"),
               true);

    ordinaryVirtualEditor.setPlainText(
        QStringLiteral("123456\n12345\n"));
    ordinaryVirtualEditor.document()->setModified(false);
    QTextBlock resetFirst =
        ordinaryVirtualEditor.document()->findBlockByNumber(0);
    QTextCursor resetEnd(resetFirst);
    resetEnd.setPosition(
        resetFirst.position() + resetFirst.text().size());
    ordinaryVirtualEditor.setTextCursor(resetEnd);
    QTest::keyClick(&ordinaryVirtualEditor, Qt::Key_Right);
    expectBool("Right at a real EOL keeps ordinary Qt cursor semantics",
               !ordinaryVirtualEditor.virtualCursorActiveForTest()
                   && ordinaryVirtualEditor.textCursor().blockNumber() == 1
                   && ordinaryVirtualEditor.textCursor().positionInBlock() == 0
                   && ordinaryVirtualEditor.toPlainText()
                          == QStringLiteral("123456\n12345\n")
                   && !ordinaryVirtualEditor.document()->isModified(),
               true);
    QTest::mouseClick(
        ordinaryVirtualEditor.viewport(),
        Qt::LeftButton,
        Qt::AltModifier,
        pointAtVisualColumn(
            ordinaryVirtualEditor,
            0,
            visualColumnAtLineEnd(ordinaryVirtualEditor, 0) + 1));
    QTest::keyClick(&ordinaryVirtualEditor, Qt::Key_Left);
    expectBool("Left returns from an explicit virtual column to real EOL",
               !ordinaryVirtualEditor.virtualCursorActiveForTest()
                   && ordinaryVirtualEditor.toPlainText()
                          == QStringLiteral("123456\n12345\n")
                   && !ordinaryVirtualEditor.document()->isModified(),
               true);

    QTest::mouseClick(
        ordinaryVirtualEditor.viewport(),
        Qt::LeftButton,
        Qt::AltModifier,
        pointAtVisualColumn(
            ordinaryVirtualEditor, 0, firstTargetVisual));
    ordinaryVirtualEditor.undo();
    expectBool("undo clears a pending virtual offset without editing",
               !ordinaryVirtualEditor.virtualCursorActiveForTest()
                   && ordinaryVirtualEditor.toPlainText()
                          == QStringLiteral("123456\n12345\n"),
               true);
    QTest::mouseClick(
        ordinaryVirtualEditor.viewport(),
        Qt::LeftButton,
        Qt::AltModifier,
        pointAtVisualColumn(
            ordinaryVirtualEditor, 0, firstTargetVisual));
    ordinaryVirtualEditor.setDocumentFileName(
        QDir::temp().filePath(
            QStringLiteral("zeroslack_virtual_switch.sv")));
    expectBool("file identity change clears a pending virtual offset",
               !ordinaryVirtualEditor.virtualCursorActiveForTest(),
               true);
    QTest::mouseClick(
        ordinaryVirtualEditor.viewport(),
        Qt::LeftButton,
        Qt::AltModifier,
        pointAtVisualColumn(
            ordinaryVirtualEditor, 0, firstTargetVisual));
    QTextCursor realJump(
        ordinaryVirtualEditor.document()
            ->findBlockByNumber(0));
    realJump.setPosition(realJump.block().position() + 2);
    ordinaryVirtualEditor.setTextCursor(realJump);
    expectBool("real cursor jump clears a pending virtual offset",
               !ordinaryVirtualEditor.virtualCursorActiveForTest(),
               true);

    MyCodeEditor layoutVirtualEditor;
    layoutVirtualEditor.resize(520, 140);
    layoutVirtualEditor.setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont zoomedFixedFont = fixedFont;
    zoomedFixedFont.setPointSize(
        qMax(12, zoomedFixedFont.pointSize() + 4));
    layoutVirtualEditor.setFont(zoomedFixedFont);
    layoutVirtualEditor.setTabStopDistance(
        spaceAdvance(layoutVirtualEditor) * 4.0);
    layoutVirtualEditor.setPlainText(
        QStringLiteral("\t中x\n"));
    layoutVirtualEditor.document()->setModified(false);
    layoutVirtualEditor.show();
    layoutVirtualEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    // A four-column tab and two characters occupy six logical columns,
    // regardless of the fallback font's CJK glyph width.
    const int layoutEndVisual = 6;
    const QTextBlock unicodeBlock = layoutVirtualEditor.document()->firstBlock();
    QTextCursor unicodeEnd(unicodeBlock);
    unicodeEnd.setPosition(unicodeBlock.position() + unicodeBlock.text().size());
    const QRect unicodeEndRect = layoutVirtualEditor.cursorRect(unicodeEnd);
    QTest::mouseClick(
        layoutVirtualEditor.viewport(),
        Qt::LeftButton,
        Qt::AltModifier,
        QPoint(qRound(unicodeEndRect.left() + 2 * spaceAdvance(layoutVirtualEditor)),
               unicodeEndRect.center().y()));
    expectBool("virtual columns anchor to the rendered line end for Tab Unicode and zoom",
               layoutVirtualEditor.virtualCursorActiveForTest()
                   && layoutVirtualEditor.virtualCursorColumnForTest()
                          == layoutEndVisual + 2
                   && layoutVirtualEditor.toPlainText()
                          == QStringLiteral("\t中x\n"),
               true);
    QTest::keyClicks(&layoutVirtualEditor, "K");
    expectBool("Unicode line materializes only layout-derived padding",
               layoutVirtualEditor.toPlainText()
                   == QStringLiteral("\t中x  K\n"),
               true);

    MyCodeEditor scrolledVirtualEditor;
    scrolledVirtualEditor.resize(360, 140);
    scrolledVirtualEditor.setLineWrapMode(QPlainTextEdit::NoWrap);
    scrolledVirtualEditor.setFont(fixedFont);
    scrolledVirtualEditor.setPlainText(
        QString(100, QLatin1Char('a'))
        + QStringLiteral("\n123456789012345678901234\n"));
    scrolledVirtualEditor.show();
    scrolledVirtualEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    scrolledVirtualEditor.horizontalScrollBar()->setValue(
        qMin(scrolledVirtualEditor.horizontalScrollBar()->maximum(),
             qRound(spaceAdvance(scrolledVirtualEditor) * 7.0)));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const int scrolledEndVisual =
        visualColumnAtLineEnd(scrolledVirtualEditor, 1);
    const QPoint scrolledTarget =
        pointAtVisualColumn(
            scrolledVirtualEditor, 1, scrolledEndVisual + 2);
    QTest::mouseClick(scrolledVirtualEditor.viewport(),
                      Qt::LeftButton,
                      Qt::AltModifier,
                      scrolledTarget);
    expectBool("virtual click remains layout-correct when horizontally scrolled",
               scrolledVirtualEditor.horizontalScrollBar()->value() > 0
                   && scrolledVirtualEditor.virtualCursorActiveForTest()
                   && scrolledVirtualEditor.virtualCursorColumnForTest()
                          == scrolledEndVisual + 2,
               true);

    MyCodeEditor virtualColumnClickEditor;
    virtualColumnClickEditor.resize(520, 160);
    virtualColumnClickEditor.setLineWrapMode(QPlainTextEdit::NoWrap);
    virtualColumnClickEditor.setFont(fixedFont);
    virtualColumnClickEditor.setPlainText(
        QStringLiteral("123456\n12345\n"));
    virtualColumnClickEditor.document()->setModified(false);
    virtualColumnClickEditor.show();
    virtualColumnClickEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const int sharedVirtualColumn = 9;
    const QImage beforeVirtualColumn =
        renderWidgetImage(virtualColumnClickEditor.viewport());
    QTest::mouseClick(
        virtualColumnClickEditor.viewport(),
        Qt::LeftButton,
        Qt::AltModifier,
        pointAtVisualColumn(
            virtualColumnClickEditor, 0, sharedVirtualColumn));
    QTest::mouseClick(
        virtualColumnClickEditor.viewport(),
        Qt::LeftButton,
        columnModifiers,
        pointAtVisualColumn(
            virtualColumnClickEditor, 1, sharedVirtualColumn));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QImage afterVirtualColumn =
        renderWidgetImage(virtualColumnClickEditor.viewport());
    const QRect virtualColumnOverlayDiff =
        differentPixelBounds(
            beforeVirtualColumn, afterVirtualColumn);
    const QRect firstRowRect =
        virtualColumnClickEditor.cursorRect(
            QTextCursor(
                virtualColumnClickEditor.document()
                    ->findBlockByNumber(0)));
    const QRect secondRowRect =
        virtualColumnClickEditor.cursorRect(
            QTextCursor(
                virtualColumnClickEditor.document()
                    ->findBlockByNumber(1)));
    const EditorColumnModeSnapshot
        virtualColumnSelection =
            virtualColumnClickEditor.state
                ->columnMode.snapshotForTest();
    expectBool("Alt virtual start plus Shift+Alt click forms column mode",
               virtualColumnClickEditor.columnSelectionActive()
                   && virtualColumnSelection.anchorLine == 0
                   && virtualColumnSelection.currentLine == 1
                   && virtualColumnSelection.anchorColumn
                          == sharedVirtualColumn
                   && virtualColumnSelection.currentColumn
                          == sharedVirtualColumn
                   && virtualColumnClickEditor.toPlainText()
                          == QStringLiteral("123456\n12345\n"),
               true);
    expectBool("column virtual overlay covers every selected row",
               !virtualColumnOverlayDiff.isNull()
                   && virtualColumnOverlayDiff.intersects(firstRowRect)
                   && virtualColumnOverlayDiff.intersects(secondRowRect),
               true);
    QTest::mouseClick(
        virtualColumnClickEditor.viewport(),
        Qt::LeftButton,
        columnModifiers,
        pointAtVisualColumn(
            virtualColumnClickEditor, 1, sharedVirtualColumn - 1));
    const EditorColumnModeSnapshot
        adjustedVirtualColumnSelection =
            virtualColumnClickEditor.state
                ->columnMode.snapshotForTest();
    expectBool("subsequent Shift+Alt click adjusts only the endpoint",
               adjustedVirtualColumnSelection.anchorLine == 0
                   && adjustedVirtualColumnSelection.anchorColumn
                          == sharedVirtualColumn
                   && adjustedVirtualColumnSelection.currentLine == 1
                   && adjustedVirtualColumnSelection.currentColumn
                          == sharedVirtualColumn - 1,
               true);
    QTest::mouseClick(
        virtualColumnClickEditor.viewport(),
        Qt::LeftButton,
        columnModifiers,
        pointAtVisualColumn(
            virtualColumnClickEditor, 1, sharedVirtualColumn));
    QApplication::clipboard()->setText(
        QStringLiteral("column-virtual-sentinel"));
    QTest::keyClick(&virtualColumnClickEditor,
                    Qt::Key_C,
                    Qt::ControlModifier);
    expectBool("zero-width virtual column copy contains no padding",
               QApplication::clipboard()->text()
                       == QStringLiteral("\n")
                   && virtualColumnClickEditor.toPlainText()
                          == QStringLiteral("123456\n12345\n"),
               true);
    QTest::keyClicks(&virtualColumnClickEditor, "X");
    expectBool("column editing materializes each row to the shared endpoint",
               virtualColumnClickEditor.toPlainText()
                   == QStringLiteral("123456   X\n12345    X\n"),
               true);

    MyCodeEditor virtualCopyEditor;
    virtualCopyEditor.resize(520, 140);
    virtualCopyEditor.setLineWrapMode(QPlainTextEdit::NoWrap);
    virtualCopyEditor.setFont(fixedFont);
    virtualCopyEditor.setPlainText(
        QStringLiteral("123456\n12345\n"));
    virtualCopyEditor.show();
    virtualCopyEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTextBlock copyFirst =
        virtualCopyEditor.document()->findBlockByNumber(0);
    QTextCursor copyStart(copyFirst);
    copyStart.setPosition(copyFirst.position() + 5);
    QTest::mouseClick(virtualCopyEditor.viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      virtualCopyEditor.cursorRect(copyStart).center());
    QTest::mouseClick(
        virtualCopyEditor.viewport(),
        Qt::LeftButton,
        columnModifiers,
        pointAtVisualColumn(
            virtualCopyEditor, 1, sharedVirtualColumn));
    QTest::keyClick(&virtualCopyEditor,
                    Qt::Key_C,
                    Qt::ControlModifier);
    expectBool("rectangular copy omits trailing virtual cells",
               QApplication::clipboard()->text()
                       == QStringLiteral("6\n")
                   && virtualCopyEditor.toPlainText()
                          == QStringLiteral("123456\n12345\n"),
               true);
}

static void runEditorLineActionRegression()
{
    MyCodeEditor shortcutCommentEditor;
    shortcutCommentEditor.resize(480, 120);
    shortcutCommentEditor.setPlainText(QStringLiteral("logic a;\n"));
    shortcutCommentEditor.show();
    shortcutCommentEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::keyClick(&shortcutCommentEditor,
                    Qt::Key_Slash,
                    Qt::ControlModifier);
    expectBool("Ctrl+/ comments current line",
               shortcutCommentEditor.toPlainText()
                   == QStringLiteral("// logic a;\n"),
               true);
    QTest::keyClick(&shortcutCommentEditor,
                    Qt::Key_Slash,
                    Qt::ControlModifier | Qt::ShiftModifier);
    expectBool("Ctrl+Shift+/ uncomments current line",
               shortcutCommentEditor.toPlainText()
                   == QStringLiteral("logic a;\n"),
               true);
    shortcutCommentEditor.commentSelectionOrLine();
    QKeyEvent questionShortcut(
        QEvent::KeyPress,
        Qt::Key_Question,
        Qt::ControlModifier | Qt::ShiftModifier,
        QStringLiteral("?"));
    QApplication::sendEvent(&shortcutCommentEditor, &questionShortcut);
    expectBool("Ctrl+Shift+? key report uncomments current line",
               shortcutCommentEditor.toPlainText()
                   == QStringLiteral("logic a;\n"),
               true);

    MyCodeEditor toggleCaseEditor;
    toggleCaseEditor.setPlainText(QStringLiteral("Ab_c1"));
    QTextCursor toggleCursor(toggleCaseEditor.document());
    toggleCursor.select(QTextCursor::Document);
    toggleCaseEditor.setTextCursor(toggleCursor);
    QString toggleCaseFailure;
    expectBool("selection case action toggles letters only",
               toggleCaseEditor.toggleSelectionCase(&toggleCaseFailure)
                   && toggleCaseEditor.toPlainText()
                          == QStringLiteral("aB_C1")
                   && toggleCaseEditor.textCursor().hasSelection(),
               true);
    toggleCaseEditor.undo();
    expectBool("selection case action is one undoable edit",
               toggleCaseEditor.toPlainText()
                   == QStringLiteral("Ab_c1"),
               true);

    toggleCaseEditor.setPlainText(QStringLiteral("Ab\nCd"));
    toggleCursor = QTextCursor(toggleCaseEditor.document());
    toggleCursor.select(QTextCursor::Document);
    toggleCaseEditor.setTextCursor(toggleCursor);
    expectBool("selection case action preserves multiline structure",
               toggleCaseEditor.toggleSelectionCase(&toggleCaseFailure)
                   && toggleCaseEditor.toPlainText()
                          == QStringLiteral("aB\ncD")
                   && toggleCaseEditor.textCursor().hasSelection(),
               true);

    MyCodeEditor selectionCommentEditor;
    selectionCommentEditor.resize(480, 150);
    selectionCommentEditor.setPlainText(QStringLiteral("aa\n  bb\ncc\n"));
    selectionCommentEditor.show();
    selectionCommentEditor.setFocus();
    QTextBlock selectFirst =
        selectionCommentEditor.document()->findBlockByNumber(0);
    QTextBlock selectThird =
        selectionCommentEditor.document()->findBlockByNumber(2);
    QTextCursor selectionCommentCursor(selectFirst);
    selectionCommentCursor.setPosition(selectFirst.position());
    selectionCommentCursor.setPosition(selectThird.position(),
                                       QTextCursor::KeepAnchor);
    selectionCommentEditor.setTextCursor(selectionCommentCursor);
    selectionCommentEditor.commentSelectionOrLine();
    expectBool("comment action comments selected touched lines",
               selectionCommentEditor.toPlainText()
                   == QStringLiteral("// aa\n  // bb\ncc\n"),
               true);
    expectBool("comment action excludes selection endpoint line",
               !selectionCommentEditor.toPlainText().contains(
                   QStringLiteral("// cc")),
               true);
    selectionCommentEditor.undo();
    expectBool("comment action undo restores selected lines",
               selectionCommentEditor.toPlainText()
                   == QStringLiteral("aa\n  bb\ncc\n"),
               true);

    selectionCommentEditor.setTextCursor(selectionCommentCursor);
    selectionCommentEditor.commentSelectionOrLine();
    selectionCommentEditor.uncommentSelectionOrLine();
    expectBool("uncomment action restores selected line comments",
               selectionCommentEditor.toPlainText()
                   == QStringLiteral("aa\n  bb\ncc\n"),
               true);

    MyCodeEditor inlineCommentEditor;
    inlineCommentEditor.resize(480, 120);
    inlineCommentEditor.setPlainText(
        QStringLiteral("assign a = b; // keep\n"));
    inlineCommentEditor.show();
    inlineCommentEditor.setFocus();
    inlineCommentEditor.uncommentSelectionOrLine();
    expectBool("uncomment action ignores trailing comments",
               inlineCommentEditor.toPlainText()
                   == QStringLiteral("assign a = b; // keep\n"),
               true);

    MyCodeEditor shortcutIndentEditor;
    shortcutIndentEditor.resize(480, 120);
    shortcutIndentEditor.setPlainText(QStringLiteral("logic a;\n"));
    shortcutIndentEditor.show();
    shortcutIndentEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::keyClick(&shortcutIndentEditor,
                    Qt::Key_BracketRight,
                    Qt::ControlModifier);
    expectBool("Ctrl+] indents current line",
               shortcutIndentEditor.toPlainText()
                   == QStringLiteral("    logic a;\n"),
               true);
    QTest::keyClick(&shortcutIndentEditor,
                    Qt::Key_BracketLeft,
                    Qt::ControlModifier);
    expectBool("Ctrl+[ unindents current line",
               shortcutIndentEditor.toPlainText()
                   == QStringLiteral("logic a;\n"),
               true);

    MyCodeEditor selectionIndentEditor;
    selectionIndentEditor.resize(480, 150);
    selectionIndentEditor.setPlainText(QStringLiteral("aa\n  bb\ncc\n"));
    selectionIndentEditor.show();
    selectionIndentEditor.setFocus();
    QTextBlock indentFirst =
        selectionIndentEditor.document()->findBlockByNumber(0);
    QTextBlock indentThird =
        selectionIndentEditor.document()->findBlockByNumber(2);
    QTextCursor selectionIndentCursor(indentFirst);
    selectionIndentCursor.setPosition(indentFirst.position());
    selectionIndentCursor.setPosition(indentThird.position(),
                                      QTextCursor::KeepAnchor);
    selectionIndentEditor.setTextCursor(selectionIndentCursor);
    selectionIndentEditor.indentSelectionOrLine();
    expectBool("indent action indents selected touched lines",
               selectionIndentEditor.toPlainText()
                   == QStringLiteral("    aa\n      bb\ncc\n"),
               true);
    expectBool("indent action excludes selection endpoint line",
               !selectionIndentEditor.toPlainText().startsWith(
                   QStringLiteral("    aa\n      bb\n    cc")),
               true);
    selectionIndentEditor.undo();
    expectBool("indent action undo restores selected lines",
               selectionIndentEditor.toPlainText()
                   == QStringLiteral("aa\n  bb\ncc\n"),
               true);

    selectionIndentEditor.setTextCursor(selectionIndentCursor);
    selectionIndentEditor.indentSelectionOrLine();
    selectionIndentEditor.unindentSelectionOrLine();
    expectBool("unindent action restores selected indentation",
               selectionIndentEditor.toPlainText()
                   == QStringLiteral("aa\n  bb\ncc\n"),
               true);

    MyCodeEditor tabUnindentEditor;
    tabUnindentEditor.resize(480, 120);
    tabUnindentEditor.setPlainText(QStringLiteral("\tlogic a;\n"));
    tabUnindentEditor.show();
    tabUnindentEditor.setFocus();
    tabUnindentEditor.unindentSelectionOrLine();
    expectBool("unindent action removes one leading tab",
               tabUnindentEditor.toPlainText()
                   == QStringLiteral("logic a;\n"),
               true);

    MyCodeEditor gotoLineEditor;
    gotoLineEditor.resize(480, 150);
    gotoLineEditor.setPlainText(QStringLiteral("line1\nline2\nline3\n"));
    gotoLineEditor.show();
    gotoLineEditor.setFocus();
    expectBool("goto line accepts valid line",
               gotoLineEditor.goToLineNumber(2),
               true);
    QTextCursor gotoCursor = gotoLineEditor.textCursor();
    expectBool("goto line moves to requested line start",
               gotoCursor.blockNumber() == 1
                   && gotoCursor.positionInBlock() == 0,
               true);
    const int validGotoPosition = gotoCursor.position();
    expectBool("goto line rejects out-of-range line",
               !gotoLineEditor.goToLineNumber(20)
                   && gotoLineEditor.textCursor().position()
                          == validGotoPosition,
               true);

    MyCodeEditor replaceNextEditor;
    replaceNextEditor.resize(480, 150);
    replaceNextEditor.setPlainText(QStringLiteral("aa bb aa\n"));
    replaceNextEditor.show();
    replaceNextEditor.setFocus();
    expectBool("replace next replaces first match",
               replaceNextEditor.replaceNextText(
                   QStringLiteral("aa"),
                   QStringLiteral("zz")),
               true);
    expectBool("replace next updates first occurrence",
               replaceNextEditor.toPlainText()
                   == QStringLiteral("zz bb aa\n"),
               true);
    expectBool("replace next advances to later match",
               replaceNextEditor.replaceNextText(
                   QStringLiteral("aa"),
                   QStringLiteral("yy")),
               true);
    expectBool("replace next updates later occurrence",
               replaceNextEditor.toPlainText()
                   == QStringLiteral("zz bb yy\n"),
               true);
    const int cursorBeforeMissingReplace =
        replaceNextEditor.textCursor().position();
    expectBool("replace next rejects missing text",
               !replaceNextEditor.replaceNextText(
                   QStringLiteral("missing"),
                   QStringLiteral("unused"))
                   && replaceNextEditor.textCursor().position()
                          == cursorBeforeMissingReplace,
               true);

    MyCodeEditor replaceSelectionEditor;
    replaceSelectionEditor.resize(480, 150);
    replaceSelectionEditor.setPlainText(QStringLiteral("aa bb aa\n"));
    QTextBlock replaceSelectionBlock =
        replaceSelectionEditor.document()->findBlockByNumber(0);
    QTextCursor replaceSelectionCursor(replaceSelectionBlock);
    replaceSelectionCursor.setPosition(replaceSelectionBlock.position() + 3);
    replaceSelectionCursor.setPosition(replaceSelectionBlock.position() + 5,
                                       QTextCursor::KeepAnchor);
    replaceSelectionEditor.setTextCursor(replaceSelectionCursor);
    expectBool("replace next uses matching current selection",
               replaceSelectionEditor.replaceNextText(
                   QStringLiteral("bb"),
                   QStringLiteral("cc")),
               true);
    expectBool("replace next updates current selection",
               replaceSelectionEditor.toPlainText()
                   == QStringLiteral("aa cc aa\n"),
               true);

    MyCodeEditor replaceAllEditor;
    replaceAllEditor.resize(480, 150);
    replaceAllEditor.setPlainText(QStringLiteral("foo FOO\nfoo\n"));
    const int replaceAllCount =
        replaceAllEditor.replaceAllText(QStringLiteral("foo"),
                                        QStringLiteral("bar"),
                                        false);
    expectBool("replace all is case-insensitive by default",
               replaceAllCount == 3
                   && replaceAllEditor.toPlainText()
                          == QStringLiteral("bar bar\nbar\n"),
               true);
    replaceAllEditor.undo();
    expectBool("replace all undo restores document",
               replaceAllEditor.toPlainText()
                   == QStringLiteral("foo FOO\nfoo\n"),
               true);

    MyCodeEditor occurrenceEditor;
    occurrenceEditor.resize(480, 180);
    const QString occurrenceSource =
        QStringLiteral(
            "one\n"
            "sig = sig + 1;\n"
            "sig = sig + 2;\n");
    occurrenceEditor.setPlainText(occurrenceSource);
    occurrenceEditor.show();
    occurrenceEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    const int firstOccurrence =
        occurrenceSource.indexOf(QStringLiteral("sig"));
    QTextCursor occurrenceCursor(occurrenceEditor.document());
    occurrenceCursor.setPosition(firstOccurrence + 1);
    occurrenceEditor.setTextCursor(occurrenceCursor);
    QString occurrenceFailure;
    expectBool("next-occurrence command selects the current structured symbol",
               occurrenceEditor.addNextSymbolOccurrence(&occurrenceFailure)
                   && occurrenceFailure.isEmpty()
                   && occurrenceEditor.toPlainText()
                          == occurrenceSource
                   && occurrenceEditor.textCursor().selectedText()
                          == QStringLiteral("sig")
                   && !occurrenceEditor.editorModeActiveForTest(
                       EditorModeId::MultiCursor),
               true);
    expectBool("next-occurrence command adds exactly one occurrence cursor",
               occurrenceEditor.addNextSymbolOccurrence(&occurrenceFailure)
                   && occurrenceFailure.isEmpty()
                   && occurrenceEditor.toPlainText()
                          == occurrenceSource
                   && occurrenceEditor.editorModeActiveForTest(
                       EditorModeId::MultiCursor),
               true);
    QTest::keyClicks(&occurrenceEditor, QStringLiteral("x"));
    expectBool("multi-cursor input replaces only the selected occurrences",
               occurrenceEditor.toPlainText()
                   == QStringLiteral(
                       "one\n"
                       "x = x + 1;\n"
                       "sig = sig + 2;\n"),
               true);
    occurrenceEditor.undo();
    expectBool("multi-cursor replacement is one undo transaction",
               occurrenceEditor.toPlainText() == occurrenceSource,
               true);
    occurrenceCursor.setPosition(firstOccurrence + 1);
    occurrenceEditor.setTextCursor(occurrenceCursor);
    QTest::keyClick(
        &occurrenceEditor,
        Qt::Key_D,
        Qt::ControlModifier);
    expectBool("Ctrl+D duplicates the current logical line",
               occurrenceEditor.toPlainText()
                   == QStringLiteral(
                       "one\n"
                       "sig = sig + 1;\n"
                       "sig = sig + 1;\n"
                       "sig = sig + 2;\n"),
               true);
    occurrenceEditor.undo();
    expectBool("Ctrl+D duplicate is one undo transaction",
               occurrenceEditor.toPlainText() == occurrenceSource,
               true);

    MyCodeEditor moveEditor;
    moveEditor.resize(480, 180);
    moveEditor.setPlainText(QStringLiteral("aa\nbb\ncc\n"));
    moveEditor.show();
    moveEditor.setFocus();
    QTextBlock bbBlock = moveEditor.document()->findBlockByNumber(1);
    QTextCursor moveCursor(bbBlock);
    moveCursor.setPosition(bbBlock.position() + 1);
    moveEditor.setTextCursor(moveCursor);
    QTest::keyClick(&moveEditor, Qt::Key_Up, Qt::AltModifier);
    expectBool("Alt+Up moves current logical line up",
               moveEditor.toPlainText() == QStringLiteral("bb\naa\ncc\n"),
               true);
    expectBool("Alt+Up preserves cursor column",
               moveEditor.textCursor().blockNumber() == 0
                   && moveEditor.textCursor().position()
                          - moveEditor.textCursor().block().position()
                          == 1,
               true);
    QTest::keyClick(&moveEditor, Qt::Key_Down, Qt::AltModifier);
    expectBool("Alt+Down moves current logical line down",
               moveEditor.toPlainText() == QStringLiteral("aa\nbb\ncc\n"),
               true);

    MyCodeEditor heldMoveEditor;
    heldMoveEditor.resize(480, 180);
    heldMoveEditor.setPlainText(
        QStringLiteral("aa\nbb\ncc\ndd\nee\nff\n"));
    heldMoveEditor.show();
    heldMoveEditor.setFocus();
    const QTextBlock heldBlock =
        heldMoveEditor.document()->findBlockByNumber(1);
    QTextCursor heldCursor(heldBlock);
    heldCursor.setPosition(heldBlock.position() + 1);
    heldMoveEditor.setTextCursor(heldCursor);
    QKeyEvent initialMove(
        QEvent::KeyPress,
        Qt::Key_Down,
        Qt::AltModifier,
        QString(),
        false,
        1);
    QApplication::sendEvent(&heldMoveEditor, &initialMove);
    for (int repeat = 0; repeat < 3; ++repeat) {
        QKeyEvent repeatedMove(
            QEvent::KeyPress,
            Qt::Key_Down,
            Qt::AltModifier,
            QString(),
            true,
            repeat + 2);
        QApplication::sendEvent(&heldMoveEditor, &repeatedMove);
    }
    QKeyEvent releaseMove(
        QEvent::KeyRelease,
        Qt::Key_Down,
        Qt::AltModifier);
    QApplication::sendEvent(&heldMoveEditor, &releaseMove);
    expectBool("held Alt+Down repeats and keeps the caret on the moved line",
               heldMoveEditor.toPlainText()
                    == QStringLiteral("aa\ncc\ndd\nee\nff\nbb\n")
                    && heldMoveEditor.textCursor().blockNumber() == 5
                    && heldMoveEditor.textCursor().position()
                           - heldMoveEditor.textCursor().block().position()
                           == 1,
               true);

    MyCodeEditor touchedSelectionEditor;
    touchedSelectionEditor.resize(480, 180);
    touchedSelectionEditor.setPlainText(QStringLiteral("aa\nbb\ncc\n"));
    touchedSelectionEditor.show();
    touchedSelectionEditor.setFocus();
    QTextBlock touchedStart =
        touchedSelectionEditor.document()->findBlockByNumber(1);
    QTextBlock touchedNext =
        touchedSelectionEditor.document()->findBlockByNumber(2);
    QTextCursor touchedCursor(touchedStart);
    touchedCursor.setPosition(touchedStart.position());
    touchedCursor.setPosition(touchedNext.position(), QTextCursor::KeepAnchor);
    touchedSelectionEditor.setTextCursor(touchedCursor);
    QTest::keyClick(&touchedSelectionEditor,
                    Qt::Key_Up,
                    Qt::AltModifier);
    expectBool("Alt+Up excludes selection ending at next line column zero",
               touchedSelectionEditor.toPlainText()
                   == QStringLiteral("bb\naa\ncc\n"),
               true);

    MyCodeEditor columnMoveEditor;
    columnMoveEditor.resize(480, 180);
    columnMoveEditor.setPlainText(QStringLiteral("abc\nabc\nabc\n"));
    columnMoveEditor.show();
    columnMoveEditor.setFocus();
    QString columnStatus;
    QObject::connect(&columnMoveEditor,
                     &MyCodeEditor::editorStatusMessageRequested,
                     &columnMoveEditor,
                     [&](const QString& message) {
                         columnStatus = message;
                     });
    QTextBlock columnStart =
        columnMoveEditor.document()->findBlockByNumber(0);
    QTextBlock columnEnd =
        columnMoveEditor.document()->findBlockByNumber(2);
    QTextCursor columnStartCursor(columnStart);
    columnStartCursor.setPosition(columnStart.position() + 1);
    QTextCursor columnEndCursor(columnEnd);
    columnEndCursor.setPosition(columnEnd.position() + 1);
    const Qt::KeyboardModifiers columnModifiers =
        Qt::ShiftModifier | Qt::AltModifier;
    columnMoveEditor.setTextCursor(columnStartCursor);
    QTest::mouseClick(columnMoveEditor.viewport(),
                      Qt::LeftButton,
                      columnModifiers,
                      columnMoveEditor.cursorRect(columnEndCursor).center());
    QTest::keyClick(&columnMoveEditor, Qt::Key_Down, Qt::AltModifier);
    expectBool("Alt+Down is disabled while column selection is active",
               columnMoveEditor.toPlainText()
                       == QStringLiteral("abc\nabc\nabc\n")
                   && columnStatus.contains(QStringLiteral("Column selection")),
               true);
}

static void runEditorCtrlClickNavigationRegression()
{
    const QString path =
        sourceFixturePath(
            QStringLiteral("test_sv/huge_prj/" ZS_FIXTURE_TOP_CTL_FILE));
    QFile file(path);
    expectBool("vendor ctrl-click fixture opens",
               file.open(QIODevice::ReadOnly | QIODevice::Text),
               true);
    if (!file.isOpen())
        return;

    const QString text = QString::fromUtf8(file.readAll());
    const QString symbol =
        QStringLiteral("CC_IB_MCPL_SEG_BUF_RAM_ADDR_WD");
    MyCodeEditor editor;
    editor.setDocumentFileName(path);
    editor.setPlainText(text);
    editor.resize(980, 520);
    editor.show();

    const QTextBlock useBlock =
        editor.document()->findBlockByNumber(1659);
    const QTextBlock definitionBlock =
        editor.document()->findBlockByNumber(339);
    expectBool("vendor ctrl-click use line exists",
               useBlock.isValid() && useBlock.text().contains(symbol),
               true);
    expectBool("vendor ctrl-click definition line exists",
               definitionBlock.isValid()
                   && definitionBlock.text().contains(symbol),
               true);
    if (!useBlock.isValid() || !definitionBlock.isValid())
        return;

    bool navigationRequested = false;
    QObject::connect(
        &editor,
        &MyCodeEditor::sourceNavigationRequested,
        &editor,
        [&](const EditorSourceNavigationTarget& target,
            const EditorSemanticContext&) {
            if (target.text != symbol)
                return;
            navigationRequested = true;
            const SourceLineNavigationTarget lineTarget =
                SourceNavigationService::getInstance()
                    ->lineNavigationTarget(340,
                                           definitionBlock.text().indexOf(symbol)
                                               + 1);
            editor.applyLineNavigationTarget(lineTarget);
        });

    const int useStart = useBlock.text().indexOf(symbol);
    const int usePosition = useBlock.position() + useStart;
    QTextCursor selected(editor.document());
    selected.setPosition(usePosition);
    selected.setPosition(usePosition + symbol.size(),
                         QTextCursor::KeepAnchor);
    editor.setTextCursor(selected);
    editor.centerCursor();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextCursor clickCursor(editor.document());
    clickCursor.setPosition(usePosition + 6);
    const QPoint clickPoint = editor.cursorRect(clickCursor).center();
    QTest::mousePress(editor.viewport(),
                      Qt::LeftButton,
                      Qt::ControlModifier,
                      clickPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QMouseEvent dragAfterJump(QEvent::MouseMove,
                              editor.cursorRect().center(),
                              editor.viewport()->mapToGlobal(
                                  editor.cursorRect().center()),
                              Qt::NoButton,
                              Qt::LeftButton,
                              Qt::ControlModifier);
    QCoreApplication::sendEvent(editor.viewport(), &dragAfterJump);
    QTest::mouseRelease(editor.viewport(),
                        Qt::LeftButton,
                        Qt::ControlModifier,
                        clickPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    expectBool("vendor ctrl-click requested parameter navigation",
               navigationRequested,
               true);
    expectBool("vendor ctrl-click lands on parameter definition",
               editor.textCursor().blockNumber() == 339,
               true);
    expectBool("vendor ctrl-click does not extend selection",
               !editor.textCursor().hasSelection(),
               true);
}

static void runSignalKernelGraphPopupInteractionRegression()
{
    QMainWindow host;
    host.resize(900, 560);
    SignalKernelGraphPanelCoordinator graph(&host);
    host.addDockWidget(Qt::BottomDockWidgetArea, graph.dock());
    host.show();
    graph.dock()->show();

    SignalKernelGraphReport report;
    report.found = true;
    report.kernelModuleName = QStringLiteral("graph_top");
    report.kernel.id = 10;
    report.kernel.role = SignalKernelGraphNodeRole::Kernel;
    report.kernel.displayName = QStringLiteral("kernel_sig");
    report.kernel.moduleDisplayName = QStringLiteral("graph_top");
    report.kernel.typeDisplayName = QStringLiteral("logic");
    report.kernel.detailDisplayName =
        QStringLiteral("assignment with enough detail to build a popup");
    report.kernel.navigateCodeLink =
        RtlInsightLink::fromFileLine(QStringLiteral("graph_top.sv"), 10, 3);

    SignalKernelGraphNode input;
    input.id = 11;
    input.role = SignalKernelGraphNodeRole::Input;
    input.inputLane = SignalKernelGraphInputLane::Data;
    input.displayName = QStringLiteral("data_in");
    input.moduleDisplayName = QStringLiteral("graph_top");
    input.typeDisplayName = QStringLiteral("logic");
    report.inputs.append(input);

    SignalKernelGraphNode output;
    output.id = 12;
    output.role = SignalKernelGraphNodeRole::Output;
    output.displayName = QStringLiteral("data_out");
    output.moduleDisplayName = QStringLiteral("graph_top");
    output.typeDisplayName = QStringLiteral("logic");
    report.outputs.append(output);
    report.edges.append({input.id, report.kernel.id, QString()});
    report.edges.append({report.kernel.id, output.id, QString()});

    bool graphNavigated = false;
    graph.setNavigationHandler(
        [&](const QString& fileName, int line, int column) {
            graphNavigated =
                fileName == QStringLiteral("graph_top.sv")
                && line == 10
                && column == 3;
            return graphNavigated;
        });
    graph.renderReport(report);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QGraphicsView* view = graph.view();
    expectBool("signal kernel graph view exists", view != nullptr, true);
    expectBool("signal kernel graph popup starts hidden",
               graph.hoverPopup && !graph.hoverPopup->isVisible(),
               true);
    if (!view || !graph.hoverPopup)
        return;

    const QPoint kernelPoint = view->mapFromScene(QPointF(0, 0));
    QGraphicsRectItem* kernelRectItem = nullptr;
    for (QGraphicsItem* item : view->items(kernelPoint)) {
        auto* rectItem = dynamic_cast<QGraphicsRectItem*>(item);
        if (!rectItem)
            continue;
        const QRectF rect = rectItem->rect();
        if (rect.width() > 120.0 && rect.width() < 260.0
            && rect.height() > 40.0 && rect.height() < 90.0) {
            kernelRectItem = rectItem;
            break;
        }
    }
    expectBool("signal kernel graph kernel item found",
               kernelRectItem != nullptr,
               true);
    if (!kernelRectItem)
        return;

    QTest::mouseMove(view->viewport(), kernelPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("signal kernel graph hover does not show popup",
               !graph.hoverPopup->isVisible(),
               true);

    QTest::mouseClick(view->viewport(),
                      Qt::RightButton,
                      Qt::NoModifier,
                      kernelPoint);
    expectBool("signal kernel graph right-click shows popup",
               waitUntil([&]() { return graph.hoverPopup->isVisible(); },
                         1000),
               true);
    expectBool("signal kernel graph preview is embedded",
               !graph.hoverPopup->isWindow()
                   && graph.hoverPopup->window()
                          == view->window(),
               true);

    const QRect nodeViewRect =
        view->mapFromScene(kernelRectItem->sceneBoundingRect())
            .boundingRect();
    const QRect nodeGlobalRect(
        view->viewport()->mapToGlobal(nodeViewRect.topLeft()),
        view->viewport()->mapToGlobal(nodeViewRect.bottomRight()));
    const QRect popupRect(
        graph.hoverPopup->mapToGlobal(QPoint(0, 0)),
        graph.hoverPopup->size());
    expectBool("signal kernel graph popup avoids clicked node",
               !popupRect.intersects(nodeGlobalRect.normalized()),
               true);

    const QPoint outputPoint = view->mapFromScene(QPointF(430, 0));
    QTest::mouseClick(view->viewport(),
                      Qt::RightButton,
                      Qt::NoModifier,
                      outputPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    bool outputPopupVisible = false;
    const QString popupText = visibleEditorHoverPopupText(&outputPopupVisible);
    expectBool("signal kernel graph right-click switches popup",
               outputPopupVisible
                   && popupText.contains(QStringLiteral("Output: data_out")),
               true);

    const QPoint blankPoint(4, view->viewport()->height() - 4);
    QTest::mouseClick(view->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      blankPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("signal kernel graph blank left-click closes popup",
               !graph.hoverPopup->isVisible(),
               true);

    QTest::mouseClick(view->viewport(),
                      Qt::RightButton,
                      Qt::NoModifier,
                      kernelPoint);
    expectBool("signal kernel graph right-click reopens popup",
               waitUntil([&]() { return graph.hoverPopup->isVisible(); },
                         1000),
               true);
    QTest::mouseClick(view->viewport(),
                      Qt::RightButton,
                      Qt::NoModifier,
                      blankPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("signal kernel graph blank right-click closes popup",
               !graph.hoverPopup->isVisible(),
               true);

    QTest::mouseDClick(view->viewport(),
                       Qt::LeftButton,
                       Qt::NoModifier,
                       kernelPoint);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("signal kernel graph double-click navigates",
               graphNavigated,
               true);
}

static void runEditorFormatterRegression()
{
    MyCodeEditor editor;
    editor.resize(360, 160);
    editor.setPlainText(QStringLiteral("module top;\n"
                                       "logic a;\n"
                                       "always_comb begin\n"
                                       "a = \"end\";\n"
                                       "end\n"
                                       "endmodule\n"));
    QString statusMessage;
    QObject::connect(&editor,
                     &MyCodeEditor::editorStatusMessageRequested,
                     &editor,
                     [&](const QString& message) {
                         statusMessage = message;
                     });
    editor.formatDocument();
    expectBool("editor formatter indents document",
               editor.toPlainText()
                   == QStringLiteral("module top;\n"
                                     "logic a;\n"
                                     "always_comb begin\n"
                                     "    a = \"end\";\n"
                                     "end\n"
                                     "endmodule\n"),
               true);
    expectBool("editor formatter emits status",
               statusMessage.contains(QStringLiteral("Formatted document")),
               true);
    editor.undo();
    expectBool("editor formatter undo restores text",
               editor.toPlainText()
                   == QStringLiteral("module top;\n"
                                     "logic a;\n"
                                     "always_comb begin\n"
                                     "a = \"end\";\n"
                                     "end\n"
                                     "endmodule\n"),
               true);
}

static void runEditorHoverPreviewRegression(const QString& workspacePath)
{
    const QString rtlTopPath =
        QDir(workspacePath).filePath(
            QStringLiteral("elec_phy_import/top/rtl_top.sv"));
    QFile rtlTopFile(rtlTopPath);
    expectBool("hover fixture opens",
               rtlTopFile.open(QIODevice::ReadOnly | QFile::Text),
               true);
    if (!rtlTopFile.isOpen())
        return;
    const QString rtlTopText = QTextStream(&rtlTopFile).readAll();
    rtlTopFile.close();

    const QStringList rtlLines = rtlTopText.split(QLatin1Char('\n'));
    int moduleLine = -1;
    int signalLine = -1;
    int useLine = -1;
    for (int i = 0; i < rtlLines.size(); ++i) {
        if (moduleLine < 0 && rtlLines.at(i).contains(QStringLiteral("module rtl_top")))
            moduleLine = i + 1;
        if (signalLine < 0 && rtlLines.at(i).contains(QStringLiteral("clk_main")))
            signalLine = i + 1;
        if (useLine < 0 && signalLine > 0 && i + 1 != signalLine
            && rtlLines.at(i).contains(QStringLiteral("clk_main"))
            && !rtlLines.at(i).trimmed().startsWith(QStringLiteral("//"))) {
            useLine = i + 1;
        }
    }
    expectBool("hover fixture has module line", moduleLine > 0, true);
    expectBool("hover fixture has signal line", signalLine > 0, true);
    expectBool("hover fixture has signal use line", useLine > 0, true);
    if (moduleLine <= 0 || signalLine <= 0 || useLine <= 0)
        return;

    const int signalColumn =
        rtlLines.at(signalLine - 1).indexOf(QStringLiteral("clk_main"));
    const int useColumn =
        rtlLines.at(useLine - 1).indexOf(QStringLiteral("clk_main"));
    expectBool("hover fixture has signal column", signalColumn >= 0, true);
    expectBool("hover fixture has signal use column", useColumn >= 0, true);
    if (signalColumn < 0 || useColumn < 0)
        return;

    const SemanticSymbolRecord moduleRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("rtl_top"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(rtlTopPath)
            .withLocalHandle(101)
            .withLine(moduleLine, 8)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    const SemanticSymbolRecord signalRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("clk_main"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(rtlTopPath)
            .withLocalHandle(102)
            .withLine(signalLine, signalColumn + 1)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .inModule(QStringLiteral("rtl_top"))
            .withType(QStringLiteral("logic"))
            .record();

    SemanticIndex hoverIndex;
    hoverIndex.setSnapshot(
        snapshotFromRecords(
            {moduleRecord, signalRecord},
            {},
            {},
            {{rtlTopPath, rtlTopText}}));

    EditorSemanticContext context;
    context.fileName = rtlTopPath;
    context.moduleName = QStringLiteral("rtl_top");
    context.lineText = rtlLines.at(signalLine - 1);
    context.lineUpToCursor = context.lineText.left(signalColumn + 1);
    context.cursorLine = signalLine;
    context.column = signalColumn + 1;
    context.cursorPosition = 0;

    SymbolHoverService hoverService(&hoverIndex);
    const SymbolHoverReport hover = hoverService.hoverForContext(context);
    expectBool("hover returns local signal",
               hover.available && hover.symbolName == QStringLiteral("clk_main"),
               true);
    expectBool("hover returns kind owner location",
               !hover.displayKind.isEmpty()
                   && hover.ownerName == QStringLiteral("rtl_top")
                   && hover.definitionFile == rtlTopPath
                   && hover.definitionLine == signalLine,
               true);

    DefinitionPreviewService previewService(&hoverIndex);
    const DefinitionPreviewReport preview =
        previewService.previewForContext(context);
    bool previewContainsDefinition = false;
    for (const QString& line : preview.codeLines)
        previewContainsDefinition |= line.contains(QStringLiteral("clk_main"));
    expectBool("preview resolves target file line",
               preview.targetResolved
                   && preview.targetFile == rtlTopPath
                   && preview.targetLine == signalLine,
               true);
    expectBool("preview includes definition snippet",
               preview.available && previewContainsDefinition,
               true);
    expectBool("preview highlights target line",
               preview.highlightedLine == signalLine,
               true);

    DocumentModel documents;
    MyCodeEditor dirtyEditor;
    documents.registerEditor(&dirtyEditor, rtlTopPath);
    QString dirtyText = rtlTopText;
    QStringList dirtyLines = dirtyText.split(QLatin1Char('\n'));
    dirtyLines[signalLine - 1].append(QStringLiteral(" // dirty preview marker"));
    dirtyText = dirtyLines.join(QLatin1Char('\n'));
    dirtyEditor.setPlainText(dirtyText);
    documents.refreshEditorState(&dirtyEditor);
    previewService.setDocumentModel(&documents);
    const DefinitionPreviewReport dirtyPreview =
        previewService.previewForContext(context);
    bool dirtyPreviewUsesOpenText = false;
    for (const QString& line : dirtyPreview.codeLines)
        dirtyPreviewUsesOpenText |= line.contains(QStringLiteral("dirty preview marker"));
    expectBool("preview prefers dirty open text",
               dirtyPreviewUsesOpenText,
               true);

    EditorSemanticContext commentContext = context;
    commentContext.lineText = QStringLiteral("// clk_main");
    commentContext.column = 4;
    expectBool("hover ignores comment identifier",
               !hoverService.hoverForContext(commentContext).available,
               true);

    EditorSemanticContext stringContext = context;
    stringContext.lineText = QStringLiteral("assign s = \"clk_main\";");
    stringContext.column = stringContext.lineText.indexOf(QStringLiteral("clk_main"));
    expectBool("hover ignores string identifier",
               !hoverService.hoverForContext(stringContext).available,
               true);

    SemanticIndex missingTextIndex;
    missingTextIndex.setSnapshot(snapshotFromRecords({signalRecord}));
    DefinitionPreviewService missingTextPreview(&missingTextIndex);
    const DefinitionPreviewReport missingPreview =
        missingTextPreview.previewForContext(context);
    expectBool("preview explains unavailable text",
               missingPreview.targetResolved
                   && !missingPreview.available
                   && missingPreview.unavailableReason.contains(
                       QStringLiteral("preview unavailable")),
               true);

    QWidget popupHost;
    popupHost.resize(800, 480);
    popupHost.show();
    EditorHoverPopup popup(&popupHost);
    bool popupNavigationRequested = false;
    QString popupFile;
    int popupLine = -1;
    int popupColumn = -1;
    popup.setNavigationHandler(
        [&](const QString& fileName, int line, int column) {
            popupNavigationRequested = true;
            popupFile = fileName;
            popupLine = line;
            popupColumn = column;
        });
    DefinitionPreviewReport popupReport = missingPreview;
    popupReport.targetFile = rtlTopPath;
    popupReport.targetLine = signalLine;
    popupReport.targetColumn = signalColumn + 1;
    popup.showPreview(popupReport, QPoint(20, 20), QFont(QStringLiteral("Consolas"), 10));
    QTest::mouseDClick(&popup, Qt::LeftButton, Qt::NoModifier, QPoint(8, 8));
    expectBool("preview popup double-click requests navigation",
               popupNavigationRequested
                   && popupFile == rtlTopPath
                   && popupLine == signalLine
                   && popupColumn == signalColumn + 1,
               true);
    popup.closePopup();
    popupHost.hide();

    const auto previousGlobalSnapshot = SemanticIndex::getInstance()->snapshot();
    SemanticIndex::getInstance()->setSnapshot(hoverIndex.snapshot());
    SymbolHoverService::getInstance()->setSemanticIndex(SemanticIndex::getInstance());

    MyCodeEditor hoverEditor;
    hoverEditor.setDocumentFileName(rtlTopPath);
    hoverEditor.setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont hoverCodeFont = QFontDatabase::systemFont(
        QFontDatabase::GeneralFont);
    hoverCodeFont.setPointSize(13);
    hoverEditor.setFont(hoverCodeFont);
    hoverEditor.resize(800, 320);
    hoverEditor.setPlainText(rtlTopText);
    QTextBlock signalBlock =
        hoverEditor.document()->findBlockByNumber(signalLine - 1);
    QTextCursor hoverCursor(signalBlock);
    hoverCursor.setPosition(signalBlock.position() + signalColumn);
    hoverEditor.setTextCursor(hoverCursor);
    hoverEditor.show();
    hoverEditor.ensureCursorVisible();
    QApplication::processEvents();

    const QRect signalRect = hoverEditor.cursorRect(hoverCursor);
    const QPoint signalPoint(signalRect.left() + 3, signalRect.center().y());
    hideEditorHoverPopups();
    QTest::mouseMove(hoverEditor.viewport(), signalPoint);
    QApplication::processEvents();
    QTest::qWait(20);
    bool ordinaryHoverVisible = false;
    visibleEditorHoverPopupText(&ordinaryHoverVisible);
    expectBool("ordinary mouse hover never shows semantic popup",
               ordinaryHoverVisible,
               false);

    QTest::mouseDClick(hoverEditor.viewport(),
                       Qt::LeftButton,
                       Qt::NoModifier,
                       signalPoint);
    QApplication::processEvents();
    QTest::qWait(20);

    bool doubleClickHoverVisible = false;
    const QString doubleClickHoverText =
        visibleEditorHoverPopupText(&doubleClickHoverVisible);
    expectBool("editor double-click shows symbol hover",
               doubleClickHoverVisible
                   && doubleClickHoverText.contains(QStringLiteral("clk_main"))
                   && doubleClickHoverText.contains(QStringLiteral("owner: rtl_top")),
               true);
    QWidget* embeddedPopup = visibleEditorHoverPopupWidget();
    expectBool("editor semantic popup is an embedded child",
               embeddedPopup
                   && !embeddedPopup->isWindow()
                   && embeddedPopup->parentWidget() == &hoverEditor,
               true);
    expectBool("double-click popup separates symbol and UI typography",
               popupTextUsesSemanticTypography(embeddedPopup,
                                       hoverEditor.font()),
               true);
    expectBool("double-click popup has no global top-level flags",
               embeddedPopup
                   && embeddedPopup->windowType() == Qt::Widget
                   && !embeddedPopup->windowFlags().testFlag(
                       Qt::WindowStaysOnTopHint)
                   && embeddedPopup->window() == hoverEditor.window(),
               true);
    QTest::mouseMove(hoverEditor.viewport(), signalPoint + QPoint(2, 0));
    QApplication::processEvents();
    QTest::qWait(20);
    bool movedDoubleClickHoverVisible = false;
    visibleEditorHoverPopupText(&movedDoubleClickHoverVisible);
    expectBool("editor double-click hover survives tiny mouse move",
               movedDoubleClickHoverVisible,
               true);
    QTest::mouseClick(hoverEditor.viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      QPoint(hoverEditor.viewport()->width() - 8,
                             hoverEditor.viewport()->height() - 8));
    QApplication::processEvents();
    bool outsideClickPopupVisible = false;
    visibleEditorHoverPopupText(&outsideClickPopupVisible);
    expectBool("outside click closes semantic popup",
               outsideClickPopupVisible,
               false);

    EditorHoverPopup fontAuditPopup(&hoverEditor);
    SymbolHoverReport parameterFontReport;
    parameterFontReport.available = true;
    parameterFontReport.symbolName = QStringLiteral("P_WIDTH");
    parameterFontReport.displayKind = QStringLiteral("parameter");
    parameterFontReport.sourceRole = QStringLiteral("definition");
    parameterFontReport.ownerName = QStringLiteral("rtl_top");
    parameterFontReport.declarationText = QStringLiteral(
        "parameter logic [31:0] P_WIDTH = BASE + 1");
    parameterFontReport.definitionFile = rtlTopPath;
    parameterFontReport.definitionLine = signalLine;
    parameterFontReport.parameterLike = true;
    parameterFontReport.effectiveValueStatus =
        EffectiveValueStatus::Current;
    parameterFontReport.valueText = QStringLiteral("32'd17");
    parameterFontReport.valueSource = QStringLiteral("slang elaboration");
    parameterFontReport.instanceBound = true;
    parameterFontReport.instancePath = QStringLiteral("top.u0");
    parameterFontReport.resolvedTypeText = QStringLiteral(
        "logic [31:0]");
    parameterFontReport.bitWidthText = QStringLiteral("32");
    parameterFontReport.expressionText = QStringLiteral("BASE + 1");
    parameterFontReport.evaluationFailureReason = QStringLiteral(
        "representative status text");
    parameterFontReport.macroSignatureText = QStringLiteral("`WIDTH(x)");
    parameterFontReport.macroBodyText = QStringLiteral("((x) + 1)");
    fontAuditPopup.showHover(parameterFontReport,
                             hoverEditor.mapToGlobal(QPoint(40, 40)),
                             hoverEditor.font());
    QApplication::processEvents();
    const int parameterFontLabelCount =
        fontAuditPopup.findChildren<QLabel*>().size();
    const bool parameterFontsUnified =
        parameterFontLabelCount >= 12
        && popupTextUsesSemanticTypography(&fontAuditPopup,
                                  hoverEditor.font());

    SymbolHoverReport portFontReport = parameterFontReport;
    portFontReport.symbolName = QStringLiteral("data_i");
    portFontReport.displayKind = QStringLiteral("input port");
    portFontReport.parameterLike = false;
    portFontReport.port = true;
    portFontReport.evaluationFailureReason.clear();
    portFontReport.resolvedTypeText = QStringLiteral("test_t");
    portFontReport.packedDimensionsText = QStringLiteral("[7:0]");
    portFontReport.unpackedDimensionsText = QStringLiteral("[0:1]");
    portFontReport.signednessText = QStringLiteral("unsigned");
    portFontReport.interfaceName = QStringLiteral("axi_if");
    portFontReport.modportName = QStringLiteral("master");
    fontAuditPopup.showHover(portFontReport,
                             hoverEditor.mapToGlobal(QPoint(40, 40)),
                             hoverEditor.font());
    QApplication::processEvents();
    const bool portFontsUnified =
        fontAuditPopup.findChildren<QLabel*>().size() >= 12
        && popupTextUsesSemanticTypography(&fontAuditPopup,
                                  hoverEditor.font());

    SymbolHoverReport staleFontReport = portFontReport;
    staleFontReport.effectiveValueStatus = EffectiveValueStatus::Stale;
    staleFontReport.evaluationFailureReason = QStringLiteral("stale");
    fontAuditPopup.showHover(staleFontReport,
                             hoverEditor.mapToGlobal(QPoint(40, 40)),
                             hoverEditor.font());
    QApplication::processEvents();
    const bool staleFontsUnified =
        visibleEditorHoverPopupText().contains(
            QStringLiteral("waiting for the current document revision"))
        && popupTextUsesSemanticTypography(&fontAuditPopup,
                                  hoverEditor.font());
    expectBool("all double-click popup paths preserve semantic typography",
               parameterFontsUnified
                   && portFontsUnified
                   && staleFontsUnified,
               true);
    fontAuditPopup.closePopup();

    QTest::mouseDClick(hoverEditor.viewport(),
                       Qt::LeftButton,
                       Qt::NoModifier,
                       signalPoint);
    QApplication::processEvents();
    const QPoint nonClientGlobal = hoverEditor.mapToGlobal(
        QPoint(hoverEditor.width() - 1, hoverEditor.height() - 1));
    QMouseEvent nonClientPress(
        QEvent::NonClientAreaMouseButtonPress,
        QPointF(hoverEditor.width() - 1, hoverEditor.height() - 1),
        QPointF(hoverEditor.width() - 1, hoverEditor.height() - 1),
        QPointF(nonClientGlobal),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(&hoverEditor, &nonClientPress);
    QApplication::processEvents();
    bool nonClientClickPopupVisible = false;
    visibleEditorHoverPopupText(&nonClientClickPopupVisible);
    expectBool("non-client outside click closes semantic popup",
               nonClientClickPopupVisible,
               false);

    QTest::mouseDClick(hoverEditor.viewport(),
                       Qt::LeftButton,
                       Qt::NoModifier,
                       signalPoint);
    QApplication::processEvents();
    QTest::keyClick(&hoverEditor, Qt::Key_Escape);
    QApplication::processEvents();
    bool escapePopupVisible = false;
    visibleEditorHoverPopupText(&escapePopupVisible);
    expectBool("Escape closes semantic popup",
               escapePopupVisible,
               false);

    QTextBlock useBlock =
        hoverEditor.document()->findBlockByNumber(useLine - 1);
    QTextCursor useCursor(useBlock);
    useCursor.setPosition(useBlock.position() + useColumn);
    hoverEditor.setTextCursor(useCursor);
    hoverEditor.ensureCursorVisible();
    QApplication::processEvents();
    const QPoint usePoint = hoverEditor.cursorRect(useCursor).center();
    QTest::mouseMove(hoverEditor.viewport(), usePoint);
    QKeyEvent controlPress(QEvent::KeyPress,
                           Qt::Key_Control,
                           Qt::ControlModifier);
    QApplication::sendEvent(&hoverEditor, &controlPress);
    QApplication::processEvents();
    bool modifierPreviewVisible = false;
    visibleEditorHoverPopupText(&modifierPreviewVisible);
    expectBool("explicit modifier preview remains available",
               modifierPreviewVisible,
               true);
    QEvent applicationDeactivate(QEvent::ApplicationDeactivate);
    QApplication::sendEvent(&hoverEditor, &applicationDeactivate);
    QApplication::processEvents();
    bool deactivatedPreviewVisible = false;
    visibleEditorHoverPopupText(&deactivatedPreviewVisible);
    expectBool("application focus loss hides modifier preview",
               deactivatedPreviewVisible,
               false);
    QKeyEvent controlRelease(QEvent::KeyRelease,
                             Qt::Key_Control,
                             Qt::NoModifier);
    QApplication::sendEvent(&hoverEditor, &controlRelease);
    QEvent applicationActivate(QEvent::ApplicationActivate);
    QApplication::sendEvent(&hoverEditor, &applicationActivate);
    hideEditorHoverPopups();

    MyCodeEditor numericEditor;
    numericEditor.setLineWrapMode(QPlainTextEdit::NoWrap);
    numericEditor.resize(500, 160);
    numericEditor.setPlainText(
        QStringLiteral("module radix_hover;\n"
                       "initial a <= 16'haaaa;\n"
                       "endmodule\n"));
    numericEditor.show();
    QApplication::processEvents();
    const int numericPosition =
        numericEditor.toPlainText().indexOf(QStringLiteral("haaaa"));
    expectBool("numeric hover fixture has literal",
               numericPosition >= 0,
               true);
    if (numericPosition < 0) {
        hideEditorHoverPopups();
        if (previousGlobalSnapshot)
            SemanticIndex::getInstance()->setSnapshot(previousGlobalSnapshot);
        else
            SemanticIndex::getInstance()->clearSnapshot();
        return;
    }
    QTextCursor numericCursor(numericEditor.document());
    numericCursor.setPosition(numericPosition);
    numericEditor.setTextCursor(numericCursor);
    numericEditor.ensureCursorVisible();
    const QPoint numericPoint = numericEditor.cursorRect(numericCursor).center();
    QTest::mouseDClick(numericEditor.viewport(),
                       Qt::LeftButton,
                       Qt::NoModifier,
                       numericPoint);
    QApplication::processEvents();
    QTest::qWait(20);
    bool numericHoverVisible = false;
    const QString numericHoverText =
        visibleEditorHoverPopupText(&numericHoverVisible);
    expectBool("numeric double-click shows Slang alternate-base values",
               numericHoverVisible
                   && numericHoverText.contains(
                       QStringLiteral(
                           "binary: 1010_1010_1010_1010"))
                   && numericHoverText.contains(
                       QStringLiteral("octal: 12_5252"))
                   && numericHoverText.contains(
                       QStringLiteral("decimal: 4_3690"))
                   && numericHoverText.contains(
                       QStringLiteral("hex: aaaa")),
               true);
    QTest::mouseMove(numericEditor.viewport(), numericPoint + QPoint(2, 0));
    QApplication::processEvents();
    QTest::qWait(20);
    bool movedNumericHoverVisible = false;
    visibleEditorHoverPopupText(&movedNumericHoverVisible);
    expectBool("numeric double-click hover survives tiny mouse move",
               movedNumericHoverVisible,
               true);
    hideEditorHoverPopups();

    MyCodeEditor asciiEditor;
    asciiEditor.setLineWrapMode(QPlainTextEdit::NoWrap);
    asciiEditor.resize(500, 160);
    asciiEditor.setPlainText(
        QStringLiteral("module ascii_hover;\n"
                       "initial ch = \"A\";\n"
                       "endmodule\n"));
    asciiEditor.show();
    QApplication::processEvents();
    QTextCursor asciiCursor(asciiEditor.document());
    asciiCursor.setPosition(
        asciiEditor.toPlainText().indexOf(QStringLiteral("A\"")));
    asciiEditor.setTextCursor(asciiCursor);
    asciiEditor.ensureCursorVisible();
    QTest::mouseDClick(
        asciiEditor.viewport(),
        Qt::LeftButton,
        Qt::NoModifier,
        asciiEditor.cursorRect(asciiCursor).center());
    QApplication::processEvents();
    bool asciiHoverVisible = false;
    const QString asciiHoverText =
        visibleEditorHoverPopupText(&asciiHoverVisible);
    expectBool("ASCII double-click shows character and equivalent number",
               asciiHoverVisible
                   && asciiHoverText.contains(
                       QStringLiteral("character: \"A\""))
                   && asciiHoverText.contains(
                       QStringLiteral("decimal: 65"))
                   && asciiHoverText.contains(
                       QStringLiteral("hex: 41")),
               true);
    hideEditorHoverPopups();

    QTabWidget popupTabs;
    TabManager popupTabManager(&popupTabs, &popupTabs);
    popupTabManager.createNewTab();
    MyCodeEditor* tabPopupEditor = popupTabManager.getCurrentEditor();
    expectBool("tab popup fixture creates editor",
               tabPopupEditor != nullptr,
               true);
    if (tabPopupEditor) {
        tabPopupEditor->setPlainText(QStringLiteral("initial q = 8'h2a;\n"));
        tabPopupEditor->document()->setModified(false);
        popupTabManager.getDocumentModel()->markSaved(tabPopupEditor);
        popupTabs.resize(480, 180);
        popupTabs.show();
        QApplication::processEvents();
        QTextCursor tabLiteralCursor(tabPopupEditor->document());
        tabLiteralCursor.setPosition(
            tabPopupEditor->toPlainText().indexOf(QStringLiteral("h2a")));
        tabPopupEditor->setTextCursor(tabLiteralCursor);
        const QPoint tabLiteralPoint =
            tabPopupEditor->cursorRect(tabLiteralCursor).center();
        QTest::mouseDClick(tabPopupEditor->viewport(),
                           Qt::LeftButton,
                           Qt::NoModifier,
                           tabLiteralPoint);
        QApplication::processEvents();
        bool tabPopupVisible = false;
        visibleEditorHoverPopupText(&tabPopupVisible);
        expectBool("tab popup fixture opens popup",
                   tabPopupVisible,
                   true);
        popupTabManager.closeTab(0);
        QApplication::processEvents();
        bool closedTabPopupVisible = false;
        visibleEditorHoverPopupText(&closedTabPopupVisible);
        expectBool("closing corresponding tab closes popup",
                   popupTabs.count() == 0 && !closedTabPopupVisible,
                   true);
    }

    hideEditorHoverPopups();
    const QString parameterHoverFile = QStringLiteral(
        "C:/fixture/parameter_hover.sv");
    const QString parameterHoverText = QStringLiteral(
        "module parameter_hover #(parameter int P = 8);\n"
        "endmodule\n");
    SemanticSymbolRecord parameterHoverRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("P"),
            SymbolTaxonomy::DeclarationKind::Parameter)
            .withFile(parameterHoverFile)
            .withLocalHandle(103)
            .withLine(1, 39)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Parameter)
            .inModule(QStringLiteral("parameter_hover"))
            .withType(QStringLiteral("int"))
            .record();
    parameterHoverRecord.presentation.declarationText =
        QStringLiteral("parameter int P = 8");
    parameterHoverRecord.presentation.expressionText = QStringLiteral("8");
    parameterHoverRecord.presentation.defaultInfo.available = true;
    parameterHoverRecord.presentation.defaultInfo.valueText =
        QStringLiteral("8");
    parameterHoverRecord.presentation.defaultInfo.resolvedTypeText =
        QStringLiteral("int");
    parameterHoverRecord.presentation.defaultInfo.bitWidthText =
        QStringLiteral("32");
    parameterHoverRecord.presentation.defaultInfo.valueSourceText =
        QStringLiteral("slang elaborated parameter default");
    SemanticElaboratedSymbolInfo u0Info =
        parameterHoverRecord.presentation.defaultInfo;
    u0Info.valueText = QStringLiteral("11");
    u0Info.valueSourceText =
        QStringLiteral("slang elaborated parameter override");
    SemanticElaboratedSymbolInfo u1Info = u0Info;
    u1Info.valueText = QStringLiteral("22");
    parameterHoverRecord.presentation.instanceInfoByPath.insert(
        QStringLiteral("top.u0"), u0Info);
    parameterHoverRecord.presentation.instanceInfoByPath.insert(
        QStringLiteral("top.u1"), u1Info);
    SemanticIndex parameterHoverIndex;
    parameterHoverIndex.setSnapshot(snapshotFromRecords(
        {parameterHoverRecord},
        {},
        {},
        {{parameterHoverFile, parameterHoverText}}));
    SemanticIndex::getInstance()->setSnapshot(parameterHoverIndex.snapshot());
    SymbolHoverService::getInstance()->setSemanticIndex(
        SemanticIndex::getInstance());

    MyCodeEditor parameterHoverEditor;
    parameterHoverEditor.setDocumentFileName(parameterHoverFile);
    parameterHoverEditor.setPlainText(parameterHoverText);
    parameterHoverEditor.resize(680, 180);
    HierarchyInstanceContext u1Context;
    u1Context.workspacePath = QStringLiteral("C:/fixture");
    u1Context.activeTopModule = QStringLiteral("top");
    u1Context.instancePath = QStringLiteral("top.u1");
    parameterHoverEditor.setHierarchyInstanceContext(u1Context);
    parameterHoverEditor.show();
    QApplication::processEvents();
    parameterHoverRecord.presentation.documentRevision =
        parameterHoverEditor.semanticDocumentRevision();
    parameterHoverIndex.setSnapshot(snapshotFromRecords(
        {parameterHoverRecord},
        {},
        {},
        {{parameterHoverFile, parameterHoverText}}));
    SemanticIndex::getInstance()->setSnapshot(parameterHoverIndex.snapshot());
    QTextCursor parameterCursor(parameterHoverEditor.document());
    parameterCursor.setPosition(
        parameterHoverText.indexOf(QStringLiteral("P =")));
    parameterHoverEditor.setTextCursor(parameterCursor);
    EditorSemanticContext exactTextUnknownRevisionContext;
    exactTextUnknownRevisionContext.fileName = parameterHoverFile;
    exactTextUnknownRevisionContext.moduleName =
        QStringLiteral("parameter_hover");
    exactTextUnknownRevisionContext.documentText = parameterHoverText;
    exactTextUnknownRevisionContext.lineText =
        parameterHoverText.section(QLatin1Char('\n'), 0, 0);
    exactTextUnknownRevisionContext.cursorLine = 1;
    exactTextUnknownRevisionContext.column =
        exactTextUnknownRevisionContext.lineText.indexOf(
            QStringLiteral("P ="));
    exactTextUnknownRevisionContext.documentRevision =
        parameterHoverEditor.semanticDocumentRevision();
    exactTextUnknownRevisionContext.hierarchyInstance = u1Context;
    SymbolHoverService exactTextUnknownRevisionHover(&parameterHoverIndex);
    const SymbolHoverReport exactTextUnknownRevisionReport =
        exactTextUnknownRevisionHover.hoverForContext(
            exactTextUnknownRevisionContext);
    expectBool("matching published editor revision is current",
               exactTextUnknownRevisionReport.effectiveValueStatus
                       == EffectiveValueStatus::Current
                   && exactTextUnknownRevisionReport.valueText
                          == QStringLiteral("22"),
               true);
    const QPoint parameterPoint =
        parameterHoverEditor.cursorRect(parameterCursor).center();
    QTest::mouseDClick(parameterHoverEditor.viewport(),
                       Qt::LeftButton,
                       Qt::NoModifier,
                       parameterPoint);
    QApplication::processEvents();
    bool boundValuePopupVisible = false;
    const QString boundValuePopup =
        visibleEditorHoverPopupText(&boundValuePopupVisible);
    expectBool("double-click popup shows bound instance value",
               boundValuePopupVisible
                   && boundValuePopup.contains(
                       QStringLiteral("effective value: 22"))
                    && boundValuePopup.contains(
                        QStringLiteral("instance path: top.u1"))
                    && !boundValuePopup.contains(
                        QStringLiteral("effective value: 11"))
                    && !boundValuePopup.contains(
                        QStringLiteral("\nsource:")),
                true);

    HierarchyInstanceContext unboundContext;
    unboundContext.workspacePath = QStringLiteral("C:/fixture");
    parameterHoverEditor.setHierarchyInstanceContext(unboundContext);
    QTest::mouseDClick(parameterHoverEditor.viewport(),
                       Qt::LeftButton,
                       Qt::NoModifier,
                       parameterPoint);
    QApplication::processEvents();
    bool defaultValuePopupVisible = false;
    const QString defaultValuePopup =
        visibleEditorHoverPopupText(&defaultValuePopupVisible);
    expectBool("unbound double-click popup suppresses non-current default",
               defaultValuePopupVisible
                   && defaultValuePopup.contains(
                       QStringLiteral(
                           "effective value: unavailable"))
                   && !defaultValuePopup.contains(
                       QStringLiteral("default value"))
                   && !defaultValuePopup.contains(
                       QStringLiteral("effective value: 8")),
               true);
    parameterHoverEditor.closeSemanticPopup();

    EditorSemanticContext staleParameterContext;
    staleParameterContext.fileName = parameterHoverFile;
    staleParameterContext.moduleName = QStringLiteral("parameter_hover");
    staleParameterContext.documentText =
        parameterHoverText + QStringLiteral("// unsaved edit\n");
    staleParameterContext.lineText =
        parameterHoverText.section(QLatin1Char('\n'), 0, 0);
    staleParameterContext.cursorLine = 1;
    staleParameterContext.column =
        staleParameterContext.lineText.indexOf(QStringLiteral("P ="));
    SymbolHoverService parameterHoverService(&parameterHoverIndex);
    const SymbolHoverReport staleParameterHover =
        parameterHoverService.hoverForContext(staleParameterContext);
    expectBool("hover propagates stale effective value status",
               staleParameterHover.effectiveValueStatus
                   == EffectiveValueStatus::Stale,
               true);
    EditorHoverPopup staleParameterPopup(&parameterHoverEditor);
    staleParameterPopup.showHover(staleParameterHover,
                                  QPoint(20, 20),
                                  parameterHoverEditor.font());
    QApplication::processEvents();
    QStringList staleParameterLabels;
    for (QLabel* label : staleParameterPopup.findChildren<QLabel*>())
        staleParameterLabels.append(peekLabelText(label));
    const QString staleParameterPopupText =
        staleParameterLabels.join(QLatin1Char('\n'));
    expectBool("stale hover suppresses old current/default value",
               staleParameterPopupText.contains(
                    QStringLiteral("effective value: stale"))
                   && !staleParameterPopupText.contains(
                       QStringLiteral("effective value: 8"))
                   && !staleParameterPopupText.contains(
                       QStringLiteral("default value")),
               true);
    staleParameterPopup.close();

    const QString packageHoverFile = QStringLiteral(
        "C:/fixture/package_hover.sv");
    const QString packageHoverText = QStringLiteral(
        "package cfg_pkg;\n"
        "  parameter int PKG_P = 12;\n"
        "endpackage\n"
        "module package_user;\n"
        "  localparam int SHOULD_NOT_RESOLVE = PKG_P;\n"
        "endmodule\n");
    const QString packageParameterLine =
        packageHoverText.section(QLatin1Char('\n'), 1, 1);
    const int packageParameterColumn =
        packageParameterLine.indexOf(QStringLiteral("PKG_P"));
    SemanticSymbolRecord packageHoverRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("PKG_P"),
            SymbolTaxonomy::DeclarationKind::Parameter)
            .withFile(packageHoverFile)
            .withLocalHandle(104)
            .withLine(2, packageParameterColumn + 1)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Parameter)
            .inPackage(QStringLiteral("cfg_pkg"))
            .withType(QStringLiteral("int"))
            .record();
    packageHoverRecord.presentation.declarationText =
        QStringLiteral("parameter int PKG_P = 12");
    packageHoverRecord.presentation.expressionText = QStringLiteral("12");
    packageHoverRecord.presentation.defaultInfo.available = true;
    packageHoverRecord.presentation.defaultInfo.valueText =
        QStringLiteral("12");
    packageHoverRecord.presentation.defaultInfo.resolvedTypeText =
        QStringLiteral("int");
    packageHoverRecord.presentation.defaultInfo.valueSourceText =
        QStringLiteral("Slang package elaboration");
    SemanticIndex packageHoverIndex;
    packageHoverIndex.setSnapshot(snapshotFromRecords(
        {packageHoverRecord},
        {},
        {},
        {{packageHoverFile, packageHoverText}}));
    EditorSemanticContext packageHoverContext;
    packageHoverContext.fileName = packageHoverFile;
    packageHoverContext.documentText = packageHoverText;
    packageHoverContext.lineText = packageParameterLine;
    packageHoverContext.cursorLine = 2;
    packageHoverContext.column = packageParameterColumn;
    SymbolHoverService packageHoverService(&packageHoverIndex);
    const SymbolHoverReport packageHover =
        packageHoverService.hoverForContext(packageHoverContext);
    expectBool("package hover is current and instance-independent",
               packageHover.effectiveValueStatus
                       == EffectiveValueStatus::Current
                   && !packageHover.instanceBound
                   && !packageHover.defaultEvaluation,
               true);
    EditorHoverPopup packagePopup(&parameterHoverEditor);
    packagePopup.showHover(packageHover,
                           QPoint(20, 20),
                           parameterHoverEditor.font());
    QApplication::processEvents();
    QStringList packageLabels;
    for (QLabel* label : packagePopup.findChildren<QLabel*>())
        packageLabels.append(peekLabelText(label));
    const QString packagePopupText = packageLabels.join(QLatin1Char('\n'));
    expectBool("package hover never labels value as unbound default",
               packagePopupText.contains(
                   QStringLiteral("effective value: 12"))
                   && !packagePopupText.contains(
                       QStringLiteral("default value"))
                   && !packagePopupText.contains(
                       QStringLiteral("\u672a\u7ed1\u5b9a\u5b9e\u4f8b")),
               true);
    packagePopup.close();

    const QString unimportedPackageUseLine =
        packageHoverText.section(QLatin1Char('\n'), 4, 4);
    EditorSemanticContext unimportedPackageUseContext;
    unimportedPackageUseContext.fileName = packageHoverFile;
    unimportedPackageUseContext.moduleName = QStringLiteral("package_user");
    unimportedPackageUseContext.documentText = packageHoverText;
    unimportedPackageUseContext.lineText = unimportedPackageUseLine;
    unimportedPackageUseContext.cursorLine = 5;
    unimportedPackageUseContext.column =
        unimportedPackageUseLine.lastIndexOf(QStringLiteral("PKG_P"));
    const SymbolHoverReport unimportedPackageUse =
        packageHoverService.hoverForContext(unimportedPackageUseContext);
    expectBool("package self declaration does not widen package visibility",
               !unimportedPackageUse.unavailableReason.isEmpty()
                   && unimportedPackageUse.effectiveValueStatus
                          == EffectiveValueStatus::Unavailable,
               true);

    if (previousGlobalSnapshot)
        SemanticIndex::getInstance()->setSnapshot(previousGlobalSnapshot);
    else
        SemanticIndex::getInstance()->clearSnapshot();

    const EditorSourceNavigationTarget navigationTarget =
        EditorSemanticContextService::getInstance()
            ->editorSourceNavigationTarget(context, 0);
    const EditorSourceNavigationClickState clickState =
        EditorSemanticContextService::getInstance()
            ->sourceNavigationClickState(navigationTarget);
    expectBool("ctrl click navigation remains definition action",
               clickState.action
                   == EditorSourceNavigationClickAction::NavigateToDefinition,
               true);
}

static void runActivityInboxRegression()
{
    MainWindow window;
    window.resize(1000, 720);
    window.show();
    auto* controller = window.panelLayoutController.get();
    auto* service = ActivityLogService::getInstance();
    expectBool("Activity inbox has a drawer controller", controller != nullptr, true);
    if (!controller)
        return;
    controller->setAnimationsEnabled(false);
    controller->setBottomCollapsed(true);
    QCoreApplication::processEvents();
    service->clear();
    auto* button = controller->buttonForPanel(QStringLiteral("activity"));
    expectBool("Activity inbox button exists", button != nullptr, true);
    const QString active = controller->activeBottomPanelId();
    service->append(QStringLiteral("Test"), ActivityLogLevel::Warning, QStringLiteral("first important result"));
    service->append(QStringLiteral("Test"), ActivityLogLevel::Error, QStringLiteral("second important result"));
    QCoreApplication::processEvents();
    expectBool("Activity displays unread number without opening or switching",
        controller->panelBadgeText(QStringLiteral("activity")) == QStringLiteral("2")
        && controller->isBottomCollapsed() && controller->activeBottomPanelId() == active, true);
    if (button)
        button->click();
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents();
    auto* output = window.findChild<QPlainTextEdit*>(QStringLiteral("activityOutputText"));
    expectBool("opening Activity shows messages and acknowledges unread",
        output && output->isVisible()
        && output->toPlainText().contains(QStringLiteral("second important result"))
        && service->unreadCount() == 0
        && controller->panelBadgeText(QStringLiteral("activity")).isEmpty(), true);
    controller->setBottomCollapsed(true);
    QCoreApplication::processEvents();
    service->append(QStringLiteral("Test"), ActivityLogLevel::Info, QStringLiteral("progress only"));
    window.workspaceManager->workspaceScanStarted(QStringLiteral("test-workspace"));
    window.workspaceManager->workspaceScanFinished(QStringLiteral("test-workspace"), 12, 4);
    QCoreApplication::processEvents();
    expectBool("scan messages are retained without unread badge", service->unreadCount() == 0, true);
    expectBool("Activity routing does not recreate status bar", window.findChild<QStatusBar*>() == nullptr, true);
    service->clear();
}

static void runActivityLogServiceRegression()
{
    ActivityLogService* service = ActivityLogService::getInstance();
    service->clear();
    QSignalSpy eventSpy(service, &ActivityLogService::eventAppended);
    QSignalSpy clearSpy(service, &ActivityLogService::cleared);

    service->append(QStringLiteral("Test"), ActivityLogLevel::Info, QStringLiteral("progress"));
    expectBool("ordinary progress does not create unread attention", service->unreadCount() == 0, true);
    service->append(QStringLiteral("Test"), ActivityLogLevel::Warning, QStringLiteral("warning"));
    const quint64 firstUnread = service->events().last().sequence;
    service->append(QStringLiteral("Test"), ActivityLogLevel::Info, QStringLiteral("operation result"),
                    -1, QString(), true);
    expectBool("warnings and explicit results count once each", service->unreadCount() == 2, true);
    service->markReadThrough(firstUnread);
    expectBool("reading an older event preserves newer unread results", service->unreadCount() == 1, true);
    service->markReadThrough(service->events().last().sequence);
    expectBool("reading displayed results clears attention", service->unreadCount() == 0, true);
    service->clear();
    eventSpy.clear();

    service->append(QStringLiteral("Test"),
                    ActivityLogLevel::Info,
                    QStringLiteral("probe"),
                    12,
                    QStringLiteral("abc"));
    expectBool("activity log appends event",
               service->events().size() == 1 && eventSpy.count() == 1,
               true);
    const QString formatted =
        ActivityLogService::formatEvent(service->events().first());
    expectBool("activity log formats source",
               formatted.contains(QStringLiteral("[Test]")),
               true);
    expectBool("activity log formats duration",
               formatted.contains(QStringLiteral("12 ms")),
               true);
    service->clear();
    expectBool("activity log clears events",
               service->events().isEmpty() && clearSpy.count() >= 1,
               true);

    AnalysisProgressCoordinator coordinator(nullptr);
    QSignalSpy workspaceStatusSpy(
        &coordinator,
        &AnalysisProgressCoordinator::statusMessageRequested);
    WorkspaceAnalysisRequestTelemetry queuedTelemetry;
    queuedTelemetry.active = true;
    queuedTelemetry.pending = true;
    queuedTelemetry.activeAgeMs = 25;
    queuedTelemetry.pendingAgeMs = 5;
    queuedTelemetry.pendingUpdateCount = 2;
    coordinator.handleWorkspaceAnalysisRequestQueued(queuedTelemetry);
    WorkspaceAnalysisRequestTelemetry resolvedTelemetry;
    resolvedTelemetry.lastFinishedActiveAgeMs = 40;
    resolvedTelemetry.lastTakenPendingAgeMs = 12;
    resolvedTelemetry.lastTakenPendingUpdateCount = 2;
    coordinator.handleWorkspaceAnalysisRequestResolved(resolvedTelemetry);

    bool sawQueuedTelemetry = false;
    bool sawResolvedTelemetry = false;
    bool sawResolvedRestart = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawQueuedTelemetry = sawQueuedTelemetry
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Workspace request queued"))
                && event.message.contains(QStringLiteral("pending updates 2")));
        sawResolvedTelemetry = sawResolvedTelemetry
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Workspace request resolved"))
                && event.message.contains(QStringLiteral("pending wait 12 ms")));
        sawResolvedRestart = sawResolvedRestart
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("restarting latest request")));
    }
    expectBool("activity log records workspace queued telemetry",
               sawQueuedTelemetry,
               true);
    expectBool("activity log records workspace resolved telemetry",
               sawResolvedTelemetry,
               true);
    expectBool("activity log records workspace restart visibility",
               sawResolvedRestart,
               true);
    expectBool("workspace request telemetry emits status visibility",
               workspaceStatusSpy.count() >= 2
                   && workspaceStatusSpy.at(0).at(0).toString().contains(
                       QStringLiteral("latest request"))
                   && workspaceStatusSpy.at(1).at(0).toString().contains(
                       QStringLiteral("restarting")),
               true);
    service->clear();

    AnalysisProgressCoordinator planCoordinator(nullptr);
    QSignalSpy planStatusSpy(
        &planCoordinator,
        &AnalysisProgressCoordinator::statusMessageRequested);
    WorkspaceAnalysisPlan planSummary;
    planSummary.project.workspaceRoot = QStringLiteral("E:/workspace");
    planSummary.project.systemVerilogFiles = {
        QStringLiteral("E:/workspace/current.sv"),
        QStringLiteral("E:/workspace/dirty.sv"),
        QStringLiteral("E:/workspace/open.sv"),
        QStringLiteral("E:/workspace/background.sv")
    };
    planSummary.openFiles = {
        QStringLiteral("E:/workspace/dirty.sv"),
        QStringLiteral("E:/workspace/open.sv")
    };
    planSummary.dirtyOpenFiles = {
        QStringLiteral("E:/workspace/dirty.sv")
    };
    planSummary.currentFilePriorityFiles = {
        QStringLiteral("E:/workspace/current.sv")
    };
    planSummary.dirtyOpenPriorityFiles = {
        QStringLiteral("E:/workspace/dirty.sv")
    };
    planSummary.cleanOpenPriorityFiles = {
        QStringLiteral("E:/workspace/open.sv")
    };
    planSummary.backgroundFiles = {
        QStringLiteral("E:/workspace/background.sv")
    };
    planSummary.bandSummaries = {
        {QStringLiteral("current"), QStringLiteral("current"), 1, true, 1},
        {QStringLiteral("dirty-open"), QStringLiteral("dirty"), 1, true, 2},
        {QStringLiteral("open"), QStringLiteral("open"), 1, true, 3},
        {QStringLiteral("background"), QStringLiteral("background"), 1, false, 0}
    };
    planSummary.priorityFileCount = 3;
    planSummary.backgroundFileCount = 1;
    planSummary.currentFileInWorkspace = true;
    planCoordinator.handleWorkspaceAnalysisPlanPrepared(planSummary);
    bool sawPlanSummary = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawPlanSummary = sawPlanSummary
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Workspace plan prepared"))
                && event.message.contains(QStringLiteral("3 priority"))
                && event.message.contains(QStringLiteral("1 background"))
                && event.message.contains(QStringLiteral("1 dirty"))
                && event.message.contains(
                    QStringLiteral("bands current 1, dirty 1, open 1, background 1")));
    }
    expectBool("activity log records workspace plan summary",
               sawPlanSummary,
               true);
    expectBool("workspace plan summary emits status",
               planStatusSpy.count() == 1
                   && planStatusSpy.at(0).at(0).toString().contains(
                       QStringLiteral("3 priority (1/1/1) / 4 files")),
               true);
    planCoordinator.handleWorkspaceSymbolProgress(
        1,
        4,
        QStringLiteral("E:/workspace/current.sv"));
    bool sawPlanBandProgress = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawPlanBandProgress = sawPlanBandProgress
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(
                    QStringLiteral("Symbol analysis progress: 25%"))
                && event.message.contains(QStringLiteral("[current]"))
                && event.message.contains(QStringLiteral("current.sv")));
    }
    expectBool("activity log records workspace plan progress band",
               sawPlanBandProgress,
               true);
    expectBool("workspace plan progress emits band status",
               planStatusSpy.count() == 2
                   && planStatusSpy.at(1).at(0).toString().contains(
                       QStringLiteral("[current]"))
                   && planStatusSpy.at(1).at(0).toString().contains(
                       QStringLiteral("current.sv")),
               true);
    service->clear();

    const QString bandReportCurrentFile =
        QDir::temp().absoluteFilePath(QStringLiteral("zs_activity_current.sv"));
    const QString bandReportBackgroundFile =
        QDir::temp().absoluteFilePath(QStringLiteral("zs_activity_background.sv"));
    QHash<QString, SemanticAnalysisBandMetadata> activityBandMetadata;
    SemanticAnalysisBandMetadata currentBand;
    currentBand.label = QStringLiteral("current");
    currentBand.displayName = QStringLiteral("current");
    currentBand.priority = true;
    currentBand.publicationCheckpoint = 1;
    activityBandMetadata.insert(bandReportCurrentFile, currentBand);
    SemanticAnalysisBandMetadata backgroundBand;
    backgroundBand.label = QStringLiteral("background");
    backgroundBand.displayName = QStringLiteral("background");
    backgroundBand.priority = false;
    activityBandMetadata.insert(bandReportBackgroundFile, backgroundBand);
    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    semanticIndex->setWorkspaceFileAnalysisBands(activityBandMetadata);
    const SemanticSymbolRecord currentBandModule =
        SemanticFixtureRecordBuilder(
            QStringLiteral("activity_current_top"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(bandReportCurrentFile)
            .withLocalHandle(91001)
            .record();
    const SemanticSymbolRecord currentBandSignal =
        SemanticFixtureRecordBuilder(
            QStringLiteral("activity_current_sig"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(bandReportCurrentFile)
            .withLocalHandle(91002)
            .inModule(QStringLiteral("activity_current_top"))
            .record();
    const SemanticSymbolRecord backgroundBandModule =
        SemanticFixtureRecordBuilder(
            QStringLiteral("activity_background_top"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(bandReportBackgroundFile)
            .withLocalHandle(91003)
            .record();
    semanticIndex->updateSymbolRecordsForFile(
        bandReportCurrentFile,
        {currentBandModule, currentBandSignal},
        QStringLiteral("module activity_current_top; logic activity_current_sig; endmodule\n"));
    semanticIndex->updateSymbolRecordsForFile(
        bandReportBackgroundFile,
        {backgroundBandModule},
        QStringLiteral("module activity_background_top; endmodule\n"));
    ProjectSnapshot bandReportProject;
    bandReportProject.workspaceRoot = QDir::tempPath();
    bandReportProject.systemVerilogFiles = {
        bandReportCurrentFile,
        bandReportBackgroundFile
    };
    QSignalSpy bandReportStatusSpy(
        &planCoordinator,
        &AnalysisProgressCoordinator::statusMessageRequested);
    planCoordinator.handleWorkspaceSymbolAnalysisFinished(
        bandReportProject,
        2,
        3);
    bool sawAnalysisBandReport = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawAnalysisBandReport = sawAnalysisBandReport
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Parsed 2 files, 3 symbols"))
                && event.message.contains(
                    QStringLiteral("bands current 2 symbols/1 file"))
                && event.message.contains(
                    QStringLiteral("background 1 symbol/1 file")));
    }
    expectBool("activity log records analysis band report summary",
               sawAnalysisBandReport,
               true);
    expectBool("workspace symbol finish keeps status visibility",
               bandReportStatusSpy.count() == 1
                   && bandReportStatusSpy.at(0).at(0).toString().contains(
                       QStringLiteral("2 files, 3 symbols")),
               true);
    semanticIndex->updateSymbolRecordsForFile(
        bandReportCurrentFile,
        {},
        QString());
    semanticIndex->updateSymbolRecordsForFile(
        bandReportBackgroundFile,
        {},
        QString());
    semanticIndex->clearWorkspaceFileAnalysisBands();
    service->clear();

    const QString diagnosticActivityCurrentFile =
        QDir::temp().absoluteFilePath(QStringLiteral("zs_activity_diag_current.sv"));
    const QString diagnosticActivityBackgroundFile =
        QDir::temp().absoluteFilePath(QStringLiteral("zs_activity_diag_background.sv"));
    SemanticDiagnostic diagnosticActivityWarning;
    diagnosticActivityWarning.fileName = diagnosticActivityCurrentFile;
    diagnosticActivityWarning.line = 3;
    diagnosticActivityWarning.column = 5;
    diagnosticActivityWarning.message = QStringLiteral("current warning");
    diagnosticActivityWarning.severity = SemanticDiagnostic::Warning;
    diagnosticActivityWarning.owner = SemanticDiagnostic::SemanticIndexOwner;
    SemanticDiagnostic diagnosticActivityInfo;
    diagnosticActivityInfo.fileName = diagnosticActivityCurrentFile;
    diagnosticActivityInfo.line = 4;
    diagnosticActivityInfo.column = 7;
    diagnosticActivityInfo.message = QStringLiteral("current info");
    diagnosticActivityInfo.severity = SemanticDiagnostic::Info;
    diagnosticActivityInfo.owner = SemanticDiagnostic::SemanticIndexOwner;
    SemanticDiagnostic diagnosticActivityError;
    diagnosticActivityError.fileName = diagnosticActivityBackgroundFile;
    diagnosticActivityError.line = 8;
    diagnosticActivityError.column = 2;
    diagnosticActivityError.message = QStringLiteral("background error");
    diagnosticActivityError.severity = SemanticDiagnostic::Error;
    diagnosticActivityError.owner = SemanticDiagnostic::SlangCompiler;
    semanticIndex->setSnapshot(snapshotFromRecords(
        {},
        {},
        {diagnosticActivityWarning,
         diagnosticActivityInfo,
         diagnosticActivityError}));
    QHash<QString, SemanticAnalysisBandMetadata> diagnosticActivityBands;
    diagnosticActivityBands.insert(diagnosticActivityCurrentFile, currentBand);
    diagnosticActivityBands.insert(diagnosticActivityBackgroundFile, backgroundBand);
    semanticIndex->setWorkspaceFileAnalysisBands(diagnosticActivityBands);

    QWidget diagnosticActivityParent;
    ProblemsPanelCoordinator diagnosticActivityProblems(&diagnosticActivityParent);
    diagnosticActivityProblems.setCurrentFileProvider([=]() {
        return diagnosticActivityCurrentFile;
    });
    diagnosticActivityProblems.setWorkspaceFilesProvider([=]() {
        return QStringList{
            diagnosticActivityCurrentFile,
            diagnosticActivityBackgroundFile,
        };
    });
    if (diagnosticActivityProblems.scopeCombo()) {
        const int allFilesIndex =
            diagnosticActivityProblems.scopeCombo()->findText(
                QStringLiteral("All Files"));
        if (allFilesIndex >= 0)
            diagnosticActivityProblems.scopeCombo()->setCurrentIndex(allFilesIndex);
    }
    if (diagnosticActivityProblems.dock())
        diagnosticActivityProblems.dock()->hide();
    const int hiddenProblemsHeightBeforeUpdate =
        diagnosticActivityProblems.dock()
        ? diagnosticActivityProblems.dock()->height()
        : -1;
    QWidget* const focusBeforeProblemsUpdate =
        QApplication::focusWidget();
    diagnosticActivityProblems.update();
    expectBool("diagnostic result update keeps a closed page passive",
               diagnosticActivityProblems.dock()
                   && diagnosticActivityProblems.dock()->isHidden()
                   && diagnosticActivityProblems.dock()->height()
                          == hiddenProblemsHeightBeforeUpdate
                   && QApplication::focusWidget()
                          == focusBeforeProblemsUpdate,
               true);
    expectBool("problems current file diagnostic summary",
               diagnosticActivityProblems.summaryLabel()
                   && diagnosticActivityProblems.summaryLabel()->text()
                          == QStringLiteral(
                              "Current file: 0 errors, 1 warnings, 1 info"),
               true);
    expectBool("problems diagnostic state label",
               diagnosticActivityProblems.stateLabel()
                   && diagnosticActivityProblems.stateLabel()->text()
                          == QStringLiteral(
                              "Diagnostics: current + background"),
               true);
    diagnosticActivityProblems.setAnalysisState(QStringLiteral("analyzing"));
    expectBool("problems explicit diagnostic state label",
               diagnosticActivityProblems.stateLabel()
                   && diagnosticActivityProblems.stateLabel()->text()
                          == QStringLiteral("Diagnostics: analyzing"),
               true);
    diagnosticActivityProblems.setAnalysisState(QString());
    bool sawBandCountLabels = false;
    if (diagnosticActivityProblems.bandCombo()) {
        const int allBandIndex =
            diagnosticActivityProblems.bandCombo()->findData(QString());
        const int currentIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("current"));
        const int backgroundIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("background"));
        sawBandCountLabels =
            allBandIndex >= 0
            && currentIndex >= 0
            && backgroundIndex >= 0
            && diagnosticActivityProblems.bandCombo()->itemText(allBandIndex)
                == QStringLiteral("All Bands (3)")
            && diagnosticActivityProblems.bandCombo()->itemText(currentIndex)
                == QStringLiteral("Current (2)")
            && diagnosticActivityProblems.bandCombo()->itemText(backgroundIndex)
                == QStringLiteral("Background (1)");
    }
    expectBool("problems band filter shows counts",
               sawBandCountLabels,
               true);
    bool sawBandSeverityTooltips = false;
    if (diagnosticActivityProblems.bandCombo()) {
        const int allBandIndex =
            diagnosticActivityProblems.bandCombo()->findData(QString());
        const int currentIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("current"));
        const int backgroundIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("background"));
        const QString allToolTip =
            allBandIndex >= 0
                ? diagnosticActivityProblems.bandCombo()
                      ->itemData(allBandIndex, Qt::ToolTipRole)
                      .toString()
                : QString();
        const QString currentToolTip =
            currentIndex >= 0
                ? diagnosticActivityProblems.bandCombo()
                      ->itemData(currentIndex, Qt::ToolTipRole)
                      .toString()
                : QString();
        const QString backgroundToolTip =
            backgroundIndex >= 0
                ? diagnosticActivityProblems.bandCombo()
                      ->itemData(backgroundIndex, Qt::ToolTipRole)
                      .toString()
                : QString();
        sawBandSeverityTooltips =
            allToolTip.contains(
                QStringLiteral("current 2 diagnostics"))
            && allToolTip.contains(QStringLiteral("1 warning"))
            && allToolTip.contains(QStringLiteral("1 info"))
            && allToolTip.contains(
                QStringLiteral("background 1 diagnostic"))
            && allToolTip.contains(QStringLiteral("1 error"))
            && currentToolTip
                == QStringLiteral(
                    "current 2 diagnostics (1 warning, 1 info)")
            && backgroundToolTip
                == QStringLiteral("background 1 diagnostic (1 error)");
    }
    expectBool("problems band filter shows severity tooltips",
               sawBandSeverityTooltips,
               true);
    QTreeWidget* diagnosticActivityTree = diagnosticActivityProblems.tree();
    expectBool("problems band column visible",
               diagnosticActivityTree
                   && diagnosticActivityTree->columnCount() == 7
                   && diagnosticActivityTree->headerItem()
                   && diagnosticActivityTree->headerItem()->text(5)
                       == QStringLiteral("Owner")
                   && diagnosticActivityTree->headerItem()->text(6)
                       == QStringLiteral("Band"),
               true);
    bool sawCurrentBandRow = false;
    bool sawBackgroundBandRow = false;
    bool sawSlangOwnerRow = false;
    bool sawSemanticOwnerRow = false;
    if (diagnosticActivityTree) {
        for (int i = 0; i < diagnosticActivityTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* group = diagnosticActivityTree->topLevelItem(i);
            if (!group)
                continue;
            sawCurrentBandRow = sawCurrentBandRow
                || group->text(6) == QStringLiteral("current");
            sawBackgroundBandRow = sawBackgroundBandRow
                || group->text(6) == QStringLiteral("background");
            for (int child = 0; child < group->childCount(); ++child) {
                QTreeWidgetItem* row = group->child(child);
                sawCurrentBandRow = sawCurrentBandRow
                    || (row && row->text(6) == QStringLiteral("current"));
                sawBackgroundBandRow = sawBackgroundBandRow
                    || (row && row->text(6) == QStringLiteral("background"));
                sawSlangOwnerRow = sawSlangOwnerRow
                    || (row && row->text(5) == QStringLiteral("Slang"));
                sawSemanticOwnerRow = sawSemanticOwnerRow
                    || (row && row->text(5)
                               == QStringLiteral("Semantic index"));
            }
        }
    }
    expectBool("problems rows show diagnostic bands",
               sawCurrentBandRow && sawBackgroundBandRow,
               true);
    expectBool("problems rows show diagnostic owners",
               sawSlangOwnerRow && sawSemanticOwnerRow,
               true);
    if (diagnosticActivityProblems.bandCombo()) {
        const int backgroundIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("background"));
        if (backgroundIndex >= 0)
            diagnosticActivityProblems.bandCombo()->setCurrentIndex(backgroundIndex);
    }
    bool sawOnlyBackgroundRows = false;
    if (diagnosticActivityTree) {
        int diagnosticRows = 0;
        bool allBackground = true;
        for (int i = 0; i < diagnosticActivityTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* group = diagnosticActivityTree->topLevelItem(i);
            if (!group)
                continue;
            if (group->childCount() == 0) {
                ++diagnosticRows;
                allBackground = allBackground
                    && group->text(6) == QStringLiteral("background");
                continue;
            }
            for (int child = 0; child < group->childCount(); ++child) {
                QTreeWidgetItem* row = group->child(child);
                if (!row)
                    continue;
                ++diagnosticRows;
                allBackground = allBackground
                    && row->text(6) == QStringLiteral("background");
            }
        }
        sawOnlyBackgroundRows = diagnosticRows == 1 && allBackground;
    }
    expectBool("problems band filter narrows diagnostics",
               sawOnlyBackgroundRows,
               true);
    bool keptUnfilteredBandCounts = false;
    if (diagnosticActivityProblems.bandCombo()) {
        const int currentIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("current"));
        const int backgroundIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("background"));
        keptUnfilteredBandCounts =
            currentIndex >= 0
            && backgroundIndex >= 0
            && diagnosticActivityProblems.bandCombo()->itemText(currentIndex)
                == QStringLiteral("Current (2)")
            && diagnosticActivityProblems.bandCombo()->itemText(backgroundIndex)
                == QStringLiteral("Background (1)");
    }
    expectBool("problems band counts ignore band filter",
               keptUnfilteredBandCounts,
               true);
    bool keptUnfilteredBandTooltips = false;
    if (diagnosticActivityProblems.bandCombo()) {
        const int currentIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("current"));
        const int backgroundIndex =
            diagnosticActivityProblems.bandCombo()->findData(
                QStringLiteral("background"));
        const QString currentToolTip =
            currentIndex >= 0
                ? diagnosticActivityProblems.bandCombo()
                      ->itemData(currentIndex, Qt::ToolTipRole)
                      .toString()
                : QString();
        const QString backgroundToolTip =
            backgroundIndex >= 0
                ? diagnosticActivityProblems.bandCombo()
                      ->itemData(backgroundIndex, Qt::ToolTipRole)
                      .toString()
                : QString();
        keptUnfilteredBandTooltips =
            currentToolTip
                == QStringLiteral(
                    "current 2 diagnostics (1 warning, 1 info)")
            && backgroundToolTip
                == QStringLiteral("background 1 diagnostic (1 error)");
    }
    expectBool("problems band tooltips ignore band filter",
               keptUnfilteredBandTooltips,
               true);
    bool sawDiagnosticBandActivity = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawDiagnosticBandActivity = sawDiagnosticBandActivity
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(
                    QStringLiteral("Diagnostics visible: 3 diagnostics"))
                && event.message.contains(
                    QStringLiteral("diagnostic bands current 2 diagnostics"))
                && event.message.contains(QStringLiteral("1 warning"))
                && event.message.contains(QStringLiteral("1 info"))
                && event.message.contains(
                    QStringLiteral("background 1 diagnostic"))
                && event.message.contains(QStringLiteral("1 error")));
    }
    expectBool("activity log records diagnostic band summary",
               sawDiagnosticBandActivity,
               true);
    semanticIndex->clearSnapshot();
    semanticIndex->clearWorkspaceFileAnalysisBands();
    service->clear();

    AnalysisProgressCoordinator progressCoordinator(nullptr);
    progressCoordinator.handleWorkspaceSymbolProgress(
        1,
        4,
        QStringLiteral("E:/workspace/a.sv"));
    progressCoordinator.handleWorkspaceSymbolProgress(
        2,
        4,
        QStringLiteral("E:/workspace/b.sv"));
    progressCoordinator.handleWorkspaceSymbolProgress(
        4,
        4,
        QStringLiteral("E:/workspace/d.sv"));
    progressCoordinator.showWorkspaceRelationshipProgress(1, 4);
    progressCoordinator.showWorkspaceRelationshipProgress(3, 4);
    progressCoordinator.showWorkspaceRelationshipProgress(4, 4);
    bool sawSymbol25 = false;
    bool sawSymbol50 = false;
    bool sawSymbol100 = false;
    bool sawRelationship25 = false;
    bool sawRelationship75 = false;
    bool sawRelationship100 = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawSymbol25 = sawSymbol25
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Symbol analysis progress: 25%"))
                && event.message.contains(QStringLiteral("a.sv")));
        sawSymbol50 = sawSymbol50
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Symbol analysis progress: 50%"))
                && event.message.contains(QStringLiteral("b.sv")));
        sawSymbol100 = sawSymbol100
            || event.message.contains(QStringLiteral("Symbol analysis progress: 100%"));
        sawRelationship25 = sawRelationship25
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Relationship analysis progress: 25%")));
        sawRelationship75 = sawRelationship75
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(QStringLiteral("Relationship analysis progress: 75%")));
        sawRelationship100 = sawRelationship100
            || event.message.contains(QStringLiteral("Relationship analysis progress: 100%"));
    }
    expectBool("activity log records symbol progress checkpoints",
               sawSymbol25 && sawSymbol50 && !sawSymbol100,
               true);
    expectBool("activity log records relationship progress checkpoints",
               sawRelationship25 && sawRelationship75 && !sawRelationship100,
               true);
    service->clear();

    QSignalSpy relationshipFinishedStatusSpy(
        &progressCoordinator,
        &AnalysisProgressCoordinator::statusMessageRequested);
    WorkspaceRelationshipAnalysisResult relationshipTelemetry;
    relationshipTelemetry.totalFiles = 4;
    relationshipTelemetry.processedFiles = 3;
    relationshipTelemetry.relationshipCount = 7;
    relationshipTelemetry.elapsedMs = 22;
    progressCoordinator.showRelationshipAnalysisFinished(relationshipTelemetry);
    bool sawRelationshipTelemetry = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawRelationshipTelemetry = sawRelationshipTelemetry
            || (event.source == QStringLiteral("Analyzer")
                && event.message.contains(
                    QStringLiteral("Relationship analysis complete"))
                && event.message.contains(QStringLiteral("3/4 files"))
                && event.message.contains(QStringLiteral("7 relationships"))
                && event.message.contains(QStringLiteral("22 ms")));
    }
    expectBool("activity log records relationship result telemetry",
               sawRelationshipTelemetry,
               true);
    expectBool("relationship result telemetry emits status",
               relationshipFinishedStatusSpy.count() == 1
                   && relationshipFinishedStatusSpy.at(0).at(0).toString().contains(
                       QStringLiteral("3/4 files"))
                   && relationshipFinishedStatusSpy.at(0).at(0).toString().contains(
                       QStringLiteral("7 relationships")),
               true);
    service->clear();

    QSignalSpy workspaceCancelStatusSpy(
        &progressCoordinator,
        &AnalysisProgressCoordinator::statusMessageRequested);
    progressCoordinator.showWorkspaceRelationshipCancelled();
    bool sawWorkspaceRelationshipCancel = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawWorkspaceRelationshipCancel = sawWorkspaceRelationshipCancel
            || (event.source == QStringLiteral("Analyzer")
                && event.level == ActivityLogLevel::Warning
                && event.message.contains(
                    QStringLiteral("Workspace relationship analysis cancelled")));
    }
    expectBool("activity log records workspace relationship cancellation",
               sawWorkspaceRelationshipCancel,
               true);
    expectBool("workspace relationship cancellation emits status",
               workspaceCancelStatusSpy.count() == 1
                   && workspaceCancelStatusSpy.at(0).at(0).toString().contains(
                       QStringLiteral("Workspace relationship analysis cancelled")),
               true);
    service->clear();

    WorkspaceAnalysisRequestTelemetry symbolCancelTelemetry;
    symbolCancelTelemetry.lastFinishedActiveAgeMs = 44;
    symbolCancelTelemetry.lastTakenPendingAgeMs = 9;
    symbolCancelTelemetry.lastTakenPendingUpdateCount = 3;
    progressCoordinator.handleWorkspaceSymbolAnalysisCancelled(
        symbolCancelTelemetry);
    bool sawWorkspaceSymbolCancel = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawWorkspaceSymbolCancel = sawWorkspaceSymbolCancel
            || (event.source == QStringLiteral("Analyzer")
                && event.level == ActivityLogLevel::Warning
                && event.message.contains(
                    QStringLiteral("Workspace symbol analysis cancelled"))
                && event.message.contains(
                    QStringLiteral("pending updates 3")));
    }
    expectBool("activity log records workspace symbol cancellation",
               sawWorkspaceSymbolCancel,
               true);
    expectBool("workspace symbol cancellation emits status",
               workspaceCancelStatusSpy.count() == 2
                   && workspaceCancelStatusSpy.at(1).at(0).toString().contains(
                       QStringLiteral("Workspace symbol analysis cancelled"))
                   && progressCoordinator.isSymbolAnalysisCancelled(),
               true);
    service->clear();
}

static void runRtlInsightsOnDemandRegression()
{
    ActivityLogService* service = ActivityLogService::getInstance();
    service->clear();

    RtlInsightsPanelCoordinator panel(nullptr);
    panel.updateModuleContext(QStringLiteral("probe.sv"),
                              QStringLiteral("probe_module"),
                              QStringLiteral("probe_signal"));
    expectBool("RTL insights context update is passive",
               service->events().isEmpty(),
               true);
    expectBool("RTL insights action list rendered",
               panel.tree()
                   && panel.tree()->topLevelItemCount() > 0
                   && panel.tree()->topLevelItem(0)->text(0).contains(
                       QStringLiteral("Ready")),
               true);

    panel.showModuleBrief();
    bool sawModuleBriefLog = false;
    for (const ActivityLogEvent& event : service->events()) {
        sawModuleBriefLog = sawModuleBriefLog
            || (event.source == QStringLiteral("RTL Insights")
                && event.message.contains(QStringLiteral("Module Brief")));
    }
    expectBool("RTL insights report logs on demand",
               sawModuleBriefLog,
               true);
    service->clear();
}

static bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (predicate())
            return true;
        QTest::qWait(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

static QString largestFile(const QStringList& files)
{
    QStringList sorted = files;
    std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) {
        return QFileInfo(a).size() > QFileInfo(b).size();
    });
    return sorted.isEmpty() ? QString() : sorted.first();
}

static QTextBlock findBlockContaining(QTextDocument* doc, const QString& needle)
{
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        if (block.text().contains(needle))
            return block;
    }
    return QTextBlock();
}

static QTreeWidgetItem* findItemByText(
    QTreeWidgetItem* item,
    const QString& text,
    int column = 0)
{
    if (!item)
        return nullptr;
    if (item->text(column) == text)
        return item;
    for (int i = 0; i < item->childCount(); ++i) {
        if (QTreeWidgetItem* found = findItemByText(item->child(i), text, column))
            return found;
    }
    return nullptr;
}

static QTreeWidgetItem* findItemByText(
    QTreeWidget* tree,
    const QString& text,
    int column = 0)
{
    if (!tree)
        return nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem* found =
                findItemByText(tree->topLevelItem(i), text, column))
            return found;
    }
    return nullptr;
}

static void runWavePersistentTreeStateRegression()
{
    QWidget owner;
    WavePreviewPanelCoordinator coordinator(&owner);
    QTreeWidget* tree = coordinator.tree();
    const QString fileName =
        QDir::cleanPath(QStringLiteral("C:/fixtures/wave_tree_state.sv"));
    const QString scopeLabel = QStringLiteral("module wave_tree_state");
    const QString initialText =
        QStringLiteral("module wave_tree_state;\n"
                       "logic a;\n"
                       "logic stale;\n"
                       "logic q;\n"
                       "logic fresh;\n"
                       "always_comb begin\n"
                       "    stale = a;\n"
                       "    q = a;\n"
                       "end\n"
                       "endmodule\n");

    auto laneItem = [tree](const QString& signalName) {
        if (!tree)
            return static_cast<QTreeWidgetItem*>(nullptr);
        for (int index = 0;
             index < tree->topLevelItemCount();
             ++index) {
            QTreeWidgetItem* item = tree->topLevelItem(index);
            if (item && item->text(0) == signalName
                && item->childCount() > 0) {
                return item;
            }
        }
        return static_cast<QTreeWidgetItem*>(nullptr);
    };
    auto documentChange = [](const QString& before,
                             int position,
                             const QString& removed,
                             const QString& inserted,
                             std::uint64_t revision) {
        DocumentChange change;
        change.position = position;
        change.removedLength = removed.size();
        change.removedText = removed;
        change.insertedText = inserted;
        change.oldLength = before.size();
        change.newLength =
            before.size() - removed.size() + inserted.size();
        change.startLine =
            before.left(position).count(QLatin1Char('\n'));
        change.startColumn =
            position
            - before.lastIndexOf(QLatin1Char('\n'), position - 1)
            - 1;
        change.oldEndLine =
            change.startLine + removed.count(QLatin1Char('\n'));
        change.newEndLine =
            change.startLine + inserted.count(QLatin1Char('\n'));
        change.lineDelta =
            inserted.count(QLatin1Char('\n'))
            - removed.count(QLatin1Char('\n'));
        change.revision = revision;
        return change;
    };

    coordinator.refreshFromDocument(fileName,
                                    initialText,
                                    true,
                                    0,
                                    initialText.size(),
                                    scopeLabel,
                                    0);
    QTreeWidgetItem* originalQ = laneItem(QStringLiteral("q"));
    QTreeWidgetItem* originalStale = laneItem(QStringLiteral("stale"));
    expectBool("Wave persistent tree fixture exposes lane nodes",
               originalQ && originalStale
                   && originalQ->isExpanded(),
               true);
    if (originalQ)
        originalQ->setExpanded(false);

    const QString staleLine = QStringLiteral("    stale = a;\n");
    const int stalePosition = initialText.indexOf(staleLine);
    QString textWithoutStale = initialText;
    if (stalePosition >= 0)
        textWithoutStale.remove(stalePosition, staleLine.size());
    const DocumentChange removeStale =
        documentChange(initialText,
                       stalePosition,
                       staleLine,
                       QString(),
                       1);
    coordinator.applyDocumentChange(fileName,
                                    removeStale,
                                    textWithoutStale,
                                    true,
                                    0,
                                    textWithoutStale.size(),
                                    scopeLabel,
                                    0);

    QTreeWidgetItem* updatedQ = laneItem(QStringLiteral("q"));
    QString navigatedFile;
    int navigatedLine = 0;
    int navigatedColumn = 0;
    coordinator.setNavigationHandler(
        [&](const QString& file, int line, int column) {
            navigatedFile = file;
            navigatedLine = line;
            navigatedColumn = column;
        });
    if (updatedQ && updatedQ->childCount() > 0)
        coordinator.navigateItem(updatedQ->child(0));
    const int expectedQLine =
        textWithoutStale
            .left(textWithoutStale.indexOf(QStringLiteral("q = a")))
            .count(QLatin1Char('\n'))
        + 1;
    expectBool("Wave document delta preserves collapsed existing lane",
               updatedQ == originalQ && updatedQ
                   && !updatedQ->isExpanded(),
               true);
    expectBool("Wave document delta removes stale lane",
               laneItem(QStringLiteral("stale")) == nullptr,
               true);
    expectBool("Wave document delta refreshes navigation data",
               navigatedFile == fileName
                   && navigatedLine == expectedQLine
                   && navigatedColumn > 0,
               true);

    const QString freshLine = QStringLiteral("    fresh = a;\n");
    const int freshPosition =
        textWithoutStale.indexOf(QStringLiteral("end\nendmodule"));
    QString textWithFresh = textWithoutStale;
    if (freshPosition >= 0)
        textWithFresh.insert(freshPosition, freshLine);
    const DocumentChange addFresh =
        documentChange(textWithoutStale,
                       freshPosition,
                       QString(),
                       freshLine,
                       2);
    coordinator.applyDocumentChange(fileName,
                                    addFresh,
                                    textWithFresh,
                                    true,
                                    0,
                                    textWithFresh.size(),
                                    scopeLabel,
                                    0);
    QTreeWidgetItem* fresh = laneItem(QStringLiteral("fresh"));
    expectBool("Wave new lane uses staging expanded default",
               fresh && fresh->isExpanded()
                   && updatedQ && !updatedQ->isExpanded(),
               true);
}

static int navigableItemCount(QTreeWidgetItem* item)
{
    if (!item)
        return 0;
    int count = item->data(0, Qt::UserRole).toString().isEmpty() ? 0 : 1;
    for (int i = 0; i < item->childCount(); ++i)
        count += navigableItemCount(item->child(i));
    return count;
}

static int navigableItemCount(QTreeWidget* tree)
{
    if (!tree)
        return 0;
    int count = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        count += navigableItemCount(tree->topLevelItem(i));
    return count;
}

static bool hasNavigableFile(QTreeWidgetItem* item, const QString& fileName)
{
    if (!item)
        return false;
    const QString itemFileName = item->data(0, Qt::UserRole).toString();
    if (!itemFileName.isEmpty()
        && QFileInfo(itemFileName).absoluteFilePath() == QFileInfo(fileName).absoluteFilePath()) {
        return true;
    }
    for (int i = 0; i < item->childCount(); ++i) {
        if (hasNavigableFile(item->child(i), fileName))
            return true;
    }
    return false;
}

static bool hasNavigableFile(QTreeWidget* tree, const QString& fileName)
{
    if (!tree)
        return false;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (hasNavigableFile(tree->topLevelItem(i), fileName))
            return true;
    }
    return false;
}

static QTreeWidgetItem* firstNavigableItem(QTreeWidgetItem* item)
{
    if (!item)
        return nullptr;
    if (!item->data(0, Qt::UserRole).toString().isEmpty())
        return item;
    for (int i = 0; i < item->childCount(); ++i) {
        if (QTreeWidgetItem* found = firstNavigableItem(item->child(i)))
            return found;
    }
    return nullptr;
}

static QTreeWidgetItem* firstNavigableItem(QTreeWidget* tree)
{
    if (!tree)
        return nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem* found = firstNavigableItem(tree->topLevelItem(i)))
            return found;
    }
    return nullptr;
}

static void collectNavigableItems(QTreeWidgetItem* item, QList<QTreeWidgetItem*>& out)
{
    if (!item)
        return;
    if (!item->data(0, Qt::UserRole).toString().isEmpty())
        out.append(item);
    for (int i = 0; i < item->childCount(); ++i)
        collectNavigableItems(item->child(i), out);
}

static QList<QTreeWidgetItem*> navigableItems(QTreeWidget* tree)
{
    QList<QTreeWidgetItem*> out;
    if (!tree)
        return out;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        collectNavigableItems(tree->topLevelItem(i), out);
    return out;
}

static bool hasDiagnosticTreeItem(QTreeWidget* tree,
                                  const QString& fileName,
                                  const QString& message)
{
    const QString normalizedFile =
        QDir::cleanPath(
            QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
    const QList<QTreeWidgetItem*> items = navigableItems(tree);
    for (QTreeWidgetItem* item : items) {
        const QString itemFileName = item->data(0, Qt::UserRole).toString();
        const QString normalizedItemFile =
            QDir::cleanPath(
                QDir::fromNativeSeparators(
                    QFileInfo(itemFileName).absoluteFilePath()));
        if (normalizedItemFile == normalizedFile
            && item->text(4).contains(message)) {
            return true;
        }
    }
    return false;
}

static QTreeWidget* problemsTree(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->problemsPanelCoordinator()
        ? window.semanticDocks->problemsPanelCoordinator()->tree()
        : nullptr;
}

static QComboBox* problemsScopeCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->problemsPanelCoordinator()
        ? window.semanticDocks->problemsPanelCoordinator()->scopeCombo()
        : nullptr;
}

static QComboBox* problemsBandCombo(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->problemsPanelCoordinator()
        ? window.semanticDocks->problemsPanelCoordinator()->bandCombo()
        : nullptr;
}

static QTreeWidget* rtlInsightsTree(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->rtlInsightsPanelCoordinator()
        ? window.semanticDocks->rtlInsightsPanelCoordinator()->tree()
        : nullptr;
}

static QTreeWidget* wavePreviewTree(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->wavePreviewPanelCoordinator()
        ? window.semanticDocks->wavePreviewPanelCoordinator()->tree()
        : nullptr;
}

static QWidget* wavePreviewCanvas(MainWindow& window)
{
    return window.semanticDocks && window.semanticDocks->wavePreviewPanelCoordinator()
        ? window.semanticDocks->wavePreviewPanelCoordinator()->canvas()
        : nullptr;
}

static QLabel* wavePreviewSummaryLabel(MainWindow& window)
{
    QDockWidget* dock =
        window.findChild<QDockWidget*>(QStringLiteral("wavePreviewDock"));
    return dock ? dock->findChild<QLabel*>(QStringLiteral("wavePreviewSummary"))
                : nullptr;
}

static bool renderedWidgetHasColorVariation(QWidget* widget)
{
    if (!widget)
        return false;
    if (widget->width() < 120 || widget->height() < 80)
        widget->resize(420, 150);

    QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget->render(&image);
    const QRgb firstPixel = image.pixel(0, 0);
    int differentPixels = 0;
    for (int y = 0; y < image.height(); y += 5) {
        for (int x = 0; x < image.width(); x += 5) {
            if (image.pixel(x, y) != firstPixel)
                ++differentPixels;
            if (differentPixels > 20)
                return true;
        }
    }
    return false;
}

static SemanticPanelRefreshCoordinator* semanticPanelRefresh(MainWindow& window)
{
    return window.semanticDocks ? window.semanticDocks->refreshCoordinator() : nullptr;
}

static void drainRelationshipWork(MainWindow& window)
{
    SmartRelationshipBuilder* builder = window.semanticRuntime
        ? window.semanticRuntime->relationshipBuilder()
        : nullptr;
    if (builder)
        builder->cancelAnalysis();
    if (window.analysisScheduler) {
        window.analysisScheduler->cancelAllScheduledRelationshipAnalyses();
        window.analysisScheduler->cancelRelationshipAnalysis();
        window.analysisScheduler->cancelWorkspaceRelationshipAnalysis();
    }
}

static void runSemanticStateUiRegression()
{
    printf("\n-- semantic state UI regression --\n");
    QTemporaryDir directory;
    expectBool("semantic state UI temp dir valid", directory.isValid(), true);
    if (!directory.isValid())
        return;
    const QString currentFile =
        directory.filePath(QStringLiteral("current_dirty.sv"));
    const QString backgroundFile =
        directory.filePath(QStringLiteral("background.sv"));
    const QString currentText =
        QStringLiteral("module current_dirty; endmodule\n");
    const QString backgroundText =
        QStringLiteral("module background; endmodule\n");
    expectBool("semantic state UI current source writable",
               writeTextFile(currentFile, currentText),
               true);
    expectBool("semantic state UI background source writable",
               writeTextFile(backgroundFile, backgroundText),
               true);

    MainWindow window;
    SemanticIndex::getInstance()->setSnapshot(snapshotFromRecords(
        {SemanticFixtureRecordBuilder(
             QStringLiteral("current_dirty"),
             SymbolTaxonomy::DeclarationKind::Module)
             .withFile(currentFile)
             .withLocalHandle(40101)
             .withTextSpan(7, 13)
             .record(),
         SemanticFixtureRecordBuilder(
             QStringLiteral("background"),
             SymbolTaxonomy::DeclarationKind::Module)
             .withFile(backgroundFile)
             .withLocalHandle(40102)
             .withTextSpan(7, 10)
             .record()},
        {},
        {},
        {{currentFile, currentText}, {backgroundFile, backgroundText}}));
    expectBool("semantic state UI opens current source",
               window.tabManager
                   && window.tabManager->openFileInTab(currentFile),
               true);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    ProblemsPanelCoordinator* problems = window.semanticDocks
        ? window.semanticDocks->problemsPanelCoordinator()
        : nullptr;
    MyCodeEditor* editor = window.tabManager
        ? window.tabManager->getCurrentEditor()
        : nullptr;
    expectBool("semantic state UI has Problems coordinator and editor",
               problems && editor,
               true);
    if (!problems || !editor || !window.analysisScheduler) {
        drainRelationshipWork(window);
        return;
    }

    int semanticSchedulingCount = 0;
    QObject::connect(
        window.analysisScheduler.get(),
        &AnalysisScheduler::semanticAnalysisTelemetry,
        &window,
        [&](const SemanticAnalysisTelemetry& telemetry) {
            if (telemetry.stage == SemanticAnalysisStage::Scheduling)
                ++semanticSchedulingCount;
        });
    const int updatesBeforeTyping = problems->updateInvocationCount();
    QTextCursor cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    editor->setTextCursor(cursor);
    for (int index = 0; index < 100; ++index)
        editor->insertPlainText(QStringLiteral(" "));
    expectBool("100 edits cause only the first Dirty visible-state refresh",
               problems->updateInvocationCount() == updatesBeforeTyping + 1,
               true);
    expectBool("100 edits schedule zero semantic requests",
               semanticSchedulingCount == 0,
               true);
    expectBool("Problems shows current document Dirty",
               problems->stateLabel()
                   && problems->stateLabel()->text()
                          == QStringLiteral("Diagnostics: dirty"),
               true);

    SymbolAnalyzer* analyzer = window.analysisScheduler->symbolAnalyzer;
    expectBool("semantic state UI has symbol analyzer", analyzer != nullptr, true);
    if (!analyzer) {
        drainRelationshipWork(window);
        return;
    }
    ProjectSnapshot backgroundProject;
    backgroundProject.workspaceRoot = directory.path();
    backgroundProject.systemVerilogFiles = {backgroundFile};
    backgroundProject.allFiles = backgroundProject.systemVerilogFiles;
    backgroundProject.includeDirs = {directory.path()};
    std::atomic_bool releaseWorker{false};
    analyzer->setWorkspaceWorkerStartGateForTesting(
        [&releaseWorker](const std::function<bool()>& isCancelled) {
            while (!releaseWorker.load(std::memory_order_relaxed)
                   && !isCancelled()) {
                QThread::msleep(2);
            }
        });
    QSignalSpy finishedSpy(
        window.analysisScheduler.get(),
        &AnalysisScheduler::workspaceSymbolAnalysisFinished);
    window.analysisScheduler->requestWorkspaceAnalysis(backgroundProject);
    expectBool("background analysis starts while current document is Dirty",
               waitUntil(
                   [&]() {
                       return window.analysisScheduler
                           ->isSemanticAnalysisActive();
                   },
                   3000),
               true);
    expectBool("background start does not replace Dirty with Analyzing",
               problems->stateLabel()
                   && problems->stateLabel()->text()
                          == QStringLiteral("Diagnostics: dirty"),
               true);
    releaseWorker.store(true, std::memory_order_relaxed);
    expectBool("background analysis finishes",
               waitUntil([&]() { return !finishedSpy.isEmpty(); }, 10000),
               true);
    expectBool("background finish does not replace Dirty with Current",
               problems->stateLabel()
                   && problems->stateLabel()->text()
                          == QStringLiteral("Diagnostics: dirty"),
               true);

    std::atomic_bool releaseCancelledWorker{false};
    analyzer->setWorkspaceWorkerStartGateForTesting(
        [&releaseCancelledWorker](const std::function<bool()>& isCancelled) {
            while (!releaseCancelledWorker.load(std::memory_order_relaxed)
                   && !isCancelled()) {
                QThread::msleep(2);
            }
        });
    window.analysisScheduler->requestWorkspaceAnalysis(backgroundProject);
    expectBool("cancellable background analysis starts",
               waitUntil(
                   [&]() {
                       return window.analysisScheduler
                           ->isSemanticAnalysisActive();
                   },
                   3000),
               true);
    QSignalSpy expiredSpy(analyzer, &SymbolAnalyzer::workspaceAnalysisExpired);
    window.analysisScheduler->cancelWorkspaceAnalysis();
    releaseCancelledWorker.store(true, std::memory_order_relaxed);
    expectBool("cancelled background worker reaches its terminal expired signal",
               waitUntil([&]() { return !expiredSpy.isEmpty(); }, 5000),
               true);
    expectBool("background cancel leaves current document Dirty",
               problems->stateLabel()
                   && problems->stateLabel()->text()
                          == QStringLiteral("Diagnostics: dirty"),
               true);

    analyzer->setWorkspaceWorkerStartGateForTesting({});
    expectBool("current Dirty source saves",
               window.tabManager->saveCurrentTab(),
               true);
    expectBool("current clean save converges to Current",
               waitUntil(
                   [&]() {
                       return window.analysisScheduler
                                      ->semanticStatus(currentFile)
                                      .state
                                  == DocumentSemanticState::Current
                           && problems->stateLabel()
                                  && problems->stateLabel()->text()
                                      == QStringLiteral(
                                          "Diagnostics: current");
                   },
                   10000),
               true);
    drainRelationshipWork(window);
    window.analysisScheduler->shutdown();
    SemanticIndex::getInstance()->clearSemanticState();
}

static SemanticSymbolRecord makeGuiSmokeRecord(
    int id,
    const QString& fileName,
    const QString& name,
    SymbolTaxonomy::DeclarationKind declarationKind,
    SymbolTaxonomy::CollectorKind collectorKind,
    int line,
    SymbolTaxonomy::SymbolOwnerScope ownerScope =
        SymbolTaxonomy::SymbolOwnerScope::Unknown,
    const QString& ownerName = QString(),
    const QString& rawTypeText = QString())
{
    SemanticFixtureRecordBuilder builder(name, declarationKind);
    builder.withFile(fileName)
        .withLocalHandle(id)
        .withLine(line)
        .withCollectorKind(collectorKind)
        .withTextSpan(0, name.length());
    if (ownerScope != SymbolTaxonomy::SymbolOwnerScope::Unknown
        || !ownerName.isEmpty()) {
        builder.withOwner(ownerScope, ownerName);
    }
    if (!rawTypeText.isEmpty())
        builder.withType(rawTypeText);
    return builder.record();
}

static void runNavigationDesignCacheWorkspaceActivationRegression()
{
    QTemporaryDir workspaceA;
    QTemporaryDir workspaceB;
    expectBool("navigation design cache temp dirs valid",
               workspaceA.isValid() && workspaceB.isValid(),
               true);
    if (!workspaceA.isValid() || !workspaceB.isValid())
        return;

    const QString fileA =
        QDir(workspaceA.path()).absoluteFilePath(QStringLiteral("a_top.sv"));
    const QString fileB =
        QDir(workspaceB.path()).absoluteFilePath(QStringLiteral("b_top.sv"));
    {
        QFile out(fileA);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write("module a_top; endmodule\n");
    }
    {
        QFile out(fileB);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write("module b_top; endmodule\n");
    }

    const SemanticSymbolRecord aTop =
        makeGuiSmokeRecord(1300,
                           fileA,
                           QStringLiteral("a_top"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           1);
    const SemanticSymbolRecord bTop =
        makeGuiSmokeRecord(1301,
                           fileB,
                           QStringLiteral("b_top"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           1);
    SemanticIndex index;
    index.setSnapshot(snapshotFromRecords({aTop, bTop}));
    NavigationService service(&index);
    NavigationWidget widget;
    WorkspaceManager workspace;
    NavigationManager manager;
    manager.setNavigationService(&service);
    manager.setNavigationWidget(&widget);
    manager.connectToWorkspaceManager(&workspace);

    expectBool("navigation design cache opens A",
               workspace.openWorkspace(workspaceA.path()),
               true);
    expectBool("navigation design cache scans A",
               waitUntil([&]() {
                   return workspace.getSystemVerilogFiles().contains(
                       QDir::cleanPath(QDir::fromNativeSeparators(
                           QFileInfo(fileA).absoluteFilePath())));
               }, 2000),
               true);
    manager.setActiveView(NavigationManager::DesignHierarchyView);
    expectBool("navigation design cache builds A once",
               manager.caches.designHierarchyValid
                   && manager.caches.designHierarchy.topModule
                          == QStringLiteral("a_top"),
               true);
    expectBool("navigation design cache keeps A immediately",
               !manager.updateDesignHierarchyData(false),
               true);

    manager.setActiveView(NavigationManager::FileHierarchyView);
    expectBool("navigation design cache opens B",
               workspace.openWorkspace(workspaceB.path()),
               true);
    expectBool("navigation design cache scans B",
               waitUntil([&]() {
                   return workspace.getSystemVerilogFiles().contains(
                       QDir::cleanPath(QDir::fromNativeSeparators(
                           QFileInfo(fileB).absoluteFilePath())));
               }, 2000),
               true);
    manager.setActiveView(NavigationManager::DesignHierarchyView);
    expectBool("navigation design cache builds B once",
               manager.caches.designHierarchyValid
                   && manager.caches.designHierarchy.topModule
                          == QStringLiteral("b_top"),
               true);

    manager.setActiveView(NavigationManager::FileHierarchyView);
    expectBool("navigation design cache switches back to A",
               workspace.switchWorkspace(0),
               true);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    expectBool("navigation design cache restores A without rebuild",
               manager.caches.designHierarchyValid
                   && manager.caches.designHierarchy.topModule
                          == QStringLiteral("a_top")
                   && !manager.updateDesignHierarchyData(false),
               true);

    manager.setActiveView(NavigationManager::FileHierarchyView);
    expectBool("navigation design cache close A activates B",
               workspace.closeWorkspace(0),
               true);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    expectBool("navigation design cache restores B after close",
               manager.caches.designHierarchyValid
                   && manager.caches.designHierarchy.topModule
                          == QStringLiteral("b_top")
                   && !manager.updateDesignHierarchyData(false),
               true);

    manager.setActiveView(NavigationManager::DesignHierarchyView);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QAbstractItemModel* designTreeModel = widget.designTreeWidget
        ? widget.designTreeWidget->model()
        : nullptr;
    expectBool("navigation design refresh tree model exists",
               designTreeModel != nullptr,
               true);
    if (!designTreeModel)
        return;

    QSignalSpy presentationRowsInsertedSpy(
        designTreeModel,
        &QAbstractItemModel::rowsInserted);
    QSignalSpy presentationRowsRemovedSpy(
        designTreeModel,
        &QAbstractItemModel::rowsRemoved);
    QSignalSpy presentationRefreshSpy(
        &manager,
        &NavigationManager::dataRefreshed);

    SemanticSymbolRecord bTopWithPresentation = bTop;
    bTopWithPresentation.presentation.declarationText =
        QStringLiteral("module b_top;");
    bTopWithPresentation.presentation.defaultInfo.available = true;
    bTopWithPresentation.presentation.defaultInfo.valueText =
        QStringLiteral("presentation-only");
    index.setSnapshot(snapshotFromRecords({aTop, bTopWithPresentation}));

    manager.onSymbolAnalysisCompleted(fileB, 1);
    manager.onSymbolAnalysisCompleted(fileA, 1);
    manager.onSymbolAnalysisCompleted(fileB, 1);
    expectBool("presentation-only snapshot does not rebuild Design tree",
               presentationRowsInsertedSpy.isEmpty()
                   && presentationRowsRemovedSpy.isEmpty(),
               true);
    expectBool("one snapshot coalesces per-file Design refreshes",
               presentationRefreshSpy.count() == 1,
               true);
    expectBool("presentation-only snapshot advances Design generation",
               manager.caches.designSnapshotGeneration
                   == service.semanticSnapshotRevision(),
               true);

    const SemanticSymbolRecord bChild =
        makeGuiSmokeRecord(1302,
                           fileB,
                           QStringLiteral("b_child"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           4);
    const SemanticSymbolRecord bChildInstance =
        SemanticFixtureRecordBuilder(QStringLiteral("u_child"),
                                     SymbolTaxonomy::DeclarationKind::Instance)
            .withFile(fileB)
            .withLocalHandle(1303)
            .withLine(2)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Inst)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("b_top"),
                       bTopWithPresentation.stableKey)
            .withType(QStringLiteral("b_child"),
                      QStringLiteral("b_child"),
                      SymbolTaxonomy::DeclarationKind::Module)
            .record();
    const SemanticRelationship bInstantiatesChild =
        semanticFixtureRelationship(bTopWithPresentation,
                                    bChildInstance,
                                    SymbolRelationshipEngine::INSTANTIATES);
    index.setSnapshot(snapshotFromRecords(
        {aTop, bTopWithPresentation, bChild, bChildInstance},
        {bInstantiatesChild}));

    QSignalSpy structureRefreshSpy(
        &manager,
        &NavigationManager::dataRefreshed);
    manager.onSymbolAnalysisCompleted(fileB, 4);
    manager.onSymbolAnalysisCompleted(fileA, 1);
    manager.onBatchSymbolAnalysisCompleted(2, 4);
    expectBool("changed Design structure rebuilds once per snapshot",
               structureRefreshSpy.count() == 1,
               true);
    expectBool("changed Design structure publishes child hierarchy",
               manager.caches.designHierarchy.nodes.size() == 2
                   && manager.caches.designHierarchy.nodes.last().moduleType
                          == QStringLiteral("b_child"),
               true);
}

static void runRtlInsightsPanelRegression(MainWindow& window, const QString& fixturePath)
{
    printf("\n-- RTL insights panel regression --\n");

    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using SymbolOwnerScope = SymbolTaxonomy::SymbolOwnerScope;

    QList<SemanticSymbolRecord> records;
    SemanticSymbolRecord module = makeGuiSmokeRecord(
        9601,
        fixturePath,
        QStringLiteral("insight_top"),
        DeclarationKind::Module,
        CollectorKind::Module,
        1);
    module.location.endLine = 19;
    records.append(module);
    const SemanticSymbolRecord clk = makeGuiSmokeRecord(
        9602,
        fixturePath,
        QStringLiteral("clk"),
        DeclarationKind::Port,
        CollectorKind::PortInput,
        2,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(clk);
    const SemanticSymbolRecord reset = makeGuiSmokeRecord(
        9603,
        fixturePath,
        QStringLiteral("rst_n"),
        DeclarationKind::Port,
        CollectorKind::PortInput,
        3,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(reset);
    const SemanticSymbolRecord stageInstance = makeGuiSmokeRecord(
        9604,
        fixturePath,
        QStringLiteral("u_stage"),
        DeclarationKind::Instance,
        CollectorKind::Inst,
        8,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(stageInstance);

    const SemanticSymbolRecord stateQ = makeGuiSmokeRecord(
        9605,
        fixturePath,
        QStringLiteral("state_q"),
        DeclarationKind::Enum,
        CollectorKind::EnumVariable,
        10,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"),
        QStringLiteral("state_t"));
    records.append(stateQ);
    const SemanticSymbolRecord stateD = makeGuiSmokeRecord(
        9606,
        fixturePath,
        QStringLiteral("state_d"),
        DeclarationKind::Enum,
        CollectorKind::EnumVariable,
        11,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"),
        QStringLiteral("state_t"));
    records.append(stateD);
    const SemanticSymbolRecord idle = makeGuiSmokeRecord(
        9607,
        fixturePath,
        QStringLiteral("IDLE"),
        DeclarationKind::Enum,
        CollectorKind::EnumValue,
        5,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"),
        QStringLiteral("state_t"));
    records.append(idle);
    const SemanticSymbolRecord run = makeGuiSmokeRecord(
        9608,
        fixturePath,
        QStringLiteral("RUN"),
        DeclarationKind::Enum,
        CollectorKind::EnumValue,
        5,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"),
        QStringLiteral("state_t"));
    records.append(run);
    const SemanticSymbolRecord dataQ = makeGuiSmokeRecord(
        9609,
        fixturePath,
        QStringLiteral("data_q"),
        DeclarationKind::Signal,
        CollectorKind::Logic,
        12,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(dataQ);
    const SemanticSymbolRecord nextData = makeGuiSmokeRecord(
        9610,
        fixturePath,
        QStringLiteral("next_data"),
        DeclarationKind::Signal,
        CollectorKind::Logic,
        13,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(nextData);
    const SemanticSymbolRecord consumer = makeGuiSmokeRecord(
        9611,
        fixturePath,
        QStringLiteral("consumer"),
        DeclarationKind::Process,
        CollectorKind::AlwaysFf,
        14,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(consumer);
    const SemanticSymbolRecord stageDataPin = makeGuiSmokeRecord(
        9612,
        fixturePath,
        QStringLiteral("u_stage.data_i"),
        DeclarationKind::Instance,
        CollectorKind::InstPin,
        15,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(stageDataPin);
    const SemanticSymbolRecord scanClk = makeGuiSmokeRecord(
        9613,
        fixturePath,
        QStringLiteral("scan_clk"),
        DeclarationKind::Port,
        CollectorKind::PortInput,
        16,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"));
    records.append(scanClk);
    const SemanticSymbolRecord insightPackage = makeGuiSmokeRecord(
        9614,
        fixturePath,
        QStringLiteral("insight_pkg"),
        DeclarationKind::Package,
        CollectorKind::Package,
        18);
    records.append(insightPackage);
    records.append(makeGuiSmokeRecord(
        9617,
        fixturePath,
        QStringLiteral("PKG_DEPTH"),
        DeclarationKind::Parameter,
        CollectorKind::Parameter,
        21,
        SymbolOwnerScope::Package,
        QStringLiteral("insight_pkg")));
    records.append(makeGuiSmokeRecord(
        9615,
        fixturePath,
        QStringLiteral("insight_if"),
        DeclarationKind::Interface,
        CollectorKind::Interface,
        19));
    const SemanticSymbolRecord interfaceBus = makeGuiSmokeRecord(
        9616,
        fixturePath,
        QStringLiteral("if_bus"),
        DeclarationKind::Instance,
        CollectorKind::Inst,
        20,
        SymbolOwnerScope::Module,
        QStringLiteral("insight_top"),
        QStringLiteral("insight_if"));
    records.append(interfaceBus);

    QList<SemanticRelationship> relationships;
    relationships.append(semanticFixtureRelationship(
        module,
        insightPackage,
        SymbolRelationshipEngine::REFERENCES));
    relationships.append(semanticFixtureRelationship(
        clk,
        module,
        SymbolRelationshipEngine::CLOCKS));
    relationships.append(semanticFixtureRelationship(
        reset,
        module,
        SymbolRelationshipEngine::RESETS));
    relationships.append(semanticFixtureRelationship(
        nextData,
        dataQ,
        SymbolRelationshipEngine::ASSIGNS_TO));
    relationships.append(semanticFixtureRelationship(
        consumer,
        dataQ,
        SymbolRelationshipEngine::READS_FROM));
    relationships.append(semanticFixtureRelationship(
        stageDataPin,
        dataQ,
        SymbolRelationshipEngine::REFERENCES));
    relationships.append(semanticFixtureRelationship(
        interfaceBus,
        dataQ,
        SymbolRelationshipEngine::REFERENCES));
    relationships.append(semanticFixtureRelationship(
        module,
        stageInstance,
        SymbolRelationshipEngine::INSTANTIATES));

    const QString content = QStringLiteral(
        "module insight_top(input logic clk, input logic rst_n);\n"
        "  typedef enum logic {IDLE, RUN} state_t;\n"
        "  state_t state_q;\n"
        "  state_t state_d;\n"
        "  always_ff @(posedge clk or negedge rst_n) begin\n"
        "    if (!rst_n) state_q <= IDLE;\n"
        "    else state_q <= state_d;\n"
        "  end\n"
        "  always_comb begin\n"
        "    case (state_q)\n"
        "      IDLE: state_d = RUN;\n"
        "      RUN: state_d = IDLE;\n"
        "    endcase\n"
        "  end\n"
        "  input logic scan_clk;\n"
        "endmodule\n");
    QHash<QString, QString> fileContents;
    fileContents.insert(fixturePath, content);
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        fixturePath,
        records,
        content);
    SemanticDiagnostic insightDiagnostic;
    insightDiagnostic.fileName = fixturePath;
    insightDiagnostic.line = 6;
    insightDiagnostic.column = 5;
    insightDiagnostic.message = QStringLiteral("insight warning");
    insightDiagnostic.severity = SemanticDiagnostic::Warning;
    auto installRtlInsightsFixtureSnapshot = [&]() {
        SemanticIndex::getInstance()->updateSymbolRecordsForFile(
            fixturePath,
            records,
            content);
        SemanticIndex::getInstance()->setSnapshot(
            snapshotFromRecords(
                records,
                relationships,
                QList<SemanticDiagnostic>{insightDiagnostic},
                fileContents));
    };
    installRtlInsightsFixtureSnapshot();
    expectBool("legacy RTL insights bottom panel is removed",
               window.semanticDocks
                   && window.semanticDocks
                          ->rtlInsightsPanelCoordinator() == nullptr
                   && rtlInsightsTree(window) == nullptr,
               true);
    return;

    installRtlInsightsFixtureSnapshot();
    window.semanticDocks->rtlInsightsPanelCoordinator()->showModuleInsights(
        fixturePath,
        QStringLiteral("insight_top"),
        QStringLiteral("data_q"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("RTL insights defaults to on-demand actions",
               rtlInsightsTree(window)
                   && rtlInsightsTree(window)->topLevelItemCount() > 0
                   && rtlInsightsTree(window)->topLevelItem(0)->text(0).contains(
                       QStringLiteral("Ready")),
               true);

    bool sawPort = false;
    bool sawClock = false;
    bool sawRelationshipEvidence = false;
    bool sawRelationshipFromEndpoint = false;
    bool sawRelationshipToEndpoint = false;
    bool sawContextKind = false;
    bool sawContextType = false;
    bool sawContextSourceRole = false;
    bool sawPackageMemberContext = false;
    bool sawPackageMemberKind = false;
    bool sawPackageMemberType = false;
    bool sawModuleBriefDiagnostic = false;
    bool sawModuleBriefDiagnosticSourceRole = false;
    bool sawClockSignalEndpoint = false;
    bool sawClockModuleEndpoint = false;
    bool sawClockRelationshipType = false;
    bool sawClockCategory = false;
    bool sawClockEvidenceReason = false;
    bool sawClockSourceRole = false;
    bool sawClockDomainMemberType = false;
    bool sawClockDomainMemberSourceRole = false;
    bool sawClockDomainMemberSignal = false;
    bool sawResetDomainMemberType = false;
    bool sawResetDomainMemberSignal = false;
    bool sawUnmappedClock = false;
    bool sawUnmappedClockCategory = false;
    bool sawTransition = false;
    bool sawFsmStateType = false;
    bool sawFsmStateSourceRole = false;
    bool sawFsmStateModule = false;
    bool sawFsmRegisterType = false;
    bool sawFsmRegisterSourceRole = false;
    bool sawFsmNextStateSignal = false;
    bool sawFsmNextStateSourceRole = false;
    bool sawFsmFromStateEndpoint = false;
    bool sawFsmToStateEndpoint = false;
    bool sawFsmTransitionSourceRole = false;
    bool sawSignalJourney = false;
    bool sawSignalJourneyDeclarationSourceRole = false;
    bool sawSignalJourneyFromEndpoint = false;
    bool sawSignalJourneyToEndpoint = false;
    bool sawSignalJourneyFromEndpointType = false;
    bool sawSignalJourneyFromEndpointSourceRole = false;
    bool sawSignalJourneyToEndpointType = false;
    bool sawSignalJourneyToEndpointSourceRole = false;
    bool sawSignalJourneyInterfaceConnection = false;
    bool sawSignalJourneyInterfaceKind = false;
    bool sawSignalJourneyInterfaceBase = false;
    bool sawSignalJourneyInterfacePeerType = false;
    bool sawSignalJourneyInterfaceSourceRole = false;
    auto scanRtlInsightItems = [&]() {
        const QList<QTreeWidgetItem*> items = navigableItems(rtlInsightsTree(window));
        for (QTreeWidgetItem* item : items) {
        sawPort = sawPort
            || (item->text(0) == QStringLiteral("Port")
                && item->text(1) == QStringLiteral("clk"));
        sawClock = sawClock
            || (item->text(0) == QStringLiteral("Clock")
                && item->text(1) == QStringLiteral("clk"));
        sawRelationshipEvidence = sawRelationshipEvidence
            || (item->text(0) == QStringLiteral("Outgoing")
                && item->text(1) == QStringLiteral("u_stage")
                && item->text(2) == QStringLiteral("Outgoing Instantiates"));
        sawRelationshipFromEndpoint = sawRelationshipFromEndpoint
            || (item->text(0) == QStringLiteral("From")
                && item->text(1) == QStringLiteral("insight_top")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawRelationshipToEndpoint = sawRelationshipToEndpoint
            || (item->text(0) == QStringLiteral("To")
                && item->text(1) == QStringLiteral("u_stage")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawContextKind = sawContextKind
            || (item->text(0) == QStringLiteral("Kind")
                && item->text(1) == QStringLiteral("package import")
                && item->text(2) == QStringLiteral("Package"));
        sawContextType = sawContextType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("package")
                && item->text(2) == QStringLiteral("package import"));
        sawContextSourceRole = sawContextSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Package"));
        sawPackageMemberContext = sawPackageMemberContext
            || (item->text(0) == QStringLiteral("Package Member")
                && item->text(1) == QStringLiteral("PKG_DEPTH")
                && item->text(2) == QStringLiteral("package parameter"));
        sawPackageMemberKind = sawPackageMemberKind
            || (item->text(0) == QStringLiteral("Kind")
                && item->text(1) == QStringLiteral("package parameter")
                && item->text(2) == QStringLiteral("Package Member"));
        sawPackageMemberType = sawPackageMemberType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("parameter")
                && item->text(2) == QStringLiteral("package parameter"));
        sawModuleBriefDiagnostic = sawModuleBriefDiagnostic
            || (item->text(0) == QStringLiteral("Warning")
                && item->text(1) == QStringLiteral("insight warning")
                && item->text(2) == QStringLiteral("diagnostic"));
        sawModuleBriefDiagnosticSourceRole =
            sawModuleBriefDiagnosticSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Warning"));
        sawClockSignalEndpoint = sawClockSignalEndpoint
            || (item->text(0) == QStringLiteral("Signal")
                && item->text(1) == QStringLiteral("clk")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockModuleEndpoint = sawClockModuleEndpoint
            || (item->text(0) == QStringLiteral("Module")
                && item->text(1) == QStringLiteral("insight_top")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockRelationshipType = sawClockRelationshipType
            || (item->text(0) == QStringLiteral("Relationship Type")
                && item->text(1) == QStringLiteral("Clock")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockCategory = sawClockCategory
            || (item->text(0) == QStringLiteral("Category")
                && item->text(1) == QStringLiteral("mapped domain")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockEvidenceReason = sawClockEvidenceReason
            || (item->text(0) == QStringLiteral("Reason")
                && item->text(1) == QStringLiteral("relationship")
                && item->text(2) == QStringLiteral("clk clocks insight_top"));
        sawClockSourceRole = sawClockSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Clock"));
        sawClockDomainMemberType = sawClockDomainMemberType
            || (item->text(0) == QStringLiteral("Relationship Type")
                && item->text(1) == QStringLiteral("Clock")
                && item->text(2) == QStringLiteral("insight_top"));
        sawClockDomainMemberSourceRole = sawClockDomainMemberSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("insight_top"));
        sawClockDomainMemberSignal = sawClockDomainMemberSignal
            || (item->text(0) == QStringLiteral("Domain Signal")
                && item->text(1) == QStringLiteral("clk")
                && item->text(2) == QStringLiteral("Clock"));
        sawResetDomainMemberType = sawResetDomainMemberType
            || (item->text(0) == QStringLiteral("Relationship Type")
                && item->text(1) == QStringLiteral("Reset")
                && item->text(2) == QStringLiteral("insight_top"));
        sawResetDomainMemberSignal = sawResetDomainMemberSignal
            || (item->text(0) == QStringLiteral("Domain Signal")
                && item->text(1) == QStringLiteral("rst_n")
                && item->text(2) == QStringLiteral("Reset"));
        sawUnmappedClock = sawUnmappedClock
            || (item->text(0) == QStringLiteral("Unmapped Clock")
                && item->text(1) == QStringLiteral("scan_clk")
                && item->text(2).contains(
                    QStringLiteral("no clock domain relationship")));
        sawUnmappedClockCategory = sawUnmappedClockCategory
            || (item->text(0) == QStringLiteral("Category")
                && item->text(1) == QStringLiteral("unmapped timing")
                && item->text(2) == QStringLiteral("Unmapped Clock"));
        sawTransition = sawTransition
            || (item->text(0) == QStringLiteral("IDLE")
                && item->text(1) == QStringLiteral("RUN"));
        sawFsmStateType = sawFsmStateType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("enum value")
                && item->text(2) == QStringLiteral("IDLE"));
        sawFsmStateSourceRole = sawFsmStateSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("IDLE"));
        sawFsmStateModule = sawFsmStateModule
            || (item->text(0) == QStringLiteral("Module")
                && item->text(1) == QStringLiteral("insight_top")
                && item->text(2) == QStringLiteral("IDLE"));
        sawFsmRegisterType = sawFsmRegisterType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("enum")
                && item->text(2) == QStringLiteral("state_q"));
        sawFsmRegisterSourceRole = sawFsmRegisterSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("state_q"));
        sawFsmNextStateSignal = sawFsmNextStateSignal
            || (item->text(0) == QStringLiteral("Next State Signal")
                && item->text(1) == QStringLiteral("state_d")
                && item->text(2) == QStringLiteral("enum"));
        sawFsmNextStateSourceRole = sawFsmNextStateSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("state_d"));
        sawFsmFromStateEndpoint = sawFsmFromStateEndpoint
            || (item->text(0) == QStringLiteral("From State")
                && item->text(1) == QStringLiteral("IDLE")
                && item->text(2) == QStringLiteral("unconditional"));
        sawFsmToStateEndpoint = sawFsmToStateEndpoint
            || (item->text(0) == QStringLiteral("To State")
                && item->text(1) == QStringLiteral("RUN")
                && item->text(2) == QStringLiteral("unconditional"));
        sawFsmTransitionSourceRole = sawFsmTransitionSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2).startsWith(QStringLiteral("line ")));
        sawSignalJourney = sawSignalJourney
            || (item->text(0) == QStringLiteral("Assignments")
                && item->text(1) == QStringLiteral("next_data"));
        sawSignalJourneyDeclarationSourceRole =
            sawSignalJourneyDeclarationSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("data_q"));
        sawSignalJourneyFromEndpoint = sawSignalJourneyFromEndpoint
            || (item->text(0) == QStringLiteral("From")
                && item->text(1) == QStringLiteral("next_data")
                && item->text(2) == QStringLiteral("Assigns To"));
        sawSignalJourneyToEndpoint = sawSignalJourneyToEndpoint
            || (item->text(0) == QStringLiteral("To")
                && item->text(1) == QStringLiteral("data_q")
                && item->text(2) == QStringLiteral("Assigns To"));
        sawSignalJourneyFromEndpointType = sawSignalJourneyFromEndpointType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("logic")
                && item->text(2) == QStringLiteral("next_data"));
        sawSignalJourneyFromEndpointSourceRole =
            sawSignalJourneyFromEndpointSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("next_data"));
        sawSignalJourneyToEndpointType = sawSignalJourneyToEndpointType
            || (item->text(0) == QStringLiteral("Type")
                && item->text(1) == QStringLiteral("logic")
                && item->text(2) == QStringLiteral("data_q"));
        sawSignalJourneyToEndpointSourceRole =
            sawSignalJourneyToEndpointSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("data_q"));
        sawSignalJourneyInterfaceConnection =
            sawSignalJourneyInterfaceConnection
            || (item->text(0) == QStringLiteral("Interface Connections")
                && item->text(1) == QStringLiteral("if_bus")
                && item->text(2) == QStringLiteral("interface incoming References"));
        sawSignalJourneyInterfaceKind = sawSignalJourneyInterfaceKind
            || (item->text(0) == QStringLiteral("Connection")
                && item->text(1) == QStringLiteral("interface instance")
                && item->text(2) == QStringLiteral("interface incoming References"));
        sawSignalJourneyInterfaceBase = sawSignalJourneyInterfaceBase
            || (item->text(0) == QStringLiteral("Interface")
                && item->text(1) == QStringLiteral("insight_if")
                && item->text(2) == QStringLiteral("interface instance"));
        sawSignalJourneyInterfacePeerType = sawSignalJourneyInterfacePeerType
            || (item->text(0) == QStringLiteral("Peer Type")
                && item->text(1) == QStringLiteral("instance")
                && item->text(2) == QStringLiteral("References"));
        sawSignalJourneyInterfaceSourceRole = sawSignalJourneyInterfaceSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("if_bus"));
        }
    };

    installRtlInsightsFixtureSnapshot();
    window.semanticDocks->rtlInsightsPanelCoordinator()->showModuleBrief();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    scanRtlInsightItems();
    installRtlInsightsFixtureSnapshot();
    window.semanticDocks->rtlInsightsPanelCoordinator()->showClockResetDomainMap();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    scanRtlInsightItems();
    installRtlInsightsFixtureSnapshot();
    RtlInsightsPanelCoordinator* mainRtlInsights =
        window.semanticDocks->rtlInsightsPanelCoordinator();
    const QString graphModeBeforeDisabledState =
        mainRtlInsights->graphModeForTest();
    mainRtlInsights->showFsmGraph();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QString graphModeAfterDisabledState =
        mainRtlInsights->graphModeForTest();
    installRtlInsightsFixtureSnapshot();
    window.semanticDocks->rtlInsightsPanelCoordinator()->updateModuleContext(
        fixturePath,
        QStringLiteral("insight_top"),
        QStringLiteral("data_q"));
    window.semanticDocks->rtlInsightsPanelCoordinator()->showSignalJourney();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    scanRtlInsightItems();

    expectBool("RTL insights renders module port", sawPort, true);
    expectBool("RTL insights renders clock domain", sawClock, true);
    expectBool("RTL insights renders relationship evidence",
               sawRelationshipEvidence,
               true);
    expectBool("RTL insights renders relationship from endpoint",
               sawRelationshipFromEndpoint,
               true);
    expectBool("RTL insights renders relationship to endpoint",
               sawRelationshipToEndpoint,
               true);
    expectBool("RTL insights renders context kind",
               sawContextKind,
               true);
    expectBool("RTL insights renders context type",
               sawContextType,
               true);
    expectBool("RTL insights renders context source role",
               sawContextSourceRole,
               true);
    expectBool("RTL insights renders package member context",
               sawPackageMemberContext,
               true);
    expectBool("RTL insights renders package member kind",
               sawPackageMemberKind,
               true);
    expectBool("RTL insights renders package member type",
               sawPackageMemberType,
               true);
    expectBool("RTL insights renders module brief diagnostic",
               sawModuleBriefDiagnostic,
               true);
    expectBool("RTL insights renders module brief diagnostic source role",
               sawModuleBriefDiagnosticSourceRole,
               true);
    expectBool("RTL insights renders clock signal endpoint",
               sawClockSignalEndpoint,
               true);
    expectBool("RTL insights renders clock module endpoint",
               sawClockModuleEndpoint,
               true);
    expectBool("RTL insights renders clock relationship type",
               sawClockRelationshipType,
               true);
    expectBool("RTL insights renders clock category",
               sawClockCategory,
               true);
    expectBool("RTL insights renders clock evidence reason",
               sawClockEvidenceReason,
               true);
    expectBool("RTL insights renders clock source role",
               sawClockSourceRole,
               true);
    expectBool("RTL insights renders clock domain member type",
               sawClockDomainMemberType,
               true);
    expectBool("RTL insights renders clock domain member source role",
               sawClockDomainMemberSourceRole,
               true);
    expectBool("RTL insights renders clock domain member signal",
               sawClockDomainMemberSignal,
               true);
    expectBool("RTL insights renders reset domain member type",
               sawResetDomainMemberType,
               true);
    expectBool("RTL insights renders reset domain member signal",
               sawResetDomainMemberSignal,
               true);
    expectBool("RTL insights renders unmapped clock", sawUnmappedClock, true);
    expectBool("RTL insights renders unmapped clock category",
               sawUnmappedClockCategory,
               true);
    expectBool("RTL insights bottom dock disables State views",
               !mainRtlInsights->stateViewEnabledForTest(),
               true);
    expectBool("disabled State command preserves bottom dock graph mode",
               graphModeAfterDisabledState
                   == graphModeBeforeDisabledState,
               true);
    expectBool("RTL insights bottom dock does not carry State",
               mainRtlInsights->dock()
                   && !mainRtlInsights->dock()
                           ->property("carriesStateInsight")
                           .toBool(),
               true);
    expectBool("RTL insights renders signal journey", sawSignalJourney, true);
    expectBool("RTL insights renders signal journey declaration source role",
               sawSignalJourneyDeclarationSourceRole,
               true);
    expectBool("RTL insights renders signal journey from endpoint",
               sawSignalJourneyFromEndpoint,
               true);
    expectBool("RTL insights renders signal journey to endpoint",
               sawSignalJourneyToEndpoint,
               true);
    expectBool("RTL insights renders signal journey from endpoint type",
               sawSignalJourneyFromEndpointType,
               true);
    expectBool("RTL insights renders signal journey from endpoint source role",
               sawSignalJourneyFromEndpointSourceRole,
               true);
    expectBool("RTL insights renders signal journey to endpoint type",
               sawSignalJourneyToEndpointType,
               true);
    expectBool("RTL insights renders signal journey to endpoint source role",
               sawSignalJourneyToEndpointSourceRole,
               true);
    expectBool("RTL insights renders signal journey interface connection",
               sawSignalJourneyInterfaceConnection,
               true);
    expectBool("RTL insights renders signal journey interface kind",
               sawSignalJourneyInterfaceKind,
               true);
    expectBool("RTL insights renders signal journey interface base",
               sawSignalJourneyInterfaceBase,
               true);
    expectBool("RTL insights renders signal journey interface peer type",
               sawSignalJourneyInterfacePeerType,
               true);
    expectBool("RTL insights renders signal journey interface source role",
               sawSignalJourneyInterfaceSourceRole,
               true);

    installRtlInsightsFixtureSnapshot();
    window.semanticDocks->rtlInsightsPanelCoordinator()->showModuleInsights(
        fixturePath,
        QStringLiteral("insight_top"),
        QStringLiteral("clk"));
    window.semanticDocks->rtlInsightsPanelCoordinator()->showSignalJourney();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    expectBool("RTL insights renders timing signal journey",
               waitUntil(
                   [&]() {
                       const QList<QTreeWidgetItem*> timingItems =
                           navigableItems(rtlInsightsTree(window));
                       for (QTreeWidgetItem* item : timingItems) {
                           const QString section = item->text(0);
                           const QString symbol = item->text(1);
                           const QString detail = item->text(2);
                           if (section == QStringLiteral("Timing Connections")
                               && symbol == QStringLiteral("insight_top")
                               && detail == QStringLiteral("timing outgoing Clocks")) {
                               return true;
                           }
                       }
                       return false;
                   },
                   500),
               true);
}

static void runRtlInsightsSemanticDiffRegression(MainWindow& window,
                                                 const QString& fixturePath)
{
    printf("\n-- RTL insights semantic diff regression --\n");

    QList<SemanticSymbolRecord> beforeRecords;
    const SemanticSymbolRecord beforeModule = makeGuiSmokeRecord(
        9701,
        fixturePath,
        QStringLiteral("diff_top"),
        SymbolTaxonomy::DeclarationKind::Module,
        SymbolTaxonomy::CollectorKind::Module,
        1);
    beforeRecords.append(beforeModule);
    beforeRecords.append(makeGuiSmokeRecord(
        9702,
        fixturePath,
        QStringLiteral("data"),
        SymbolTaxonomy::DeclarationKind::Port,
        SymbolTaxonomy::CollectorKind::PortInput,
        2,
        SymbolTaxonomy::SymbolOwnerScope::Module,
        QStringLiteral("diff_top")));
    beforeRecords.append(makeGuiSmokeRecord(
        9703,
        fixturePath,
        QStringLiteral("stale_q"),
        SymbolTaxonomy::DeclarationKind::Signal,
        SymbolTaxonomy::CollectorKind::Logic,
        6,
        SymbolTaxonomy::SymbolOwnerScope::Module,
        QStringLiteral("diff_top")));
    const SemanticSymbolRecord beforeInstance = makeGuiSmokeRecord(
        9704,
        fixturePath,
        QStringLiteral("u_old"),
        SymbolTaxonomy::DeclarationKind::Instance,
        SymbolTaxonomy::CollectorKind::Inst,
        10,
        SymbolTaxonomy::SymbolOwnerScope::Module,
        QStringLiteral("diff_top"));
    beforeRecords.append(beforeInstance);

    QList<SemanticSymbolRecord> afterRecords;
    const SemanticSymbolRecord afterModule = makeGuiSmokeRecord(
        9801,
        fixturePath,
        QStringLiteral("diff_top"),
        SymbolTaxonomy::DeclarationKind::Module,
        SymbolTaxonomy::CollectorKind::Module,
        1);
    afterRecords.append(afterModule);
    afterRecords.append(makeGuiSmokeRecord(
        9802,
        fixturePath,
        QStringLiteral("data"),
        SymbolTaxonomy::DeclarationKind::Port,
        SymbolTaxonomy::CollectorKind::PortOutput,
        2,
        SymbolTaxonomy::SymbolOwnerScope::Module,
        QStringLiteral("diff_top")));
    afterRecords.append(makeGuiSmokeRecord(
        9803,
        fixturePath,
        QStringLiteral("state_q"),
        SymbolTaxonomy::DeclarationKind::Signal,
        SymbolTaxonomy::CollectorKind::Logic,
        7,
        SymbolTaxonomy::SymbolOwnerScope::Module,
        QStringLiteral("diff_top")));
    const SemanticSymbolRecord afterInstance = makeGuiSmokeRecord(
        9804,
        fixturePath,
        QStringLiteral("u_new"),
        SymbolTaxonomy::DeclarationKind::Instance,
        SymbolTaxonomy::CollectorKind::Inst,
        10,
        SymbolTaxonomy::SymbolOwnerScope::Module,
        QStringLiteral("diff_top"));
    afterRecords.append(afterInstance);

    const SemanticRelationship beforeRelationship =
        semanticFixtureRelationship(beforeModule,
                                    beforeInstance,
                                    SymbolRelationshipEngine::INSTANTIATES);
    const SemanticRelationship afterRelationship =
        semanticFixtureRelationship(afterModule,
                                    afterInstance,
                                    SymbolRelationshipEngine::INSTANTIATES);

    SemanticDiagnostic beforeDiagnostic;
    beforeDiagnostic.fileName = fixturePath;
    beforeDiagnostic.line = 6;
    beforeDiagnostic.column = 3;
    beforeDiagnostic.message = QStringLiteral("old warning");
    beforeDiagnostic.severity = SemanticDiagnostic::Warning;

    SemanticDiagnostic afterDiagnostic;
    afterDiagnostic.fileName = fixturePath;
    afterDiagnostic.line = 7;
    afterDiagnostic.column = 5;
    afterDiagnostic.message = QStringLiteral("new error");
    afterDiagnostic.severity = SemanticDiagnostic::Error;

    auto beforeSnapshot = snapshotFromRecords(
        beforeRecords,
        QList<SemanticRelationship>{beforeRelationship},
        QList<SemanticDiagnostic>{beforeDiagnostic});
    auto afterSnapshot = snapshotFromRecords(
        afterRecords,
        QList<SemanticRelationship>{afterRelationship},
        QList<SemanticDiagnostic>{afterDiagnostic});

    expectBool("semantic diff has no legacy RTL insights bottom panel",
               window.semanticDocks
                   && window.semanticDocks
                          ->rtlInsightsPanelCoordinator() == nullptr
                   && rtlInsightsTree(window) == nullptr,
               true);
    return;

    window.semanticDocks->rtlInsightsPanelCoordinator()->showSemanticDiff(
        beforeSnapshot,
        afterSnapshot,
        QStringLiteral("diff_top"),
        fixturePath,
        fixturePath);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    bool sawModifiedPort = false;
    bool sawModifiedPortBefore = false;
    bool sawModifiedPortAfter = false;
    bool sawModifiedPortSourceRole = false;
    bool sawAddedSignal = false;
    bool sawRemovedSignal = false;
    bool sawAddedRelationship = false;
    bool sawRemovedRelationship = false;
    bool sawAddedRelationshipFromEndpoint = false;
    bool sawAddedRelationshipToEndpoint = false;
    bool sawAddedRelationshipSourceRole = false;
    bool sawAddedDiagnostic = false;
    bool sawAddedDiagnosticSourceRole = false;
    bool sawRemovedDiagnostic = false;
    const QList<QTreeWidgetItem*> items = navigableItems(rtlInsightsTree(window));
    for (QTreeWidgetItem* item : items) {
        sawModifiedPort = sawModifiedPort
            || (item->text(0) == QStringLiteral("Modified Ports")
                && item->text(1) == QStringLiteral("data")
                && item->text(2).contains(QStringLiteral("input -> output"))
                && item->text(2).contains(QStringLiteral("scope diff_top")));
        sawModifiedPortBefore = sawModifiedPortBefore
            || (item->text(0) == QStringLiteral("Before")
                && item->text(1) == QStringLiteral("input")
                && item->text(2) == QStringLiteral("scope diff_top"));
        sawModifiedPortAfter = sawModifiedPortAfter
            || (item->text(0) == QStringLiteral("After")
                && item->text(1) == QStringLiteral("output")
                && item->text(2) == QStringLiteral("scope diff_top"));
        sawModifiedPortSourceRole = sawModifiedPortSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("port"));
        sawAddedSignal = sawAddedSignal
            || (item->text(0) == QStringLiteral("Added Signals")
                && item->text(1) == QStringLiteral("state_q")
                && item->text(2).contains(QStringLiteral("scope diff_top")));
        sawRemovedSignal = sawRemovedSignal
            || (item->text(0) == QStringLiteral("Removed Signals")
                && item->text(1) == QStringLiteral("stale_q")
                && item->text(2).contains(QStringLiteral("scope diff_top")));
        sawAddedRelationship = sawAddedRelationship
            || (item->text(0) == QStringLiteral("Added")
                && item->text(1) == QStringLiteral("Instantiates")
                && item->text(2) == QStringLiteral("diff_top -> u_new"));
        sawAddedRelationshipFromEndpoint = sawAddedRelationshipFromEndpoint
            || (item->text(0) == QStringLiteral("From")
                && item->text(1) == QStringLiteral("diff_top")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawAddedRelationshipToEndpoint = sawAddedRelationshipToEndpoint
            || (item->text(0) == QStringLiteral("To")
                && item->text(1) == QStringLiteral("u_new")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawAddedRelationshipSourceRole = sawAddedRelationshipSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Instantiates"));
        sawRemovedRelationship = sawRemovedRelationship
            || (item->text(0) == QStringLiteral("Removed")
                && item->text(1) == QStringLiteral("Instantiates")
                && item->text(2) == QStringLiteral("diff_top -> u_old"));
        sawAddedDiagnostic = sawAddedDiagnostic
            || (item->text(0) == QStringLiteral("Added")
                && item->text(1) == QStringLiteral("new error")
                && item->text(2) == QStringLiteral("Error, design source"));
        sawAddedDiagnosticSourceRole = sawAddedDiagnosticSourceRole
            || (item->text(0) == QStringLiteral("Source Role")
                && item->text(1) == QStringLiteral("design source")
                && item->text(2) == QStringLiteral("Error"));
        sawRemovedDiagnostic = sawRemovedDiagnostic
            || (item->text(0) == QStringLiteral("Removed")
                && item->text(1) == QStringLiteral("old warning")
                && item->text(2) == QStringLiteral("Warning, design source"));
    }

    expectBool("RTL insights renders modified diff port", sawModifiedPort, true);
    expectBool("RTL insights renders modified diff before",
               sawModifiedPortBefore,
               true);
    expectBool("RTL insights renders modified diff after",
               sawModifiedPortAfter,
               true);
    expectBool("RTL insights renders modified diff source role",
               sawModifiedPortSourceRole,
               true);
    expectBool("RTL insights renders added diff signal", sawAddedSignal, true);
    expectBool("RTL insights renders removed diff signal", sawRemovedSignal, true);
    expectBool("RTL insights renders added diff relationship",
               sawAddedRelationship,
               true);
    expectBool("RTL insights renders added diff from endpoint",
               sawAddedRelationshipFromEndpoint,
               true);
    expectBool("RTL insights renders added diff to endpoint",
               sawAddedRelationshipToEndpoint,
               true);
    expectBool("RTL insights renders added diff relationship source role",
               sawAddedRelationshipSourceRole,
               true);
    expectBool("RTL insights renders removed diff relationship",
               sawRemovedRelationship,
               true);
    expectBool("RTL insights renders added diff diagnostic", sawAddedDiagnostic, true);
    expectBool("RTL insights renders added diff diagnostic source role",
               sawAddedDiagnosticSourceRole,
               true);
    expectBool("RTL insights renders removed diff diagnostic", sawRemovedDiagnostic, true);
}

static void runTreeSitterFoldingProviderRegression()
{
    printf("\n-- tree-sitter folding provider regression --\n");

    TSDocument syntaxDocument;
    syntaxDocument.setText(QStringLiteral(
        "module fold_top;\n"
        "  initial begin\n"
        "    case (sel)\n"
        "      1'b0: a = b;\n"
        "      default: a = c;\n"
        "    endcase\n"
        "  end\n"
        "endmodule\n"));
    const QList<TSFoldRange> syntaxRanges = syntaxDocument.foldingRanges();
    bool hasModuleFold = false;
    bool hasNestedFold = false;
    for (const TSFoldRange& range : syntaxRanges) {
        hasModuleFold = hasModuleFold
            || (range.kind == TSFoldRangeKind::Syntax
                && range.startLine == 0
                && range.endLine >= 7);
        hasNestedFold = hasNestedFold
            || (range.kind == TSFoldRangeKind::Syntax
                && range.startLine > 0
                && range.endLine > range.startLine);
    }
    expectBool("folding provider finds module range", hasModuleFold, true);
    expectBool("folding provider finds nested syntax range", hasNestedFold, true);

    TSDocument customDocument;
    customDocument.setText(QStringLiteral(
        "module fold_top;\n"
        "// fold clock   reset path\n"
        "logic clk;\n"
        "logic rst_n;\n"
        "// endfold\n"
        "endmodule\n"));
    const QList<TSFoldRange> customRanges = customDocument.foldingRanges();
    bool hasCustomFold = false;
    for (const TSFoldRange& range : customRanges) {
        hasCustomFold = hasCustomFold
            || (range.kind == TSFoldRangeKind::Custom
                && range.startLine == 1
                && range.endLine == 4
                && range.label == QStringLiteral("clock   reset path"));
    }
    expectBool("custom fold marker creates range", hasCustomFold, true);

    TSDocument malformedDocument;
    malformedDocument.setText(QStringLiteral(
        "// endfold\n"
        "module fold_top;\n"
        "// fold never closed\n"
        "endmodule\n"));
    const QList<TSFoldRange> malformedRanges = malformedDocument.foldingRanges();
    bool hasMalformedCustom = false;
    for (const TSFoldRange& range : malformedRanges)
        hasMalformedCustom = hasMalformedCustom || range.kind == TSFoldRangeKind::Custom;
    expectBool("malformed custom fold markers do not create range",
               hasMalformedCustom,
               false);

    MyCodeEditor editor;
    editor.setPlainText(QStringLiteral(
        "module fold_top;\n"
        "  logic a;\n"
        "endmodule\n"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("editor folding has module start",
               editor.state->folding.hasFoldAtLine(0),
               true);
    const bool folded = editor.state->folding.toggleFoldAtLine(&editor, 0);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("editor folding collapses block",
               folded && !editor.foldLineVisibleForTest(1),
               true);
    const bool unfolded = editor.state->folding.toggleFoldAtLine(&editor, 0);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("editor folding expands block",
               unfolded && editor.foldLineVisibleForTest(1),
               true);

    MyCodeEditor gutterEditor;
    gutterEditor.resize(320, 160);
    gutterEditor.setPlainText(QStringLiteral(
        "module fold_top;\n"
        "  logic a;\n"
        "endmodule\n"));
    gutterEditor.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QMouseEvent foldClick(QEvent::MouseButtonPress,
                          QPointF(EditorGutter::foldLeft(&gutterEditor) + 6, 4),
                          QPointF(EditorGutter::foldLeft(&gutterEditor) + 6, 4),
                          QPointF(EditorGutter::foldLeft(&gutterEditor) + 6, 4),
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
    const bool gutterCollapsed =
        gutterEditor.state->handleGutterMousePress(&gutterEditor, &foldClick)
        && !gutterEditor.foldLineVisibleForTest(1);
    expectBool("editor gutter click collapses fold", gutterCollapsed, true);
    QMouseEvent unfoldClick(QEvent::MouseButtonPress,
                            QPointF(EditorGutter::foldLeft(&gutterEditor) + 6, 4),
                            QPointF(EditorGutter::foldLeft(&gutterEditor) + 6, 4),
                            QPointF(EditorGutter::foldLeft(&gutterEditor) + 6, 4),
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    const bool gutterExpanded =
        gutterEditor.state->handleGutterMousePress(&gutterEditor, &unfoldClick)
        && gutterEditor.foldLineVisibleForTest(1);
    expectBool("editor gutter click expands fold", gutterExpanded, true);
    gutterEditor.showFindDialog();
    auto* inlineFind = gutterEditor.findChild<QWidget*>(QStringLiteral("editorFindBar"));
    auto* inlineInput = inlineFind ? inlineFind->findChild<QLineEdit*>(QStringLiteral("editorFindInput")) : nullptr;
    if (inlineInput) inlineInput->setText(QStringLiteral("logic"));
    gutterEditor.showReplaceDialog();
    expectBool("find and replace reuse an inline bar and preserve the query",
               inlineFind && !inlineFind->isWindow() && inlineInput
                   && inlineInput->text() == QStringLiteral("logic")
                   && gutterEditor.findChild<QWidget*>(QStringLiteral("editorFindBar")) == inlineFind, true);
    if (inlineFind) inlineFind->close();


    MyCodeEditor commandEditor;
    QSignalSpy commandStatusSpy(&commandEditor,
                                &MyCodeEditor::editorStatusMessageRequested);
    commandEditor.startFoldRegionMarkMode();
    expectBool("fold region action enters mark mode",
               commandEditor.foldRegionMarkModeActive(),
               true);
    expectBool("fold region action emits start-line mode status",
               commandStatusSpy.count() > 0
                   && commandStatusSpy.last().at(0).toString().contains(
                       QStringLiteral("click start line")),
               true);
    QMouseEvent markStartClick(QEvent::MouseButtonPress,
                               QPointF(EditorGutter::foldLeft(&gutterEditor) + 6, 4),
                               QPointF(EditorGutter::foldLeft(&gutterEditor) + 6, 4),
                               QPointF(EditorGutter::foldLeft(&gutterEditor) + 6, 4),
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
    const bool startLineSelected =
        commandEditor.state->handleGutterMousePress(&commandEditor, &markStartClick);
    expectBool("fold region gutter start selects next stage",
               startLineSelected
                   && commandStatusSpy.count() > 0
                   && commandStatusSpy.last().at(0).toString().contains(
                       QStringLiteral("click end line")),
               true);
    QKeyEvent blockedText(QEvent::KeyPress,
                          Qt::Key_A,
                          Qt::NoModifier,
                          QStringLiteral("a"));
    QApplication::sendEvent(&commandEditor, &blockedText);
    expectBool("fold region mode blocks text input",
               commandEditor.toPlainText().isEmpty(),
               true);
    QKeyEvent cancelFold(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&commandEditor, &cancelFold);
    expectBool("Esc cancels fold region mark mode",
               !commandEditor.foldRegionMarkModeActive(),
               true);

    MyCodeEditor markerEditor;
    const QString markerOriginal = QStringLiteral(
        "module fold_top;\n"
        "  logic clk;\n"
        "  logic rst_n;\n"
        "endmodule\n");
    markerEditor.setPlainText(markerOriginal);
    const bool markersInserted =
        markerEditor.insertCustomFoldMarkersForTest(
            1,
            2,
            QStringLiteral("clock   reset path"));
    const QString markerText = markerEditor.toPlainText();
    expectBool("fold marker insertion adds alias",
               markersInserted
                   && markerText.contains(QStringLiteral("// fold clock   reset path"))
                   && markerText.contains(QStringLiteral("// endfold")),
               true);
    markerEditor.undo();
    expectBool("fold marker insertion is one undo block",
               markerEditor.toPlainText() == markerOriginal,
               true);

    FoldBlockShelfModel shelfModel;
    FoldShelfItem shelfItem;
    shelfItem.alias = QStringLiteral("clock reset");
    shelfItem.text = QStringLiteral("// fold clock reset\nlogic clk;\n// endfold\n");
    shelfItem.sourceFile = QStringLiteral("C:/fixture/fold_top.sv");
    shelfItem.sourceStartLine = 2;
    shelfItem.sourceEndLine = 4;
    shelfItem.originKind = FoldShelfOriginKind::Moved;
    const QString shelfId = shelfModel.addItem(shelfItem);
    expectBool("fold shelf model stores item",
               !shelfId.isEmpty()
                   && shelfModel.items().size() == 1
                   && shelfModel.item(shelfId).lineCount == 3,
               true);
    expectBool("fold shelf model consumes item",
               shelfModel.consumeItem(shelfId)
                   && shelfModel.item(shelfId).consumed,
               true);
    expectBool("fold shelf model removes item",
               shelfModel.removeItem(shelfId)
                   && shelfModel.items().isEmpty(),
               true);
    const QByteArray encodedShelfItem = encodeFoldShelfItem(shelfItem);
    const FoldShelfItem decodedShelfItem = decodeFoldShelfItem(encodedShelfItem);
    expectBool("fold shelf item mime round-trips",
               decodedShelfItem.alias == shelfItem.alias
                   && decodedShelfItem.text == shelfItem.text
                   && decodedShelfItem.sourceFile == shelfItem.sourceFile
                   && decodedShelfItem.originKind == FoldShelfOriginKind::Moved,
               true);

    MyCodeEditor shelfEditor;
    shelfEditor.setDocumentFileName(QStringLiteral("C:/fixture/fold_top.sv"));
    shelfEditor.setPlainText(QStringLiteral(
        "module fold_top;\n"
        "// fold reusable block\n"
        "  logic clk;\n"
        "  logic rst_n;\n"
        "// endfold\n"
        "endmodule\n"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const FoldShelfItem extractedShelfItem =
        shelfEditor.foldShelfItemAtLineForTest(2, FoldShelfOriginKind::Moved);
    expectBool("fold shelf extracts custom fold block",
               extractedShelfItem.alias == QStringLiteral("reusable block")
                   && extractedShelfItem.text.contains(QStringLiteral("logic clk"))
                   && extractedShelfItem.sourceStartLine == 1
                   && extractedShelfItem.sourceEndLine == 4
                   && extractedShelfItem.originKind == FoldShelfOriginKind::Moved,
               true);
    const bool deletedFoldBlock = shelfEditor.deleteCustomFoldAtLineForTest(2);
    expectBool("fold shelf move deletes source block",
               deletedFoldBlock
                   && !shelfEditor.toPlainText().contains(QStringLiteral("logic clk")),
               true);
    shelfEditor.undo();
    expectBool("fold shelf move delete is one undo block",
               shelfEditor.toPlainText().contains(QStringLiteral("logic clk")),
               true);

    MyCodeEditor insertShelfEditor;
    insertShelfEditor.setPlainText(QStringLiteral("module fold_top;\nendmodule\n"));
    const bool insertedShelfItem =
        insertShelfEditor.insertFoldShelfItemAtLineForTest(extractedShelfItem, 1);
    expectBool("fold shelf inserts item at line boundary",
               insertedShelfItem
                   && insertShelfEditor.toPlainText().contains(QStringLiteral("// fold reusable block"))
                   && insertShelfEditor.toPlainText().contains(QStringLiteral("// endfold")),
               true);
    insertShelfEditor.undo();
    expectBool("fold shelf insert is one undo block",
               insertShelfEditor.toPlainText() == QStringLiteral("module fold_top;\nendmodule\n"),
               true);

    MyCodeEditor shelfCommandEditor;
    QSignalSpy shelfStatusSpy(&shelfCommandEditor,
                              &MyCodeEditor::editorStatusMessageRequested);
    shelfCommandEditor.startFoldShelfMode();
    expectBool("fold shelf action enters shelf mode",
               shelfCommandEditor.foldShelfModeActive(),
               true);
    expectBool("fold shelf action emits shelf mode status",
               shelfStatusSpy.count() > 0
                   && shelfStatusSpy.last().at(0).toString().contains(
                       QStringLiteral("Fold Shelf")),
               true);
    QKeyEvent blockedShelfText(QEvent::KeyPress,
                               Qt::Key_X,
                               Qt::NoModifier,
                               QStringLiteral("x"));
    QApplication::sendEvent(&shelfCommandEditor, &blockedShelfText);
    expectBool("fold shelf mode blocks text input",
               shelfCommandEditor.toPlainText().isEmpty()
                   && shelfCommandEditor.foldShelfModeActive(),
               true);
    QKeyEvent cancelShelf(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&shelfCommandEditor, &cancelShelf);
    expectBool("Esc cancels fold shelf mode",
               !shelfCommandEditor.foldShelfModeActive(),
               true);
}

static void runNavigationHierarchyModelRegression()
{
    printf("\n-- navigation hierarchy model regression --\n");

    NavigationWidget widget;
    expectBool("navigation only exposes files and design tabs",
               widget.tabWidget && widget.tabWidget->count() == 2
                   && widget.tabWidget->tabText(0) == QStringLiteral("Files")
                   && widget.tabWidget->tabText(1) == QStringLiteral("Design"),
               true);
    widget.setSearchFilter(NavigationWidget::FileTab,
                           QStringLiteral("rtl/top"));
    widget.setSearchFilter(NavigationWidget::DesignTab,
                           QStringLiteral("u_stage"));
    widget.setActiveTab(NavigationWidget::FileTab);
    expectBool("Files search keeps its independent query",
               widget.searchLineEdit
                   && widget.searchLineEdit->text()
                          == QStringLiteral("rtl/top"),
               true);
    widget.setActiveTab(NavigationWidget::DesignTab);
    expectBool("Design search restores its independent query",
               widget.searchLineEdit
                   && widget.searchLineEdit->text()
                          == QStringLiteral("u_stage")
                   && widget.searchLineEdit->placeholderText().contains(
                          QStringLiteral("instances")),
               true);
    widget.setSearchFilter(NavigationWidget::DesignTab, QString());
    widget.setActiveTab(NavigationWidget::FileTab);
    widget.setSearchFilter(NavigationWidget::FileTab, QString());

    const QString fileTabPath = QStringLiteral("C:/fixture/relationship_top.sv");
    widget.updateFileHierarchy({fileTabPath});
    QTreeWidgetItem* fileItem = widget.findFileItemByPath(fileTabPath);
    expectBool("files tab renders file item", fileItem != nullptr, true);

    QSignalSpy fileClicks(&widget, &NavigationWidget::fileDoubleClicked);
    if (fileItem)
        widget.onFileTreeDoubleClicked(fileItem, 0);
    expectBool("files tab double-click emits file",
               fileClicks.count() == 1
                   && fileClicks.takeFirst().at(0).toString() == fileTabPath,
               true);

    const QString designTopFile = QStringLiteral("C:/fixture/design_top.sv");
    const QString designStageFile = QStringLiteral("C:/fixture/design_stage.sv");
    const QString designExtraFile = QStringLiteral("C:/fixture/design_extra.sv");
    const SemanticSymbolRecord designTop =
        makeGuiSmokeRecord(1240,
                           designTopFile,
                           QStringLiteral("design_top"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           3);
    const SemanticSymbolRecord designStage =
        makeGuiSmokeRecord(1241,
                           designStageFile,
                           QStringLiteral("design_stage"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           1);
    const SemanticSymbolRecord designInstance =
        makeGuiSmokeRecord(1242,
                           designTopFile,
                           QStringLiteral("u_stage"),
                           SymbolTaxonomy::DeclarationKind::Instance,
                           SymbolTaxonomy::CollectorKind::Inst,
                           8,
                           SymbolTaxonomy::SymbolOwnerScope::Module,
                           QStringLiteral("design_top"),
                           QStringLiteral("design_stage"));
    const SemanticSymbolRecord designInterface =
        makeGuiSmokeRecord(1243,
                           designTopFile,
                           QStringLiteral("design_if"),
                           SymbolTaxonomy::DeclarationKind::Interface,
                           SymbolTaxonomy::CollectorKind::Interface,
                           20);
    const SemanticSymbolRecord designInterfaceInstance =
        SemanticFixtureRecordBuilder(QStringLiteral("u_design_if"),
                                     SymbolTaxonomy::DeclarationKind::Instance)
            .withFile(designTopFile)
            .withLocalHandle(1244)
            .withLine(9)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Inst)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("design_top"),
                       designTop.stableKey)
            .withType(QStringLiteral("design_if"),
                      QStringLiteral("design_if"),
                      SymbolTaxonomy::DeclarationKind::Interface)
            .record();

    const SemanticRelationship instantiatesStage =
        semanticFixtureRelationship(designTop,
                                    designInstance,
                                    SymbolRelationshipEngine::INSTANTIATES);
    const SemanticRelationship instantiatesInterface =
        semanticFixtureRelationship(designTop,
                                    designInterfaceInstance,
                                    SymbolRelationshipEngine::INSTANTIATES);
    SemanticIndex designIndex;
    designIndex.setSnapshot(snapshotFromRecords(
        {designTop,
         designStage,
         designInstance,
         designInterface,
         designInterfaceInstance},
        {instantiatesStage, instantiatesInterface}));
    HierarchyService designHierarchyService(&designIndex);
    const DesignHierarchyReport designReport =
        designHierarchyService.getDesignHierarchyReport(QStringLiteral("design_top"));
    expectBool("design hierarchy infers top from instantiation graph",
               designHierarchyService.inferDesignTopModule()
                   == QStringLiteral("design_top"),
               true);
    expectBool("design hierarchy report has top and instance",
               designReport.nodes.size() == 2,
               true);
    expectBool("design hierarchy stores exact instance paths",
               designReport.nodes.size() == 2
                   && designReport.nodes.first().instancePath
                          == QStringLiteral("design_top")
                   && designReport.nodes.last().instancePath
                          == QStringLiteral("design_top.u_stage"),
               true);
    bool designReportHasInterfaceNode = false;
    for (const DesignHierarchyNode& node : designReport.nodes) {
        designReportHasInterfaceNode = designReportHasInterfaceNode
            || node.instanceName == QStringLiteral("u_design_if")
            || node.moduleType == QStringLiteral("design_if");
    }
    expectBool("design hierarchy report filters interface instance",
               !designReportHasInterfaceNode,
               true);
    expectBool("design hierarchy report keeps selected top",
               designReport.topModule == QStringLiteral("design_top"),
               true);
    expectBool("design hierarchy report has no unresolved modules",
               designReport.unresolvedModules.isEmpty(),
               true);
    expectBool("modules defined in file lists design top",
               designHierarchyService.modulesDefinedInFile(designTopFile)
                   .contains(QStringLiteral("design_top")),
               true);

    const QString ws1TopFile = QStringLiteral("C:/fixture/ws1/top.sv");
    const QString ws1LeafFile = QStringLiteral("C:/fixture/ws1/shared_leaf.sv");
    const QString ws2TopFile = QStringLiteral("C:/fixture/ws2/top.sv");
    const QString ws2LeafFile = QStringLiteral("C:/fixture/ws2/shared_leaf.sv");
    const SemanticSymbolRecord ws1Top =
        makeGuiSmokeRecord(1250,
                           ws1TopFile,
                           QStringLiteral("ws1_top"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           1);
    const SemanticSymbolRecord ws1Leaf =
        makeGuiSmokeRecord(1251,
                           ws1LeafFile,
                           QStringLiteral("shared_leaf"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           1);
    const SemanticSymbolRecord ws2Top =
        makeGuiSmokeRecord(1252,
                           ws2TopFile,
                           QStringLiteral("ws2_top"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           1);
    const SemanticSymbolRecord ws2Leaf =
        makeGuiSmokeRecord(1253,
                           ws2LeafFile,
                           QStringLiteral("shared_leaf"),
                           SymbolTaxonomy::DeclarationKind::Module,
                           SymbolTaxonomy::CollectorKind::Module,
                           50);
    const SemanticSymbolRecord ws2Instance =
        makeGuiSmokeRecord(1254,
                           ws2TopFile,
                           QStringLiteral("u_shared_leaf"),
                           SymbolTaxonomy::DeclarationKind::Instance,
                           SymbolTaxonomy::CollectorKind::Inst,
                           5,
                           SymbolTaxonomy::SymbolOwnerScope::Module,
                           QStringLiteral("ws2_top"),
                           QStringLiteral("shared_leaf"));
    const SemanticRelationship scopedInstantiates =
        semanticFixtureRelationship(ws2Top,
                                    ws2Instance,
                                    SymbolRelationshipEngine::INSTANTIATES);
    SemanticIndex scopedDesignIndex;
    scopedDesignIndex.setSnapshot(snapshotFromRecords(
        {ws1Top, ws1Leaf, ws2Top, ws2Leaf, ws2Instance},
        {scopedInstantiates}));
    HierarchyService scopedHierarchyService(&scopedDesignIndex);
    QSet<QString> ws2Scope;
    ws2Scope.insert(QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(ws2TopFile).absoluteFilePath())));
    ws2Scope.insert(QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(ws2LeafFile).absoluteFilePath())));
    const QStringList scopedTops =
        scopedHierarchyService.inferDesignTopModules(ws2Scope);
    const DesignHierarchyReport scopedReport =
        scopedHierarchyService.getDesignHierarchyReport(scopedTops,
                                                        QStringLiteral("ws2_top"),
                                                        ws2Scope);
    expectBool("scoped design top excludes other workspace",
               scopedTops.contains(QStringLiteral("ws2_top"))
                   && !scopedTops.contains(QStringLiteral("ws1_top")),
               true);
    expectBool("scoped design hierarchy uses scoped duplicate module",
               scopedReport.nodes.size() == 2
                   && scopedReport.nodes.last().moduleType == QStringLiteral("shared_leaf")
                   && scopedReport.nodes.last().definitionFile
                          == QDir::cleanPath(QDir::fromNativeSeparators(
                              QFileInfo(ws2LeafFile).absoluteFilePath())),
               true);

    const QString normalizedTopFile =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(designTopFile).absoluteFilePath()));
    const QString normalizedStageFile =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(designStageFile).absoluteFilePath()));
    expectBool("design hierarchy participating files include top",
               designReport.participatingFiles.contains(normalizedTopFile),
               true);
    expectBool("design hierarchy participating files include child definition",
               designReport.participatingFiles.contains(normalizedStageFile),
               true);

    widget.setActiveTab(NavigationWidget::DesignTab);
    widget.updateFileHierarchy({designTopFile, designStageFile, designExtraFile});
    widget.updateDesignHierarchy(designReport);

    QTreeWidgetItem* designItem = nullptr;
    QLabel* designStatusLabel = nullptr;
    for (QLabel* label : widget.findChildren<QLabel*>()) {
        if (label->text().contains(QStringLiteral("Design Top: design_top"))) {
            designStatusLabel = label;
            break;
        }
    }
    for (QTreeWidget* tree : widget.findChildren<QTreeWidget*>()) {
        designItem = findItemByText(tree, QStringLiteral("u_stage"), 0);
        if (designItem
            && designItem->text(1) != QStringLiteral("design_stage")) {
            designItem = nullptr;
        }
        if (designItem)
            break;
    }
    expectBool("design hierarchy status shows top",
               designStatusLabel != nullptr,
               true);
    expectBool("design hierarchy renders instance text",
               designItem != nullptr,
               true);
    QTreeWidgetItem* designInterfaceItem = nullptr;
    for (QTreeWidget* tree : widget.findChildren<QTreeWidget*>()) {
        designInterfaceItem = findItemByText(tree, QStringLiteral("u_design_if"), 0);
        if (designInterfaceItem)
            break;
    }
    expectBool("design hierarchy tree filters interface instance",
               designInterfaceItem == nullptr,
               true);

    bool designDoubleClicked = false;
    DesignHierarchyNode clickedDesignNode;
    QObject::connect(&widget, &NavigationWidget::designNodeDoubleClicked,
                     &widget, [&](const DesignHierarchyNode& node) {
                         designDoubleClicked = true;
                         clickedDesignNode = node;
                     });
    if (designItem)
        widget.onDesignTreeDoubleClicked(designItem, 0);
    expectBool("design hierarchy double-click emits node",
               designDoubleClicked,
               true);
    expectBool("design hierarchy double-click preserves instance site",
               clickedDesignNode.instanceName == QStringLiteral("u_stage")
                   && clickedDesignNode.instanceLine == 8
                   && clickedDesignNode.instancePath
                          == QStringLiteral("design_top.u_stage"),
               true);

    NavigationWidget contextualNavigationWidget;
    NavigationManager contextualNavigationManager;
    contextualNavigationManager.setNavigationWidget(
        &contextualNavigationWidget);
    contextualNavigationManager.onWorkspaceChanged(
        QStringLiteral("C:/fixture"));
    bool instanceNavigationEmitted = false;
    HierarchyInstanceContext emittedInstanceContext;
    QObject::connect(
        &contextualNavigationManager,
        &NavigationManager::instanceNavigationRequested,
        &contextualNavigationWidget,
        [&](const QString&, int, const HierarchyInstanceContext& context) {
            instanceNavigationEmitted = true;
            emittedInstanceContext = context;
        });
    contextualNavigationWidget.designNodeDoubleClicked(clickedDesignNode);
    expectBool("Design navigation binds exact hierarchy context",
               instanceNavigationEmitted
                   && emittedInstanceContext.isBound()
                   && emittedInstanceContext.activeTopModule
                          == QStringLiteral("design_top")
                   && emittedInstanceContext.instancePath
                          == QStringLiteral("design_top.u_stage"),
               true);

    bool fileNavigationEmitted = false;
    QObject::connect(&contextualNavigationManager,
                     &NavigationManager::navigationRequested,
                     &contextualNavigationWidget,
                     [&](const QString&, int) {
                         fileNavigationEmitted = true;
                     });
    contextualNavigationWidget.fileDoubleClicked(fileTabPath);
    expectBool("Files navigation uses unbound navigation path",
               fileNavigationEmitted,
               true);

    QTreeWidgetItem* topFileItem = widget.findFileItemByPath(designTopFile);
    QTreeWidgetItem* extraFileItem = widget.findFileItemByPath(designExtraFile);
    expectBool("design participating file remains visible",
               topFileItem != nullptr
                   && extraFileItem != nullptr
                   && topFileItem->foreground(0).color().alpha()
                          > extraFileItem->foreground(0).color().alpha(),
               true);
    expectBool("design non-participating file is dimmed",
               extraFileItem != nullptr
                   && extraFileItem->foreground(0).color().alpha() < 255,
               true);

    widget.clearDesignHierarchy();
    topFileItem = widget.findFileItemByPath(designTopFile);
    extraFileItem = widget.findFileItemByPath(designExtraFile);
    expectBool("clear design restores file opacity",
               topFileItem != nullptr
                   && extraFileItem != nullptr
                   && topFileItem->foreground(0).color().alpha()
                          == extraFileItem->foreground(0).color().alpha(),
               true);
}

static void sendWidgetKey(QWidget* widget,
                          int key,
                          const QString& text = QString(),
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    if (!widget)
        return;
    QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
    QApplication::sendEvent(widget, &event);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

static void runCommandLayerRegression(MainWindow& window)
{
    printf("\n-- command layer regression --\n");

    QTemporaryDir tempDir;
    expectBool("command layer temp workspace valid", tempDir.isValid(), true);
    const QString root = QDir::cleanPath(tempDir.path());
    QDir(root).mkpath(QStringLiteral("rtl"));
    const QString topFile =
        QDir(root).filePath(QStringLiteral("rtl/top.sv"));
    const QString childFile =
        QDir(root).filePath(QStringLiteral("rtl/child.sv"));
    const QString packageFile =
        QDir(root).filePath(QStringLiteral("rtl/cfg_pkg.sv"));
    const QString outsideFile =
        QDir(root).filePath(QStringLiteral("../outside.sv"));

    ProjectModel project;
    project.setWorkspaceState(root, {topFile, childFile, packageFile});
    const SemanticSymbolRecord topModule =
        SemanticFixtureRecordBuilder(QStringLiteral("top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(topFile)
            .withLocalHandle(87001)
            .withRange(10, 1, 15, 10)
            .record();
    const SemanticSymbolRecord childModule =
        SemanticFixtureRecordBuilder(QStringLiteral("child"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(childFile)
            .withLocalHandle(87002)
            .withRange(30, 1, 35, 10)
            .record();
    const SemanticSymbolRecord outsideModule =
        SemanticFixtureRecordBuilder(QStringLiteral("outside"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(outsideFile)
            .withLocalHandle(87003)
            .withRange(1, 1, 4, 10)
            .record();
    const SemanticSymbolRecord packageRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("cfg_pkg"),
                                     SymbolTaxonomy::DeclarationKind::Package)
            .withFile(packageFile)
            .withLocalHandle(87004)
            .withRange(5, 1, 10, 11)
            .record();
    const SemanticSymbolRecord outsidePackage =
        SemanticFixtureRecordBuilder(QStringLiteral("outside_pkg"),
                                     SymbolTaxonomy::DeclarationKind::Package)
            .withFile(outsideFile)
            .withLocalHandle(87005)
            .withRange(10, 1, 14, 11)
            .record();
    QHash<QString, QString> contents;
    contents.insert(
        topFile,
        QStringLiteral("// 1\n"
                       "// 2\n"
                       "// 3\n"
                       "// 4\n"
                       "// 5\n"
                       "// 6\n"
                       "// 7\n"
                       "// 8\n"
                       "// 9\n"
                       "module top;\n"
                       "  logic a;\n"
                       "  assign y = a;\n"
                       "  logic b;\n"
                       "  logic c;\n"
                       "endmodule\n"));
    contents.insert(childFile,
                    QStringLiteral("module child;\nendmodule\n"));
    contents.insert(packageFile,
                    QStringLiteral("package cfg_pkg;\nendpackage\n"));
    const auto snapshot =
        snapshotFromRecords({topModule,
                             childModule,
                             outsideModule,
                             packageRecord,
                             outsidePackage},
                            {},
                            {},
                            contents);

    CommandLayerService service;
    CommandLayerPickerQuery pickerQuery;
    pickerQuery.snapshot = snapshot;
    pickerQuery.project = project.snapshot();
    const QList<CommandLayerPickerItem> moduleItems =
        service.moduleItems(pickerQuery);
    bool sawTop = false;
    bool sawChild = false;
    bool sawOutside = false;
    for (const CommandLayerPickerItem& item : moduleItems) {
        sawTop |= item.name == QStringLiteral("top");
        sawChild |= item.name == QStringLiteral("child");
        sawOutside |= item.name == QStringLiteral("outside");
    }
    expectBool("go module service stays workspace scoped",
               sawTop && sawChild && !sawOutside,
               true);
    pickerQuery.filter = QStringLiteral("chd");
    const QList<CommandLayerPickerItem> fuzzyModules =
        service.moduleItems(pickerQuery);
    expectBool("go module service keeps fuzzy picker matching",
               !fuzzyModules.isEmpty()
                   && fuzzyModules.first().name == QStringLiteral("child"),
               true);

    pickerQuery.filter.clear();
    const QList<CommandLayerPickerItem> packageItems =
        service.packageItems(pickerQuery);
    expectBool("go package service stays workspace scoped",
               packageItems.size() == 1
                   && packageItems.first().name
                          == QStringLiteral("cfg_pkg"),
               true);

    CommandLayerRelativeLineQuery lineTargetQuery;
    lineTargetQuery.snapshot = snapshot;
    lineTargetQuery.fileName = topFile;
    lineTargetQuery.currentModuleName = QStringLiteral("top");
    lineTargetQuery.currentLine = 12;
    lineTargetQuery.requestedModuleLine = 3;
    const CommandLayerRelativeLineResult lineTarget =
        service.relativeLineTarget(lineTargetQuery);
    expectBool("go number service resolves module-relative line",
               lineTarget.ok
                   && lineTarget.line == 12
                   && lineTarget.column == 1,
               true);
    lineTargetQuery.requestedModuleLine = 0;
    expectBool("go number service rejects zero",
               service.relativeLineTarget(lineTargetQuery).message
                   == QStringLiteral("Line number must be >= 1"),
               true);
    lineTargetQuery.requestedModuleLine = 99;
    expectBool("go number service rejects out-of-range line",
               service.relativeLineTarget(lineTargetQuery).message
                   == QStringLiteral("Module has only 5 lines"),
               true);

    QString registryError;
    expectBool("Command Layer registry is valid",
               commandLayerCommandRegistryIsValid(&registryError),
               true);
    int expectedCommandCount = 0;
    bool canonicalRegistryOk = true;
    for (const ActionDescriptor* descriptor :
         actionDescriptorsForSurface(
             ActionSurface::CommandLayer)) {
        const ActionAliasDescriptor* commandAlias =
            descriptor
            ? findActionAlias(
                  *descriptor,
                  ActionSurface::CommandLayer)
            : nullptr;
        if (!commandAlias || !commandAlias->triggerAdapter)
            continue;
        ++expectedCommandCount;
        const QString name = commandAlias->token;
        const CommandLayerCommandMetadata* command =
            findCommandLayerCommand(name);
        const QList<CommandLayerCommandMatch> exactMatches =
            commandLayerCommandMatches(name);
        canonicalRegistryOk =
            canonicalRegistryOk
            && command
            && command->actionId == descriptor->id
            && command->executionRoute
                   == descriptor->executionRoute
            && !command->description.isEmpty()
            && !exactMatches.isEmpty()
            && exactMatches.first().command.name == name
            && exactMatches.first().rank == CommandLayerMatchRank::Exact;
    }
    canonicalRegistryOk =
        canonicalRegistryOk
        && commandLayerCommandRegistry().size()
               == expectedCommandCount;
    expectBool("Command Layer exposes every Action Registry command",
               canonicalRegistryOk,
               true);

    const QList<QPair<QString, QString>> requiredAbbreviations = {
        {QStringLiteral("gm"), QStringLiteral("go module")},
        {QStringLiteral("gpk"), QStringLiteral("go package")},
        {QStringLiteral("gopack"), QStringLiteral("go package")},
        {QStringLiteral("goendm"), QStringLiteral("go endmodule")},
        {QStringLiteral("cr"), QStringLiteral("clear right")},
        {QStringLiteral("clearr"), QStringLiteral("clear right")},
        {QStringLiteral("sbe"), QStringLiteral("select begin end")},
        {QStringLiteral("selectbe"), QStringLiteral("select begin end")},
        {QStringLiteral("ss"), QStringLiteral("select signals")},
        {QStringLiteral("selectsig"), QStringLiteral("select signals")},
    };
    bool abbreviationMatchesOk = true;
    for (const auto& abbreviation : requiredAbbreviations) {
        const QList<CommandLayerCommandMatch> commandMatches =
            commandLayerCommandMatches(abbreviation.first);
        abbreviationMatchesOk =
            abbreviationMatchesOk
            && !commandMatches.isEmpty()
            && commandMatches.first().command.name == abbreviation.second;
    }
    expectBool("Command Layer supports required abbreviations",
               abbreviationMatchesOk,
               true);

    const QList<CommandLayerCommandMatch> exactRanking =
        commandLayerCommandMatches(QStringLiteral("GO PACKAGE"));
    const QList<CommandLayerCommandMatch> prefixRanking =
        commandLayerCommandMatches(QStringLiteral("go pack"));
    const QList<CommandLayerCommandMatch> wordRanking =
        commandLayerCommandMatches(QStringLiteral("gpk"));
    const QList<CommandLayerCommandMatch> subsequenceRanking =
        commandLayerCommandMatches(QStringLiteral("gdl"));
    expectBool("Command Layer ranking tiers are explainable",
               !exactRanking.isEmpty()
                   && exactRanking.first().rank
                          == CommandLayerMatchRank::Exact
                   && !prefixRanking.isEmpty()
                   && prefixRanking.first().rank
                          == CommandLayerMatchRank::Prefix
                   && !wordRanking.isEmpty()
                   && wordRanking.first().rank
                          == CommandLayerMatchRank::WordPrefix
                   && !subsequenceRanking.isEmpty()
                   && subsequenceRanking.first().rank
                          == CommandLayerMatchRank::Subsequence,
               true);
    const QList<CommandLayerCommandMatch> ambiguousGo =
        commandLayerCommandMatches(QStringLiteral("go"));
    expectBool("Command Layer preserves visible stable ambiguity",
               ambiguousGo.size() == 4
                   && ambiguousGo.at(0).command.name
                          == QStringLiteral("go <number>")
                   && ambiguousGo.at(1).command.name
                          == QStringLiteral("go module"),
               true);

    const QStringList retiredNames = {
        QStringLiteral("c"),
        QStringLiteral("ga"),
        QStringLiteral("ge"),
        QStringLiteral("gp"),
        QStringLiteral("gi"),
        QStringLiteral("gs"),
        QStringLiteral("gpa"),
        QStringLiteral("gsd"),
        QStringLiteral("gii"),
        QStringLiteral("gac"),
        QStringLiteral("cn"),
    };
    bool retiredNamesAbsent = true;
    for (const QString& name : retiredNames) {
        retiredNamesAbsent =
            retiredNamesAbsent && findCommandLayerCommand(name) == nullptr;
    }
    expectBool("retired commands and prefix records are absent",
               retiredNamesAbsent,
               true);

    const CommandLayerLineParseResult g100 =
        parseCommandLayerLineQuery(QStringLiteral("g100"));
    const CommandLayerLineParseResult go100 =
        parseCommandLayerLineQuery(QStringLiteral("Go100"));
    const CommandLayerLineParseResult goSpace100 =
        parseCommandLayerLineQuery(QStringLiteral("go 100"));
    const CommandLayerLineParseResult zeroLine =
        parseCommandLayerLineQuery(QStringLiteral("go 0"));
    const CommandLayerLineParseResult negativeLine =
        parseCommandLayerLineQuery(QStringLiteral("g-1"));
    expectBool("go number parser accepts all three forms",
               g100.state == CommandLayerLineParseState::Valid
                   && g100.line == 100
                   && go100.state == CommandLayerLineParseState::Valid
                   && go100.line == 100
                   && goSpace100.state
                          == CommandLayerLineParseState::Valid
                   && goSpace100.line == 100,
               true);
    expectBool("go number parser reports invalid and zero lines",
               zeroLine.state == CommandLayerLineParseState::Invalid
                   && zeroLine.failureReason
                          == QStringLiteral("Line number must be >= 1")
                   && negativeLine.state
                          == CommandLayerLineParseState::Invalid,
               true);

    CommandLayerCoordinator* coordinator =
        window.commandLayerCoordinator.get();
    MyCodeEditor* editor =
        window.tabManager ? window.tabManager->getCurrentEditor() : nullptr;
    expectBool("Command Layer coordinator and active editor exist",
               coordinator && editor,
               true);
    if (!coordinator || !editor)
        return;

    const auto sendKeyEvent =
        [](QWidget* target,
           QEvent::Type type,
           int key,
           Qt::KeyboardModifiers modifiers = Qt::NoModifier,
           const QString& text = QString(),
           bool autoRepeat = false) {
        if (!target)
            return;
        QKeyEvent event(type,
                        key,
                        modifiers,
                        text,
                        autoRepeat,
                        autoRepeat ? 2 : 1);
        QApplication::sendEvent(target, &event);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    };
    const auto typeQuery =
        [&](QWidget* target, const QString& text) {
        for (const QChar ch : text) {
            int key = 0;
            if (ch.isLetter()) {
                key = Qt::Key_A
                    + ch.toLower().unicode() - QLatin1Char('a').unicode();
            } else if (ch.isDigit()) {
                key = Qt::Key_0
                    + ch.unicode() - QLatin1Char('0').unicode();
            } else if (ch == QLatin1Char(' ')) {
                key = Qt::Key_Space;
            } else if (ch == QLatin1Char('-')) {
                key = Qt::Key_Minus;
            }
            if (key != 0)
                sendKeyEvent(target, QEvent::KeyPress, key);
        }
    };
    const auto pressF24 = [&](QWidget* target) {
        sendKeyEvent(target, QEvent::KeyPress, Qt::Key_F24);
    };
    const auto releaseF24 = [&](QWidget* target) {
        sendKeyEvent(target, QEvent::KeyRelease, Qt::Key_F24);
    };

    editor->cancelFoldRegionMarkMode();
    editor->cancelFoldShelfMode();
    if (QCompleter* completer = editor->findChild<QCompleter*>())
        completer->popup()->hide();
    editor->setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);

    pressF24(editor);
    CommandLayerPanel* commandPanel = coordinator->panelWidget();
    expectBool("F24 press enters application Command Layer with every registered command",
               coordinator->isActive()
                   && coordinator->isF24Held()
                   && commandPanel
                   && commandPanel->isVisible()
                   && commandPanel->candidateListWidget()->count()
                          == commandLayerCommandRegistry().size(),
               true);
    typeQuery(editor, QStringLiteral("gm"));
    sendKeyEvent(editor,
                 QEvent::KeyPress,
                 Qt::Key_F24,
                 Qt::NoModifier,
                 QString(),
                 true);
    sendKeyEvent(editor,
                 QEvent::KeyRelease,
                 Qt::Key_F24,
                 Qt::NoModifier,
                 QString(),
                 true);
    expectBool("F24 auto-repeat press and release are ignored",
               coordinator->isActive()
                   && coordinator->isF24Held()
                   && coordinator->query() == QStringLiteral("gm"),
               true);
    releaseF24(editor);
    expectBool("F24 release exits unfinished search",
               !coordinator->isActive()
                   && !coordinator->isF24Held()
                   && !commandPanel->isVisible(),
               true);

    pressF24(editor);
    sendKeyEvent(editor,
                 QEvent::KeyPress,
                 Qt::Key_unknown,
                 Qt::NoModifier,
                 QStringLiteral("goendm"));
    expectBool("F24 search consumes event text without losing fast input",
               coordinator->query() == QStringLiteral("goendm"),
               true);
    releaseF24(editor);

    editor->setPlainText(QStringLiteral("alpha beta gamma"));
    QTextCursor directDelete(editor->document());
    directDelete.setPosition(6);
    directDelete.setPosition(10, QTextCursor::KeepAnchor);
    editor->setTextCursor(directDelete);
    pressF24(editor);
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_D,
                 Qt::NoModifier, QStringLiteral("d"));
    expectBool("F24+D waits for D release",
               editor->toPlainText()
                   == QStringLiteral("alpha beta gamma"),
               true);
    sendKeyEvent(editor, QEvent::KeyRelease, Qt::Key_D,
                 Qt::NoModifier, QStringLiteral("d"));
    expectBool("F24+D release deletes one selection",
               coordinator->isActive()
                   && editor->toPlainText()
                          == QStringLiteral("alpha  gamma"),
               true);
    releaseF24(editor);

    QTextCursor repeatedDelete(editor->document());
    repeatedDelete.setPosition(7);
    repeatedDelete.setPosition(12, QTextCursor::KeepAnchor);
    editor->setTextCursor(repeatedDelete);
    pressF24(editor);
    releaseF24(editor);
    expectBool("empty F24 tap repeats the last executable action",
               editor->toPlainText() == QStringLiteral("alpha  "),
               true);

    editor->setPlainText(QStringLiteral("keep this"));
    QTextCursor cancelledDelete(editor->document());
    cancelledDelete.setPosition(5);
    cancelledDelete.setPosition(9, QTextCursor::KeepAnchor);
    editor->setTextCursor(cancelledDelete);
    pressF24(editor);
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_D,
                 Qt::NoModifier, QStringLiteral("d"));
    releaseF24(editor);
    sendKeyEvent(editor, QEvent::KeyRelease, Qt::Key_D,
                 Qt::NoModifier, QStringLiteral("d"));
    expectBool("releasing F24 before direct key cancels the gesture",
               editor->toPlainText() == QStringLiteral("keep this"),
               true);

    pressF24(editor);
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_D,
                 Qt::NoModifier, QStringLiteral("d"));
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Escape);
    sendKeyEvent(editor, QEvent::KeyRelease, Qt::Key_D,
                 Qt::NoModifier, QStringLiteral("d"));
    expectBool("Escape cancels a pending F24 direct gesture",
               editor->toPlainText() == QStringLiteral("keep this"),
               true);
    releaseF24(editor);

    pressF24(editor);
    QMouseEvent commandLayerMousePress(
        QEvent::MouseButtonPress,
        QPointF(2.0, 2.0),
        QPointF(2.0, 2.0),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(commandPanel,
                                &commandLayerMousePress);
    // Complete the gesture: an ignored press can reach a main-window dock
    // separator, whose pending resize otherwise freezes subsequent layouts.
    QMouseEvent commandLayerMouseRelease(
        QEvent::MouseButtonRelease, QPointF(2.0, 2.0), QPointF(2.0, 2.0),
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(commandPanel, &commandLayerMouseRelease);
    releaseF24(editor);
    expectBool("mouse input cancels empty F24 tap repeat",
               editor->toPlainText() == QStringLiteral("keep this"),
               true);

    pressF24(editor);
    sendKeyEvent(editor,
                 QEvent::KeyPress,
                 Qt::Key_G,
                 Qt::ControlModifier
                     | Qt::ShiftModifier
                     | Qt::AltModifier,
                 QString());
    expectBool("Command Layer query uses key codes with modifiers",
               coordinator->query() == QStringLiteral("g"),
               true);
    releaseF24(editor);

    const QString commandLineFile = editor->documentFileName();
    const QString commandLineText =
        QStringLiteral("module command_line;\n"
                       "  logic a;\n"
                       "  logic b;\n"
                       "  assign b = a;\n"
                       "endmodule\n");
    expectBool("go number command has current file identity",
               !commandLineFile.isEmpty(),
               true);
    if (!commandLineFile.isEmpty()) {
        const SemanticSymbolRecord commandLineModule =
            SemanticFixtureRecordBuilder(
                QStringLiteral("command_line"),
                SymbolTaxonomy::DeclarationKind::Module)
                .withFile(commandLineFile)
                .withLocalHandle(87006)
                .withRange(1, 1, 6, 1)
                .record();
        SemanticIndex commandLineSemanticIndex;
        commandLineSemanticIndex.setSnapshot(
            snapshotFromRecords(
                {commandLineModule},
                {},
                {},
                {{commandLineFile, commandLineText}}));
        SemanticIndex* previousCoordinatorIndex = coordinator->semanticIndex;
        coordinator->semanticIndex = &commandLineSemanticIndex;
        editor->setPlainText(commandLineText);
        QTextCursor commandLineCursor(editor->document());
        commandLineCursor.setPosition(0);
        editor->setTextCursor(commandLineCursor);
        pressF24(editor);
        typeQuery(editor, QStringLiteral("go 3"));
        sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Return);
        expectBool("go number command navigates module-relative line",
                   coordinator->isActive()
                       && coordinator->query().isEmpty()
                       && editor->textCursor().blockNumber() == 2,
                   true);
        releaseF24(editor);
        coordinator->semanticIndex = previousCoordinatorIndex;
    }

    editor->setPlainText(QString());
    sendWidgetKey(editor, Qt::Key_QuoteLeft, QStringLiteral("`"));
    expectBool("ordinary backtick remains SystemVerilog input",
               editor->toPlainText() == QStringLiteral("`")
                   && !coordinator->isActive(),
               true);

    pressF24(editor);
    typeQuery(editor, QStringLiteral("go"));
    QListWidget* candidates = commandPanel->candidateListWidget();
    const int initialRow = candidates->currentRow();
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Down);
    const int downRow = candidates->currentRow();
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Up);
    expectBool("ambiguous candidates allow up and down selection",
               candidates->count() == 4
                   && initialRow == 0
                   && downRow == 1
                   && candidates->currentRow() == 0,
               true);
    releaseF24(editor);

    editor->setPlainText(QStringLiteral(
        "module lifecycle;\n"
        "  logic a;\n"
        "endmodule\n"));
    QTextCursor lifecycleCursor = editor->textCursor();
    lifecycleCursor.setPosition(
        editor->toPlainText().indexOf(QStringLiteral("logic")));
    editor->setTextCursor(lifecycleCursor);
    const QString lifecycleText = editor->toPlainText();
    pressF24(editor);
    typeQuery(editor, QStringLiteral("goendm"));
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Return);
    expectBool("command completion clears query and continues while F24 held",
               coordinator->isActive()
                   && coordinator->isF24Held()
                   && coordinator->query().isEmpty()
                   && editor->toPlainText() == lifecycleText
                   && editor->textCursor().position()
                      == lifecycleText.indexOf(
                          QStringLiteral("endmodule")),
               true);
    typeQuery(editor, QStringLiteral("help"));
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Return);
    bool helpHasInlineSemanticAlias = false;
    bool helpHasVisibleSymbolAlias = false;
    bool helpHasInlineTemplateAlias = false;
    bool helpHasGlobalControlAlias = false;
    bool helpHasExposeAction = false;
    for (int row = 0;
         row < commandPanel->candidateListWidget()->count();
         ++row) {
        const QString text =
            commandPanel->candidateListWidget()->item(row)->text();
        helpHasInlineSemanticAlias =
            helpHasInlineSemanticAlias
            || text.contains(
                QStringLiteral(
                    "Inline semantic command: ;l"));
        helpHasVisibleSymbolAlias =
            helpHasVisibleSymbolAlias
            || text.contains(
                QStringLiteral(
                    "Inline semantic command: ;v"));
        helpHasInlineTemplateAlias =
            helpHasInlineTemplateAlias
            || text.contains(
                QStringLiteral(
                    "Inline template command: ;;l"));
        helpHasGlobalControlAlias =
            helpHasGlobalControlAlias
            || text.contains(
                QStringLiteral(
                    "Global Control: ow s save"));
        helpHasExposeAction =
            helpHasExposeAction
            || text.contains(
                QStringLiteral("Expose Signal to Top"));
    }
    expectBool("help renders the unified action catalog",
               coordinator->isActive()
                   && commandPanel->isVisible()
                   && commandPanel->candidateListWidget()->count()
                          == actionRegistry().size()
                   && helpHasInlineSemanticAlias
                   && helpHasVisibleSymbolAlias
                   && helpHasInlineTemplateAlias
                   && helpHasGlobalControlAlias
                   && helpHasExposeAction,
               true);
    typeQuery(editor, QStringLiteral("g"));
    expectBool("help returns to search while F24 remains held",
               coordinator->isActive()
                   && coordinator->query() == QStringLiteral("g"),
               true);
    releaseF24(editor);

    pressF24(editor);
    typeQuery(editor, QStringLiteral("zz"));
    expectBool("Command Layer displays no-match failure reason",
               commandPanel->failureLabelWidget()->isVisible()
                   && commandPanel->failureLabelWidget()->text()
                          .contains(QStringLiteral("No command matches")),
               true);
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Escape);
    typeQuery(editor, QStringLiteral("g0"));
    expectBool("Command Layer displays invalid line reason",
               commandPanel->failureLabelWidget()->isVisible()
                   && commandPanel->failureLabelWidget()->text()
                          == QStringLiteral("Line number must be >= 1"),
               true);
    releaseF24(editor);

    pressF24(editor);
    typeQuery(editor, QStringLiteral("select signals"));
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Return);
    expectBool("select signals executes through Command Layer",
               coordinator->isActive()
                   && coordinator->query().isEmpty()
                   && editor->signalSelectionModeActiveForTest(),
               true);
    releaseF24(editor);
    sendWidgetKey(editor, Qt::Key_Escape);
    expectBool("select signals Esc returns to normal editor mode",
               !editor->signalSelectionModeActiveForTest(),
               true);

    const QString selectClearInput =
        QStringLiteral("module select_clear;\n"
                       "  always_comb begin\n"
                       "    a <= foo;\n"
                       "    b = bar;\n"
                       "  end\n"
                       "endmodule\n");
    editor->setPlainText(selectClearInput);
    QTextCursor selectCursor = editor->textCursor();
    selectCursor.setPosition(
        selectClearInput.indexOf(QStringLiteral("foo")));
    editor->setTextCursor(selectCursor);
    pressF24(editor);
    typeQuery(editor, QStringLiteral("sbe"));
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Return);
    const bool selectedInterior =
        editor->textCursor().hasSelection()
        && editor->textCursor().selectedText().contains(
            QStringLiteral("a <= foo"));
    typeQuery(editor, QStringLiteral("cr"));
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Return);
    expectBool("select begin end then clear right executes continuously",
               selectedInterior
                   && coordinator->isActive()
                   && editor->templateSlotModeActive()
                   && editor->templateSlotModeSlotCount() == 2
                   && editor->toPlainText().contains(
                       QStringLiteral("a <= ;"))
                   && editor->toPlainText().contains(
                       QStringLiteral("b = ;")),
               true);
    releaseF24(editor);
    sendWidgetKey(editor, Qt::Key_Escape);

    editor->setPlainText(QStringLiteral(
        "module clear_failure;\nendmodule\n"));
    pressF24(editor);
    typeQuery(editor, QStringLiteral("cr"));
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Return);
    expectBool("editing failure remains visible without leaving held layer",
               coordinator->isActive()
                   && coordinator->query().isEmpty()
                   && commandPanel->failureLabelWidget()->isVisible()
                   && commandPanel->failureLabelWidget()->text()
                          .contains(QStringLiteral("No assignment RHS")),
               true);
    releaseF24(editor);

    pressF24(editor);
    typeQuery(editor, QStringLiteral("gm"));
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Return);
    CommandLayerPickerPanel* modulePicker = coordinator->pickerPanel();
    expectBool("go module opens secondary picker",
               coordinator->isActive()
                   && coordinator->isF24Held()
                   && modulePicker
                   && modulePicker->isVisible()
                   && !commandPanel->isVisible(),
               true);
    releaseF24(modulePicker);
    expectBool("F24 release does not cancel open module picker",
               coordinator->isActive()
                   && !coordinator->isF24Held()
                   && modulePicker->isVisible(),
               true);
    sendWidgetKey(modulePicker, Qt::Key_Escape);
    expectBool("module picker completion returns to editor after release",
               !coordinator->isActive()
                   && !modulePicker->isVisible(),
               true);

    pressF24(editor);
    typeQuery(editor, QStringLiteral("gm"));
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Return);
    CommandLayerPickerItem activatedModule;
    activatedModule.kind = CommandLayerPickerItemKind::Module;
    activatedModule.name = QStringLiteral("lifecycle");
    coordinator->activatePickerItem(activatedModule);
    expectBool("module picker activation returns to search while F24 is held",
               coordinator->isActive()
                   && coordinator->isF24Held()
                   && commandPanel->isVisible()
                   && coordinator->query().isEmpty(),
               true);
    releaseF24(editor);

    pressF24(editor);
    typeQuery(editor, QStringLiteral("gpk"));
    sendKeyEvent(editor, QEvent::KeyPress, Qt::Key_Return);
    expectBool("go package opens secondary picker",
               coordinator->isActive()
                   && coordinator->pickerPanel()->isVisible(),
               true);
    releaseF24(coordinator->pickerPanel());
    expectBool("F24 release does not cancel open package picker",
               coordinator->isActive()
                   && coordinator->pickerPanel()->isVisible(),
               true);
    sendWidgetKey(coordinator->pickerPanel(), Qt::Key_Escape);

    QLineEdit offEditorFocus(&window);
    offEditorFocus.setObjectName(
        QStringLiteral("commandLayerOffEditorFocus"));
    offEditorFocus.setGeometry(10, 10, 100, 24);
    offEditorFocus.show();
    offEditorFocus.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    pressF24(&offEditorFocus);
    typeQuery(&offEditorFocus, QStringLiteral("help"));
    expectBool("application-level input survives focus changes",
               coordinator->isActive()
                   && coordinator->query() == QStringLiteral("help"),
               true);
    releaseF24(&offEditorFocus);
    offEditorFocus.hide();

    QTabWidget* tabs =
        window.findChild<QTabWidget*>(QStringLiteral("tabWidget"));
    if (tabs && window.tabManager) {
        const int originalIndex = tabs->currentIndex();
        const int previousCount = window.tabManager->editorCount();
        window.tabManager->createNewTab();
        const int newIndex = tabs->currentIndex();
        MyCodeEditor* newEditor = window.tabManager->getCurrentEditor();
        tabs->setCurrentIndex(originalIndex);
        MyCodeEditor* originalEditor =
            window.tabManager->getCurrentEditor();
        if (originalEditor)
            originalEditor->setFocus();
        pressF24(originalEditor);
        typeQuery(originalEditor, QStringLiteral("g"));
        tabs->setCurrentIndex(newIndex);
        if (newEditor)
            newEditor->setFocus();
        typeQuery(newEditor, QStringLiteral("m"));
        expectBool("Command Layer survives tab switching",
                   previousCount + 1 == window.tabManager->editorCount()
                       && coordinator->isActive()
                       && coordinator->query() == QStringLiteral("gm")
                       && window.tabManager->getCurrentEditor() == newEditor,
                   true);
        releaseF24(newEditor);
        window.tabManager->closeTab(newIndex);
        if (tabs->count() > 0)
            tabs->setCurrentIndex(qBound(0,
                                         originalIndex,
                                         tabs->count() - 1));
        editor = window.tabManager->getCurrentEditor();
    }

    if (editor) {
        editor->setPlainText(QStringLiteral("0010\n9999\n"));
        editor->setFocus();
        const QTextBlock first =
            editor->document()->findBlockByNumber(0);
        const QTextBlock second =
            editor->document()->findBlockByNumber(1);
        QTextCursor start(first);
        start.setPosition(first.position());
        QTextCursor end(second);
        end.setPosition(second.position() + 4);
        editor->setTextCursor(start);
        QTest::mouseClick(editor->viewport(),
                          Qt::LeftButton,
                          Qt::ShiftModifier | Qt::AltModifier,
                          editor->cursorRect(end).center());
        expectBool("column selection fixture is active",
                   editor->columnSelectionActive(),
                   true);
        QTest::keyClick(editor, Qt::Key_C, Qt::AltModifier);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        QWidget* numberTool =
            window.findChild<QWidget*>(
                QStringLiteral("columnNumberToolPanel"));
        expectBool("Alt+C still opens Column Number Tool",
                   numberTool
                       && numberTool->isVisible()
                       && !coordinator->isActive(),
                   true);
        expectBool("Column Number Tool has no preview-only widget",
                   numberTool
                       && !numberTool->findChild<QWidget*>(
                           QStringLiteral("columnNumberPreview")),
                   true);
        QSpinBox* startValue = numberTool
            ? numberTool->findChild<QSpinBox*>(
                  QStringLiteral("columnNumberStart"))
            : nullptr;
        QSpinBox* stepValue = numberTool
            ? numberTool->findChild<QSpinBox*>(
                  QStringLiteral("columnNumberStep"))
            : nullptr;
        QSpinBox* repeatValue = numberTool
            ? numberTool->findChild<QSpinBox*>(
                  QStringLiteral("columnNumberRepeat"))
            : nullptr;
        QRadioButton* octalBase = numberTool
            ? numberTool->findChild<QRadioButton*>(
                  QStringLiteral("columnNumberBaseOctal"))
            : nullptr;
        if (startValue)
            startValue->setValue(7);
        if (stepValue)
            stepValue->setValue(3);
        if (repeatValue)
            repeatValue->setValue(2);
        if (octalBase)
            octalBase->setChecked(true);
        expectBool("Column Number Tool exposes compact remembered controls",
                   startValue && stepValue && repeatValue && octalBase,
                   true);
        sendWidgetKey(numberTool, Qt::Key_Return);

        sendWidgetKey(editor, Qt::Key_Escape);
        editor->setPlainText(QStringLiteral("0010\n9999\n"));
        const QTextBlock reopenedFirst =
            editor->document()->findBlockByNumber(0);
        const QTextBlock reopenedSecond =
            editor->document()->findBlockByNumber(1);
        QTextCursor reopenedStart(reopenedFirst);
        reopenedStart.setPosition(reopenedFirst.position());
        QTextCursor reopenedEnd(reopenedSecond);
        reopenedEnd.setPosition(reopenedSecond.position() + 4);
        editor->setTextCursor(reopenedStart);
        QTest::mouseClick(editor->viewport(),
                          Qt::LeftButton,
                          Qt::ShiftModifier | Qt::AltModifier,
                          editor->cursorRect(reopenedEnd).center());
        QTest::keyClick(editor, Qt::Key_C, Qt::AltModifier);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        expectBool("Column Number Tool restores the last applied start",
                   startValue && startValue->value() == 7,
                   true);
        expectBool("Column Number Tool restores the last applied step",
                   stepValue && stepValue->value() == 3,
                   true);
        expectBool("Column Number Tool restores the last applied repeat",
                   repeatValue && repeatValue->value() == 2,
                   true);
        expectBool("Column Number Tool restores the last applied base",
                   octalBase && octalBase->isChecked(),
                   true);
        sendWidgetKey(numberTool, Qt::Key_Escape);
        sendWidgetKey(editor, Qt::Key_Escape);
    }
}

static void runGlobalControlRegression(MainWindow& window,
                                       NavigationWidget* navWidget)
{
    printf("\n-- global control regression --\n");

    GlobalControlService service;
    const QList<GlobalControlItem> rootMatches =
        service.query(QString());
    QSet<QString> rootDomainIds;
    QSet<QString> rootCommandIds;
    for (const GlobalControlItem& item : rootMatches) {
        if (item.kind == GlobalControlItemKind::Domain)
            rootDomainIds.insert(item.id);
        if (item.kind == GlobalControlItemKind::Command)
            rootCommandIds.insert(item.id);
    }
    expectBool("global control root shows command domains",
               rootDomainIds == QSet<QString>({
                   QStringLiteral("ow"),
                   QStringLiteral("fd"),
               }),
               true);
    expectBool("global control root hides direct commands",
               rootCommandIds.isEmpty(),
               true);

    const QList<GlobalControlItem> workspaceMatches =
        service.query(QStringLiteral("ow"));
    bool foundOpenOneWorkspaceAction = false;
    bool foundOpenTwoWorkspacesAction = false;
    bool foundRecentWorkspaceAction = false;
    bool foundSessionWorkspaceAction = false;
    bool foundDeprecatedWorkspaceAction = false;
    for (const GlobalControlItem& item : workspaceMatches) {
        if (item.id == QStringLiteral("ow 1")
            && item.kind == GlobalControlItemKind::Command)
            foundOpenOneWorkspaceAction = true;
        if (item.id == QStringLiteral("ow 2")
            && item.kind == GlobalControlItemKind::Command)
            foundOpenTwoWorkspacesAction = true;
        if (item.id == QStringLiteral("ow r")
            && item.kind == GlobalControlItemKind::Command
            && item.subtitle.contains(QStringLiteral("Recent Workspaces")))
            foundRecentWorkspaceAction = true;
        if (item.id == QStringLiteral("ow s")
            && item.kind == GlobalControlItemKind::Domain
            && item.subtitle.contains(
                QStringLiteral("Local Workspace Session"))
            && item.subtitle.contains(
                QStringLiteral(".zeroslack/project.json")))
            foundSessionWorkspaceAction = true;
        if (item.id == QStringLiteral("ow"))
            foundDeprecatedWorkspaceAction = true;
    }
    expectBool("global control ow domain shows ow 1",
               foundOpenOneWorkspaceAction,
               true);
    expectBool("global control ow domain shows ow 2",
               foundOpenTwoWorkspacesAction,
               true);
    expectBool("global control ow domain shows ow r",
               foundRecentWorkspaceAction,
               true);
    expectBool("global control ow domain shows ow s",
               foundSessionWorkspaceAction,
               true);
    expectBool("global control ow domain hides deprecated commands",
               foundDeprecatedWorkspaceAction,
               false);

    const QList<GlobalControlItem> workspaceCountMatches =
        service.query(QStringLiteral("ow 3"));
    bool foundOpenThreeWorkspacesAction = false;
    for (const GlobalControlItem& item : workspaceCountMatches) {
        if (item.id == QStringLiteral("ow 3")
            && item.kind == GlobalControlItemKind::Command
            && item.subtitle.contains(QStringLiteral("Open 3 workspaces"))) {
            foundOpenThreeWorkspacesAction = true;
        }
    }
    expectBool("global control ow count command is parameterized",
               foundOpenThreeWorkspacesAction,
               true);

    const QList<GlobalControlItem> recentWorkspaceMatches =
        service.query(QStringLiteral("ow r"));
    bool foundRecentByExactCommand = false;
    bool foundWorkspaceCountHint = false;
    for (const GlobalControlItem& item : recentWorkspaceMatches) {
        if (item.id == QStringLiteral("ow r"))
            foundRecentByExactCommand = true;
        if (item.kind == GlobalControlItemKind::Domain
            && item.title == QStringLiteral("ow <num>"))
            foundWorkspaceCountHint = true;
    }
    expectBool("global control ow r query finds recent command",
               foundRecentByExactCommand,
               true);
    expectBool("global control ow r query hides count hint",
               foundWorkspaceCountHint,
               false);

    const QList<GlobalControlItem> sessionWorkspaceMatches =
        service.query(QStringLiteral("ow s"));
    bool foundSessionSave = false;
    bool foundSessionRestore = false;
    bool foundSessionClean = false;
    for (const GlobalControlItem& item : sessionWorkspaceMatches) {
        if (item.id == QStringLiteral("ow s save")
            && item.subtitle.contains(QStringLiteral("local AppData"))
            && item.subtitle.contains(
                QStringLiteral("project configuration is unchanged")))
            foundSessionSave = true;
        if (item.id == QStringLiteral("ow s restore")
            && item.subtitle.contains(QStringLiteral("local tabs"))
            && item.subtitle.contains(QStringLiteral("read-only")))
            foundSessionRestore = true;
        if (item.id == QStringLiteral("ow s clean")
            && item.subtitle.contains(
                QStringLiteral("local UI/session partition"))
            && item.subtitle.contains(QStringLiteral("project.json")))
            foundSessionClean = true;
    }
    expectBool("global control ow s query finds session commands",
               foundSessionSave && foundSessionRestore && foundSessionClean,
               true);
    expectBool("global control ow s w resolves save",
               !service.query(QStringLiteral("ow s w"))
                    .isEmpty()
                   && service.query(QStringLiteral("ow s w"))
                          .first()
                          .id == QStringLiteral("ow s save"),
               true);
    expectBool("global control ow s r resolves restore",
               !service.query(QStringLiteral("ow s r"))
                    .isEmpty()
                   && service.query(QStringLiteral("ow s r"))
                          .first()
                          .id == QStringLiteral("ow s restore"),
               true);
    expectBool("global control ow s c resolves clean",
               !service.query(QStringLiteral("ow s c"))
                    .isEmpty()
                   && service.query(QStringLiteral("ow s c"))
                          .first()
                          .id == QStringLiteral("ow s clean"),
               true);

    const QList<GlobalControlItem> foldActionMatches =
        service.query(QStringLiteral("fd"));
    bool foundFoldRegionAction = false;
    bool foundFoldShelfAction = false;
    bool foldActionsExplainBehavior = false;
    bool foundDeprecatedFoldAction = false;
    for (const GlobalControlItem& item : foldActionMatches) {
        if (item.id == QStringLiteral("fd r")) {
            foundFoldRegionAction = item.title == QStringLiteral("fd r")
                && item.subtitle.contains(QStringLiteral("Fold Region"));
        }
        if (item.id == QStringLiteral("fd s")) {
            foundFoldShelfAction = item.title == QStringLiteral("fd s")
                && item.subtitle.contains(QStringLiteral("Fold Shelf"));
        }
        if (item.id == QStringLiteral("fd")
            || item.id == QStringLiteral("fds"))
            foundDeprecatedFoldAction = true;
    }
    foldActionsExplainBehavior = foundFoldRegionAction && foundFoldShelfAction;
    expectBool("global control fd domain finds fold subcommands",
               foundFoldRegionAction && foundFoldShelfAction,
               true);
    expectBool("global control fold actions include explanations",
               foldActionsExplainBehavior,
               true);
    expectBool("global control fd domain hides deprecated commands",
               foundDeprecatedFoldAction,
               false);

    expectBool("global control omits workspace files",
               service.query(QStringLiteral("SVH_interface")).isEmpty(),
               true);

    bool dispatched = false;
    GlobalControlCoordinator dispatcherProbe(&window);
    dispatcherProbe.setActionHandler([&](const GlobalControlItem& item) {
        dispatched = item.id == QStringLiteral("ow 1");
    });
    dispatcherProbe.dispatch(
        GlobalControlItem{GlobalControlItemKind::Command,
                          QStringLiteral("ow 1"),
                          QStringLiteral("ow 1"),
                          QStringLiteral("Open 1 workspace")});
    expectBool("global control dispatches command",
               dispatched,
               true);

    window.globalControlCoordinator->dispatch(
        GlobalControlItem{GlobalControlItemKind::Command,
                          QStringLiteral("ow r"),
                          QStringLiteral("ow r"),
                          QStringLiteral("Recent Workspaces")});
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    QDialog* recentDialog =
        window.findChild<QDialog*>(QStringLiteral("recentWorkspacesDialog"));
    QTreeWidget* recentTree = recentDialog
        ? recentDialog->findChild<QTreeWidget*>(
              QStringLiteral("recentWorkspacesTree"))
        : nullptr;
    const QString activeAlias =
        window.workspaceManager ? window.workspaceManager->getWorkspaceAlias()
                                : QString();
    const QString activePath =
        window.workspaceManager ? window.workspaceManager->getWorkspacePath()
                                : QString();
    bool recentWindowShowsActiveWorkspace = false;
    QTreeWidgetItem* activeRecentItem = nullptr;
    if (recentTree) {
        for (int i = 0; i < recentTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = recentTree->topLevelItem(i);
            if (item
                && item->text(0) == activeAlias
                && item->data(0, Qt::UserRole).toString() == activePath) {
                recentWindowShowsActiveWorkspace = true;
                activeRecentItem = item;
            }
        }
        if (activeRecentItem)
            recentTree->setCurrentItem(activeRecentItem);
    }
    expectBool("ow r opens recent workspace window",
               recentDialog && recentDialog->isVisible() && recentTree,
               true);
    expectBool("ow r window lists alias and path",
               recentWindowShowsActiveWorkspace,
               true);
    QPushButton* recentRemoveButton = recentDialog
        ? recentDialog->findChild<QPushButton*>(
              QStringLiteral("recentWorkspacesRemoveButton"))
        : nullptr;
    expectBool("ow r exposes remove-from-recent action",
               recentRemoveButton && recentRemoveButton->isEnabled(),
               true);
    if (recentRemoveButton) {
        QTest::mouseClick(recentRemoveButton, Qt::LeftButton);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    bool activeWorkspaceStillRecent = false;
    if (window.workspaceManager) {
        for (const WorkspaceManager::WorkspaceEntry& entry :
             window.workspaceManager->recentWorkspaceEntries()) {
            activeWorkspaceStillRecent = activeWorkspaceStillRecent
                || entry.path == activePath;
        }
    }
    expectBool("ow r removal preserves workspace files",
               !activeWorkspaceStillRecent
                   && QDir(activePath).exists()
                   && window.workspaceManager
                   && window.workspaceManager->getWorkspacePath()
                          == activePath,
               true);
    if (recentDialog)
        recentDialog->close();

    QWidget* focusTarget = navWidget ? static_cast<QWidget*>(navWidget) : &window;
    focusTarget->setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QKeyEvent ctrlSpace(QEvent::KeyPress,
                        Qt::Key_Space,
                        Qt::ControlModifier);
    qApp->notify(focusTarget, &ctrlSpace);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    expectBool("Ctrl+Space opens global control outside editor",
               window.globalControlCoordinator
                   && window.globalControlCoordinator->panel
                   && window.globalControlCoordinator->panel->isVisible(),
               true);

    if (window.globalControlCoordinator
        && window.globalControlCoordinator->panel) {
        bool panelShowsWorkspaceDomain = false;
        bool panelShowsFoldDomain = false;
        bool panelShowsRootCommand = false;
        for (const GlobalControlItem& item :
             window.globalControlCoordinator->panel->currentItems) {
            if (item.kind == GlobalControlItemKind::Domain
                && item.id == QStringLiteral("ow"))
                panelShowsWorkspaceDomain = true;
            if (item.kind == GlobalControlItemKind::Domain
                && item.id == QStringLiteral("fd"))
                panelShowsFoldDomain = true;
            if (item.kind == GlobalControlItemKind::Command)
                panelShowsRootCommand = true;
        }
        expectBool("Ctrl+Space command category combines domains and actions",
                   panelShowsWorkspaceDomain && panelShowsFoldDomain
                       && panelShowsRootCommand
                       && window.globalControlCoordinator->panel->category()
                              == GlobalControlCategory::Commands,
                   true);

        window.globalControlCoordinator->panel->searchEdit->setText(
            QStringLiteral("fd"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        bool panelShowsFoldRegion = false;
        bool panelShowsFoldShelf = false;
        for (const GlobalControlItem& item :
             window.globalControlCoordinator->panel->currentItems) {
            if (item.id == QStringLiteral("fd r")
                && item.subtitle.contains(QStringLiteral("Fold Region"))) {
                panelShowsFoldRegion = true;
            }
            if (item.id == QStringLiteral("fd s")
                && item.subtitle.contains(QStringLiteral("Fold Shelf"))) {
                panelShowsFoldShelf = true;
            }
        }
        expectBool("global control fd query shows fd r and fd s",
                   panelShowsFoldRegion && panelShowsFoldShelf,
                   true);
        window.globalControlCoordinator->panel->hide();
    }

    MyCodeEditor* activeEditor = window.tabManager
        ? window.tabManager->getCurrentEditor()
        : nullptr;
    if (activeEditor)
        activeEditor->cancelFoldShelfMode();
    window.globalControlCoordinator->dispatch(
        GlobalControlItem{GlobalControlItemKind::Command,
                          QStringLiteral("fd r"),
                          QStringLiteral("fd r"),
                          QStringLiteral("Fold Region")});
    expectBool("global control fd r starts fold region mode",
               activeEditor && activeEditor->foldRegionMarkModeActive(),
               true);
    if (activeEditor)
        activeEditor->cancelFoldRegionMarkMode();

    window.globalControlCoordinator->dispatch(
        GlobalControlItem{GlobalControlItemKind::Command,
                          QStringLiteral("fd s"),
                          QStringLiteral("fd s"),
                          QStringLiteral("Fold Shelf")});
    QDockWidget* foldShelfDock =
        window.findChild<QDockWidget*>(QStringLiteral("FoldShelfDock"));
    expectBool("global control fd s starts fold shelf mode",
               activeEditor
                   && activeEditor->foldShelfModeActive()
                   && foldShelfDock
                   && window.panelLayoutController
                   && window.panelLayoutController->isPanelOpen(
                          QStringLiteral("foldShelf")),
               true);
    if (activeEditor)
        activeEditor->cancelFoldShelfMode();
    if (foldShelfDock)
        foldShelfDock->hide();
}

static void runStructuralEditingRegression()
{
    const auto proceduralPreviousSnapshot =
        SemanticIndex::getInstance()->snapshot();
    const QString proceduralFile =
        QDir::current().absoluteFilePath(
            QStringLiteral("procedural_undefined_test.sv"));
    const QString proceduralBaselineSource = QStringLiteral(
        "`define BASE_MACRO clk\n"
        "module child_type;\n"
        "endmodule\n"
        "package base_pkg;\n"
        "endpackage\n"
        "module procedural_top;\n"
        "  typedef logic local_t;\n"
        "  parameter int PARAM = 1;\n"
        "  typedef enum logic { ENUM_VALUE } state_t;\n"
        "  logic clk;\n"
        "  logic sink;\n"
        "  logic existing_q;\n"
        "  assign sink = baseline_missing;\n"
        "  always_ff @(posedge clk) begin\n"
        "    \n"
        "  end\n"
        "endmodule\n");
    SlangManager proceduralSlang;
    QList<EffectiveValueFact> proceduralFacts;
    QList<SemanticSymbolRecord> proceduralRecords =
        proceduralSlang.extractSymbolRecords(
            proceduralFile,
            proceduralBaselineSource,
            {},
            {},
            &proceduralFacts);
    QList<SemanticDiagnostic> proceduralBaselineDiagnostics =
        proceduralSlang.extractDiagnostics(
            proceduralFile,
            proceduralBaselineSource);
    SemanticIndex::getInstance()->setSnapshot(
        snapshotFromRecords(
            proceduralRecords,
            {},
            proceduralBaselineDiagnostics,
            {{proceduralFile, proceduralBaselineSource}}));
    MyCodeEditor proceduralEditor;
    proceduralEditor.resize(520, 180);
    proceduralEditor.setPlainText(proceduralBaselineSource);
    proceduralEditor.setDocumentFileName(proceduralFile);
    proceduralEditor.show();
    for (SemanticDiagnostic& diagnostic :
         proceduralBaselineDiagnostics) {
        diagnostic.documentRevision =
            proceduralEditor.semanticDocumentRevision();
    }
    proceduralEditor.setDiagnosticHighlights(
        proceduralBaselineDiagnostics);
    expectBool("undefined input baseline has real Slang diagnostics",
               !proceduralBaselineDiagnostics.isEmpty()
                   && !proceduralEditor
                           .diagnosticOverviewLinesForTest()
                           .isEmpty(),
               true);
    QTextCursor typedMissingCursor(
        proceduralEditor.document());
    const int typedMissingInsert =
        proceduralBaselineSource.indexOf(
            QStringLiteral("    \n  end\n"));
    typedMissingCursor.setPosition(
        typedMissingInsert + 4);
    proceduralEditor.setTextCursor(typedMissingCursor);
    proceduralEditor.setFocus();
    QTest::keyClicks(
        &proceduralEditor,
        QStringLiteral("missing_q <= clk;"));
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 20);
    const QString proceduralSource =
        proceduralEditor.toPlainText();
    const int missingPosition =
        proceduralSource.indexOf(QStringLiteral("missing_q")) + 2;
    expectBool("undefined input invalidates baseline diagnostics",
               proceduralEditor
                   .diagnosticOverviewLinesForTest()
                   .isEmpty(),
               true);
    QString reason;
    expectBool("unsaved undefined procedural lhs offers logic declaration",
               proceduralEditor.signalDefinitionCandidateForTest(
                   missingPosition, &reason)
                   == QStringLiteral("logic missing_q;"),
               true);
    expectBool("undefined procedural lhs menu excludes unrelated instance action",
               proceduralEditor.structuralContextMenuActionsForTest(
                   missingPosition)
                   == QStringList{
                       QStringLiteral(
                           "Create Signal Definition...")},
               true);

    MyCodeEditor analyzedCategoryEditor;
    analyzedCategoryEditor.setPlainText(
        proceduralBaselineSource);
    analyzedCategoryEditor.setDocumentFileName(
        proceduralFile);
    QTextCursor analyzedCategoryCursor(
        analyzedCategoryEditor.document());
    const int analyzedCategoryInsert =
        proceduralBaselineSource.indexOf(
            QStringLiteral("    \n  end\n"));
    analyzedCategoryCursor.setPosition(
        analyzedCategoryInsert + 4);
    analyzedCategoryCursor.insertText(
        QStringLiteral(
            "existing_q <= clk;\n"
            "    PARAM <= clk;\n"
            "    ENUM_VALUE <= clk;\n"
            "    child_type <= clk;\n"
            "    base_pkg <= clk;\n"
            "    local_t <= clk;\n"
            "    `BASE_MACRO <= clk;"));
    analyzedCategoryEditor.setTextCursor(
        analyzedCategoryCursor);
    const QString analyzedCategorySource =
        analyzedCategoryEditor.toPlainText();
    const auto hasNoSignalCreation =
        [&analyzedCategoryEditor,
         &analyzedCategorySource](const QString& name) {
        const int position =
            analyzedCategorySource.lastIndexOf(name);
        return position >= 0
            && analyzedCategoryEditor
                   .structuralContextMenuActionsForTest(
                       position)
                   .isEmpty();
    };
    expectBool("analyzed declarations and macro expose no create action",
               hasNoSignalCreation(
                   QStringLiteral("existing_q"))
                   && hasNoSignalCreation(
                       QStringLiteral("PARAM"))
                   && hasNoSignalCreation(
                       QStringLiteral("ENUM_VALUE"))
                   && hasNoSignalCreation(
                       QStringLiteral("child_type"))
                   && hasNoSignalCreation(
                       QStringLiteral("base_pkg"))
                   && hasNoSignalCreation(
                       QStringLiteral("local_t"))
                   && hasNoSignalCreation(
                       QStringLiteral("BASE_MACRO")),
               true);

    const QString beforePopup = proceduralEditor.toPlainText();
    expectBool("undefined signal opens inline editable declaration",
               proceduralEditor.beginSignalDefinitionEditorForTest(
                   missingPosition, &reason)
                   && proceduralEditor.findChild<QLineEdit*>(
                       QStringLiteral(
                           "signalDefinitionInlineEditor")),
               true);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QLineEdit* signalDefinitionEditor =
        proceduralEditor.findChild<QLineEdit*>(
            QStringLiteral("signalDefinitionInlineEditor"));
    EditorHoverPopup* signalDefinitionPeek =
        proceduralEditor.findChild<EditorHoverPopup*>(
            QStringLiteral("editorHoverPopup"));
    expectBool("undefined signal editor uses the shared embedded peek",
               signalDefinitionEditor
                   && signalDefinitionPeek
                   && !signalDefinitionPeek->isWindow()
                   && signalDefinitionEditor->parentWidget()
                          == signalDefinitionPeek
                   && signalDefinitionPeek->contentModel().kind
                          == PeekContentKind::DeclarationPreview
                   && signalDefinitionPeek->editableLineEdit()
                          == signalDefinitionEditor,
               true);
    bool inferredClassificationVisible = false;
    bool structuredCandidateVisible = false;
    if (signalDefinitionPeek) {
        for (const PeekContentRow& row :
             signalDefinitionPeek->contentModel().rows) {
            inferredClassificationVisible =
                inferredClassificationVisible
                || row.text
                       == QStringLiteral(
                           "Classification: Inferred");
            structuredCandidateVisible =
                structuredCandidateVisible
                || row.text.contains(
                    QStringLiteral(
                        "Candidate 1 (block-local variable): "
                        "logic missing_q;"));
        }
    }
    expectBool("Declare Signal peek shows classification and structured candidate",
               inferredClassificationVisible
                   && structuredCandidateVisible,
               true);
    expectBool("undefined signal inline editor does not insert eagerly",
               proceduralEditor.toPlainText() == beforePopup,
               true);
    if (QLineEdit* inlineEditor =
            proceduralEditor.findChild<QLineEdit*>(
                QStringLiteral(
                    "signalDefinitionInlineEditor"))) {
        QTest::keyClick(inlineEditor, Qt::Key_Escape);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    expectBool("undefined signal Esc cancels without text change",
               proceduralEditor.toPlainText() == beforePopup
                   && !proceduralEditor.findChild<QLineEdit*>(
                       QStringLiteral(
                           "signalDefinitionInlineEditor")),
               true);

    expectBool("undefined signal editor opens for generation guard",
               proceduralEditor.beginSignalDefinitionEditorForTest(
                   missingPosition, &reason),
               true);
    const auto sameProceduralSnapshot =
        SemanticIndex::getInstance()->snapshot();
    SemanticIndex::getInstance()->setSnapshot(
        sameProceduralSnapshot);
    bool staleGenerationRejected = false;
    if (QLineEdit* staleEditor =
            proceduralEditor.findChild<QLineEdit*>(
                QStringLiteral(
                    "signalDefinitionInlineEditor"))) {
        QTest::keyClick(staleEditor, Qt::Key_Return);
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 20);
        staleGenerationRejected =
            !proceduralEditor.findChild<QLineEdit*>(
                QStringLiteral(
                    "signalDefinitionInlineEditor"));
    }
    expectBool("Declare Signal rechecks semantic generation before apply",
               staleGenerationRejected
                   && proceduralEditor.toPlainText()
                          == beforePopup,
               true);

    QTextCursor originalCursor(proceduralEditor.document());
    originalCursor.setPosition(
        proceduralSource.indexOf(
            QStringLiteral("missing_q")) + 4);
    proceduralEditor.setTextCursor(originalCursor);
    const int oldCursorPosition =
        proceduralEditor.textCursor().position();
    const int oldVerticalScroll =
        proceduralEditor.verticalScrollBar()->value();
    expectBool("undefined signal editor reopens after cancel",
               proceduralEditor.beginSignalDefinitionEditorForTest(
                   missingPosition, &reason),
               true);
    bool declarationSubmittedFromPeek = false;
    if (QLineEdit* reopenedEditor =
            proceduralEditor.findChild<QLineEdit*>(
                QStringLiteral(
                    "signalDefinitionInlineEditor"))) {
        reopenedEditor->setText(
            QStringLiteral(
                "logic signed [3:0] missing_q;"));
        QTest::keyClick(reopenedEditor, Qt::Key_Return);
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 20);
        declarationSubmittedFromPeek =
            !proceduralEditor.findChild<QLineEdit*>(
                QStringLiteral(
                    "signalDefinitionInlineEditor"));
    }
    expectBool("undefined signal confirmation inserts edited declaration",
               declarationSubmittedFromPeek
                   && proceduralEditor.toPlainText().contains(
                       QStringLiteral(
                           "    logic signed [3:0] missing_q;\n"))
                   && proceduralEditor.toPlainText().indexOf(
                          QStringLiteral(
                              "logic signed [3:0] missing_q;"))
                      > proceduralEditor.toPlainText().indexOf(
                          QStringLiteral("always_ff"))
                   && proceduralEditor.toPlainText().indexOf(
                          QStringLiteral(
                              "logic signed [3:0] missing_q;"))
                      < proceduralEditor.toPlainText().indexOf(
                          QStringLiteral(
                              "missing_q <= clk;")),
               true);
    const int insertionDelta =
        proceduralEditor.toPlainText().size()
        - proceduralSource.size();
    expectBool("undefined signal confirmation preserves source cursor context",
               proceduralEditor.textCursor().position()
                       == oldCursorPosition + insertionDelta
                   && proceduralEditor.toPlainText().mid(
                          proceduralEditor.textCursor().position() - 4,
                          QStringLiteral("missing_q").size())
                      == QStringLiteral("missing_q")
                   && proceduralEditor.verticalScrollBar()->value()
                       == oldVerticalScroll,
               true);
    bool insertionFlashVisible = false;
    const int insertedDeclarationPosition =
        proceduralEditor.toPlainText().indexOf(
            QStringLiteral(
                "logic signed [3:0] missing_q;"));
    const int insertedDeclarationBlock =
        proceduralEditor.document()
            ->findBlock(insertedDeclarationPosition)
            .blockNumber();
    for (const QTextEdit::ExtraSelection& selection :
         proceduralEditor.extraSelections()) {
        const QColor background =
            selection.format.background().color();
        QColor expectedFlash =
            InsightVisualStyle::theme().accent;
        expectedFlash.setAlpha(72);
        insertionFlashVisible =
            insertionFlashVisible
            || (selection.cursor.blockNumber()
                    == insertedDeclarationBlock
                && background
                       == expectedFlash);
    }
    expectBool("Declare Signal briefly flashes the insertion line",
               insertionFlashVisible,
               true);
    const QString confirmedSignalDefinitionSource =
        proceduralEditor.toPlainText();
    proceduralEditor.undo();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 20);
    const bool signalDefinitionUndone =
        proceduralEditor.toPlainText()
            == proceduralSource;
    proceduralEditor.redo();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 20);
    expectBool("Declare Signal confirmation is one undo transaction",
               signalDefinitionUndone
                   && proceduralEditor.toPlainText()
                          == confirmedSignalDefinitionSource,
               true);

    const QString classificationSource = QStringLiteral(
        "module procedural_top;\n"
        "  assign mixed_missing = clk;\n"
        "  assign net_missing = clk;\n"
        "  always_comb begin\n"
        "    mixed_missing = clk;\n"
        "    uncertain_missing = 1'b0;\n"
        "  end\n"
        "endmodule\n");
    MyCodeEditor classificationEditor;
    classificationEditor.resize(620, 260);
    classificationEditor.setPlainText(
        classificationSource);
    classificationEditor.setDocumentFileName(
        proceduralFile);
    classificationEditor.show();
    const int mixedPosition =
        classificationSource.indexOf(
            QStringLiteral("mixed_missing")) + 2;
    expectBool("mixed driver Declare Signal opens a non-editable conflict peek",
               classificationEditor
                   .beginSignalDefinitionEditorForTest(
                       mixedPosition, &reason),
               true);
    EditorHoverPopup* classificationPeek =
        classificationEditor.findChild<EditorHoverPopup*>(
            QStringLiteral("editorHoverPopup"));
    bool conflictClassificationVisible = false;
    if (classificationPeek) {
        for (const PeekContentRow& row :
             classificationPeek->contentModel().rows) {
            conflictClassificationVisible =
                conflictClassificationVisible
                || row.text
                       == QStringLiteral(
                           "Classification: Conflict");
        }
    }
    expectBool("conflict proposal is visible but cannot apply string edits",
               conflictClassificationVisible
                   && !classificationEditor.findChild<QLineEdit*>(
                       QStringLiteral(
                           "signalDefinitionInlineEditor")),
               true);
    if (classificationPeek)
        classificationPeek->closePopup();

    const int uncertainPosition =
        classificationSource.indexOf(
            QStringLiteral("uncertain_missing")) + 2;
    expectBool("missing type evidence opens an Uncertain explanation",
               classificationEditor
                   .beginSignalDefinitionEditorForTest(
                       uncertainPosition, &reason),
               true);
    classificationPeek =
        classificationEditor.findChild<EditorHoverPopup*>(
            QStringLiteral("editorHoverPopup"));
    bool uncertainClassificationVisible = false;
    if (classificationPeek) {
        for (const PeekContentRow& row :
             classificationPeek->contentModel().rows) {
            uncertainClassificationVisible =
                uncertainClassificationVisible
                || row.text
                       == QStringLiteral(
                           "Classification: Uncertain");
        }
    }
    expectBool("Uncertain proposal without a Slang type has no editor",
               uncertainClassificationVisible
                   && !classificationEditor.findChild<QLineEdit*>(
                       QStringLiteral(
                           "signalDefinitionInlineEditor")),
               true);
    if (classificationPeek)
        classificationPeek->closePopup();

    const int netPosition =
        classificationSource.indexOf(
            QStringLiteral("net_missing")) + 2;
    expectBool("continuous assignment opens a module-net proposal",
               classificationEditor
                   .beginSignalDefinitionEditorForTest(
                       netPosition, &reason),
               true);
    classificationPeek =
        classificationEditor.findChild<EditorHoverPopup*>(
            QStringLiteral("editorHoverPopup"));
    bool moduleNetCandidateVisible = false;
    if (classificationPeek) {
        for (const PeekContentRow& row :
             classificationPeek->contentModel().rows) {
            moduleNetCandidateVisible =
                moduleNetCandidateVisible
                || row.text.contains(
                    QStringLiteral(
                        "Candidate 1 (module net): "
                        "wire logic net_missing;"));
        }
    }
    expectBool("continuous assignment candidate explicitly prefers net",
               moduleNetCandidateVisible,
               true);
    if (classificationPeek)
        classificationPeek->closePopup();
    SemanticIndex::getInstance()->setSnapshot(
        proceduralPreviousSnapshot);

    const QString categoryFile =
        QDir::current().absoluteFilePath(
            QStringLiteral("undefined_category_test.sv"));
    const QString categorySource = QStringLiteral(
        "module category_top;\n"
        "  missing_module u_missing();\n"
        "  missing_pkg::missing_type typed_value;\n"
        "  `MISSING_MACRO\n"
        "endmodule\n");
    const auto categoryPreviousSnapshot =
        SemanticIndex::getInstance()->snapshot();
    SlangManager categorySlang;
    QList<EffectiveValueFact> categoryFacts;
    const QList<SemanticSymbolRecord> categoryRecords =
        categorySlang.extractSymbolRecords(
            categoryFile,
            categorySource,
            {},
            {},
            &categoryFacts);
    const QList<SemanticDiagnostic> categoryDiagnostics =
        categorySlang.extractDiagnostics(
            categoryFile,
            categorySource);
    SemanticIndex::getInstance()->setSnapshot(
        snapshotFromRecords(
            categoryRecords,
            {},
            categoryDiagnostics,
            {{categoryFile, categorySource}}));
    MyCodeEditor categoryEditor;
    categoryEditor.setPlainText(categorySource);
    categoryEditor.setDocumentFileName(categoryFile);
    expectBool("undefined category baseline has real Slang diagnostics",
               !categoryDiagnostics.isEmpty(),
               true);
    expectBool("undefined module package type and macro expose no signal action",
               categoryEditor.structuralContextMenuActionsForTest(
                   categorySource.indexOf(
                       QStringLiteral("missing_module")))
                       .isEmpty()
                   && categoryEditor.structuralContextMenuActionsForTest(
                       categorySource.indexOf(
                           QStringLiteral("missing_pkg")))
                          .isEmpty()
                   && categoryEditor.structuralContextMenuActionsForTest(
                       categorySource.indexOf(
                           QStringLiteral("missing_type")))
                          .isEmpty()
                   && categoryEditor.structuralContextMenuActionsForTest(
                       categorySource.indexOf(
                           QStringLiteral("MISSING_MACRO")))
                           .isEmpty(),
               true);
    SemanticIndex::getInstance()->setSnapshot(
        categoryPreviousSnapshot);

    const QString instanceSource = QStringLiteral(
        "module slot_top;\n"
        "  child #(\n"
        "    .WIDTH(P + fn(a, {b, c})),\n"
        "    .DEPTH(4)\n"
        "  ) u_child(\n"
        "    .clk(clk),\n"
        "    .data(data),\n"
        "    .ready()\n"
        "  );\n"
        "endmodule\n");
    MyCodeEditor slotEditor;
    slotEditor.resize(640, 260);
    slotEditor.setPlainText(instanceSource);
    slotEditor.show();
    slotEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const int slotPosition =
        instanceSource.indexOf(QStringLiteral("u_child"));
    expectBool("instance context menu is available on instance name",
               slotEditor.structuralContextMenuActionsForTest(
                   slotPosition)
                   == QStringList{
                       QStringLiteral("Edit Instance Slots")},
               true);
    const QString beforeSlotMode = slotEditor.toPlainText();
    expectBool("existing instance enters Slot Mode without editing",
               slotEditor.editInstanceSlotsAtForTest(
                   slotPosition, &reason)
                   && slotEditor.toPlainText() == beforeSlotMode
                   && slotEditor.templateSlotModeActive()
                   && slotEditor.templateSlotModeSlotCount() == 5
                   && slotEditor.textCursor().selectedText()
                      == QStringLiteral("P + fn(a, {b, c})"),
               true);
    QTest::keyClick(&slotEditor, Qt::Key_Tab);
    expectBool("instance Slot Mode follows source parameter order",
               slotEditor.templateSlotModeActiveIndex() == 1
                   && slotEditor.textCursor().selectedText()
                      == QStringLiteral("4"),
               true);
    QTest::keyClick(&slotEditor, Qt::Key_Tab);
    QTest::keyClick(&slotEditor, Qt::Key_Tab);
    QTest::keyClick(&slotEditor, Qt::Key_Tab);
    expectBool("instance Slot Mode includes empty named actual",
               slotEditor.templateSlotModeActiveIndex() == 4
                   && !slotEditor.textCursor().hasSelection()
                   && slotEditor.toPlainText() == beforeSlotMode,
               true);
    QTest::keyClick(&slotEditor, Qt::Key_Backtab);
    expectBool("instance Slot Mode Shift+Tab cycles backward",
               slotEditor.templateSlotModeActiveIndex() == 3
                   && slotEditor.textCursor().selectedText()
                      == QStringLiteral("data"),
               true);
    QTest::keyClick(&slotEditor, Qt::Key_Escape);
    expectBool("instance Slot Mode Esc exits without editing",
               !slotEditor.templateSlotModeActive()
                   && slotEditor.toPlainText() == beforeSlotMode,
               true);

    const QString incompleteSource =
        QStringLiteral(
            "module bad; child #(.P(1) u0(.a(x); endmodule\n");
    slotEditor.setPlainText(incompleteSource);
    expectBool("incomplete instance exposes no slot action",
               slotEditor.structuralContextMenuActionsForTest(
                   incompleteSource.indexOf(
                       QStringLiteral("u0")))
                   .isEmpty(),
               true);

    const QString semanticFile =
        QDir::current().absoluteFilePath(
            QStringLiteral("undefined_instance_test.sv"));
    const QString semanticBaselineSource = QStringLiteral(
        "package type_pkg;\n"
        "  typedef logic signed [5:0] payload_t;\n"
        "endpackage\n"
        "interface bus_if;\n"
        "  logic [3:0] data;\n"
        "  modport master(input data);\n"
        "endinterface\n"
        "module metadata_child(\n"
        "  input type_pkg::payload_t typed [0:1],\n"
        "  bus_if.master bus\n"
        ");\n"
        "endmodule\n"
        "module typed_child #(\n"
        "  parameter int P = 4\n"
        ") (\n"
        "  input logic signed [P-1:0] test [0:1]\n"
        ");\n"
        "endmodule\n"
        "module typed_top;\n"
        "  logic sig0;\n"
        "  logic sig1;\n"
        "  logic sig2;\n"
        "  logic sink;\n"
        "  parameter int NOT_SIGNAL = 1;\n"
        "  typedef enum logic { ENUM_VALUE } state_t;\n"
        "  /* block comment keeps sig0 non-semantic\n"
        "     sig0 nested-style /* text\n"
        "  */ assign sink = sig0;\n"
        "  always_comb begin\n"
        "    // queue_here sig1\n"
        "  end\n"
        "  initial $display(\"sig2\");\n"
        "  typed_child #(.P(8)) u0(.test());\n"
        "  typed_child #(.P(12)) u1(.test());\n"
        "  metadata_child u_meta(\n"
        "    .typed(),\n"
        "    .bus()\n"
        "  );\n"
        "endmodule\n");
    SlangManager slang;
    QList<EffectiveValueFact> facts;
    QList<SemanticSymbolRecord> records =
        slang.extractSymbolRecords(
            semanticFile,
            semanticBaselineSource,
            {},
            {},
            &facts);
    SemanticIndex* semanticIndex =
        SemanticIndex::getInstance();
    EffectiveValueService* values =
        EffectiveValueService::getInstance();
    const auto previousSnapshot = semanticIndex->snapshot();
    values->clearPublishedFacts();

    MyCodeEditor semanticEditor;
    semanticEditor.resize(260, 260);
    semanticEditor.setPlainText(
        semanticBaselineSource);
    semanticEditor.setDocumentFileName(semanticFile);
    const std::uint64_t computation =
        values->beginComputation({semanticFile});
    const std::uint64_t revision =
        semanticEditor.semanticDocumentRevision();
    for (SemanticSymbolRecord& record : records) {
        record.presentation.computationRevision =
            computation;
        record.presentation.documentRevision = revision;
    }
    semanticIndex->setSnapshot(snapshotFromRecords(
        records,
        {},
        {},
        {{semanticFile, semanticBaselineSource}}));
    values->publishDocumentFacts(semanticFile,
                                 semanticBaselineSource,
                                 facts,
                                 computation,
                                 revision);
    semanticEditor.show();
    semanticEditor.setFocus();
    const auto typeNamedActual =
        [&semanticEditor](const QString& emptyAssociation,
                          const QString& identifier) {
        const QString current =
            semanticEditor.toPlainText();
        const int association =
            current.indexOf(emptyAssociation);
        if (association < 0)
            return false;
        QTextCursor cursor(
            semanticEditor.document());
        cursor.setPosition(
            association
            + emptyAssociation.indexOf(
                QLatin1Char('('))
            + 1);
        semanticEditor.setTextCursor(cursor);
        QTest::keyClicks(
            &semanticEditor, identifier);
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 10);
        return true;
    };
    expectBool("undefined named-port actuals are typed after baseline analysis",
               typeNamedActual(
                   QStringLiteral(".test()"),
                   QStringLiteral("signal_u0"))
                   && typeNamedActual(
                       QStringLiteral(".test()"),
                       QStringLiteral("signal_u1"))
                   && typeNamedActual(
                       QStringLiteral(".typed()"),
                       QStringLiteral("undefined_typed"))
                   && typeNamedActual(
                       QStringLiteral(".bus()"),
                       QStringLiteral("undefined_bus"))
                   && semanticEditor
                          .diagnosticOverviewLinesForTest()
                          .isEmpty(),
               true);
    const QString semanticSource =
        semanticEditor.toPlainText();
    HierarchyInstanceContext topContext;
    topContext.workspacePath = QDir::currentPath();
    topContext.activeTopModule =
        QStringLiteral("typed_top");
    topContext.instancePath =
        QStringLiteral("typed_top");
    semanticEditor.setHierarchyInstanceContext(topContext);
    const int u0Signal =
        semanticSource.indexOf(QStringLiteral("signal_u0")) + 2;
    const int u1Signal =
        semanticSource.indexOf(QStringLiteral("signal_u1")) + 2;
    const QString u0Candidate =
        semanticEditor.signalDefinitionCandidateForTest(
            u0Signal, &reason);
    const QString u1Candidate =
        semanticEditor.signalDefinitionCandidateForTest(
            u1Signal, &reason);
    expectBool("undefined named-port copies exact bound formal type",
               u0Candidate.contains(QStringLiteral("logic"))
                   && u0Candidate.contains(QStringLiteral("signed"))
                   && u0Candidate.contains(QStringLiteral("[7:0]"))
                   && u0Candidate.contains(QStringLiteral("signal_u0"))
                   && u0Candidate.contains(QStringLiteral("[0:1]"))
                   && !u0Candidate.contains(QStringLiteral("input")),
               true);
    expectBool("undefined named-port keeps instance-specific width",
               u1Candidate.contains(QStringLiteral("[11:0]"))
                   && !u1Candidate.contains(QStringLiteral("[7:0]"))
                   && u1Candidate.contains(QStringLiteral("signal_u1")),
               true);
    expectBool("exact named-port opens the shared structured proposal peek in a narrow editor",
               semanticEditor.beginSignalDefinitionEditorForTest(
                   u0Signal, &reason),
               true);
    EditorHoverPopup* exactProposalPeek =
        semanticEditor.findChild<EditorHoverPopup*>(
            QStringLiteral("editorHoverPopup"));
    bool exactClassificationVisible = false;
    bool exactCandidateVisible = false;
    if (exactProposalPeek) {
        for (const PeekContentRow& row :
             exactProposalPeek->contentModel().rows) {
            exactClassificationVisible =
                exactClassificationVisible
                || row.text
                       == QStringLiteral(
                           "Classification: Exact");
            exactCandidateVisible =
                exactCandidateVisible
                || (row.text.contains(
                         QStringLiteral("signal_u0"))
                    && row.text.contains(
                        QStringLiteral("[7:0]"))
                    && row.text.contains(
                        QStringLiteral("[0:1]"))
                    && !row.text.contains(
                        QStringLiteral("input")));
        }
    }
    expectBool("exact proposal displays packed/unpacked candidate without direction",
               exactClassificationVisible
                   && exactCandidateVisible,
               true);
    if (exactProposalPeek)
        exactProposalPeek->closePopup();
    const int typedUndefined =
        semanticSource.indexOf(
            QStringLiteral("undefined_typed")) + 2;
    const int interfaceUndefined =
        semanticSource.indexOf(
            QStringLiteral("undefined_bus")) + 2;
    const QString typedefCandidate =
        semanticEditor.signalDefinitionCandidateForTest(
            typedUndefined, &reason);
    const QString interfaceCandidate =
        semanticEditor.signalDefinitionCandidateForTest(
            interfaceUndefined, &reason);
    expectBool("undefined named-port preserves typedef and unpacked array type",
               typedefCandidate.contains(
                   QStringLiteral("payload_t"))
                   && typedefCandidate.contains(
                       QStringLiteral("undefined_typed"))
                   && typedefCandidate.contains(
                       QStringLiteral("[0:1]"))
                   && !typedefCandidate.contains(
                       QStringLiteral("input")),
               true);
    expectBool("undefined named-port preserves interface modport type",
               interfaceCandidate
                       == QStringLiteral(
                           "bus_if.master undefined_bus;"),
               true);
    expectBool("undefined named-port accepts current unsaved buffer identity",
               !QFileInfo::exists(semanticFile)
                   && !typedefCandidate.isEmpty()
                   && !interfaceCandidate.isEmpty(),
               true);
    expectBool("undefined actual menu orders create then instance slots",
               semanticEditor.structuralContextMenuActionsForTest(
                   u0Signal)
                   == QStringList{
                       QStringLiteral(
                           "Create Signal Definition..."),
                       QStringLiteral("<separator>"),
                       QStringLiteral("Edit Instance Slots")},
               true);
    semanticEditor.setHierarchyInstanceContext(
        HierarchyInstanceContext{});
    expectBool("undefined named-port refuses an unbound instance guess",
               semanticEditor.signalDefinitionCandidateForTest(
                   u0Signal, &reason)
                       .isEmpty()
                   && (reason.contains(
                           QStringLiteral(
                               "elaborated instance"))
                       || reason.contains(
                           QStringLiteral(
                               "different structured Slang types"))),
               true);
    semanticEditor.setHierarchyInstanceContext(topContext);
    const int moduleTypePosition =
        semanticSource.indexOf(
            QStringLiteral("typed_child #(.P(8)"));
    expectBool("undefined module type is not treated as a signal",
               semanticEditor.signalDefinitionCandidateForTest(
                   moduleTypePosition, &reason)
                   .isEmpty(),
               true);

    QList<EffectiveValueFact> refreshedFacts;
    QList<SemanticSymbolRecord> refreshedRecords =
        slang.extractSymbolRecords(
            semanticFile,
            semanticSource,
            {},
            {},
            &refreshedFacts);
    const std::uint64_t refreshedComputation =
        values->beginComputation({semanticFile});
    const std::uint64_t refreshedRevision =
        semanticEditor.semanticDocumentRevision();
    for (SemanticSymbolRecord& record : refreshedRecords) {
        record.presentation.computationRevision =
            refreshedComputation;
        record.presentation.documentRevision =
            refreshedRevision;
    }
    semanticIndex->setSnapshot(snapshotFromRecords(
        refreshedRecords,
        {},
        {},
        {{semanticFile, semanticSource}}));
    values->publishDocumentFacts(
        semanticFile,
        semanticSource,
        refreshedFacts,
        refreshedComputation,
        refreshedRevision);

    semanticEditor.resize(720, 460);
    semanticEditor.show();
    semanticEditor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const int blockCommentSig =
        semanticSource.indexOf(
            QStringLiteral("sig0"),
            semanticSource.indexOf(
                QStringLiteral("/* block comment")));
    const int codeSigAfterComment =
        semanticSource.indexOf(
            QStringLiteral("sig0"),
            semanticSource.indexOf(
                QStringLiteral("*/ assign sink")));
    SemanticDecoration commentDecoration;
    commentDecoration.role =
        SemanticDecorationRole::ActualSignal;
    commentDecoration.text = QStringLiteral("sig0");
    commentDecoration.startPosition =
        blockCommentSig;
    commentDecoration.length = 4;
    SemanticDecoration codeDecoration =
        commentDecoration;
    codeDecoration.startPosition =
        codeSigAfterComment;
    const int lineCommentSig =
        semanticSource.indexOf(
            QStringLiteral("sig1"),
            semanticSource.indexOf(
                QStringLiteral("// queue_here")));
    SemanticDecoration lineCommentDecoration =
        commentDecoration;
    lineCommentDecoration.startPosition =
        lineCommentSig;
    semanticEditor.setSemanticDecorations(
        {commentDecoration,
         lineCommentDecoration,
         codeDecoration});
    bool commentSemanticDecorationVisible = false;
    bool lineCommentSemanticDecorationVisible = false;
    bool codeSemanticDecorationVisible = false;
    for (const QTextEdit::ExtraSelection& selection :
         semanticEditor.extraSelections()) {
        if (!selection.cursor.hasSelection())
            continue;
        commentSemanticDecorationVisible =
            commentSemanticDecorationVisible
            || (selection.cursor.selectionStart()
                    == blockCommentSig
                && selection.cursor.selectionEnd()
                    == blockCommentSig + 4);
        codeSemanticDecorationVisible =
            codeSemanticDecorationVisible
            || (selection.cursor.selectionStart()
                    == codeSigAfterComment
                && selection.cursor.selectionEnd()
                    == codeSigAfterComment + 4);
        lineCommentSemanticDecorationVisible =
            lineCommentSemanticDecorationVisible
            || (selection.cursor.selectionStart()
                    == lineCommentSig
                && selection.cursor.selectionEnd()
                    == lineCommentSig + 4);
    }
    expectBool("line and block comment signals have no semantic decoration",
               !commentSemanticDecorationVisible
                   && !lineCommentSemanticDecorationVisible
                   && codeSemanticDecorationVisible,
               true);
    hideEditorHoverPopups();
    QTextCursor blockCommentCursor(
        semanticEditor.document());
    blockCommentCursor.setPosition(blockCommentSig);
    semanticEditor.setTextCursor(blockCommentCursor);
    semanticEditor.ensureCursorVisible();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QTest::mouseDClick(
        semanticEditor.viewport(),
        Qt::LeftButton,
        Qt::NoModifier,
        semanticEditor.cursorRect(
            blockCommentCursor).center());
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    bool blockCommentPopupVisible = false;
    visibleEditorHoverPopupText(
        &blockCommentPopupVisible);
    expectBool("block-comment signal has no double-click popup",
               !blockCommentPopupVisible,
               true);
    QTextCursor lineCommentCursor(
        semanticEditor.document());
    lineCommentCursor.setPosition(
        lineCommentSig);
    semanticEditor.setTextCursor(
        lineCommentCursor);
    semanticEditor.ensureCursorVisible();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QTest::mouseDClick(
        semanticEditor.viewport(),
        Qt::LeftButton,
        Qt::NoModifier,
        semanticEditor.cursorRect(
            lineCommentCursor).center());
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    bool lineCommentPopupVisible = false;
    visibleEditorHoverPopupText(
        &lineCommentPopupVisible);
    expectBool("line-comment signal has no double-click popup",
               !lineCommentPopupVisible,
               true);
    QTextCursor codeAfterCommentCursor(
        semanticEditor.document());
    codeAfterCommentCursor.setPosition(
        codeSigAfterComment);
    semanticEditor.setTextCursor(
        codeAfterCommentCursor);
    semanticEditor.ensureCursorVisible();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QTest::mouseDClick(
        semanticEditor.viewport(),
        Qt::LeftButton,
        Qt::NoModifier,
        semanticEditor.cursorRect(
            codeAfterCommentCursor).center());
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    bool codeAfterCommentPopupVisible = false;
    const QString codeAfterCommentPopup =
        visibleEditorHoverPopupText(
            &codeAfterCommentPopupVisible);
    expectBool("code after block-comment close keeps semantic popup",
               codeAfterCommentPopupVisible
                   && codeAfterCommentPopup.contains(
                       QStringLiteral("sig0")),
               true);
    semanticEditor.closeSemanticPopup();

    MyCodeEditor commentGhostEditor;
    commentGhostEditor.resize(520, 150);
    const QString commentGhostSource =
        QStringLiteral(
            "module ghost_comment;\n"
            "  // line_signal_in_comment\n"
            "  /* signal_in_comment */\n"
            "  logic live_signal;\n"
            "endmodule\n");
    commentGhostEditor.setPlainText(
        commentGhostSource);
    commentGhostEditor.show();
    commentGhostEditor.clearFocus();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 30);
    const QImage ghostBaseline =
        renderWidgetImage(
            commentGhostEditor.viewport());
    GhostAnnotation commentGhost;
    commentGhost.kind =
        GhostAnnotationKind::SignalWidth;
    commentGhost.placement =
        GhostAnnotationPlacement::RightOfLine;
    commentGhost.text =
        QStringLiteral("comment ghost");
    commentGhost.line = 2;
    commentGhost.anchorPosition =
        commentGhostSource.indexOf(
            QStringLiteral("line_signal_in_comment"));
    commentGhost.anchorLength =
        QStringLiteral("line_signal_in_comment").size();
    GhostAnnotation blockCommentGhost =
        commentGhost;
    blockCommentGhost.line = 3;
    blockCommentGhost.anchorPosition =
        commentGhostSource.lastIndexOf(
            QStringLiteral("signal_in_comment"));
    blockCommentGhost.anchorLength =
        QStringLiteral("signal_in_comment").size();
    commentGhostEditor.setGhostAnnotations(
        {commentGhost, blockCommentGhost});
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 30);
    const QImage commentGhostImage =
        renderWidgetImage(
            commentGhostEditor.viewport());
    GhostAnnotation liveGhost = commentGhost;
    liveGhost.text = QStringLiteral("16 bits");
    liveGhost.line = 4;
    liveGhost.anchorPosition =
        commentGhostSource.indexOf(
            QStringLiteral("live_signal"));
    liveGhost.anchorLength =
        QStringLiteral("live_signal").size();
    commentGhostEditor.setGhostAnnotations(
        {liveGhost});
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 30);
    const QImage liveGhostImage =
        renderWidgetImage(
            commentGhostEditor.viewport());
    expectBool("comment ghost is not painted while code ghost is painted",
               differentPixelBounds(
                   ghostBaseline,
                   commentGhostImage).isNull()
                   && !differentPixelBounds(
                           ghostBaseline,
                           liveGhostImage).isNull(),
               true);

    const int originalSelectionCursor =
        semanticSource.indexOf(
            QStringLiteral("NOT_SIGNAL"));
    QTextCursor selectionCursor(
        semanticEditor.document());
    selectionCursor.setPosition(originalSelectionCursor);
    semanticEditor.setTextCursor(selectionCursor);
    expectBool("signal selection mode starts without moving cursor",
               semanticEditor.startSignalSelectionMode(&reason)
                   && semanticEditor.signalSelectionModeActiveForTest()
                   && semanticEditor.textCursor().position()
                      == originalSelectionCursor,
               true);

    const int sig0Declaration =
        semanticSource.indexOf(
            QStringLiteral("sig0;"));
    const int sig1Declaration =
        semanticSource.indexOf(
            QStringLiteral("sig1;"));
    const int sig2Declaration =
        semanticSource.indexOf(
            QStringLiteral("sig2;"));
    expectBool("signal selection accepts semantic signals in click order",
               semanticEditor.toggleSignalSelectionAtForTest(
                   sig2Declaration)
                   && semanticEditor.toggleSignalSelectionAtForTest(
                       sig0Declaration)
                   && semanticEditor.toggleSignalSelectionAtForTest(
                       sig1Declaration)
                   && semanticEditor.selectedSignalNamesForTest()
                      == QStringList{
                          QStringLiteral("sig0"),
                          QStringLiteral("sig1"),
                          QStringLiteral("sig2")},
               true);
    const int sig0Use =
        semanticSource.indexOf(
            QStringLiteral("sig0"),
            codeSigAfterComment);
    expectBool("signal selection deduplicates by semantic identity",
               semanticEditor.toggleSignalSelectionAtForTest(sig0Use)
                   && semanticEditor.selectedSignalNamesForTest()
                      == QStringList{
                          QStringLiteral("sig1"),
                          QStringLiteral("sig2")}
                   && semanticEditor.toggleSignalSelectionAtForTest(
                       sig0Declaration)
                   && semanticEditor.selectedSignalNamesForTest()
                      == QStringList{
                          QStringLiteral("sig0"),
                          QStringLiteral("sig1"),
                          QStringLiteral("sig2")},
               true);
    expectBool("signal selection rejects parameter enum module comment and string",
               !semanticEditor.toggleSignalSelectionAtForTest(
                    semanticSource.indexOf(
                        QStringLiteral("NOT_SIGNAL")))
                   && !semanticEditor.toggleSignalSelectionAtForTest(
                       semanticSource.indexOf(
                           QStringLiteral("ENUM_VALUE")))
                   && !semanticEditor.toggleSignalSelectionAtForTest(
                       semanticSource.indexOf(
                           QStringLiteral("typed_top")))
                   && !semanticEditor.toggleSignalSelectionAtForTest(
                       semanticSource.indexOf(
                           QStringLiteral("sig1"),
                           semanticSource.indexOf(
                               QStringLiteral("// queue_here"))))
                   && !semanticEditor.toggleSignalSelectionAtForTest(
                       semanticSource.lastIndexOf(
                           QStringLiteral("sig2")))
                   && semanticEditor.selectedSignalNamesForTest().size()
                      == 3,
               true);

    QTest::keyClick(&semanticEditor, Qt::Key_Escape);
    expectBool("signal selection Esc cancels without editing",
               !semanticEditor.signalSelectionModeActiveForTest()
                   && semanticEditor.selectedSignalNamesForTest().isEmpty()
                   && semanticEditor.toPlainText() == semanticSource,
               true);

    semanticEditor.startSignalSelectionMode(&reason);
    QTextCursor sig0Cursor(semanticEditor.document());
    sig0Cursor.setPosition(sig0Declaration);
    QTextCursor sig1Cursor(semanticEditor.document());
    sig1Cursor.setPosition(sig1Declaration);
    QTextCursor sig2Cursor(semanticEditor.document());
    sig2Cursor.setPosition(sig2Declaration);
    const QPoint sig0Point =
        semanticEditor.cursorRect(sig0Cursor).center();
    const QPoint sig1Point =
        semanticEditor.cursorRect(sig1Cursor).center();
    const QPoint sig2Point =
        semanticEditor.cursorRect(sig2Cursor).center();
    QTest::mousePress(semanticEditor.viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      sig0Point);
    QMouseEvent moveToSig2(
        QEvent::MouseMove,
        sig2Point,
        semanticEditor.viewport()->mapToGlobal(sig2Point),
        Qt::NoButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(
        semanticEditor.viewport(), &moveToSig2);
    QTest::mouseRelease(semanticEditor.viewport(),
                        Qt::LeftButton,
                        Qt::NoModifier,
                        sig2Point);
    expectBool("signal selection interpolates coalesced drag across lines",
               semanticEditor.selectedSignalNamesForTest()
                   == QStringList{
                       QStringLiteral("sig0"),
                       QStringLiteral("sig1"),
                       QStringLiteral("sig2")}
                   && semanticEditor.textCursor().position()
                      == originalSelectionCursor,
               true);

    const int queueContext =
        semanticSource.indexOf(
            QStringLiteral("// queue_here"));
    expectBool("signal assignment queue inserts at right-click indentation",
               semanticEditor.createAssignmentQueueAtForTest(
                   queueContext, &reason)
                   && semanticEditor.toPlainText().contains(
                       QStringLiteral(
                           "    sig0 <= ;\n"
                           "    sig1 <= ;\n"
                           "    sig2 <= ;\n"
                           "    // queue_here"))
                   && semanticEditor.templateSlotModeActive()
                   && semanticEditor.templateSlotModeSlotCount() == 3
                   && !semanticEditor.textCursor().hasSelection(),
               true);
    QTest::keyClick(&semanticEditor, Qt::Key_Tab);
    expectBool("signal assignment queue uses existing Slot Mode cycling",
               semanticEditor.templateSlotModeActiveIndex() == 1,
               true);
    QTest::keyClick(&semanticEditor, Qt::Key_Escape);
    values->clearPublishedFacts();
    semanticIndex->setSnapshot(previousSnapshot);
}

QAction* editorContextMenuActionById(
    QMenu* menu,
    const QString& actionId)
{
    if (!menu)
        return nullptr;
    for (QAction* action : menu->actions()) {
        if (action->property("actionId").toString()
            == actionId) {
            return action;
        }
        if (QMenu* child = action->menu()) {
            if (QAction* found =
                    editorContextMenuActionById(
                        child, actionId)) {
                return found;
            }
        }
    }
    return nullptr;
}

void runEditorContextMenuGroupingRegression()
{
    QTemporaryDir temp;
    expectBool("context menu temp dir valid",
               temp.isValid(),
               true);
    if (!temp.isValid())
        return;

    const QString fileName =
        temp.filePath(QStringLiteral("menu.sv"));
    const QString text =
        QStringLiteral(
            "module menu;\n"
            "  logic payload;\n"
            "  assign payload = 1'b0;\n"
            "endmodule\n");
    expectBool("write context menu fixture",
               writeTextFile(fileName, text),
               true);

    QTabWidget tabs;
    TabManager manager(&tabs);
    EditorCoordinator coordinator(&manager);
    expectBool("open context menu fixture",
               manager.openFileInTab(fileName),
               true);
    MyCodeEditor* editor = manager.getCurrentEditor();
    expectBool("context menu fixture has editor",
               editor != nullptr,
               true);
    if (!editor)
        return;
    coordinator.attachEditor(editor);

    const int payloadPosition =
        text.indexOf(QStringLiteral("payload"));
    const EditorSemanticContext context =
        editor->editorSemanticContextForPosition(
            payloadPosition, true);
    QMenu menu;
    coordinator.populateSourceSymbolContextMenuForTest(
        &menu, context);

    QStringList groupTitles;
    QStringList leadingStandardIds;
    for (QAction* action : menu.actions()) {
        if (action->menu()) {
            groupTitles.append(action->menu()->title());
        } else if (!action->isSeparator()) {
            leadingStandardIds.append(
                action->property("actionId").toString());
        }
    }
    expectBool("context menu has stable grouped order",
               groupTitles
                   == QStringList{
                       QStringLiteral("Navigate"),
                       QStringLiteral("Inspect"),
                       QStringLiteral("Refactor")},
               true);
    expectBool("context menu omits standard editing actions",
               leadingStandardIds.isEmpty(),
               true);

    QAction* undo = editorContextMenuActionById(
        &menu, QStringLiteral("edit.undo"));
    expectBool("standard editing action is absent",
               undo == nullptr,
               true);
    QAction* definition = editorContextMenuActionById(
        &menu, QStringLiteral("source.goToDefinition"));
    expectBool("definition action is absent",
               definition == nullptr
                   && editorContextMenuActionById(
                          &menu,
                          QStringLiteral("navigation.goLine"))
                       == nullptr
                   && editorContextMenuActionById(
                          &menu,
                          QStringLiteral("select.all"))
                       == nullptr,
               true);
    QAction* unavailableStateGraph = editorContextMenuActionById(
        &menu, QStringLiteral("insight.stateTransitionGraph"));
    expectBool("unavailable FSM action retains its disabled radial slot",
               unavailableStateGraph && !unavailableStateGraph->property("executable").toBool(),
               true);
    expectBool("selection-only format action is absent",
               editorContextMenuActionById(
                   &menu,
                   QStringLiteral("format.selection"))
                   == nullptr,
               true);
}

QAction* contextRailAction(
    ContextRail* rail,
    const QString& providerId)
{
    if (!rail)
        return nullptr;
    for (QAction* action : rail->actions()) {
        if (action
            && action->data().toString() == providerId) {
            return action;
        }
    }
    return nullptr;
}

QImage contextRailIconImage(
    QAction* action,
    QIcon::State state = QIcon::Off)
{
    return action
        ? action->icon()
              .pixmap(QSize(32, 32), QIcon::Normal, state)
              .toImage()
        : QImage();
}

void runContextRailIconRegression(MainWindow& window)
{
    ContextRail* rail = window.contextWorkspaceController
        ? window.contextWorkspaceController->rail()
        : nullptr;
    QAction* editorAction = contextRailAction(
        rail, QStringLiteral("temporaryEditor"));
    const QList<QAction*> insightActions = {
        contextRailAction(
            rail,
            LiveInsightsContextProvider::providerIdForKind(
                LiveInsightKind::Kernel)),
        contextRailAction(
            rail,
            LiveInsightsContextProvider::providerIdForKind(
                LiveInsightKind::Module)),
        contextRailAction(
            rail,
            LiveInsightsContextProvider::providerIdForKind(
                LiveInsightKind::Hotspot)),
        contextRailAction(
            rail,
            LiveInsightsContextProvider::providerIdForKind(
                LiveInsightKind::State)),
        contextRailAction(
            rail,
            LiveInsightsContextProvider::providerIdForKind(
                LiveInsightKind::Wave))
    };
    QAction* pinloomAction = contextRailAction(
        rail, QStringLiteral("pinloom"));
    const QImage editorIcon = contextRailIconImage(editorAction);
    const QImage pinloomIcon = contextRailIconImage(pinloomAction);

    QList<QImage> icons = {editorIcon, pinloomIcon};
    bool allActionsPresent = editorAction && pinloomAction;
    bool selectedTreatment = allActionsPresent
        && contextRailIconImage(editorAction, QIcon::On) != editorIcon
        && contextRailIconImage(pinloomAction, QIcon::On) != pinloomIcon;
    for (QAction* action : insightActions) {
        const QImage icon = contextRailIconImage(action);
        allActionsPresent = allActionsPresent && action && !icon.isNull();
        selectedTreatment = selectedTreatment && action
            && contextRailIconImage(action, QIcon::On) != icon;
        icons.append(icon);
    }
    bool visuallyDistinct = true;
    for (int left = 0; left < icons.size(); ++left) {
        visuallyDistinct = visuallyDistinct && !icons.at(left).isNull();
        for (int right = left + 1; right < icons.size(); ++right)
            visuallyDistinct = visuallyDistinct
                && icons.at(left) != icons.at(right);
    }

    expectBool("Context Rail providers have explicit icons",
               allActionsPresent,
               true);
    expectBool("Context Rail provider icons are visually distinct",
               visuallyDistinct,
               true);
    expectBool("Context Rail active icons have a selected treatment",
               selectedTreatment,
               true);
}

void runLiveInsightSidebarRoutingRegression(
    MainWindow& window,
    const QString& fixturePath)
{
    runRtlInsightsPanelRegression(window, fixturePath);
    ContextWorkspaceController* controller =
        window.contextWorkspaceController.get();
    SemanticPanelRefreshCoordinator* refresh =
        window.semanticDocks
        ? window.semanticDocks->refreshCoordinator()
        : nullptr;
    expectBool("Live Insight sidebar route dependencies exist",
               controller && refresh,
               true);
    expectBool("legacy insight bottom docks and menu toggles are absent",
               window.semanticDocks
                   && window.semanticDocks
                          ->rtlInsightsPanelCoordinator() == nullptr
                   && window.semanticDocks
                          ->signalKernelGraphPanelCoordinator() == nullptr
                   && window.semanticDocks
                          ->wavePreviewPanelCoordinator() == nullptr
                   && window.findChild<QDockWidget*>(
                          QStringLiteral("rtlInsightsDock")) == nullptr
                   && window.findChild<QDockWidget*>(
                          QStringLiteral("signalKernelGraphDock")) == nullptr
                   && window.findChild<QDockWidget*>(
                          QStringLiteral("wavePreviewDock")) == nullptr
                   && window.findChild<QAction*>(
                          QStringLiteral("viewRtlInsightsAction")) == nullptr
                   && window.findChild<QAction*>(
                          QStringLiteral("viewSignalKernelGraphAction")) == nullptr
                   && window.findChild<QAction*>(
                          QStringLiteral("viewWavePreviewAction")) == nullptr,
               true);
    if (!controller || !refresh)
        return;

    refresh->showStateTransitionGraphForSymbol(
        QStringLiteral("state_d"),
        fixturePath,
        QStringLiteral("insight_top"));
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 100);

    const ContextResource active =
        controller->dockHost()->currentResource();
    LiveInsightKind activeKind = LiveInsightKind::Module;
    auto* compactView = qobject_cast<LiveInsightsContextView*>(
        controller->dockHost()->viewForResource(
            active.stableKey()));
    QWidget* fullView = window.tabManager
        ? window.tabManager->toolPage(
              QStringLiteral("live-insight:state"))
        : nullptr;
    expectBool("State Transition command opens the right Live Insights dock",
               controller->dockWidget()->isVisible()
                   && compactView
                   && LiveInsightsContextProvider::kindFromResource(
                       active, &activeKind)
                   && activeKind == LiveInsightKind::State
                   && compactView->selectedKind()
                          == LiveInsightKind::State,
               true);
    // A symbol picked in the source now lands in the sidebar section, pinned
    // to that symbol, and opens no central tab. The full view stays reachable
    // through the section header entry, exercised further down.
    expectBool("State Transition command pins the section and opens no tab",
               compactView
                   && !compactView->followEditor()
                   && compactView->surfaceForTest() != nullptr
                   && fullView == nullptr,
               true);
    auto* statePage = compactView ? compactView->surfaceForTest() : nullptr;
    auto* stateSurface = statePage && statePage->workbenchForTest()
        ? statePage->workbenchForTest()->rtlSurfaceForTest() : nullptr;
    const QString reviewDir = qEnvironmentVariable("ZEROSLACK_UI_REVIEW_DIR");
    if (!reviewDir.isEmpty() && statePage) {
        QDir().mkpath(reviewDir);
        statePage->grab().save(reviewDir + "/state.png");
    }
    expectBool("State section renders the dedicated state graph",
        stateSurface && stateSurface->graphModeForTest() == QStringLiteral("state-transition")
            && stateSurface->graphNodeItemCountForTest() > 0, true);

    // The section header names its target and re-picking runs in the editor:
    // this is the only place that exercises the whole chain through MainWindow.
    QAbstractButton* scopeChip =
        controller->dockHost()->sectionScope(active.stableKey());
    MyCodeEditor* pickEditor = window.tabManager
        ? window.tabManager->getCurrentEditor()
        : nullptr;
    expectBool("State section header names the pinned target",
               scopeChip != nullptr && scopeChip->isVisible()
                   && !scopeChip->text().trimmed().isEmpty()
                   && scopeChip->text() != QStringLiteral("Pick target"),
               true);
    if (scopeChip && pickEditor) {
        // Put a declaration on screen first: candidates come from the visible
        // region, and this editor opens on a comment header that holds none.
        const QRegularExpression declarationPattern(
            QStringLiteral("\\n\\s*(logic|reg|wire)\\s"));
        const int declaration = pickEditor->toPlainText().indexOf(
            declarationPattern);
        if (declaration >= 0) {
            QTextCursor cursor = pickEditor->textCursor();
            cursor.setPosition(declaration);
            pickEditor->setTextCursor(cursor);
            pickEditor->centerCursor();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        scopeChip->click();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("Scope chip starts the editor target picker",
                   pickEditor->insightTargetPickModeActive(),
                   true);
        const QPair<int, int> pickRange =
            pickEditor->insightTargetEnumeratedLineRangeForTest();
        // Which symbols blink depends on the semantic records for the open
        // document, which this fixture does not analyse; the candidate set
        // itself is covered by editor_insight_target_pick_test against a known
        // snapshot. What this asserts is that the mode reads the region the
        // editor is actually showing.
        const int firstVisibleLine =
            pickEditor->cursorForPosition(QPoint(0, 0)).blockNumber();
        expectBool("Picker enumerates the editor visible region",
                   declaration >= 0
                       && pickRange.first == firstVisibleLine
                       && pickRange.second >= pickRange.first
                       && pickRange.second < pickEditor->blockCount(),
                   true);
        if (!reviewDir.isEmpty()) {
            QDir().mkpath(reviewDir);
            pickEditor->grab().save(reviewDir + "/pick_mode.png");
        }
        pickEditor->cancelInsightTargetPickMode();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("Canceling the picker leaves no mode behind",
                   !pickEditor->insightTargetPickModeActive(),
                   true);
    }
    expectBool("State Transition command has no legacy bottom-dock target",
               window.dockForPanelId(
                          QStringLiteral("rtlInsights")) == nullptr,
               true);

    struct InsightRouteProbe {
        LiveInsightKind kind;
        std::function<void()> invoke;
    };
    const QList<InsightRouteProbe> routeProbes = {
        {LiveInsightKind::Kernel,
         [&]() {
             refresh->showSignalKernelGraphForSymbol(
                 QStringLiteral("data_q"),
                 fixturePath,
                 QStringLiteral("insight_top"));
         }},
        {LiveInsightKind::Hotspot,
         [&]() {
             refresh->showSignalUsageHotspotForSymbol(
                 QStringLiteral("data_q"),
                 fixturePath,
                 QStringLiteral("insight_top"));
         }},
        {LiveInsightKind::Module,
         [&]() {
             refresh->showModuleBlockDiagramForSymbol(
                 QStringLiteral("insight_top"),
                 fixturePath,
                 QStringLiteral("insight_top"));
         }},
    };
    bool allInsightRoutesUseRightProvider = true;
    LiveInsightToolPage* moduleSectionPage = nullptr;
    for (const InsightRouteProbe& probe : routeProbes) {
        probe.invoke();
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 50);
        const ContextResource routed =
            controller->dockHost()->currentResource();
        LiveInsightKind routedKind = LiveInsightKind::State;
        auto* routedView = qobject_cast<LiveInsightsContextView*>(
            controller->dockHost()->viewForResource(routed.stableKey()));
        // Each route retargets its own section and still opens no tab.
        allInsightRoutesUseRightProvider =
            allInsightRoutesUseRightProvider
            && LiveInsightsContextProvider::kindFromResource(
                routed, &routedKind)
            && routedKind == probe.kind
            && routedView
            && !routedView->followEditor()
            && routedView->surfaceForTest() != nullptr
            && window.tabManager->toolPage(
                   QStringLiteral("live-insight:%1")
                       .arg(liveInsightKindId(probe.kind)))
                   == nullptr;
        if (probe.kind == LiveInsightKind::Module && routedView)
            moduleSectionPage = routedView->surfaceForTest();
    }
    LiveInsightToolPage* modulePage = moduleSectionPage;
    auto* moduleSurface = modulePage && modulePage->workbenchForTest()
        ? modulePage->workbenchForTest()->rtlSurfaceForTest() : nullptr;
    if (!reviewDir.isEmpty() && modulePage) modulePage->grab().save(reviewDir + "/block.png");
    expectBool("Module section preserves nested dedicated scene",
        moduleSurface && moduleSurface->graphModeForTest() == QStringLiteral("module-block")
            && moduleSurface->graphNodeItemCountForTest() > 1
            && moduleSurface->graphNestedNodeStackingReadableForTest(), true);
    expectBool("kernel hotspot module and state commands share Live Insights",
               allInsightRoutesUseRightProvider,
               true);

    const ActionDescriptor* waveCommand =
        findActionById(QString::fromLatin1(
            ActionIds::ViewWavePreview));
    ActionInvocation waveInvocation;
    waveInvocation.workspaceId = window.workspaceManager
        ? window.workspaceManager->getWorkspacePath()
        : QStringLiteral("standalone");
    const ActionExecutionResult waveResult = waveCommand
        ? executeAction(*waveCommand, window, waveInvocation)
        : ActionExecutionResult();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    const ContextResource waveResource =
        controller->dockHost()->currentResource();
    LiveInsightKind waveKind = LiveInsightKind::Kernel;
    auto* waveContextView = qobject_cast<LiveInsightsContextView*>(
        controller->dockHost()->viewForResource(
            waveResource.stableKey()));
    auto* waveSectionPage = waveContextView
        ? waveContextView->surfaceForTest()
        : nullptr;
    expectBool("Wave command opens the right provider in a pinned section",
               waveResult.succeeded
                   && LiveInsightsContextProvider::kindFromResource(
                       waveResource, &waveKind)
                   && waveKind == LiveInsightKind::Wave
                   && waveContextView
                   && !waveContextView->followEditor()
                   && waveSectionPage
                   && waveSectionPage->waveCoordinatorForTest()
                   && window.tabManager->toolPage(
                          QStringLiteral("live-insight:wave")) == nullptr,
               true);

    // The full view is now reached from the section header, and only there;
    // that page is still the detachable one.
    QWidget* waveHeader = controller->dockHost()->sectionHeader(
        waveResource.stableKey());
    auto* waveFullViewButton = waveHeader
        ? waveHeader->findChild<QToolButton*>(
              QStringLiteral("contextDockFullView"))
        : nullptr;
    if (waveFullViewButton)
        waveFullViewButton->click();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    auto* waveFullView = dynamic_cast<LiveInsightToolPage*>(
        window.tabManager->toolPage(
            QStringLiteral("live-insight:wave")));
    expectBool("Section header still opens the full Wave tool page",
               waveFullViewButton && waveFullView
                   && waveFullView->waveCoordinatorForTest(),
               true);
    QMainWindow* detachedWave = waveFullView
        ? waveFullView->detachToWindow()
        : nullptr;
    expectBool("Wave full tool page supports detachable refresh surface",
               detachedWave
                   && waveFullView->detachedWindowForTest()
                          == detachedWave
                   && detachedWave->centralWidget() != nullptr,
               true);
    if (detachedWave)
        detachedWave->close();

    if (active.isValid())
        controller->closePinnedResource(active.stableKey());
    if (fullView) {
        auto* group = qobject_cast<QTabWidget*>(
            fullView->parentWidget());
        if (group) {
            const int index = group->indexOf(fullView);
            if (index >= 0)
                window.tabManager->closePage(group, index);
        }
    }
    window.liveInsightToolPages.remove(
        static_cast<int>(LiveInsightKind::State));
    if (window.tabManager)
        window.tabManager->activateOpenFile(fixturePath);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
}

void runInsightFocusIntegrationRegression(
    MainWindow& window,
    const QString& fixturePath,
    const QString& expectedWorkspacePath)
{
    // Insight Focus is retired. This is the runtime half of that retirement:
    // action_registry_test asserts its descriptors are absent and
    // controller_boundary_test asserts its implementation is gone, while this
    // checks that a running window exposes no focus panel, no focus entry and
    // no focus page in the central stack.
    Q_UNUSED(fixturePath)
    RtlInsightsPanelCoordinator* rtl =
        window.semanticDocks
        ? window.semanticDocks->rtlInsightsPanelCoordinator()
        : nullptr;
    SignalKernelGraphPanelCoordinator* kernel =
        window.semanticDocks
        ? window.semanticDocks
              ->signalKernelGraphPanelCoordinator()
        : nullptr;
    WavePreviewPanelCoordinator* wave =
        window.semanticDocks
        ? window.semanticDocks->wavePreviewPanelCoordinator()
        : nullptr;

    QMenu* focusMenu =
        window.findChild<QMenu*>(
            QStringLiteral("insightFocusMenu"));
    QAction* focusRtlAction =
        window.findChild<QAction*>(
            QStringLiteral("focusRtlInsightsAction"));
    QAction* focusKernelAction =
        window.findChild<QAction*>(
            QStringLiteral(
                "focusSignalKernelGraphAction"));
    QAction* focusWaveAction =
        window.findChild<QAction*>(
            QStringLiteral("focusWavePreviewAction"));
    QAction* leaveFocusAction =
        window.findChild<QAction*>(
            QStringLiteral("leaveInsightFocusAction"));
    QAction* viewWaveAction =
        window.findChild<QAction*>(
            QStringLiteral("viewWavePreviewAction"));

    expectBool("legacy Insight Focus panel registrations are removed",
               !rtl
                   && !kernel
                   && !wave,
               true);
    expectBool("View menu exposes no legacy Insight Focus entries",
               !focusMenu
                   && !focusRtlAction
                   && !focusKernelAction
                   && !focusWaveAction
                   && !leaveFocusAction
                   && !viewWaveAction,
               true);
    // Stronger than the retired controller pointer check it replaces: the
    // focus shell used to add its own page to the central stack, so a stack
    // holding exactly the editor page proves no focus page was ever built.
    expectBool("central stack holds only the editor page",
               window.centralContentStack
                   && window.centralContentStack->count() == 1
                   && window.centralContentStack->widget(0)
                          == window.editorCentralPage
                   && window.centralContentStack->currentWidget()
                          == window.editorCentralPage,
               true);
    expectBool("no residual Insight Focus widget remains in the window",
               window.findChild<QWidget*>(
                   QStringLiteral("insightFocusPage")) == nullptr
                   && window.findChild<QPushButton*>(
                          QStringLiteral("insightFocusBackButton"))
                          == nullptr,
               true);

    const QString activeWorkspacePath =
        window.workspaceManager
        ? QDir::cleanPath(
              QDir::fromNativeSeparators(
                  QFileInfo(
                      window.workspaceManager
                          ->getWorkspacePath())
                      .absoluteFilePath()))
        : QString();
    const QString normalizedExpectedWorkspacePath =
        QDir::cleanPath(
            QDir::fromNativeSeparators(
                QFileInfo(expectedWorkspacePath)
                    .absoluteFilePath()));
    expectBool("Focus View regression uses active fixture workspace",
               !activeWorkspacePath.isEmpty()
                   && activeWorkspacePath
                          == normalizedExpectedWorkspacePath,
               true);
}

int main(int argc, char** argv)
{
    ScopedGuiTestSettingsRoot isolatedSettings;
    QApplication app(argc, argv);
    ApplicationThemeManager::instance().applyToApplication();

    expectBool("GUI settings use an isolated writable INI root",
               isolatedSettings.isValid()
                   && QFileInfo(isolatedSettings.path()).isDir()
                   && QSettings::defaultFormat()
                          == QSettings::IniFormat,
               true);

    runActivityLogServiceRegression();
    runActivityInboxRegression();
    runRtlInsightsOnDemandRegression();
    runEditorAppearanceSettingsRegression();
    runEditorBracketRangeRegression();
    runEditorSmartSelectionRegression();
    runEditorOccurrenceNavigationRegression();
    runEditorRegistryRenameAdapterRegression();
    runStructuralEditingRegression();
    runEditorColumnEditRegression();
    runEditorLineActionRegression();
    runEditorCtrlClickNavigationRegression();
    runSignalKernelGraphPopupInteractionRegression();
    runEditorFormatterRegression();
    runEditorAppearanceCoordinatorRegression();
    runTabOpenDedupRegression();
    runTabOpenGhostLifecycleRegression();
    runWorkspaceCloseRegression();
    runWorkspaceScanReentrancyRegression();
    runWorkspaceCachedSwitchRegression();
    runWorkspaceWatcherIncrementalRegression();
    runWorkspaceScanSignalReentrancyRegression();
    runWorkspaceAliasRenameRegression();
    runWorkspaceSessionCloseSaveOrderRegression();
    runExternalConflictReviewRegression();
    runCrashRecoveryReviewRegression();
    runNoImplicitCompletionRegression();
    runIncludeCompletionRegression();
    runTreeSitterFoldingProviderRegression();
    runNavigationDesignCacheWorkspaceActivationRegression();
    runNavigationHierarchyModelRegression();
    runSemanticStateUiRegression();
    runWavePersistentTreeStateRegression();
    runEditorContextMenuGroupingRegression();

    const QString workspaceFixturePath = (argc > 1)
        ? QString::fromLocal8Bit(argv[1])
        : QDir::current().absoluteFilePath(QStringLiteral("test_sv/new"));
    const QString symbolFixturePath = (argc > 2)
        ? QString::fromLocal8Bit(argv[2])
        : QDir::current().absoluteFilePath(QStringLiteral("test_sv/test_symbols.sv"));
    const QString normalizedSymbolFixturePath =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(symbolFixturePath).absoluteFilePath()));

    expectBool("workspace fixture exists",
               QFileInfo(workspaceFixturePath).isDir(),
               true);
    expectBool("symbol fixture exists", QFileInfo(symbolFixturePath).isFile(), true);
    QTemporaryDir isolatedWorkspaceRoot;
    const QString workspacePath =
        isolatedWorkspaceRoot.isValid()
        ? isolatedWorkspaceRoot.filePath(
              QStringLiteral("new"))
        : QString();
    expectBool("GUI workspace isolation temp dir valid",
               isolatedWorkspaceRoot.isValid(),
               true);
    expectBool("GUI workspace fixture copied for writable isolation",
               !workspacePath.isEmpty()
                   && copyFixtureTree(
                       workspaceFixturePath,
                       workspacePath),
               true);
    runEditorHoverPreviewRegression(workspacePath);

    MainWindow window;
    const QString productVersion = QLatin1String(APP_VERSION);
    expectBool("product version uses strict SemVer",
               QRegularExpression(
                   QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+$"))
                   .match(productVersion)
                   .hasMatch(),
               true);
    expectBool("product version omits dependency marker",
               !productVersion.contains(QStringLiteral("slang"), Qt::CaseInsensitive),
               true);
    expectBool("main window title shows product version",
               window.windowTitle().contains(QStringLiteral("ZeroSlack"))
                   && window.windowTitle().contains(
                       QStringLiteral("v%1").arg(productVersion))
                   && !window.windowTitle().contains(QStringLiteral("slang"),
                                                     Qt::CaseInsensitive),
               true);
    expectBool("status bar is absent", window.findChild<QStatusBar*>() == nullptr, true);

    bool workspaceSymbolsDone = false;
    bool workspaceFilesScanned = false;
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::workspaceSymbolAnalysisFinished,
                     &window,
                     [&](const ProjectSnapshot&, int filesAnalyzed, int totalSymbols) {
                         Q_UNUSED(filesAnalyzed)
                         Q_UNUSED(totalSymbols)
                         workspaceSymbolsDone = true;
                     });
    QObject::connect(window.workspaceManager.get(),
                     &WorkspaceManager::filesScanned,
                     &window,
                     [&](const QStringList&) {
                         workspaceFilesScanned = true;
                     });

    window.resize(1100, 760);
    window.show();
    expectBool("main window visible", waitUntil([&]() { return window.isVisible(); }, 2000), true);
    expectBool("startup keeps passive bottom content collapsed",
               window.panelLayoutController
                   && window.panelLayoutController->isBottomCollapsed()
                   && window.panelLayoutController->buttonBar()
                   && window.panelLayoutController->buttonBar()->isVisible(),
               true);
    auto* projectSidebar = window.navigationPane->dock();
    auto* sidebarHeader = projectSidebar->titleBarWidget();
    auto* projectButton = window.findChild<QToolButton*>(QStringLiteral("projectRailButton"));
    auto* settingsButton = window.findChild<QToolButton*>(QStringLiteral("settingsRailButton"));
    auto* collapseSidebar = window.findChild<QToolButton*>(QStringLiteral("collapseProjectSidebarButton"));
    auto* expandSidebar = window.findChild<QToolButton*>(QStringLiteral("expandProjectSidebarButton"));
    auto* fileTree = window.findChild<QTreeWidget*>(QStringLiteral("navigationFileTree"));
    expectBool("Project and Settings share the file tree sidebar header",
               sidebarHeader && projectButton && settingsButton && collapseSidebar && expandSidebar
                   && sidebarHeader->isAncestorOf(projectButton)
                   && sidebarHeader->isAncestorOf(settingsButton)
                   && projectSidebar->isAncestorOf(fileTree)
                   && projectButton->geometry().top() == settingsButton->geometry().top()
                   && projectButton->menu() && !projectButton->menu()->actions().isEmpty()
                   && projectButton->toolButtonStyle() == Qt::ToolButtonIconOnly
                   && !fileTree->alternatingRowColors()
                   && !window.findChild<QToolBar*>(QStringLiteral("projectRail"))
                   && !window.menuBar()->isVisible(), true);
    const int sidebarWidth = projectSidebar->width();
    const int editorWidthWithSidebar = window.centralWidget()->width();
    saveEditorLayoutScreenshot(window, QStringLiteral("project-sidebar-expanded.png"));
    collapseSidebar->click();
    expectBool("collapsing sidebar hides its controls and file tree and expands editor",
               waitUntil([&] {
                   return !projectSidebar->isVisible() && !fileTree->isVisible()
                       && !projectButton->isVisible() && !settingsButton->isVisible()
                       && expandSidebar->isVisible()
                       && window.centralWidget()->width() > editorWidthWithSidebar;
               }, 2000), true);
    saveEditorLayoutScreenshot(window, QStringLiteral("project-sidebar-collapsed.png"));
    expandSidebar->click();
    expectBool("title button restores the complete sidebar at its previous width",
               waitUntil([&] {
                   return projectSidebar->isVisible() && fileTree->isVisible()
                       && projectButton->isVisible() && settingsButton->isVisible()
                       && !expandSidebar->isVisible() && projectSidebar->width() == sidebarWidth;
               }, 2000), true);
    window.navigationPane->toggleVisible();
    expectBool("existing navigation toggle keeps the title restore entry in sync",
               !projectSidebar->isVisible() && expandSidebar->isVisible(), true);
    window.navigationPane->toggleVisible();
    expectBool("only Problems and Activity remain permanent in the drawer",
               window.panelLayoutController->buttonForPanel(QStringLiteral("problems"))->isVisible()
                   && window.panelLayoutController->buttonForPanel(QStringLiteral("activity"))->isVisible()
                   && !window.panelLayoutController->buttonForPanel(QStringLiteral("scopedSearch"))->isVisible()
                   && !window.panelLayoutController->buttonForPanel(QStringLiteral("foldShelf"))->isVisible()
                   && !window.panelLayoutController->buttonForPanel(QStringLiteral("connections"))->isVisible(), true);
    settingsButton->click();
    QWidget* settingsPage = window.tabManager->toolPage(QStringLiteral("settingsCenter"));
    expectBool("Settings opens in the center and keeps the dock hidden",
               settingsPage && settingsPage->isAncestorOf(window.settingsCenterPanel)
                   && !window.settingsCenterDock->isVisible()
                   && fileTree->isVisible() && projectSidebar->width() == sidebarWidth, true);
    saveEditorLayoutScreenshot(window, QStringLiteral("project-sidebar-settings.png"));
    settingsButton->click();
    expectBool("second Settings click closes its central page",
               !window.tabManager->toolPage(QStringLiteral("settingsCenter")), true);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    settingsButton->click();
    settingsPage = window.tabManager->toolPage(QStringLiteral("settingsCenter"));
    expectBool("Settings content survives closing and reopening its central tab",
               settingsPage && settingsPage->isAncestorOf(window.settingsCenterPanel), true);
    window.tabManager->closeTab(window.tabManager->activeTabWidget()->indexOf(settingsPage));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    window.panelLayoutController->restorePanel(QStringLiteral("connections"));
    QWidget* connectionsPage = window.tabManager->toolPage(QStringLiteral("connections"));
    expectBool("connection actions open in a central tab instead of the drawer",
               connectionsPage && connectionsPage->isVisible()
                   && !window.panelLayoutController->isPanelOpen(QStringLiteral("connections")), true);
    window.tabManager->closeTab(window.tabManager->activeTabWidget()->indexOf(connectionsPage));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    window.panelLayoutController->restorePanel(QStringLiteral("connections"));
    connectionsPage = window.tabManager->toolPage(QStringLiteral("connections"));
    expectBool("connection content survives closing and reopening the central tab",
               connectionsPage && connectionsPage->findChild<QTabWidget*>(QStringLiteral("instancePairDiffTabs")), true);
    window.tabManager->closeTab(window.tabManager->activeTabWidget()->indexOf(connectionsPage));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QWidget* titleOverlay = window.findChild<QWidget*>(QStringLiteral("workspaceTitleBar"));
    QTest::qWait(150);
    const QRect centerBeforeTitle = window.centralWidget()->geometry();
    const QPoint previousPointer = QCursor::pos();
    QCursor::setPos(window.mapToGlobal(QPoint(-50, -50)));
    QTest::qWait(3300);
    expectBool("fixed title stays visible after idle without overlapping content",
               titleOverlay && titleOverlay->isVisible()
                   && centerBeforeTitle.top() >= titleOverlay->geometry().bottom()
                   && window.centralWidget()->geometry() == centerBeforeTitle, true);
    QCursor::setPos(window.mapToGlobal(QPoint(100, 1)));
    QTest::qWait(150);
    expectBool("fixed title remains visible when the pointer returns",
               titleOverlay && titleOverlay->isVisible()
                   && window.centralWidget()->geometry() == centerBeforeTitle, true);
    QCursor::setPos(previousPointer);
    expectBool("outer workspace tab bar removed",
               window.findChild<QTabBar*>(
                   QStringLiteral("workspaceTabBar")) == nullptr,
               true);
    expectBool("activity output panel exists",
               window.findChild<QPlainTextEdit*>(
                   QStringLiteral("activityOutputText")) != nullptr,
               true);
    QDockWidget* foldShelfDock =
        window.findChild<QDockWidget*>(QStringLiteral("FoldShelfDock"));
    expectBool("fold shelf dock exists",
               foldShelfDock != nullptr,
               true);
    expectBool("fold shelf dock starts hidden",
               foldShelfDock && !foldShelfDock->isVisible(),
               true);
    NavigationWidget* railNavigationWidget =
        window.navigationPane ? window.navigationPane->navigationWidget : nullptr;
    QDockWidget* navigationDock =
        window.navigationPane ? window.navigationPane->dock() : nullptr;
    expectBool("left feature navigation rail removed",
               window.findChild<QDockWidget*>(
                   QStringLiteral("shellNavigationRailDock")) == nullptr
                   && window.findChild<QToolButton*>(
                          QStringLiteral("shellRail_explorer")) == nullptr
                   && window.findChild<QToolButton*>(
                          QStringLiteral("shellRail_design")) == nullptr
                   && window.findChild<QToolButton*>(
                          QStringLiteral("shellRail_search")) == nullptr,
               true);
    expectBool("navigation pane remains available",
               railNavigationWidget
                   && railNavigationWidget->tabWidget
                   && railNavigationWidget->searchLineEdit,
               true);
    if (railNavigationWidget && navigationDock) {
        navigationDock->hide();
        railNavigationWidget->setActiveTab(NavigationWidget::DesignTab);
        window.navigationPane->showFiles();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("navigation Files entry restores pane",
                   navigationDock->isVisible()
                       && railNavigationWidget->tabWidget->currentIndex()
                              == NavigationWidget::FileTab,
                   true);

        navigationDock->hide();
        railNavigationWidget->setActiveTab(NavigationWidget::FileTab);
        window.navigationPane->showDesign();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("navigation Design entry restores pane",
                   navigationDock->isVisible()
                       && railNavigationWidget->tabWidget->currentIndex()
                              == NavigationWidget::DesignTab,
                   true);

        navigationDock->hide();
        railNavigationWidget->searchLineEdit->clearFocus();
        window.navigationPane->showSearch();
        expectBool("navigation search restores pane and focus",
                   waitUntil([&]() {
                       return navigationDock->isVisible()
                           && railNavigationWidget->searchLineEdit->hasFocus();
                   }, 1000),
                   true);
    }
    QMenu* viewMenu = window.findChild<QMenu*>(QStringLiteral("viewMenu"));
    expectBool("view menu exists", viewMenu != nullptr, true);
    bool fixedMenuActionsUseRegistry = true;
    for (const ActionDescriptor* descriptor :
         actionDescriptorsForSurface(
             ActionSurface::Menu)) {
        if (!descriptor) {
            fixedMenuActionsUseRegistry = false;
            break;
        }
        const ActionAliasDescriptor alias =
            descriptor->aliasForSurface(
                ActionSurface::Menu);
        const QList<QAction*> actions =
            window.findChildren<QAction*>(
                alias.adapterKey);
        if (actions.size() != 1
            || actions.constFirst()
                   ->property("actionId").toString()
                   != descriptor->id
            || actions.constFirst()
                   ->property(
                       "executionRoute").toString()
                   != descriptor->executionRoute
            || actions.constFirst()->shortcut()
                   .toString(
                       QKeySequence::PortableText)
                   != QKeySequence::fromString(
                          effectiveActionShortcut(
                              descriptor->id),
                          QKeySequence::PortableText)
                          .toString(
                              QKeySequence::PortableText)) {
            fixedMenuActionsUseRegistry = false;
            break;
        }
    }
    expectBool(
        "all fixed menu actions are unique Action Registry adapters",
        fixedMenuActionsUseRegistry,
        true);
    QDockWidget* settingsCenterDock =
        window.findChild<QDockWidget*>(
            QStringLiteral("settingsContentHost"));
    QAction* viewSettingsCenterAction =
        window.findChild<QAction*>(
            QStringLiteral("viewSettingsCenterAction"));
    expectBool("unified Settings Center replaces appearance panel",
               settingsCenterDock
                   && settingsCenterDock->windowTitle()
                          == QStringLiteral("Settings")
                   && settingsCenterDock->widget()
                          == window.settingsCenterPanel
                   && window.settingsCenterPanel
                   && window.findChild<QWidget*>(
                          QStringLiteral(
                              "editorAppearancePanel")) == nullptr,
               true);
    expectBool("Settings Center keeps legacy dock identity",
               window.dockForPanelId(
                   QStringLiteral("settingsCenter"))
                       == settingsCenterDock
                   && window.dockForPanelId(
                          QStringLiteral("editorAppearance"))
                          == settingsCenterDock
                   && settingsCenterDock
                   && settingsCenterDock->property(
                          "legacyPanelId").toString()
                          == QStringLiteral("editorAppearance"),
               true);
    if (viewSettingsCenterAction) {
        viewSettingsCenterAction->trigger();
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 50);
    }
    expectBool("Settings Center has one View entry",
               viewSettingsCenterAction
                   && settingsCenterDock
                   && settingsCenterDock->isVisible()
                   && window.findChild<QAction*>(
                          QStringLiteral(
                              "viewEditorAppearanceAction")) == nullptr,
               true);
    if (settingsCenterDock)
        settingsCenterDock->hide();
    expectBool("Search Navigate Help menus removed",
               window.findChild<QMenu*>(
                   QStringLiteral("searchMenu")) == nullptr
                   && window.findChild<QMenu*>(
                          QStringLiteral("navigateMenu")) == nullptr
                   && window.findChild<QMenu*>(
                          QStringLiteral("helpMenu")) == nullptr,
               true);
    expectBool("standalone semantic panel actions removed",
               window.findChild<QAction*>(
                   QStringLiteral("viewReferencesAction")) == nullptr
                   && window.findChild<QAction*>(
                          QStringLiteral("viewRelationshipsAction")) == nullptr,
               true);
    expectBool("multi-workspace controls remain in Workspace menu",
               window.findChild<QMenu*>(
                   QStringLiteral("openWorkspacesMenu")) != nullptr
                   && window.findChild<QAction*>(
                          QStringLiteral("closeActiveWorkspaceAction"))
                          != nullptr,
               true);
    QToolButton* panelsStatusButton =
        window.findChild<QToolButton*>(QStringLiteral("panelsStatusButton"));
    expectBool("panels remain accessible in View menu without status button",
               !panelsStatusButton && viewMenu, true);
    QMenu* toolsMenu = window.findChild<QMenu*>(QStringLiteral("toolsMenu"));
    QMenu* userTemplatesMenu =
        window.findChild<QMenu*>(QStringLiteral("userTemplatesMenu"));
    QAction* openGlobalUserTemplatesAction =
        window.findChild<QAction*>(
            QStringLiteral("openGlobalUserTemplatesAction"));
    QAction* openWorkspaceUserTemplatesAction =
        window.findChild<QAction*>(
            QStringLiteral("openWorkspaceUserTemplatesAction"));
    QAction* reloadUserTemplatesAction =
        window.findChild<QAction*>(
            QStringLiteral("reloadUserTemplatesAction"));
    expectBool("tools menu exists", toolsMenu != nullptr, true);
    QAction* runWaveSimulationAction =
        window.findChild<QAction*>(
            QStringLiteral("runWaveSimulationAction"));
    expectBool("Wave Simulation is a formal registry-backed Tools Action",
               window.waveSimulationCoordinator
                   && runWaveSimulationAction
                   && runWaveSimulationAction
                          ->property("actionId").toString()
                          == QString::fromLatin1(
                              ActionIds::
                                  WaveSimulationRunCurrentContext)
                   && !runWaveSimulationAction->text().contains(
                       QStringLiteral("Experimental"),
                       Qt::CaseInsensitive),
               true);
    if (window.waveSimulationCoordinator
        && window.notificationCenter
        && window.tabManager
        && window.tabManager->getCurrentEditor()) {
        WaveSimulationDiagnostic diagnostic;
        diagnostic.sourceFile =
            window.tabManager->getCurrentEditor()
                ->documentFileName();
        diagnostic.line = 1;
        diagnostic.column = 1;
        diagnostic.severity = QStringLiteral("error");
        diagnostic.stage = QStringLiteral("compile");
        diagnostic.code = QStringLiteral("fixture");
        diagnostic.message =
            QStringLiteral("Wave Simulation fixture diagnostic");
        emit window.waveSimulationCoordinator
            ->diagnosticAvailable(diagnostic);
        NotificationItem item;
        const bool notificationPublished =
            window.notificationCenter->notificationByKey(
                QStringLiteral("wave-simulation:%1:1:1")
                    .arg(diagnostic.sourceFile),
                &item);
        expectBool("Wave Simulation diagnostic exposes source navigation",
                   notificationPublished
                       && item.actions.size() == 1
                       && item.actions.constFirst().id
                              == QStringLiteral(
                                  "waveSimulation.goToSource")
                       && window.waveSimulationNotificationLocations
                              .contains(item.id)
                       && window.notificationCenter->requestAction(
                           item.id,
                           QStringLiteral(
                               "waveSimulation.goToSource")),
                   true);
        window.notificationCenter->dismiss(item.id);
    }
    expectBool("user templates menu exists",
               userTemplatesMenu != nullptr,
               true);
    expectBool("user templates actions exist",
               openGlobalUserTemplatesAction
                   && openWorkspaceUserTemplatesAction
                   && reloadUserTemplatesAction,
               true);
    QMenu* rtlActionsMenu =
        window.findChild<QMenu*>(
            QStringLiteral("rtlActionsMenu"));
    QAction* rtlRenameAction =
        window.findChild<QAction*>(
            QStringLiteral(
                "rtlRenameAction"));
    QAction* rtlConnectionTransformAction =
        window.findChild<QAction*>(
            QStringLiteral(
                "rtlConnectionTransformAction"));
    QAction* connectInstancePairAction =
        window.findChild<QAction*>(
            QStringLiteral(
                "connectInstancePairAction"));
    QAction* propagateMultipleSignalsAction =
        window.findChild<QAction*>(
            QStringLiteral(
                "propagateMultipleSignalsAction"));
    QDockWidget* connectionsDock = window.semanticDocks
        ? window.semanticDocks->connectionsDock()
        : nullptr;
    QTabWidget* connectionsTabs = window.semanticDocks
        ? window.semanticDocks->connectionsTabs()
        : nullptr;
    QDockWidget* instancePairDock = window.semanticDocks
        ? window.semanticDocks->instancePairConnectionDock()
        : nullptr;
    QDockWidget* multiSignalDock = window.semanticDocks
        ? window.semanticDocks->multiSignalPropagationDock()
        : nullptr;
    RtlHighRiskEditPanelCoordinator*
        rtlHighRiskEdit =
            window.semanticDocks
            ? window.semanticDocks
                  ->rtlHighRiskEditPanelCoordinator()
            : nullptr;
    QDockWidget* rtlHighRiskEditDock =
        rtlHighRiskEdit
        ? rtlHighRiskEdit->dock()
        : nullptr;
    expectBool("RTL Actions are reachable from one registry-backed Tools submenu",
               rtlActionsMenu
                   && rtlActionsMenu->actions().size() == 4
                   && rtlRenameAction
                   && rtlConnectionTransformAction
                   && connectInstancePairAction
                   && propagateMultipleSignalsAction
                   && rtlRenameAction
                          ->property(
                              "actionId").toString()
                          == QString::fromLatin1(
                              ActionIds::RtlRename)
                   && rtlRenameAction->shortcut()
                          .toString(
                              QKeySequence::
                                  PortableText)
                          == QStringLiteral("Ctrl+R")
                   && rtlConnectionTransformAction
                          ->property(
                              "actionId").toString()
                          == QString::fromLatin1(
                              ActionIds::
                                  RtlConnectionTransform)
                   && connectInstancePairAction
                          ->property(
                              "actionId").toString()
                          == QString::fromLatin1(
                              ActionIds::
                                  RtlConnectInstancePair)
                   && propagateMultipleSignalsAction
                          ->property(
                              "actionId").toString()
                          == QString::fromLatin1(
                              ActionIds::
                                  RtlPropagateMultipleSignals),
               true);
    expectBool("Command Layer Repeat Last has the unified MainWindow fallback",
               window.commandLayerCoordinator
                   && window.commandLayerCoordinator
                          ->actionExecutionHost
                          .hasFallbackHost(),
               true);
    expectBool("Connections merges both RTL workflows into one managed page",
               connectionsDock
                   && instancePairDock == connectionsDock
                   && multiSignalDock == connectionsDock
                   && connectionsTabs
                   && connectionsTabs->count() == 2
                   && connectionsTabs->tabText(0)
                          == QStringLiteral("Instance Pair")
                   && connectionsTabs->tabText(1)
                          == QStringLiteral(
                              "Multi-Signal Propagation")
                   && window.findChild<QDockWidget*>(
                          QStringLiteral(
                              "InstancePairConnectionDock")) == nullptr
                   && window.findChild<QDockWidget*>(
                          QStringLiteral(
                              "MultiSignalPropagationDock")) == nullptr
                   && rtlHighRiskEdit
                   && rtlHighRiskEditDock
                   && window.findChildren<
                          RtlHighRiskEditPanel*>()
                          .size() == 1
                   && window.findChildren<
                          QDockWidget*>(
                              QStringLiteral(
                                  "rtlHighRiskEditDock"))
                          .size() == 1
                   && window.findChildren<
                          InstancePairConnectionPanel*>()
                          .size() == 1
                   && window.findChildren<
                          MultiSignalPropagationPanel*>()
                          .size() == 1
                   && window.panelLayoutController
                   && window.panelLayoutController
                          ->isBottomPanel(
                              rtlHighRiskEditDock)
                   && window.panelLayoutController
                          ->isBottomPanel(
                              connectionsDock),
               true);
    if (rtlRenameAction
        && rtlConnectionTransformAction
        && rtlHighRiskEditDock) {
        const int dockCountBefore =
            window.findChildren<QDockWidget*>(
                QStringLiteral(
                    "rtlHighRiskEditDock"))
                .size();
        rtlRenameAction->trigger();
        rtlRenameAction->trigger();
        rtlConnectionTransformAction->trigger();
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 50);
        expectBool(
            "repeated unified RTL actions never create duplicate pages",
            window.findChildren<QDockWidget*>(
                QStringLiteral(
                    "rtlHighRiskEditDock"))
                    .size()
                == dockCountBefore
                && !rtlHighRiskEditDock
                        ->isVisible(),
            true);
        expectBool(
            "unavailable unified RTL action exposes a failure reason",
            (!ActivityLogService::getInstance()->events().isEmpty() ? ActivityLogService::getInstance()->events().last().message : QString())
                       .contains(
                           QStringLiteral(
                               "workspace"),
                           Qt::CaseInsensitive),
            true);
    }
    if (rtlHighRiskEdit
        && rtlHighRiskEditDock
        && window.panelLayoutController) {
        window.panelLayoutController->closePanel(
            RtlHighRiskEditPanelCoordinator::
                panelId());
        QWidget* focusBefore =
            QApplication::focusWidget();
        const PanelLayoutState layoutBefore =
            window.panelLayoutController
                ->layoutState();
        QDockWidget* activityDock =
            window.dockForPanelId(
                QStringLiteral("activity"));
        const bool activityVisibleBefore =
            activityDock
            && activityDock->isVisible();
        RtlHighRiskEditPanelOutcome
            conflictOutcome;
        conflictOutcome.panelState =
            RtlHighRiskEditPanelState::Conflict;
        conflictOutcome.workflowState =
            RtlHighRiskEditWorkflowState::Failed;
        conflictOutcome.failure =
            RtlHighRiskEditWorkflowFailure::
                ExternalModification;
        conflictOutcome.transactionStatus =
            rtledit::TransactionStatus::Conflict;
        conflictOutcome.actionId =
            QString::fromLatin1(
                ActionIds::RtlRename);
        conflictOutcome.message =
            QStringLiteral(
                "RTL transaction conflict test");
        rtlHighRiskEdit->panel()
            ->presentOutcome(conflictOutcome);
        QMetaObject::invokeMethod(
            rtlHighRiskEdit,
            "stateChanged",
            Qt::DirectConnection,
            Q_ARG(
                RtlHighRiskEditPanelOutcome,
                conflictOutcome));
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 50);
        const PanelLayoutState layoutAfter =
            window.panelLayoutController
                ->layoutState();
        bool transactionNotificationFound =
            false;
        if (window.notificationCenter) {
            for (const NotificationItem& item :
                 window.notificationCenter
                     ->notifications()) {
                transactionNotificationFound =
                    transactionNotificationFound
                    || (item.topic
                            == NotificationTopic::
                                TransactionConflict
                        && item.source
                            == QStringLiteral(
                                "RtlHighRiskEdit")
                        && item.message
                               .contains(
                                   QStringLiteral(
                                       "conflict"),
                                   Qt::CaseInsensitive));
            }
        }
        expectBool(
            "RTL result updates preserve layout and focus",
            !rtlHighRiskEditDock->isVisible()
                && layoutAfter.bottomCollapsed
                    == layoutBefore
                           .bottomCollapsed
                && layoutAfter
                       .expandedBottomHeight
                    == layoutBefore
                           .expandedBottomHeight
                && QApplication::focusWidget()
                    == focusBefore,
            true);
        expectBool(
            "RTL transaction conflicts use non-blocking notifications",
            transactionNotificationFound
                && activityDock
                && activityDock->isVisible()
                    == activityVisibleBefore
                && !rtlHighRiskEditDock
                        ->isVisible(),
            true);
    }
    if (instancePairDock && multiSignalDock
        && window.semanticDocks) {
        window.panelLayoutController->closePanel(
            InstancePairConnectionCoordinator::panelId());
        window.panelLayoutController->closePanel(
            MultiSignalPropagationPanel::panelId());
        QWidget* focusBefore =
            QApplication::focusWidget();
        InstancePairConnectionAnalysis emptyAnalysis;
        window.semanticDocks
            ->instancePairConnectionCoordinator()
            ->presentAnalysis(emptyAnalysis);
        MultiSignalPropagationPanelInput emptyInput;
        window.semanticDocks
            ->multiSignalPropagationPanel()
            ->setInput(emptyInput);
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 50);
        expectBool("Connections data refresh does not expand or steal focus",
                   window.panelLayoutController
                       && !window.panelLayoutController->isPanelOpen(
                              QStringLiteral("connections"))
                       && QApplication::focusWidget()
                              == focusBefore,
                   true);
    }

    QTemporaryDir userTemplateEntryDir;
    expectBool("user template entry temp dir valid",
               userTemplateEntryDir.isValid(),
               true);
    const QString globalUserTemplatePath =
        userTemplateEntryDir.filePath(QStringLiteral("global_user_templates.json"));
    UserTemplateService::getInstance()->setGlobalTemplateFilePath(
        globalUserTemplatePath);
    UserTemplateService::getInstance()->setWorkspaceTemplateFilePath(QString());

    if (openWorkspaceUserTemplatesAction) {
        openWorkspaceUserTemplatesAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("workspace user templates requires workspace",
               (!ActivityLogService::getInstance()->events().isEmpty() ? ActivityLogService::getInstance()->events().last().message : QString()).contains(
                       QStringLiteral("Open a workspace")),
               true);

    if (openGlobalUserTemplatesAction) {
        openGlobalUserTemplatesAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
    QFile globalUserTemplateFile(globalUserTemplatePath);
    QString globalUserTemplateText;
    if (globalUserTemplateFile.open(QIODevice::ReadOnly | QFile::Text)) {
        globalUserTemplateText =
            QTextStream(&globalUserTemplateFile).readAll();
        globalUserTemplateFile.close();
    }
    const DocumentSnapshot globalUserTemplateDocument =
        window.tabManager->getCurrentDocument();
    expectBool("open global user templates creates skeleton",
               QFileInfo(globalUserTemplatePath).isFile()
                   && globalUserTemplateText
                       == QStringLiteral("{\n  \"templates\": []\n}\n"),
               true);
    expectBool("open global user templates opens tab",
               QDir::cleanPath(QDir::fromNativeSeparators(
                   QFileInfo(globalUserTemplateDocument.fileName)
                       .absoluteFilePath()))
                   == QDir::cleanPath(QDir::fromNativeSeparators(
                       QFileInfo(globalUserTemplatePath).absoluteFilePath())),
               true);

    expectBool("write reloadable user template JSON",
               writeTextFile(globalUserTemplatePath,
                             QStringLiteral(
                                 "[{\"command\":\";;menuut\","
                                 "\"description\":\"menu template\","
                                 "\"body\":\"logic menu_user;\"}]\n")),
               true);
    QStringList userTemplateReloadMessages;
    QMetaObject::Connection userTemplateReloadStatusConnection;
    {
        userTemplateReloadStatusConnection = QObject::connect(
            ActivityLogService::getInstance(),
            &ActivityLogService::eventAppended,
            &window,
            [&](const ActivityLogEvent& event) {
                userTemplateReloadMessages.append(event.message);
            });
    }
    if (reloadUserTemplatesAction) {
        reloadUserTemplatesAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
    if (userTemplateReloadStatusConnection)
        QObject::disconnect(userTemplateReloadStatusConnection);
    const bool sawSuccessfulTemplateReload = std::any_of(
        userTemplateReloadMessages.cbegin(),
        userTemplateReloadMessages.cend(),
        [](const QString& message) {
            return message.contains(QStringLiteral("loaded: 1"))
                && message.contains(QStringLiteral("ignored: 0"));
        });
    expectBool("reload user templates status has loaded count",
               sawSuccessfulTemplateReload,
               true);
    expectBool("user template menu does not create ;cmd",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral(";menuut "))
                    .matched,
               true);
    expectBool("user template menu does not create Command Layer command",
               findCommandLayerCommand(QStringLiteral("menuut")) == nullptr,
               true);
    expectBool("user template menu does not change Global Control",
               GlobalControlService()
                   .query(QStringLiteral(";;menuut"))
                   .isEmpty(),
               true);

    expectBool("write invalid-json user template file",
               writeTextFile(globalUserTemplatePath,
                             QStringLiteral("{ invalid json")),
               true);
    const UserTemplateLoadReport invalidJsonReport =
        UserTemplateService::getInstance()->reload();
    expectBool("reload report text includes invalid JSON",
               !invalidJsonReport.valid
                   && window.userTemplateIssueReportText(invalidJsonReport)
                          .contains(QStringLiteral("Invalid user template JSON")),
               true);
    expectBool("invalid reload leaves built-in template intact",
               CodeTemplateService::getInstance()
                       ->templateForCommand(QStringLiteral(";;l"),
                                            QStringLiteral("clk"))
                       .insertText
                   == QStringLiteral("logic clk;"),
               true);

    expectBool("write invalid user template issue file",
               writeTextFile(globalUserTemplatePath,
                             QStringLiteral(
                                 "[\n"
                                 "  {\"command\":\";;badslot\","
                                 "\"body\":\"logic x;\","
                                 "\"slots\":[{\"name\":\"x\","
                                 "\"start\":99,\"length\":1}]},\n"
                                 "  {\"command\":\";;pk\","
                                 "\"body\":\"reserved;\"}\n"
                                 "]\n")),
               true);
    QStringList invalidUserTemplateReloadMessages;
    QMetaObject::Connection invalidUserTemplateReloadStatusConnection;
    {
        invalidUserTemplateReloadStatusConnection = QObject::connect(
            ActivityLogService::getInstance(),
            &ActivityLogService::eventAppended,
            &window,
            [&](const ActivityLogEvent& event) {
                invalidUserTemplateReloadMessages.append(event.message);
            });
    }
    acceptNextMessageBoxOk();
    if (reloadUserTemplatesAction) {
        reloadUserTemplatesAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
    }
    if (invalidUserTemplateReloadStatusConnection)
        QObject::disconnect(invalidUserTemplateReloadStatusConnection);
    const bool sawInvalidTemplateReload = std::any_of(
        invalidUserTemplateReloadMessages.cbegin(),
        invalidUserTemplateReloadMessages.cend(),
        [](const QString& message) {
            return message.contains(QStringLiteral("loaded: 0"))
                && message.contains(QStringLiteral("ignored: 2"));
        });
    expectBool("reload user templates status has ignored count",
               sawInvalidTemplateReload,
               true);

    QTemporaryDir userTemplateWorkspaceDir;
    expectBool("user template workspace temp dir valid",
               userTemplateWorkspaceDir.isValid(),
               true);
    const QString productionSettingsPath =
        window.settingsCenterService
        ? window.settingsCenterService->workspaceSettingsFilePath(
              userTemplateWorkspaceDir.path())
        : QString();
    expectBool(
        "write production Settings Center workspace fixture",
        !productionSettingsPath.isEmpty()
            && QDir().mkpath(
                QFileInfo(productionSettingsPath).absolutePath())
            && writeTextFile(
                productionSettingsPath,
                QStringLiteral(
                    "{\n"
                    "  \"schema\": \"ZeroSlack.SettingsCenter\",\n"
                    "  \"version\": 1,\n"
                    "  \"values\": {\n"
                    "    \"font.sizePt\": 19\n"
                    "  }\n"
                    "}\n")),
        true);
    if (userTemplateWorkspaceDir.isValid()) {
        QDockWidget* passiveActivityDock =
            window.findChild<QDockWidget*>(
                QStringLiteral("activityDock"));
        if (passiveActivityDock)
            passiveActivityDock->hide();
        expectBool("open temp workspace for user templates",
                   window.workspaceManager->openWorkspace(
                       userTemplateWorkspaceDir.path()),
                   true);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        expectBool("workspace open does not expand Activity",
                   passiveActivityDock
                       && !passiveActivityDock->isVisible(),
                   true);
        expectBool("workspace activation loads Settings Center layer",
                   window.settingsCenterPanel
                       && QDir::cleanPath(
                              window.settingsCenterPanel
                                  ->workspaceRoot())
                              == QDir::cleanPath(
                                  userTemplateWorkspaceDir.path())
                       && window.settingsCenterPanel->effectiveValue(
                              QStringLiteral("font.sizePt")).toInt()
                              == 19,
                   true);
        expectBool("effective appearance drives the runtime backend",
                   window.editorAppearanceSettings
                       && window.editorAppearanceSettings
                              ->options().fontSizePt == 19,
                   true);

        window.settingsCenterPanel->setScope(
            SettingsCenterScope::Workspace);
        const QVariantMap globalLayerBeforeWorkspaceApply =
            window.settingsCenterPanel->snapshot().globalValues;
        auto* productionSizeEditor =
            qobject_cast<QSpinBox*>(
                window.settingsCenterPanel->fieldEditor(
                    QStringLiteral("font.sizePt")));
        if (productionSizeEditor)
            productionSizeEditor->setValue(21);
        window.settingsCenterPanel->applyCurrentScope();
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 50);
        const SettingsCenterSnapshot persistedSettings =
            window.settingsCenterService->load(
                userTemplateWorkspaceDir.path());
        expectBool("Settings Center apply refreshes runtime backend",
                   productionSizeEditor
                       && window.editorAppearanceSettings
                       && window.editorAppearanceSettings
                              ->options().fontSizePt == 21
                       && persistedSettings.workspaceValues.value(
                                  QStringLiteral(
                                      "font.sizePt")).toInt()
                              == 21,
                   true);
        expectBool("workspace effective values do not rewrite global layer",
                   persistedSettings.globalValues
                       == globalLayerBeforeWorkspaceApply,
                   true);
    }
    if (openWorkspaceUserTemplatesAction) {
        openWorkspaceUserTemplatesAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
    const QString workspaceUserTemplatePath =
        QDir(userTemplateWorkspaceDir.path())
            .absoluteFilePath(
                QStringLiteral(".zeroslack/user_templates.json"));
    QFile workspaceUserTemplateFile(workspaceUserTemplatePath);
    QString workspaceUserTemplateText;
    if (workspaceUserTemplateFile.open(QIODevice::ReadOnly | QFile::Text)) {
        workspaceUserTemplateText =
            QTextStream(&workspaceUserTemplateFile).readAll();
        workspaceUserTemplateFile.close();
    }
    const DocumentSnapshot workspaceUserTemplateDocument =
        window.tabManager->getCurrentDocument();
    expectBool("open workspace user templates creates skeleton",
               QFileInfo(workspaceUserTemplatePath).isFile()
                   && workspaceUserTemplateText
                       == QStringLiteral("{\n  \"templates\": []\n}\n"),
               true);
    expectBool("open workspace user templates opens tab",
               QDir::cleanPath(QDir::fromNativeSeparators(
                   QFileInfo(workspaceUserTemplateDocument.fileName)
                       .absoluteFilePath()))
                   == QDir::cleanPath(QDir::fromNativeSeparators(
                       QFileInfo(workspaceUserTemplatePath).absoluteFilePath())),
               true);
    window.workspaceManager->closeWorkspace();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("workspace close restores global effective settings",
               window.settingsCenterPanel
                   && window.settingsCenterPanel
                          ->workspaceRoot().isEmpty()
                   && window.editorAppearanceSettings
                   && window.editorAppearanceSettings
                          ->options().fontSizePt
                          == window.settingsCenterPanel
                                 ->effectiveValue(
                                     QStringLiteral(
                                         "font.sizePt")).toInt(),
               true);
    workspaceFilesScanned = false;
    workspaceSymbolsDone = false;

    QAction* viewFoldShelfAction =
        window.findChild<QAction*>(QStringLiteral("viewFoldShelfAction"));
    expectBool("view menu has fold shelf action",
               viewFoldShelfAction != nullptr,
               true);
    if (window.panelLayoutController)
        window.panelLayoutController->closePanel(
            QStringLiteral("foldShelf"));
    if (viewFoldShelfAction) {
        viewFoldShelfAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("view menu reopens fold shelf",
               foldShelfDock
                   && window.panelLayoutController
                   && window.panelLayoutController->isPanelOpen(
                          QStringLiteral("foldShelf")),
               true);
    if (window.panelLayoutController)
        window.panelLayoutController->closePanel(
            QStringLiteral("foldShelf"));

    QDockWidget* activityDock =
        window.findChild<QDockWidget*>(QStringLiteral("activityDock"));
    QAction* viewActivityAction =
        window.findChild<QAction*>(QStringLiteral("viewActivityAction"));
    expectBool("view menu has activity action",
               activityDock && viewActivityAction,
               true);
    if (window.panelLayoutController)
        window.panelLayoutController->closePanel(
            QStringLiteral("activity"));
    if (viewActivityAction) {
        viewActivityAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("view menu reopens activity panel",
               activityDock
                   && window.panelLayoutController
                   && window.panelLayoutController->isPanelOpen(
                          QStringLiteral("activity")),
               true);

    QDockWidget* signalKernelGraphDock =
        window.findChild<QDockWidget*>(QStringLiteral("signalKernelGraphDock"));
    QAction* viewSignalKernelGraphAction =
        window.findChild<QAction*>(
            QStringLiteral("viewSignalKernelGraphAction"));
    QDockWidget* wavePreviewDock =
        window.findChild<QDockWidget*>(QStringLiteral("wavePreviewDock"));
    QAction* viewWavePreviewAction =
        window.findChild<QAction*>(QStringLiteral("viewWavePreviewAction"));
    expectBool("legacy kernel and Wave bottom entries are absent",
               !signalKernelGraphDock
                   && !viewSignalKernelGraphAction
                   && !wavePreviewDock
                   && !viewWavePreviewAction,
               true);

    QAction* resetPanelLayoutAction =
        window.findChild<QAction*>(QStringLiteral("resetPanelLayoutAction"));
    expectBool("view menu has reset layout action",
               resetPanelLayoutAction != nullptr,
               true);
    if (window.navigationPane && window.navigationPane->dock())
        window.navigationPane->dock()->hide();
    if (window.panelLayoutController)
        window.panelLayoutController->setBottomCollapsed(true);
    if (resetPanelLayoutAction) {
        resetPanelLayoutAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("reset panel layout reopens navigation",
               window.navigationPane
                   && window.navigationPane->dock()
                   && window.navigationPane->dock()->isVisible(),
               true);
    expectBool("reset panel layout restores only the default drawer page",
               window.panelLayoutController
                   && window.panelLayoutController->isPanelOpen(
                          QStringLiteral("problems"))
                   && !window.panelLayoutController->isPanelOpen(
                          QStringLiteral("activity"))
                   && !signalKernelGraphDock
                   && !wavePreviewDock,
               true);

    QAction* collapseBottomAction =
        window.findChild<QAction*>(
            QStringLiteral("toggleBottomPanelCollapsedAction"));
    QAction* pinBottomAction =
        window.findChild<QAction*>(
            QStringLiteral("pinActiveBottomPanelAction"));
    QAction* closeBottomAction =
        window.findChild<QAction*>(
            QStringLiteral("closeActiveBottomPanelAction"));
    PanelLayoutController* drawerController =
        window.panelLayoutController.get();
    QDockWidget* drawerDock = drawerController
        ? drawerController->drawerDock()
        : nullptr;
    QWidget* drawerBar = drawerController
        ? drawerController->buttonBar()
        : nullptr;
    const QStringList expectedDrawerIds = {
        QStringLiteral("problems"),
        QStringLiteral("scopedSearch"),
        QStringLiteral("activity"),
        QStringLiteral("rtlHighRiskEdit"),
        QStringLiteral("connections"),
        QStringLiteral("foldShelf"),
    };
    const QStringList expectedDrawerLabels = {
        QStringLiteral("Problems"),
        QStringLiteral("Search"),
        QStringLiteral("Activity"),
        QStringLiteral("Change Preview"),
        QStringLiteral("Connections"),
        QStringLiteral("Shelf"),
    };
    bool drawerButtonsComplete = drawerController
        && drawerController->bottomPanelIds() == expectedDrawerIds;
    for (int index = 0; index < expectedDrawerIds.size(); ++index) {
        QToolButton* button = drawerController
            ? drawerController->buttonForPanel(
                  expectedDrawerIds.at(index))
            : nullptr;
        drawerButtonsComplete = drawerButtonsComplete
            && button
            && button->text() == expectedDrawerLabels.at(index)
            && button->accessibleName()
                   == expectedDrawerLabels.at(index)
            && !button->icon().isNull()
            && !button->toolTip().isEmpty();
    }
    expectBool("bottom tool drawer has registered accessible panel actions",
               drawerButtonsComplete,
               true);
    expectBool("drawer actions exist without retired pin and close menu entries",
               drawerController
                   && drawerDock
                   && drawerBar
                   && collapseBottomAction
                   && !pinBottomAction
                   && !closeBottomAction,
               true);
    if (collapseBottomAction
        && drawerController
        && !drawerController->isBottomCollapsed()) {
        collapseBottomAction->trigger();
        for (int iteration = 0; iteration < 3; ++iteration)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("bottom panel collapses while retaining its button bar",
               drawerController
                   && drawerController->isBottomCollapsed()
                   && drawerDock
                   && drawerDock->isVisible()
                   && drawerBar
                   && drawerBar->isVisible()
                   && drawerController->drawerContent()
                   && !drawerController->drawerContent()->isVisible(),
               true);
    expectBool("collapsed drawer main-window screenshot saved",
               saveEditorLayoutScreenshot(
                   window,
                   QStringLiteral("bottom_tab_only.png")),
               true);
    const QString activeBeforePassiveUpdate = drawerController
        ? drawerController->activeBottomPanelId()
        : QString();
    QWidget* focusBeforePassiveUpdate =
        QApplication::focusWidget();
    const int collapsedCentralHeight =
        window.centralWidget()
            ? window.centralWidget()->height()
            : -1;
    ActivityLogService::getInstance()->append(
        QStringLiteral("GUI"),
        ActivityLogLevel::Info,
        QStringLiteral("Passive panel update"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("background Activity preserves drawer state focus and height",
               drawerController
                   && drawerController->activeBottomPanelId()
                          == activeBeforePassiveUpdate
                   && QApplication::focusWidget()
                          == focusBeforePassiveUpdate
                   && window.centralWidget()
                   && window.centralWidget()->height()
                          == collapsedCentralHeight
                   && drawerController->isBottomCollapsed(),
               true);
    ActivityLogService::getInstance()->append(
        QStringLiteral("GUI"),
        ActivityLogLevel::Error,
        QStringLiteral("Passive failure badge probe"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("Activity failure badge is real and remains passive",
               drawerController
                   && drawerController->panelBadgeText(
                          QStringLiteral("activity"))
                          == QString::number(ActivityLogService::getInstance()->unreadCount())
                   && drawerController->panelBadgeTone(
                          QStringLiteral("activity"))
                          == QStringLiteral("error")
                   && drawerController->isBottomCollapsed()
                   && QApplication::focusWidget()
                          == focusBeforePassiveUpdate,
               true);
    ActivityLogService::getInstance()->clear();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expectBool("clearing Activity removes a stale failure badge",
               drawerController
                   && drawerController->panelBadgeText(
                          QStringLiteral("activity")).isEmpty(),
               true);
    if (collapseBottomAction) {
        collapseBottomAction->trigger();
        for (int iteration = 0; iteration < 3; ++iteration)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("Ctrl+J action restores the last drawer page and height",
               drawerController
                   && !drawerController->isBottomCollapsed()
                   && drawerController->activeBottomPanelId()
                          == activeBeforePassiveUpdate
                   && drawerController->drawerContent()
                   && drawerController->drawerContent()->isVisible()
                   && drawerController->drawerContent()->height()
                          >= PanelLayoutController::kMinimumContentHeight,
               true);

    if (drawerController) {
        drawerController->restorePanel(
            QStringLiteral("connections"));
        drawerController->setPanelHeight(
            QStringLiteral("connections"), 333);
    }
    QAction* newFileAction = window.findChild<QAction*>(QStringLiteral("new_file"));
    const int editorCountBeforeNewAction = window.tabManager->editorCount();
    if (newFileAction) {
        newFileAction->trigger();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    expectBool("file action routes through coordinator",
               newFileAction
                   && window.tabManager->editorCount()
                       == editorCountBeforeNewAction + 1,
               true);

    QAction* splitEditorLeftAction =
        window.findChild<QAction*>(
            QStringLiteral("splitEditorLeftAction"));
    QAction* splitEditorRightAction =
        window.findChild<QAction*>(
            QStringLiteral("splitEditorRightAction"));
    QAction* splitEditorAboveAction =
        window.findChild<QAction*>(
            QStringLiteral("splitEditorAboveAction"));
    QAction* splitEditorBelowAction =
        window.findChild<QAction*>(
            QStringLiteral("splitEditorBelowAction"));
    QAction* maximizeEditorSplitAction =
        window.findChild<QAction*>(
            QStringLiteral("maximizeEditorSplitAction"));
    QAction* equalizeEditorSplitsAction =
        window.findChild<QAction*>(
            QStringLiteral("equalizeEditorSplitsAction"));
    QAction* mergeEditorSplitAction =
        window.findChild<QAction*>(
            QStringLiteral("mergeEditorSplitAction"));
    QAction* reopenClosedTabAction =
        window.findChild<QAction*>(
            QStringLiteral("reopenClosedTabAction"));
    expectBool("editor layout menu exposes split lifecycle actions",
               splitEditorLeftAction
                   && splitEditorRightAction
                   && splitEditorAboveAction
                   && splitEditorBelowAction
                   && maximizeEditorSplitAction
                   && equalizeEditorSplitsAction
                   && mergeEditorSplitAction
                   && reopenClosedTabAction,
               true);

    MyCodeEditor* originalSplitView =
        window.tabManager->getCurrentEditor();
    SharedDocument* originalSplitDocument =
        window.tabManager->sharedDocumentForEditor(
            originalSplitView);
    const int splitCountBeforeAction =
        window.tabManager->splitCount();
    const int editorCountBeforeSplit =
        window.tabManager->editorCount();
    if (splitEditorRightAction)
        splitEditorRightAction->trigger();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    MyCodeEditor* duplicatedSplitView =
        window.tabManager->getCurrentEditor();
    expectBool("editor layout action creates shared right split",
               originalSplitView
                   && duplicatedSplitView
                   && duplicatedSplitView
                          != originalSplitView
                   && window.tabManager->splitCount()
                          == splitCountBeforeAction + 1
                   && window.tabManager->editorCount()
                          == editorCountBeforeSplit + 1
                   && originalSplitDocument
                          == window.tabManager
                                 ->sharedDocumentForEditor(
                                     duplicatedSplitView),
               true);

    expectBool("real main-window editor boundary screenshot saved",
               saveEditorLayoutScreenshot(
                   window,
                   QStringLiteral("editor_left_boundary.png")),
               true);
    EditorSplitController* splitController =
        window.tabManager->editorSplitController();
    QTabWidget* previewTarget =
        splitController && duplicatedSplitView
            ? splitController->groupForPage(duplicatedSplitView)
            : nullptr;
    QWidget* previewHost =
        splitController ? splitController->host() : nullptr;
    bool directionalPreviewsSaved =
        previewTarget && previewHost;
    if (directionalPreviewsSaved) {
        EditorDropPreviewOverlay preview(previewHost);
        const QPair<EditorSplitDirection, QString> previews[] = {
            {EditorSplitDirection::Left,
             QStringLiteral("split_preview_left.png")},
            {EditorSplitDirection::Right,
             QStringLiteral("split_preview_right.png")},
            {EditorSplitDirection::Above,
             QStringLiteral("split_preview_above.png")},
            {EditorSplitDirection::Below,
             QStringLiteral("split_preview_below.png")},
        };
        for (const auto& item : previews) {
            preview.showPreview(previewTarget, item.first);
            QCoreApplication::processEvents(
                QEventLoop::AllEvents, 50);
            directionalPreviewsSaved =
                directionalPreviewsSaved
                && preview.isVisible()
                && preview.target() == previewTarget
                && saveEditorLayoutScreenshot(window, item.second);
        }
        preview.clearPreview();
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 20);
    }
    expectBool("four directional split-preview screenshots saved",
               directionalPreviewsSaved,
               true);

    if (maximizeEditorSplitAction)
        maximizeEditorSplitAction->trigger();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    expectBool("editor layout action maximizes current split",
               window.tabManager->editorSplitController()
                   && window.tabManager
                          ->editorSplitController()
                          ->isGroupMaximized(),
               true);
    if (maximizeEditorSplitAction)
        maximizeEditorSplitAction->trigger();
    if (equalizeEditorSplitsAction)
        equalizeEditorSplitsAction->trigger();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    expectBool("editor split exits maximize before equalize",
               window.tabManager->editorSplitController()
                   && !window.tabManager
                           ->editorSplitController()
                           ->isGroupMaximized(),
               true);

    QString layoutArtifactRoot =
        qEnvironmentVariable(
            "ZEROSLACK_TEST_ARTIFACT_DIR");
    if (layoutArtifactRoot.isEmpty()) {
        layoutArtifactRoot =
            QDir::temp().absoluteFilePath(
                QStringLiteral("zeroslack-gui-smoke"));
    }
    const bool layoutArtifactDirectoryReady =
        QDir().mkpath(layoutArtifactRoot);
    const QString layoutScreenshotPath =
        QDir(layoutArtifactRoot).absoluteFilePath(
            QStringLiteral(
                "ui_shell_shared_document_split.png"));
    expectBool("UI shell shared-document split screenshot saved",
               layoutArtifactDirectoryReady
                   && window.grab().save(
                       layoutScreenshotPath),
               true);

    if (mergeEditorSplitAction)
        mergeEditorSplitAction->trigger();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    expectBool("editor layout action merges current split",
               window.tabManager->splitCount()
                   == splitCountBeforeAction
                   && window.tabManager->editorCount()
                          == editorCountBeforeSplit + 1,
               true);
    if (duplicatedSplitView) {
        QTabWidget* duplicatedViewGroup =
            window.tabManager
                ->editorSplitController()
                ->groupForPage(duplicatedSplitView);
        if (duplicatedViewGroup) {
            window.tabManager->closeTab(
                duplicatedViewGroup->indexOf(
                    duplicatedSplitView));
        }
    }
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    expectBool("split integration restores original view count",
               window.tabManager->editorCount()
                   == editorCountBeforeSplit,
               true);

    ContextWorkspaceController* themeContextController =
        window.contextWorkspaceController.get();
    ContextPeekHost* themePeek =
        themeContextController
            ? themeContextController->peekHost()
            : nullptr;
    ContextDockHost* themeDock =
        themeContextController
            ? themeContextController->dockHost()
            : nullptr;
    QDockWidget* themeContextDockWidget =
        themeContextController
            ? themeContextController->dockWidget()
            : nullptr;
    if (themeContextController)
        themeContextController->clearResources();

    MyCodeEditor* themeMainEditor =
        window.tabManager->getCurrentEditor();
    EditorSplitController* themeSplitController =
        window.tabManager->editorSplitController();
    QTabWidget* themeMainGroup =
        themeSplitController && themeMainEditor
            ? themeSplitController->groupForPage(themeMainEditor)
            : nullptr;
    if (themeMainGroup && themeMainEditor) {
        themeMainGroup->setCurrentWidget(themeMainEditor);
        themeSplitController->setActiveGroup(themeMainGroup);
    }
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);

    SharedDocument* themeMainDocument =
        window.tabManager->sharedDocumentForEditor(
            themeMainEditor);
    QTextDocument* themeTextDocument =
        themeMainDocument
            ? themeMainDocument->textDocument()
            : nullptr;
    if (themeMainEditor && themeTextDocument
        && themeTextDocument->characterCount() <= 8
        && themeMainDocument->fileName().isEmpty()) {
        themeMainEditor->setPlainText(
            QStringLiteral("module theme_context_preview;\n"
                           "  logic selected_signal;\n"
                           "endmodule\n"));
    }
    if (themeMainEditor && themeTextDocument) {
        const int lastPosition =
            qMax(0, themeTextDocument->characterCount() - 1);
        QTextCursor selection(themeTextDocument);
        selection.setPosition(qMin(2, lastPosition));
        selection.setPosition(qMin(8, lastPosition),
                              QTextCursor::KeepAnchor);
        themeMainEditor->setTextCursor(selection);
        if (QScrollBar* scrollBar =
                themeMainEditor->verticalScrollBar()) {
            scrollBar->setValue(scrollBar->maximum() / 2);
        }
    }

    if (window.settingsCenterPanel) {
        window.settingsCenterPanel->setScope(
            SettingsCenterScope::Global);
        window.settingsCenterPanel->reload();
    }
    QComboBox* themeSettingEditor =
        window.settingsCenterPanel
        ? qobject_cast<QComboBox*>(
              window.settingsCenterPanel->fieldEditor(
                  QStringLiteral("appearance.theme")))
        : nullptr;
    const auto selectThemeThroughSettings =
        [](QComboBox* editor, const QString& themeName) {
            if (!editor || !editor->isEnabled())
                return false;
            const int targetIndex = editor->findText(themeName);
            if (targetIndex < 0)
                return false;
            if (editor->currentIndex() == targetIndex) {
                const int alternateIndex = targetIndex == 0 ? 1 : 0;
                if (alternateIndex >= 0
                    && alternateIndex < editor->count()) {
                    editor->setCurrentIndex(alternateIndex);
                }
            }
            editor->setCurrentIndex(targetIndex);
            return editor->currentIndex() == targetIndex;
        };
    const bool lightThemeSelected =
        selectThemeThroughSettings(
            themeSettingEditor,
            QStringLiteral("Light"));
    for (int iteration = 0; iteration < 3; ++iteration) {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 50);
    }

    const int themeSplitCountBefore =
        window.tabManager->splitCount();
    const int themeEditorCountBefore =
        window.tabManager->editorCount();
    const QList<QTabWidget*> themeGroupsBefore =
        themeSplitController
            ? themeSplitController->groups()
            : QList<QTabWidget*>();
    const int themeSharedViewCountBefore =
        themeMainDocument
            ? themeMainDocument->viewCount()
            : -1;
    const QString themeFileBefore =
        themeMainDocument
            ? themeMainDocument->fileName()
            : QString();
    const int themeCursorBefore =
        themeMainEditor
            ? themeMainEditor->textCursor().position()
            : -1;
    const int themeAnchorBefore =
        themeMainEditor
            ? themeMainEditor->textCursor().anchor()
            : -1;
    const int themeScrollBefore =
        themeMainEditor && themeMainEditor->verticalScrollBar()
            ? themeMainEditor->verticalScrollBar()->value()
            : -1;

    EditorLocation themeContextLocation;
    if (themeMainDocument) {
        themeContextLocation.documentId =
            themeMainDocument->documentId();
        themeContextLocation.filePath =
            themeMainDocument->fileName();
    }
    themeContextLocation.line = 1;
    themeContextLocation.column = 1;
    themeContextLocation.symbolKey =
        QStringLiteral("theme.context.preview");
    const ContextResource themeContextResource =
        TemporaryEditorContextProvider::resourceForLocation(
            themeContextLocation,
            window.workspaceManager
                ? window.workspaceManager->getWorkspacePath()
                : QString());
    const bool themeContextOpened =
        themeContextController
        && themeContextController->openResource(
            themeContextResource,
            ContextPlacement{ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::Global});
    for (int iteration = 0; iteration < 3; ++iteration) {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 50);
    }
    auto* themeContextView =
        themePeek
        ? qobject_cast<TemporaryEditorContextView*>(
              themePeek->view())
        : nullptr;
    QPointer<QWidget> themeContextViewIdentity =
        themeContextView;
    const QColor lightWindowSurface =
        window.palette().color(QPalette::Window);
    const QColor lightPeekSurface =
        themePeek
        ? themePeek->palette().color(QPalette::Window)
        : QColor();
    expectBool("Light theme Context Peek screenshot saved",
               lightThemeSelected
                   && ApplicationThemeManager::instance().mode()
                          == ThemeMode::Light
                   && themeContextOpened
                   && themePeek
                   && themePeek->isVisible()
                   && themeContextView
                   && themeContextView->searchField()
                   && themeContextView->searchField()->isVisible()
                   && themeContextView->editor()
                   && themeContextView->editor()->document()
                          == themeTextDocument
                   && themeContextController->rail()->isVisible()
                   && saveEditorLayoutScreenshot(
                       window,
                       QStringLiteral(
                           "theme_light_context_peek.png")),
               true);

    const bool themeContextPinned =
        themeContextController
        && themeContextController->pinPeek();
    for (int iteration = 0; iteration < 3; ++iteration) {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 50);
    }
    const QColor lightPinnedSurface =
        themeContextDockWidget
        ? themeContextDockWidget->palette().color(
              QPalette::Window)
        : QColor();
    const QImage lightPinnedImage =
        window.grab().toImage();
    expectBool("Light theme pinned Context Dock screenshot saved",
               themeContextPinned
                   && themeDock
                   && themeDock->resourceCount() == 1
                   && themeDock->viewForResource(
                          themeContextResource.stableKey())
                          == themeContextViewIdentity
                   && themeContextDockWidget
                   && themeContextDockWidget->isVisible()
                   && themePeek
                   && !themePeek->hasResource()
                   && saveEditorLayoutScreenshot(
                       window,
                       QStringLiteral(
                           "theme_light_context_pinned.png")),
               true);

    const bool darkThemeSelected =
        selectThemeThroughSettings(
            themeSettingEditor,
            QStringLiteral("Dark"));
    for (int iteration = 0; iteration < 3; ++iteration) {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 50);
    }
    const QColor darkWindowSurface =
        window.palette().color(QPalette::Window);
    const QColor darkPinnedSurface =
        themeContextDockWidget
        ? themeContextDockWidget->palette().color(
              QPalette::Window)
        : QColor();
    const QImage darkPinnedImage =
        window.grab().toImage();
    expectBool("Dark theme pinned Context Dock screenshot saved",
               darkThemeSelected
                   && ApplicationThemeManager::instance().mode()
                          == ThemeMode::Dark
                   && themeContextDockWidget
                   && themeContextDockWidget->isVisible()
                   && themeContextViewIdentity
                   && lightWindowSurface != darkWindowSurface
                   && lightPinnedSurface != darkPinnedSurface
                   && !differentPixelBounds(
                           lightPinnedImage,
                           darkPinnedImage)
                           .isNull()
                   && saveEditorLayoutScreenshot(
                       window,
                       QStringLiteral(
                           "theme_dark_context_pinned.png")),
               true);

    expectBool("Context workspace preserves the active document and split model",
               window.tabManager->getCurrentEditor()
                       == themeMainEditor
                   && window.tabManager->sharedDocumentForEditor(
                          themeMainEditor)
                          == themeMainDocument
                   && themeMainDocument
                   && themeMainDocument->textDocument()
                          == themeTextDocument
                   && themeMainDocument->fileName()
                          == themeFileBefore
                   && themeMainEditor->textCursor().position()
                          == themeCursorBefore
                   && themeMainEditor->textCursor().anchor()
                          == themeAnchorBefore
                   && themeMainEditor->verticalScrollBar()->value()
                          == themeScrollBefore
                   && window.tabManager->splitCount()
                          == themeSplitCountBefore
                   && window.tabManager->editorCount()
                          == themeEditorCountBefore
                   && themeSplitController
                   && themeSplitController->groups()
                          == themeGroupsBefore
                   && themeMainDocument->viewCount()
                          == themeSharedViewCountBefore + 1,
               true);

    const bool lightThemeRestored =
        selectThemeThroughSettings(
            themeSettingEditor,
            QStringLiteral("Light"));
    for (int iteration = 0; iteration < 3; ++iteration) {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 50);
    }
    expectBool("Light-Dark-Light preserves the pinned Context view",
               lightThemeRestored
                   && ApplicationThemeManager::instance().mode()
                          == ThemeMode::Light
                   && window.palette().color(QPalette::Window)
                          == lightWindowSurface
                   && themeContextDockWidget
                   && themeContextDockWidget->palette().color(
                          QPalette::Window)
                          == lightPinnedSurface
                   && themeDock
                   && themeDock->viewForResource(
                          themeContextResource.stableKey())
                          == themeContextViewIdentity,
               true);

    const bool themeContextUnpinned =
        themeContextController
        && themeContextController->unpinResource(
            themeContextResource.stableKey());
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    expectBool("unpinning moves the exact Context view back to Peek",
               themeContextUnpinned
                   && themePeek
                   && themePeek->view()
                          == themeContextViewIdentity
                   && themeDock
                   && themeDock->resourceCount() == 0,
               true);
    if (themeContextController)
        themeContextController->closePeek();
    QCoreApplication::sendPostedEvents(
        nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    expectBool("closing Context Peek restores shared-view and split baselines",
               themeContextViewIdentity.isNull()
                   && themeMainDocument
                   && themeMainDocument->viewCount()
                          == themeSharedViewCountBefore
                   && window.tabManager->splitCount()
                          == themeSplitCountBefore
                   && window.tabManager->editorCount()
                          == themeEditorCountBefore
                   && themeSplitController
                   && themeSplitController->groups()
                          == themeGroupsBefore,
               true);
    // Retain the backend regression only for compatibility builds that still
    // instantiate a legacy semantic-dock coordinator. Production exercises
    // Wave through LiveInsightToolPage below.
    if (window.semanticDocks
        && window.semanticDocks->wavePreviewPanelCoordinator()) {
    MyCodeEditor* waveEditor = window.tabManager->getCurrentEditor();
    QTemporaryDir wavePreviewNavDir;
    expectBool("wave preview nav temp dir valid",
               wavePreviewNavDir.isValid(),
               true);
    const QString wavePreviewNavPath =
        wavePreviewNavDir.isValid()
            ? QDir::toNativeSeparators(
                  wavePreviewNavDir.filePath(QStringLiteral("wave_ui.sv")))
            : QString();
    if (waveEditor && window.tabManager->getDocumentModel()
        && !wavePreviewNavPath.isEmpty()) {
        window.tabManager->getDocumentModel()->setDocumentFileName(
            waveEditor,
            wavePreviewNavPath);
    }
    if (waveEditor) {
        waveEditor->setPlainText(
            QStringLiteral("module wave_ui;\n"
                           "logic clk;\n"
                           "input logic [7:0] data;\n"
                           "logic [7:0] q;\n"
                           "logic [7:0] loop_q;\n"
                           "logic [7:0] out;\n"
                           "assign out = q + data;\n"
                           "always_ff @(posedge clk) begin\n"
                           "    if (data[0]) q <= data;\n"
                           "end\n"
                           "always_comb begin\n"
                           "    for (int i = 0; i < 2; i++) begin\n"
                           "        loop_q = data;\n"
                           "    end\n"
                           "end\n"
                           "endmodule\n"));
    }
    if (wavePreviewDock && !wavePreviewDock->isVisible()
        && viewWavePreviewAction) {
        viewWavePreviewAction->trigger();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    WavePreviewPanelCoordinator* waveCoordinator =
        window.semanticDocks
            ? window.semanticDocks->wavePreviewPanelCoordinator()
            : nullptr;
    QTreeWidget* waveTree = wavePreviewTree(window);
    bool sawWaveQ = false;
    bool sawWaveOut = false;
    bool sawWaveClockReset = false;
    bool sawWaveGuard = false;
    bool sawWaveLoopGuard = false;
    bool sawWaveContext = false;
    bool sawWaveLaneGuardSummary = false;
    bool sawWaveLaneSummary = false;
    bool sawWaveActivityMix = false;
    bool sawWaveformTrace = false;
    QTreeWidgetItem* waveQEventItem = nullptr;
    QTreeWidgetItem* waveQLaneItem = nullptr;
    if (waveTree) {
        for (int i = 0; i < waveTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* laneItem = waveTree->topLevelItem(i);
            const QString name = laneItem->text(0);
            sawWaveQ = sawWaveQ || name == QStringLiteral("q");
            sawWaveOut = sawWaveOut || name == QStringLiteral("out");
            sawWaveClockReset = sawWaveClockReset
                || name == QStringLiteral("Clock/Reset Groups");
            if (name == QStringLiteral("Activity Mix")) {
                sawWaveActivityMix = laneItem->text(1).contains(
                                         QStringLiteral("3 events"))
                    && laneItem->text(4)
                        == QStringLiteral("assign 1/comb 1/seq 1")
                    && laneItem->toolTip(0).contains(
                        QStringLiteral("activity: assign 1/comb 1/seq 1"));
            }
            if (name == QStringLiteral("Symbolic Waveform Preview"))
                sawWaveformTrace = true;
            if (name == QStringLiteral("q")) {
                waveQLaneItem = laneItem;
                sawWaveLaneSummary = sawWaveLaneSummary
                    || (laneItem->text(1).contains(QStringLiteral("1 event"))
                        && laneItem->text(1).contains(QStringLiteral("1 src"))
                        && laneItem->text(1).contains(QStringLiteral("max t+1"))
                        && laneItem->text(1).contains(QStringLiteral("1 block"))
                        && laneItem->text(1).contains(QStringLiteral("seq 1"))
                        && laneItem->text(4) == QStringLiteral("seq 1"));
                sawWaveContext = sawWaveContext
                    || laneItem->text(5) == QStringLiteral("internal logic [7:0]");
                sawWaveLaneGuardSummary = sawWaveLaneGuardSummary
                    || (laneItem->text(3) == QStringLiteral("if data[0]")
                        && laneItem->toolTip(0).contains(
                            QStringLiteral("guards: if data[0]")));
                for (int child = 0; child < laneItem->childCount(); ++child) {
                    sawWaveClockReset = sawWaveClockReset
                        || laneItem->child(child)->text(2)
                            == QStringLiteral("clk posedge clk");
                    sawWaveGuard = sawWaveGuard
                        || laneItem->child(child)->text(3)
                            == QStringLiteral("if data[0]");
                    sawWaveContext = sawWaveContext
                        || laneItem->child(child)->text(5)
                            == QStringLiteral("internal logic [7:0]");
                    if (laneItem->child(child)->text(0).contains(
                            QStringLiteral("q = data"))) {
                        waveQEventItem = laneItem->child(child);
                    }
                }
            }
            if (name == QStringLiteral("loop_q")) {
                for (int child = 0; child < laneItem->childCount(); ++child) {
                    sawWaveLoopGuard = sawWaveLoopGuard
                        || laneItem->child(child)->text(3)
                            == QStringLiteral("for int i = 0; i < 2; i++");
                }
            }
        }
    }
    expectBool("wave preview renders active editor lanes",
               waveTree && sawWaveQ && sawWaveOut,
               true);
    expectBool("wave preview renders clock/reset groups",
               waveTree && sawWaveClockReset,
               true);
    expectBool("wave preview renders guard labels",
               waveTree && sawWaveGuard,
               true);
    expectBool("wave preview renders lane guard summaries",
               waveTree && sawWaveLaneGuardSummary,
               true);
    expectBool("wave preview renders loop guard labels",
               waveTree && sawWaveLoopGuard,
               true);
    expectBool("wave preview renders declaration context",
               waveTree && sawWaveContext,
               true);
    expectBool("wave preview renders lane summary",
               waveTree && sawWaveLaneSummary,
               true);
    expectBool("wave preview renders activity mix summary",
               waveTree && sawWaveActivityMix,
               true);
    expectBool("wave preview renders symbolic waveform preview",
               waveTree && sawWaveformTrace,
               true);
    const QString waveQLaneTooltip =
        waveQLaneItem ? waveQLaneItem->toolTip(0) : QString();
    expectBool("wave preview lane tooltip has summary",
               waveQLaneTooltip.contains(QStringLiteral("summary: 1 event"))
                   && waveQLaneTooltip.contains(QStringLiteral("activity: seq 1")),
               true);
    const QString waveQTooltip =
        waveQEventItem ? waveQEventItem->toolTip(0) : QString();
    expectBool("wave preview event tooltip has source target details",
               waveQTooltip.contains(QStringLiteral("target: q"))
                   && waveQTooltip.contains(QStringLiteral("expression: data"))
                   && waveQTooltip.contains(QStringLiteral("sources: data"))
                   && waveQTooltip.contains(QStringLiteral("timing: t+1 cycle"))
                   && waveQTooltip.contains(QStringLiteral("clock/reset: clk posedge clk"))
                   && waveQTooltip.contains(QStringLiteral("target context: internal logic [7:0]"))
                   && waveQTooltip.contains(QStringLiteral("source context: data: input logic [7:0]"))
                   && waveQTooltip.contains(QStringLiteral("location:")),
               true);
    QLabel* waveSummary = wavePreviewSummaryLabel(window);
    expectBool("wave preview report summary shows activity mix",
               waveSummary
                   && waveSummary->text().contains(
                       QStringLiteral("activity assign 1/comb 1/seq 1")),
               true);
    QWidget* waveCanvas = wavePreviewCanvas(window);
    QWidget* sharedWaveformView = waveCoordinator
        ? waveCoordinator->waveformViewForTest()
        : nullptr;
    expectBool("wave preview canvas exists",
               waveCanvas != nullptr,
               true);
    expectBool("wave preview canvas hosts shared waveform view",
               waveCanvas && sharedWaveformView
                   && sharedWaveformView->parentWidget() == waveCanvas
                   && sharedWaveformView->isVisible(),
               true);
    const QStringList waveformCapabilities = sharedWaveformView
        ? sharedWaveformView->property(
              "wavewidgets.capabilities").toStringList()
        : QStringList();
    expectBool("shared waveform view accepts symbolic payload",
               sharedWaveformView
                   && sharedWaveformView->property(
                          "wavewidgets.contract").toString()
                          == QStringLiteral(
                              "wave-workbench.waveform-view/v1")
                   && sharedWaveformView->property(
                          "wavewidgets.previewContract").toString()
                          == QStringLiteral("wave-preview/v1")
                   && sharedWaveformView->property(
                          "previewMode").toString()
                          == QStringLiteral("symbolic")
                   && sharedWaveformView->property(
                          "previewGeneration").toULongLong() > 0,
               true);
    expectBool("shared waveform view exposes interaction capabilities",
               waveformCapabilities.contains(
                   QStringLiteral("generation-replace/v1"))
                   && waveformCapabilities.contains(
                       QStringLiteral("waveform-theme/v1"))
                   && waveformCapabilities.contains(
                       QStringLiteral("source-navigation/v1")),
               true);
    if (waveEditor) {
        const QString waveSelectionText = waveEditor->toPlainText();
        const int combStart =
            waveSelectionText.indexOf(QStringLiteral("always_comb"));
        const int combEnd =
            combStart >= 0
                ? waveSelectionText.indexOf(QStringLiteral("endmodule"),
                                            combStart)
                : -1;
        if (combStart >= 0 && combEnd > combStart) {
            QTextCursor scopeCursor(waveEditor->document());
            scopeCursor.setPosition(combStart);
            scopeCursor.setPosition(combEnd, QTextCursor::KeepAnchor);
            waveEditor->setTextCursor(scopeCursor);
        }
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    bool sawScopedLoopQ = false;
    bool sawScopedQ = false;
    bool sawScopedOut = false;
    if (waveTree) {
        for (int i = 0; i < waveTree->topLevelItemCount(); ++i) {
            const QString name = waveTree->topLevelItem(i)->text(0);
            sawScopedLoopQ = sawScopedLoopQ
                || name == QStringLiteral("loop_q");
            sawScopedQ = sawScopedQ || name == QStringLiteral("q");
            sawScopedOut = sawScopedOut || name == QStringLiteral("out");
        }
    }
    expectBool("wave preview selected scope filters lanes",
               waveTree
                   && sawScopedLoopQ
                   && !sawScopedQ
                   && !sawScopedOut
                   && waveSummary
                   && waveSummary->text().contains(
                       QStringLiteral("always_comb lines"))
                   && waveSummary->text().contains(
                       QStringLiteral("symbolic preview")),
               true);
    if (waveEditor) {
        QTextCursor clearScopeCursor(waveEditor->document());
        clearScopeCursor.movePosition(QTextCursor::Start);
        waveEditor->setTextCursor(clearScopeCursor);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    bool sawRestoredQ = false;
    bool sawRestoredOut = false;
    if (waveTree) {
        for (int i = 0; i < waveTree->topLevelItemCount(); ++i) {
            const QString name = waveTree->topLevelItem(i)->text(0);
            sawRestoredQ = sawRestoredQ || name == QStringLiteral("q");
            sawRestoredOut = sawRestoredOut || name == QStringLiteral("out");
        }
    }
    expectBool("wave preview clearing selection restores full file",
               waveTree && sawRestoredQ && sawRestoredOut,
               true);
    if (waveEditor) {
        waveEditor->setPlainText(
            QStringLiteral("module wave_warning_ui;\n"
                           "logic clk;\n"
                           "logic a;\n"
                           "logic q;\n"
                           "assign q = a;\n"
                           "always_ff @(posedge clk) q <= a;\n"
                           "always_comb q = a;\n"
                           "endmodule\n"));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    bool sawWaveWarnings = false;
    bool sawWaveMixedWarning = false;
    bool sawWaveMultiBlockWarning = false;
    bool sawWaveWarningLane = false;
    if (waveTree) {
        for (int i = 0; i < waveTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* root = waveTree->topLevelItem(i);
            if (root && root->text(0) == QStringLiteral("q")) {
                sawWaveWarningLane =
                    root->text(1).contains(QStringLiteral("2 warnings"))
                    && root->toolTip(0).contains(
                        QStringLiteral("warnings: signal q mixes assign/comb/seq activity"))
                    && root->toolTip(0).contains(
                        QStringLiteral("assigned from 2 procedural blocks"));
            }
            if (!root || root->text(0) != QStringLiteral("Warnings"))
                continue;
            sawWaveWarnings =
                root->text(1) == QStringLiteral("2 warnings")
                && root->toolTip(0).contains(
                    QStringLiteral("signal q mixes assign/comb/seq activity"));
            for (int child = 0; child < root->childCount(); ++child) {
                QTreeWidgetItem* row = root->child(child);
                sawWaveMixedWarning = sawWaveMixedWarning
                    || (row && row->text(0).contains(
                            QStringLiteral(
                                "Wave Preview does not resolve writer priority")));
                sawWaveMultiBlockWarning = sawWaveMultiBlockWarning
                    || (row && row->text(0).contains(
                            QStringLiteral(
                                "assigned from 2 procedural blocks")));
            }
        }
    }
    expectBool("wave preview renders lane warnings",
               waveTree
                   && sawWaveWarnings
                   && sawWaveMixedWarning
                   && sawWaveMultiBlockWarning
                   && sawWaveWarningLane,
               true);
    expectBool("wave preview summary shows warnings",
               waveSummary
                   && waveSummary->text().contains(QStringLiteral("2 warnings")),
               true);
    if (waveEditor) {
        waveEditor->setPlainText(
            QStringLiteral("module wave_ui;\n"
                           "logic clk;\n"
                           "logic [7:0] q;\n"
                           "logic [7:0] z;\n"
                           "assign z = q;\n"
                           "always_ff @(posedge clk) begin\n"
                           "    q <= z;\n"
                           "end\n"
                           "always_comb begin\n"
                           "    z = q;\n"
                           "end\n"
                           "endmodule\n"));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    bool sawWaveZ = false;
    if (waveTree) {
        for (int i = 0; i < waveTree->topLevelItemCount(); ++i)
            sawWaveZ = sawWaveZ
                || waveTree->topLevelItem(i)->text(0) == QStringLiteral("z");
    }
    expectBool("wave preview refreshes dirty editor text",
               waveTree && sawWaveZ,
               true);
    auto currentWaveAssignmentExpression = [waveEditor]() {
        if (!waveEditor)
            return QString();
        const QString text = waveEditor->cachedDocumentText();
        const int prefix = text.indexOf(QStringLiteral("q <= "));
        if (prefix < 0)
            return QString();
        const int start = prefix + QStringLiteral("q <= ").size();
        const int end = text.indexOf(QLatin1Char(';'), start);
        return end > start ? text.mid(start, end - start).trimmed()
                           : QString();
    };
    if (waveEditor) {
        const QString waveText = waveEditor->cachedDocumentText();
        const int assignment = waveText.indexOf(QStringLiteral("q <= z"));
        if (assignment >= 0) {
            QTextCursor editCursor(waveEditor->document());
            editCursor.setPosition(
                assignment + QStringLiteral("q <= ").size());
            waveEditor->setTextCursor(editCursor);
        }
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (waveCoordinator)
        waveCoordinator->resetRefreshMetricsForTest();
    if (waveEditor)
        QTest::keyClick(waveEditor, Qt::Key_X);
    QTest::qWait(420);
    const QString visibleExpectedExpression =
        currentWaveAssignmentExpression();
    const WavePreviewRefreshMetrics visibleWaveMetrics =
        waveCoordinator
            ? waveCoordinator->refreshMetricsForTest()
            : WavePreviewRefreshMetrics();
    bool visibleWaveConsumedLatestText = false;
    if (waveCoordinator) {
        for (const WavePreviewLane& lane :
             waveCoordinator->reportForTest().lanes) {
            for (const WavePreviewAssignment& assignment : lane.assignments) {
                visibleWaveConsumedLatestText =
                    visibleWaveConsumedLatestText
                    || (lane.signalName == QStringLiteral("q")
                        && assignment.expression.trimmed()
                               == visibleExpectedExpression);
            }
        }
    }
    const bool visibleWaveRenderedLatest =
        waveEditor && waveCoordinator
        && visibleWaveMetrics.renderCount == 1
        && visibleWaveMetrics.documentChangeRenderCount == 0
        && visibleWaveMetrics.scopeDeltaUpdateCount == 0
        && visibleWaveMetrics.scopeRebuildCount == 1;
    const bool visibleWaveParsedLatest =
        waveEditor && visibleWaveConsumedLatestText
        && visibleWaveMetrics.lastParsedCharacterCount
               < waveEditor->cachedDocumentText().size();
    if (!visibleWaveRenderedLatest || !visibleWaveParsedLatest) {
        const LiveInsightSnapshot liveWaveSnapshot =
            window.liveInsightSession
                ? window.liveInsightSession->snapshot(
                      LiveInsightKind::Wave)
                : LiveInsightSnapshot();
        std::cerr << "wave latest snapshot metrics: render="
                  << visibleWaveMetrics.renderCount
                  << " documentDelta="
                  << visibleWaveMetrics.documentChangeRenderCount
                  << " scopeDelta="
                  << visibleWaveMetrics.scopeDeltaUpdateCount
                  << " scopeRebuild="
                  << visibleWaveMetrics.scopeRebuildCount
                  << " parsedChars="
                  << visibleWaveMetrics.lastParsedCharacterCount
                  << " documentChars="
                  << (waveEditor
                          ? waveEditor->cachedDocumentText().size()
                          : -1)
                  << " expected="
                  << visibleExpectedExpression.toStdString()
                  << " consumed="
                  << (visibleWaveConsumedLatestText ? "true" : "false")
                  << " dockVisible="
                  << (wavePreviewDock && wavePreviewDock->isVisible()
                          ? "true" : "false")
                  << " sessionVisible="
                  << (liveWaveSnapshot.visible ? "true" : "false")
                  << " phase="
                  << static_cast<int>(liveWaveSnapshot.phase)
                  << " requested="
                  << liveWaveSnapshot.requestedGeneration
                  << " published="
                  << liveWaveSnapshot.publishedGeneration
                  << " stale="
                  << (liveWaveSnapshot.stale ? "true" : "false")
                  << '\n';
    }
    expectBool("visible wave preview renders one debounced latest snapshot",
               visibleWaveRenderedLatest,
               true);
    expectBool("visible wave preview consumes latest scoped text",
               visibleWaveParsedLatest,
               true);

    if (waveCoordinator)
        waveCoordinator->resetRefreshMetricsForTest();
    if (waveEditor) {
        const int sameScopePosition =
            waveEditor->cachedDocumentText().indexOf(
                QStringLiteral("q <="));
        if (sameScopePosition >= 0) {
            QTextCursor sameScopeCursor(waveEditor->document());
            sameScopeCursor.setPosition(sameScopePosition + 1);
            waveEditor->setTextCursor(sameScopeCursor);
        }
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const WavePreviewRefreshMetrics sameScopeCursorMetrics =
        waveCoordinator
            ? waveCoordinator->refreshMetricsForTest()
            : WavePreviewRefreshMetrics();
    expectBool("cursor move after edit in same Wave scope does not rerender",
               sameScopeCursorMetrics.renderCount == 0
                   && sameScopeCursorMetrics.documentChangeRenderCount == 0,
               true);

    if (waveCoordinator)
        waveCoordinator->resetRefreshMetricsForTest();
    if (waveEditor) {
        const int otherScopePosition =
            waveEditor->cachedDocumentText().indexOf(
                QStringLiteral("z = q"));
        if (otherScopePosition >= 0) {
            QTextCursor otherScopeCursor(waveEditor->document());
            otherScopeCursor.setPosition(otherScopePosition);
            waveEditor->setTextCursor(otherScopeCursor);
        }
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const WavePreviewRefreshMetrics crossScopeCursorMetrics =
        waveCoordinator
            ? waveCoordinator->refreshMetricsForTest()
            : WavePreviewRefreshMetrics();
    bool crossScopeConsumedLatestText = false;
    if (waveCoordinator) {
        for (const WavePreviewLane& lane :
             waveCoordinator->reportForTest().lanes) {
            crossScopeConsumedLatestText =
                crossScopeConsumedLatestText
                || (lane.signalName == QStringLiteral("z")
                    && !lane.assignments.isEmpty()
                    && lane.assignments.first().expression.trimmed()
                           == QStringLiteral("q"));
        }
    }
    expectBool("cursor crossing Wave scopes refreshes exactly once",
               crossScopeCursorMetrics.renderCount == 1
                   && crossScopeCursorMetrics.documentChangeRenderCount == 0
                   && crossScopeCursorMetrics.scopeRebuildCount == 1
                   && crossScopeConsumedLatestText,
               true);

    if (waveEditor) {
        const int originalScopePosition =
            waveEditor->cachedDocumentText().indexOf(
                QStringLiteral("q <="));
        if (originalScopePosition >= 0) {
            QTextCursor originalScopeCursor(waveEditor->document());
            originalScopeCursor.setPosition(
                originalScopePosition + QStringLiteral("q <= ").size());
            waveEditor->setTextCursor(originalScopeCursor);
        }
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    if (wavePreviewDock)
        wavePreviewDock->hide();
    if (waveCoordinator)
        waveCoordinator->resetRefreshMetricsForTest();
    if (waveEditor)
        QTest::keyClick(waveEditor, Qt::Key_Y);
    const WavePreviewRefreshMetrics hiddenWaveMetrics =
        waveCoordinator
            ? waveCoordinator->refreshMetricsForTest()
            : WavePreviewRefreshMetrics();
    expectBool("hidden wave preview skips expensive document rendering",
               hiddenWaveMetrics.documentChangeRenderCount == 0
                   && hiddenWaveMetrics.renderCount == 0,
               true);
    window.showPanelById(QStringLiteral("wavePreview"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const QString reopenedExpectedExpression =
        currentWaveAssignmentExpression();
    bool reopenedWaveConsumedLatestText = false;
    if (waveCoordinator) {
        for (const WavePreviewLane& lane :
             waveCoordinator->reportForTest().lanes) {
            for (const WavePreviewAssignment& assignment : lane.assignments) {
                reopenedWaveConsumedLatestText =
                    reopenedWaveConsumedLatestText
                    || (lane.signalName == QStringLiteral("q")
                        && assignment.expression.trimmed()
                               == reopenedExpectedExpression);
            }
        }
    }
    expectBool("reopened wave preview catches up synchronously",
               reopenedWaveConsumedLatestText,
               true);
    if (waveEditor) {
        QString largeWaveText;
        largeWaveText.reserve(150 * 1024);
        largeWaveText += QStringLiteral("module wave_large;\n"
                                        "logic clk;\n"
                                        "logic [7:0] source;\n"
                                        "logic [7:0] huge_delayed;\n");
        for (int i = 0; i < 5000; ++i)
            largeWaveText += QStringLiteral("logic [7:0] filler_%1;\n").arg(i);
        largeWaveText += QStringLiteral("always_ff @(posedge clk) begin\n"
                                        "    huge_delayed <= source;\n"
                                        "end\n"
                                        "endmodule\n");
        waveEditor->setPlainText(largeWaveText);
    }
    expectBool("wave preview refreshes large dirty scope synchronously",
               waveTree && findItemByText(waveTree,
                                           QStringLiteral("huge_delayed"))
                               != nullptr,
               true);
    }

    MyCodeEditor* modeChipEditor = window.tabManager->getCurrentEditor();
    if (modeChipEditor)
        modeChipEditor->startFoldShelfMode();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    expectBool("fold shelf mode highlights shelf panel",
               window.foldShelfPanel && window.foldShelfPanel->shelfModeActive(),
               true);
    if (modeChipEditor)
        modeChipEditor->cancelFoldShelfMode();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);


    if (modeChipEditor) {
        modeChipEditor->setPlainText(QStringLiteral("slot"));
        QString modeReason;
        expectBool("signal selection exposes a persistent mode snapshot",
                   modeChipEditor->startSignalSelectionMode(&modeReason)
                       && modeChipEditor->editorModeSnapshot().primaryMode
                              == EditorModeId::SignalSelection,
                   true);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);


        CodeTemplateSlot slot;
        slot.name = QStringLiteral("value");
        slot.start = 0;
        slot.length = 4;
        modeChipEditor->startTemplateSlotMode(
            0, 4, CodeTemplateSlotList{slot});
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("mode conflict replaces signal selection with Slot Mode",
                   !modeChipEditor->signalSelectionModeActiveForTest()
                       && modeChipEditor->templateSlotModeActive()
                       && modeChipEditor->editorModeSnapshot().primaryMode
                              == EditorModeId::TemplateSlots
                       && modeChipEditor->state->modes.lastExitReason(
                              EditorModeId::SignalSelection)
                              == EditorModeExitReason::Conflict,
                   true);
        QTest::keyClick(modeChipEditor, Qt::Key_Escape);

        modeChipEditor->startFoldRegionMarkMode();
        modeChipEditor->startFoldShelfMode();
        expectBool("Fold Shelf conflicts through the same mode matrix",
                   !modeChipEditor->foldRegionMarkModeActive()
                       && modeChipEditor->foldShelfModeActive()
                       && modeChipEditor->state->modes.lastExitReason(
                              EditorModeId::FoldRegion)
                              == EditorModeExitReason::Conflict,
                   true);
        modeChipEditor->cancelFoldShelfMode();

        modeChipEditor->startFoldShelfMode();
        MyCodeEditor* priorTabEditor = modeChipEditor;
        window.tabManager->createNewTab();
        MyCodeEditor* replacementEditor =
            window.tabManager->getCurrentEditor();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("tab switch exits every stale editor mode",
                   replacementEditor
                       && replacementEditor != priorTabEditor
                       && !priorTabEditor->foldShelfModeActive()
                       && priorTabEditor->state->modes.lastExitReason(
                              EditorModeId::FoldShelf)
                              == EditorModeExitReason::TabChanged,
                   true);
        if (replacementEditor) {
            const int replacementIndex =
                window.tabManager->tabWidget->indexOf(
                    replacementEditor);
            window.tabManager->closeTab(replacementIndex);
        }
        modeChipEditor = window.tabManager->getCurrentEditor();

        if (modeChipEditor && window.globalControlCoordinator) {
            modeChipEditor->startFoldShelfMode();
            window.globalControlCoordinator->open();
            QCoreApplication::processEvents(
                QEventLoop::AllEvents, 50);
            expectBool("Global Control exits the active editor mode",
                       !modeChipEditor->foldShelfModeActive()
                           && modeChipEditor->state->modes
                                  .lastExitReason(
                                      EditorModeId::FoldShelf)
                                  == EditorModeExitReason::ExternalControl,
                       true);
            if (window.globalControlCoordinator->panel)
                window.globalControlCoordinator->panel->hide();
        }
    }

    QTemporaryDir saveDir;
    expectBool("save temp dir valid", saveDir.isValid(), true);
    MyCodeEditor* saveEditor = window.tabManager->getCurrentEditor();
    expectBool("save editor exists", saveEditor != nullptr, true);
    if (saveDir.isValid() && saveEditor) {
        const QString savePath = saveDir.filePath(QStringLiteral("saved_tab.sv"));
        QFile seedFile(savePath);
        expectBool("save target seed opens",
                   seedFile.open(QIODevice::WriteOnly | QFile::Text),
                   true);
        seedFile.close();

        const QString savedText =
            QStringLiteral("module saved_tab;\nendmodule\n");
        DocumentModel* saveDocuments = window.tabManager->getDocumentModel();
        if (saveDocuments)
            saveDocuments->setDocumentFileName(saveEditor, savePath);
        saveEditor->setPlainText(savedText);
        expectBool("document model caches save editor text",
                   saveDocuments
                       && saveDocuments->documentTextForEditor(saveEditor) == savedText,
                   true);
        QSignalSpy fileSavedSpy(window.tabManager.get(), &TabManager::fileSaved);
        QSignalSpy documentSavedSpy(
            saveDocuments,
            &DocumentModel::documentSaved);
        expectBool("tab manager saves current tab",
                   window.tabManager->saveCurrentTab(),
                   true);
        QFile savedFile(savePath);
        expectBool("saved file reopens",
                   savedFile.open(QIODevice::ReadOnly | QFile::Text),
                   true);
        const QString savedFileText = QTextStream(&savedFile).readAll();
        savedFile.close();
        expectBool("tab manager writes editor text",
                   savedFileText == savedText,
                   true);
        expectBool("tab manager marks document saved",
                   saveDocuments
                       && saveDocuments->documentForEditor(saveEditor).saved,
                   true);
        expectBool("tab manager emits fileSaved",
                   fileSavedSpy.count() == 1,
                   true);
        expectBool("document model emits one saved snapshot",
                   documentSavedSpy.count() == 1,
                   true);
        const DocumentSnapshot savedDoc =
            window.tabManager->getDocumentModel()
                ? window.tabManager->getDocumentModel()->documentForEditor(saveEditor)
                : DocumentSnapshot();
        expectBool("document model marks saved tab clean",
                   savedDoc.saved && !savedDoc.dirty,
                   true);
        expectBool("document model records saved version",
                   savedDoc.savedTextVersion == savedDoc.textVersion,
                   true);
        expectBool("document model retains the saved immutable text snapshot",
                   savedDoc.text == savedText,
                   true);
        QFile savedRawFile(savePath);
        const bool savedRawFileOpened =
            savedRawFile.open(QIODevice::ReadOnly);
        const QByteArray savedRawBytes = savedRawFileOpened
            ? savedRawFile.readAll()
            : QByteArray();
        savedRawFile.close();
        SharedDocument* savedSharedDocument =
            window.tabManager->sharedDocumentForEditor(saveEditor);
        expectBool("saved baseline digest matches exact disk bytes",
                   savedRawFileOpened
                       && savedSharedDocument
                       && savedSharedDocument->savedBaselineSha256()
                              == QCryptographicHash::hash(
                                     savedRawBytes,
                                     QCryptographicHash::Sha256),
                   true);

        const QString shortcutSavedText =
            QStringLiteral("module shortcut_saved_tab;\nendmodule\n");
        saveEditor->setPlainText(shortcutSavedText);
        saveEditor->setFocus();
        QTest::keyClick(saveEditor, Qt::Key_S, Qt::ControlModifier);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QFile shortcutSavedFile(savePath);
        expectBool("ctrl-s save file reopens",
                   shortcutSavedFile.open(QIODevice::ReadOnly | QFile::Text),
                   true);
        const QString shortcutSavedFileText =
            QTextStream(&shortcutSavedFile).readAll();
        shortcutSavedFile.close();
        expectBool("ctrl-s saves current editor with focus",
                   shortcutSavedFileText == shortcutSavedText,
                   true);
        expectBool("ctrl-s emits fileSaved",
                   fileSavedSpy.count() == 2,
                   true);
        expectBool("ctrl-s emits documentSaved",
                   documentSavedSpy.count() == 2,
                   true);
        const int fileSavedCountBeforeNoOp = fileSavedSpy.count();
        const int documentSavedCountBeforeNoOp = documentSavedSpy.count();
        QTest::keyClick(saveEditor, Qt::Key_S, Qt::ControlModifier);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        expectBool("unchanged ctrl-s is a zero-signal no-op",
                   fileSavedSpy.count() == fileSavedCountBeforeNoOp
                       && documentSavedSpy.count()
                              == documentSavedCountBeforeNoOp,
                   true);

    }

    const bool workspaceOpened = window.workspaceManager->openWorkspace(workspacePath);
    expectBool("open workspace", workspaceOpened, true);
    expectBool("workspace file scan completes",
               waitUntil([&]() { return workspaceFilesScanned; }, 10000),
               true);
    bool sawWorkspaceActivity = false;
    for (const ActivityLogEvent& event : ActivityLogService::getInstance()->events()) {
        sawWorkspaceActivity = sawWorkspaceActivity
            || (event.source == QStringLiteral("Workspace")
                && (event.message.startsWith(QStringLiteral("Activated "))
                    || event.message.startsWith(QStringLiteral("Scanned "))));
    }
    expectBool("workspace open logs activity",
               sawWorkspaceActivity,
               true);
    const QStringList svFiles = window.workspaceManager->getSystemVerilogFiles();
    expectBool("workspace has SystemVerilog files", !svFiles.isEmpty(), true);
    const ProjectSnapshot project = window.workspaceManager->projectSnapshot();
    const QString normalizedWorkspacePath =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(workspacePath).absoluteFilePath()));
    expectBool("project model tracks workspace root",
               project.workspaceRoot == normalizedWorkspacePath,
               true);
    expectBool("project model tracks SV files",
               project.systemVerilogFiles.size() == svFiles.size(),
               true);
    expectBool("project model has default include root",
               project.includeDirs.contains(normalizedWorkspacePath),
               true);
    const QString resolvedWorkspaceInclude =
        window.workspaceManager->resolveIncludePath(QFileInfo(svFiles.first()).fileName());
    expectBool("workspace manager resolves include by basename",
               QFileInfo(resolvedWorkspaceInclude).fileName() == QFileInfo(svFiles.first()).fileName(),
               true);
    const QString resolvedCurrentFileInclude =
        window.workspaceManager->resolveIncludePath(QFileInfo(svFiles.first()).fileName(),
                                                    svFiles.first());
    expectBool("workspace manager resolves include from current file",
               QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(resolvedCurrentFileInclude).absoluteFilePath()))
                   == QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(svFiles.first()).absoluteFilePath())),
               true);
    const QString includeSourcePath =
        QDir(workspacePath).absoluteFilePath(QStringLiteral("_svh.svh"));
    const QString includeTargetPath =
        QDir(workspacePath).absoluteFilePath(QStringLiteral("SVH_interface.sv"));
    expectBool("include source fixture exists",
               QFileInfo(includeSourcePath).isFile(),
               true);
    expectBool("include target fixture exists",
               QFileInfo(includeTargetPath).isFile(),
               true);
    const int editorCountBeforeIncludeClick = window.tabManager->editorCount();
    if (QFileInfo(includeSourcePath).isFile()
        && QFileInfo(includeTargetPath).isFile()
        && window.tabManager->openFileInTab(includeSourcePath)) {
        MyCodeEditor* includeEditor = window.tabManager->getCurrentEditor();
        expectBool("include source editor opens", includeEditor != nullptr, true);
        if (includeEditor) {
            QTextBlock includeBlock =
                findBlockContaining(includeEditor->document(),
                                    QStringLiteral("SVH_interface.sv"));
            expectBool("include directive block found",
                       includeBlock.isValid(),
                       true);
            if (includeBlock.isValid()) {
                const int includeClickPosition =
                    includeBlock.position()
                    + includeBlock.text().indexOf(QStringLiteral("SVH_interface"));
                QTextCursor includeCursor(includeEditor->document());
                includeCursor.setPosition(includeClickPosition);
                includeEditor->setTextCursor(includeCursor);
                includeEditor->centerCursor();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                const QPoint includeClickPoint =
                    includeEditor->cursorRect(includeCursor).center();
                QTest::mouseClick(includeEditor->viewport(),
                                  Qt::LeftButton,
                                  Qt::ControlModifier,
                                  includeClickPoint);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                MyCodeEditor* openedIncludeEditor =
                    window.tabManager->getCurrentEditor();
                const DocumentSnapshot openedIncludeDocument =
                    window.tabManager->getDocumentForEditor(openedIncludeEditor);
                expectBool("Ctrl+Click include opens target",
                           openedIncludeEditor
                               && QDir::cleanPath(QDir::fromNativeSeparators(
                                      QFileInfo(openedIncludeDocument.fileName)
                                          .absoluteFilePath()))
                                      == QDir::cleanPath(QDir::fromNativeSeparators(
                                             QFileInfo(includeTargetPath)
                                                 .absoluteFilePath()))
                               && window.tabManager->editorCount()
                                      == editorCountBeforeIncludeClick + 2,
                           true);
                if (openedIncludeEditor) {
                    QTest::mouseClick(openedIncludeEditor->viewport(),
                                      Qt::BackButton,
                                      Qt::NoModifier,
                                      QPoint(4, 4));
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                }
                const DocumentSnapshot includeBackDocument =
                    window.tabManager->getCurrentDocument();
                expectBool("mouse back returns from include target",
                           QDir::cleanPath(QDir::fromNativeSeparators(
                               QFileInfo(includeBackDocument.fileName)
                                   .absoluteFilePath()))
                               == QDir::cleanPath(QDir::fromNativeSeparators(
                                   QFileInfo(includeSourcePath)
                                       .absoluteFilePath())),
                           true);

                const int editorCountAfterFirstIncludeClick =
                    window.tabManager->editorCount();
                expectBool("return to include source tab",
                           window.tabManager->activateOpenFile(includeSourcePath),
                           true);
                includeEditor = window.tabManager->getCurrentEditor();
                if (includeEditor) {
                    includeEditor->setTextCursor(includeCursor);
                    includeEditor->centerCursor();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                    const QPoint repeatClickPoint =
                        includeEditor->cursorRect(includeCursor).center();
                    QTest::mouseClick(includeEditor->viewport(),
                                      Qt::LeftButton,
                                      Qt::ControlModifier,
                                      repeatClickPoint);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                }
                expectBool("Ctrl+Click include reuses target tab",
                           window.tabManager->editorCount()
                               == editorCountAfterFirstIncludeClick,
                           true);

                MyCodeEditor* targetHistoryEditor =
                    window.tabManager->getCurrentEditor();
                const bool targetHasSecondLine =
                    targetHistoryEditor
                    && targetHistoryEditor->document()->blockCount() > 1;
                expectBool("include target has line-navigation history fixture",
                           targetHasSecondLine,
                           true);
                if (targetHasSecondLine) {
                    QTextCursor oldTargetCursor(
                        targetHistoryEditor->document()->findBlockByNumber(0));
                    targetHistoryEditor->setTextCursor(oldTargetCursor);
                    expectBool("return to include source before line navigation",
                               window.tabManager->activateOpenFile(includeSourcePath),
                               true);
                    includeEditor = window.tabManager->getCurrentEditor();
                    if (includeEditor) {
                        includeEditor->setTextCursor(includeCursor);
                        includeEditor->centerCursor();
                        QCoreApplication::processEvents(
                            QEventLoop::AllEvents,
                            50);
                    }
                    window.navigationCommandCoordinator->navigateToFileAndLine(
                        includeTargetPath,
                        2,
                        1);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                    MyCodeEditor* lineTargetEditor =
                        window.tabManager->getCurrentEditor();
                    if (lineTargetEditor) {
                        QTest::mouseClick(lineTargetEditor->viewport(),
                                          Qt::BackButton,
                                          Qt::NoModifier,
                                          QPoint(4, 4));
                        QCoreApplication::processEvents(
                            QEventLoop::AllEvents,
                            50);
                    }
                    const DocumentSnapshot lineBackDocument =
                        window.tabManager->getCurrentDocument();
                    expectBool("mouse back after cross-file line jump skips target old cursor",
                               QDir::cleanPath(QDir::fromNativeSeparators(
                                   QFileInfo(lineBackDocument.fileName)
                                       .absoluteFilePath()))
                                   == QDir::cleanPath(QDir::fromNativeSeparators(
                                       QFileInfo(includeSourcePath)
                                           .absoluteFilePath())),
                               true);
                }
            }
        }
    }

    expectBool("workspace symbol analysis completes",
               waitUntil([&]() { return workspaceSymbolsDone; }, 60000), true);

    const QString largeFile = largestFile(svFiles);
    expectBool("large file selected", QFileInfo(largeFile).size() > 20000, true);
    expectBool("open large file", window.tabManager->openFileInTab(largeFile), true);

    MyCodeEditor* largeEditor = window.tabManager->getCurrentEditor();
    expectBool("large editor exists", largeEditor != nullptr, true);
    if (largeEditor) {
        DocumentModel* documents = window.tabManager->getDocumentModel();
        expectBool("document model exists", documents != nullptr, true);
        const DocumentSnapshot beforeEditDoc = documents
            ? documents->documentForFile(largeFile)
            : DocumentSnapshot();
        expectBool("document model tracks large file",
                   beforeEditDoc.fileName == QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(largeFile).absoluteFilePath())),
                   true);
        expectBool("tab manager exposes active document snapshot",
                   window.tabManager->getCurrentDocument().documentId
                       == beforeEditDoc.documentId,
                   true);
        expectBool("opened document starts saved", beforeEditDoc.saved, true);
        expectBool("opened document saved version matches text version",
                   beforeEditDoc.savedTextVersion == beforeEditDoc.textVersion,
                   true);
        expectBool("opened document snapshot mirrors semantic text revision",
                   beforeEditDoc.textVersion
                       == static_cast<int>(
                           largeEditor->semanticDocumentRevision()),
                   true);
        expectBool("document model caches opened text",
                   documents
                       && documents->documentTextForFile(largeFile)
                           == largeEditor->toPlainText(),
                   true);
        expectBool("tab manager reads model open-file text",
                   window.tabManager->getPlainTextFromOpenFile(largeFile)
                       == largeEditor->toPlainText(),
                   true);
        expectBool("tab manager reads model current-tab text",
                   window.tabManager->getPlainTextFromCurrentTab()
                       == largeEditor->toPlainText(),
                   true);

        const std::uint64_t semanticRevisionBeforeFormat =
            largeEditor->semanticDocumentRevision();
        const int qtRevisionBeforeFormat =
            largeEditor->document()->revision();
        QTextCursor formatCursor(largeEditor->document());
        formatCursor.setPosition(0);
        formatCursor.movePosition(QTextCursor::NextCharacter,
                                  QTextCursor::KeepAnchor);
        QTextCharFormat formatOnlyChange;
        formatOnlyChange.setFontUnderline(true);
        formatCursor.mergeCharFormat(formatOnlyChange);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const DocumentSnapshot afterFormatDoc = documents
            ? documents->documentForEditor(largeEditor)
            : DocumentSnapshot();
        expectBool("format-only change advances raw Qt revision",
                   largeEditor->document()->revision()
                       > qtRevisionBeforeFormat,
                   true);
        expectBool("format-only change keeps semantic text revision",
                   largeEditor->semanticDocumentRevision()
                       == semanticRevisionBeforeFormat
                       && afterFormatDoc.textVersion
                              == beforeEditDoc.textVersion,
                   true);
        expectBool("format-only change keeps source document saved",
                   afterFormatDoc.saved && !afterFormatDoc.dirty,
                   true);

        largeEditor->setFocus();
        QTextCursor cursor = largeEditor->textCursor();
        cursor.movePosition(QTextCursor::Start);
        largeEditor->setTextCursor(cursor);
        const int beforeLength = largeEditor->toPlainText().size();

        QTest::keyClick(largeEditor, Qt::Key_Return);
        QTest::keyClicks(largeEditor, "x");
        QTest::keyClick(largeEditor, Qt::Key_Down);
        QTest::keyClick(largeEditor, Qt::Key_Up);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        expectBool("large file edit applied",
                   largeEditor->toPlainText().size() >= beforeLength + 2, true);
        const DocumentSnapshot afterEditDoc = documents
            ? documents->documentForEditor(largeEditor)
            : DocumentSnapshot();
        expectBool("document model marks edit dirty", afterEditDoc.dirty, true);
        expectBool("document model increments version",
                   afterEditDoc.textVersion > beforeEditDoc.textVersion, true);
        expectBool("edited document snapshot mirrors semantic text revision",
                   afterEditDoc.textVersion
                       == static_cast<int>(
                           largeEditor->semanticDocumentRevision()),
                   true);
        expectBool("document model keeps saved version across edit",
                   afterEditDoc.savedTextVersion == beforeEditDoc.savedTextVersion
                       && afterEditDoc.savedTextVersion < afterEditDoc.textVersion,
                   true);
        const QTextCursor afterEditCursor = largeEditor->textCursor();
        expectBool("document model refreshes cursor snapshot",
                   afterEditDoc.cursorLine == afterEditCursor.blockNumber() + 1
                       && afterEditDoc.cursorColumn
                              == afterEditCursor.positionInBlock() + 1,
                   true);
        expectBool("tab manager active snapshot tracks edit",
                   window.tabManager->getCurrentDocument().textVersion
                       == afterEditDoc.textVersion
                       && window.tabManager->getCurrentDocument().dirty,
                   true);
        expectBool("document model updates cached text",
                   documents
                       && documents->documentTextForFile(largeFile)
                           == largeEditor->toPlainText(),
                   true);
        expectBool("tab manager current text follows model cache",
                   window.tabManager->getPlainTextFromCurrentTab()
                       == largeEditor->toPlainText(),
                   true);
        expectBool("document model owns editor snapshot state",
                   documents
                       && afterEditDoc.fileName
                           == QDir::cleanPath(QDir::fromNativeSeparators(
                                  QFileInfo(largeFile).absoluteFilePath()))
                       && documents->documentText(afterEditDoc.documentId)
                              == largeEditor->toPlainText()
                       && !afterEditDoc.saved
                       && afterEditDoc.cursorLine > 0,
                   true);
        drainRelationshipWork(window);
    }

    QTemporaryDir identityDir;
    expectBool("document identity temp dir valid", identityDir.isValid(), true);
    if (identityDir.isValid()) {
        QDir identityRoot(identityDir.path());
        expectBool("document identity dir A created",
                   identityRoot.mkpath(QStringLiteral("a")),
                   true);
        expectBool("document identity dir B created",
                   identityRoot.mkpath(QStringLiteral("b")),
                   true);
        const QString sameNameA =
            identityRoot.filePath(QStringLiteral("a/same_name.sv"));
        const QString sameNameB =
            identityRoot.filePath(QStringLiteral("b/same_name.sv"));
        QFile sameFileA(sameNameA);
        expectBool("same-name file A writable",
                   sameFileA.open(QIODevice::WriteOnly | QIODevice::Text),
                   true);
        if (sameFileA.isOpen()) {
            sameFileA.write("module same_name_a; logic from_a; endmodule\n");
            sameFileA.close();
        }
        QFile sameFileB(sameNameB);
        expectBool("same-name file B writable",
                   sameFileB.open(QIODevice::WriteOnly | QIODevice::Text),
                   true);
        if (sameFileB.isOpen()) {
            sameFileB.write("module same_name_b; logic from_b; endmodule\n");
            sameFileB.close();
        }

        expectBool("open same-name file A",
                   window.tabManager->openFileInTab(sameNameA),
                   true);
        MyCodeEditor* sameEditorA = window.tabManager->getCurrentEditor();
        expectBool("same-name editor A exists", sameEditorA != nullptr, true);
        expectBool("open same-name file B",
                   window.tabManager->openFileInTab(sameNameB),
                   true);
        MyCodeEditor* sameEditorB = window.tabManager->getCurrentEditor();
        DocumentModel* documents = window.tabManager->getDocumentModel();
        expectBool("same-name editor B exists", sameEditorB != nullptr, true);
        expectBool("document model resolves native path to editor",
                   sameEditorA
                       && documents
                       && documents->editorForFile(QDir::toNativeSeparators(sameNameA))
                              == sameEditorA,
                   true);
        QSignalSpy activeDocumentSpy(window.tabManager.get(),
                                     &TabManager::activeDocumentChanged);
        expectBool("tab manager activates model-indexed file",
                   sameEditorA
                       && window.tabManager->activateOpenFile(QDir::toNativeSeparators(sameNameA))
                       && window.tabManager->getCurrentEditor() == sameEditorA,
                   true);
        expectBool("tab manager emits active document snapshot",
                   activeDocumentSpy.count() == 1
                       && activeDocumentSpy.takeFirst().at(0).value<DocumentSnapshot>().fileName
                              == QDir::cleanPath(QDir::fromNativeSeparators(
                                     QFileInfo(sameNameA).absoluteFilePath())),
                   true);
        expectBool("tab manager keeps same-name file A text distinct",
                   window.tabManager->getPlainTextFromOpenFile(sameNameA)
                       .contains(QStringLiteral("from_a")),
                   true);
        expectBool("tab manager keeps same-name file B text distinct",
                   window.tabManager->getPlainTextFromOpenFile(sameNameB)
                       .contains(QStringLiteral("from_b")),
                   true);
        expectBool("tab manager rejects basename-only open-file text lookup",
                   window.tabManager
                       ->getPlainTextFromOpenFile(QStringLiteral("same_name.sv"))
                       .isNull(),
                   true);
        drainRelationshipWork(window);
    }

    bool symbolFixtureAnalyzed = false;
    QObject::connect(window.analysisScheduler.get(), &AnalysisScheduler::fileSymbolAnalysisFinished,
                     &window, [&](const QString& fileName, int symbolsFound) {
                         if (QFileInfo(fileName).absoluteFilePath()
                             == QFileInfo(symbolFixturePath).absoluteFilePath() && symbolsFound > 0) {
                             symbolFixtureAnalyzed = true;
                         }
                     });

    expectBool("open symbol fixture", window.tabManager->openFileInTab(symbolFixturePath), true);
    expectBool("symbol fixture analysis completes",
               waitUntil([&]() { return symbolFixtureAnalyzed; }, 10000), true);

    QTemporaryDir diagnosticDir;
    expectBool("diagnostic temp dir created", diagnosticDir.isValid(), true);
    const QString diagnosticPath =
        diagnosticDir.filePath(QStringLiteral("broken_diag.sv"));
    QFile diagnosticFile(diagnosticPath);
    expectBool("diagnostic fixture writable",
               diagnosticFile.open(QIODevice::WriteOnly | QIODevice::Text), true);
    if (diagnosticFile.isOpen()) {
        diagnosticFile.write(
            "module broken_diag(input logic clk);\n"
            "  logic bad;\n"
            "  assign bad = ;\n"
            "endmodule\n");
        diagnosticFile.close();
    }
    const QString cleanDiagnosticPath =
        diagnosticDir.filePath(QStringLiteral("clean_diag.sv"));
    QFile cleanDiagnosticFile(cleanDiagnosticPath);
    expectBool("clean diagnostic fixture writable",
               cleanDiagnosticFile.open(QIODevice::WriteOnly | QIODevice::Text), true);
    if (cleanDiagnosticFile.isOpen()) {
        cleanDiagnosticFile.write(
            "module clean_diag(input logic clk, output logic done);\n"
            "  assign done = clk;\n"
            "endmodule\n");
        cleanDiagnosticFile.close();
    }

    bool diagnosticFixtureAnalyzed = false;
    QObject::connect(window.analysisScheduler.get(), &AnalysisScheduler::fileSymbolAnalysisFinished,
                     &window, [&](const QString& fileName, int) {
                         if (QFileInfo(fileName).absoluteFilePath()
                             == QFileInfo(diagnosticPath).absoluteFilePath()) {
                             diagnosticFixtureAnalyzed = true;
                         }
                     });
    expectBool("open diagnostic fixture", window.tabManager->openFileInTab(diagnosticPath), true);
    expectBool("diagnostic fixture analysis completes",
               waitUntil([&]() { return diagnosticFixtureAnalyzed; }, 10000), true);
    expectBool("diagnostic fixture snapshot updates",
               waitUntil([&]() {
                   const auto snapshot = SemanticIndex::getInstance()->snapshot();
                   return snapshot
                       && !snapshot->getDiagnostics(diagnosticPath).isEmpty()
                       && window.analysisScheduler
                       && window.analysisScheduler->symbolAnalyzer
                       && window.analysisScheduler->symbolAnalyzer
                              ->fileAnalysisWatchers.isEmpty();
               }, 15000),
               true);
    expectBool("problems tree exists", problemsTree(window) != nullptr, true);
    const bool problemsShowsDiagnostic = waitUntil([&]() {
        return problemsTree(window)
            && navigableItemCount(problemsTree(window)) > 0;
    }, 10000);
    expectBool("problems tree shows diagnostic",
               problemsShowsDiagnostic,
               true);
    if (problemsScopeCombo(window)) {
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        expectBool("problems all-files groups diagnostics",
                   waitUntil([&]() {
                       QTreeWidget* tree = problemsTree(window);
                       return tree
                              && tree->topLevelItemCount() > 0
                              && tree->topLevelItem(0)->childCount() > 0;
                   }, 2000),
                   true);
        expectBool("problems all-files group shows count",
                   problemsTree(window)
                       && problemsTree(window)->topLevelItemCount() > 0
                       && problemsTree(window)->topLevelItem(0)->text(0).contains(QStringLiteral("(")),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("Current File")));
    }

    bool cleanDiagnosticFixtureAnalyzed = false;
    QObject::connect(window.analysisScheduler.get(), &AnalysisScheduler::fileSymbolAnalysisFinished,
                     &window, [&](const QString& fileName, int) {
                         if (QFileInfo(fileName).absoluteFilePath()
                             == QFileInfo(cleanDiagnosticPath).absoluteFilePath()) {
                             cleanDiagnosticFixtureAnalyzed = true;
                         }
                     });
    expectBool("open clean diagnostic fixture",
               window.tabManager->openFileInTab(cleanDiagnosticPath), true);
    expectBool("clean diagnostic fixture analysis completes",
               waitUntil([&]() { return cleanDiagnosticFixtureAnalyzed; }, 10000), true);
    if (problemsScopeCombo(window)) {
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        expectBool("problems keep previous file diagnostic",
                   waitUntil([&]() {
                       return hasNavigableFile(problemsTree(window), diagnosticPath);
                   }, 2000),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("Workspace Files")));
        expectBool("problems workspace scope hides external diagnostic",
                   waitUntil([&]() {
                       return !hasNavigableFile(problemsTree(window), diagnosticPath);
                   }, 2000),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        expectBool("problems all-files restores external diagnostic",
                   waitUntil([&]() {
                       return hasNavigableFile(problemsTree(window), diagnosticPath);
                   }, 2000),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("Current File")));
    }

    expectBool("reopen symbol fixture", window.tabManager->openFileInTab(symbolFixturePath), true);
    expectBool("symbol fixture analysis remains complete",
               waitUntil([&]() {
                   const auto snapshot = SemanticIndex::getInstance()->snapshot();
                   return snapshot
                       && !snapshot->getSymbolRecords(symbolFixturePath).isEmpty();
               }, 10000),
               true);

    MyCodeEditor* editor = window.tabManager->getCurrentEditor();
    expectBool("symbol editor exists", editor != nullptr, true);

    if (editor) {
        editor->setFocus();

        QTextBlock assignBlock = findBlockContaining(editor->document(), QStringLiteral("assign data_out"));
        expectBool("found insertion block", assignBlock.isValid(), true);
        if (assignBlock.isValid()) {
            QTextCursor cursor(editor->document());
            cursor.setPosition(assignBlock.position() + assignBlock.text().size());
            editor->setTextCursor(cursor);
            QTest::keyClick(editor, Qt::Key_Return);
            QTest::keyClicks(editor, "co");
        }

        QCompleter* completer = editor->findChild<QCompleter*>();
        expectBool("completion object exists", completer != nullptr, true);
        expectBool("plain identifier input does not auto-open completion",
                   waitUntil([&]() {
                       return completer && !completer->popup()->isVisible();
                   }, 500),
                   true);
        if (completer)
            completer->popup()->hide();
        drainRelationshipWork(window);

        QTextBlock jumpBlock = findBlockContaining(editor->document(),
                                                   QStringLiteral("counter       <= add_one(counter)"));
        expectBool("found Ctrl+Click source", jumpBlock.isValid(), true);
        if (jumpBlock.isValid()) {
            const int counterStart =
                jumpBlock.position()
                + jumpBlock.text().indexOf(QStringLiteral("counter"));
            const int clickPosition = counterStart + 3;
            QTextCursor clickCursor(editor->document());
            clickCursor.setPosition(clickPosition);
            QTextCursor selectedCounter(editor->document());
            selectedCounter.setPosition(counterStart);
            selectedCounter.setPosition(
                counterStart + QStringLiteral("counter").size(),
                QTextCursor::KeepAnchor);
            editor->setTextCursor(selectedCounter);
            editor->centerCursor();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QPoint clickPoint = editor->cursorRect(clickCursor).center();
            QTest::mousePress(editor->viewport(),
                              Qt::LeftButton,
                              Qt::ControlModifier,
                              clickPoint);
            expectBool("Ctrl+Click jumps to counter definition",
                       waitUntil([&]() { return editor->textCursor().blockNumber() == 78; }, 2000),
                       true);
            QTest::mouseRelease(editor->viewport(),
                                Qt::LeftButton,
                                Qt::ControlModifier,
                                clickPoint);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            expectBool("Ctrl+Click clears double-click style selection",
                       !editor->textCursor().hasSelection(),
                       true);
        }

        const QString moduleInstantiationFixturePath =
            normalizedSymbolFixturePath + QStringLiteral(".module_inst.sv");
        const SemanticSymbolRecord guiTargetModule =
            SemanticFixtureRecordBuilder(QStringLiteral("gui_target"),
                                         SymbolTaxonomy::DeclarationKind::Module)
                .withFile(moduleInstantiationFixturePath)
                .withLocalHandle(9401)
                .withLine(1)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
                .record();
        const QList<SemanticSymbolRecord> guiInstantiationRecords{
            guiTargetModule,
            SemanticFixtureRecordBuilder(QStringLiteral("gui_pkg"),
                                         SymbolTaxonomy::DeclarationKind::Package)
                .withFile(moduleInstantiationFixturePath)
                .withLocalHandle(9405)
                .withLine(8)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::Package)
                .record(),
            SemanticFixtureRecordBuilder(QStringLiteral("GUI_WIDTH"),
                                         SymbolTaxonomy::DeclarationKind::Parameter)
                .withFile(moduleInstantiationFixturePath)
                .withLocalHandle(9402)
                .withLine(2)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::Parameter)
                .inModule(QStringLiteral("gui_target"))
                .record(),
            SemanticFixtureRecordBuilder(QStringLiteral("clk"),
                                         SymbolTaxonomy::DeclarationKind::Port)
                .withFile(moduleInstantiationFixturePath)
                .withLocalHandle(9403)
                .withLine(4)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
                .inModule(QStringLiteral("gui_target"))
                .record(),
            SemanticFixtureRecordBuilder(QStringLiteral("rst_n"),
                                         SymbolTaxonomy::DeclarationKind::Port)
                .withFile(moduleInstantiationFixturePath)
                .withLocalHandle(9404)
                .withLine(5)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
                .inModule(QStringLiteral("gui_target"))
                .record(),
            SemanticFixtureRecordBuilder(QStringLiteral("gui_en"),
                                         SymbolTaxonomy::DeclarationKind::Signal)
                .withFile(symbolFixturePath)
                .withLocalHandle(9410)
                .withLine(20)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
                .inModule(QStringLiteral("gui_top"))
                .record(),
            SemanticFixtureRecordBuilder(QStringLiteral("en_a"),
                                         SymbolTaxonomy::DeclarationKind::Signal)
                .withFile(symbolFixturePath)
                .withLocalHandle(9411)
                .withLine(21)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
                .inModule(QStringLiteral("gui_top"))
                .record(),
            SemanticFixtureRecordBuilder(QStringLiteral("en_b"),
                                         SymbolTaxonomy::DeclarationKind::Signal)
                .withFile(symbolFixturePath)
                .withLocalHandle(9412)
                .withLine(22)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
                .inModule(QStringLiteral("gui_top"))
                .record(),
            SemanticFixtureRecordBuilder(QStringLiteral("clock_logic"),
                                         SymbolTaxonomy::DeclarationKind::Signal)
                .withFile(symbolFixturePath)
                .withLocalHandle(9413)
                .withLine(23)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
                .inModule(QStringLiteral("gui_top"))
                .record(),
            SemanticFixtureRecordBuilder(QStringLiteral("foreign_en"),
                                         SymbolTaxonomy::DeclarationKind::Signal)
                .withFile(symbolFixturePath)
                .withLocalHandle(9414)
                .withLine(24)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
                .inModule(QStringLiteral("gui_other"))
                .record(),
        };
        SemanticIndex guiInstantiationIndex;
        guiInstantiationIndex.setSnapshot(
            snapshotFromRecords(guiInstantiationRecords));
        CompletionService::getInstance()->setSemanticIndex(&guiInstantiationIndex);

        QTemporaryDir userTemplateGuiDir;
        expectBool("user template GUI temp dir valid",
                   userTemplateGuiDir.isValid(),
                   true);
        const QString userTemplateGuiJson =
            QDir(userTemplateGuiDir.path()).absoluteFilePath(
                QStringLiteral("user_templates.json"));
        expectBool("write GUI user template JSON",
                   writeTextFile(userTemplateGuiJson,
                                 QStringLiteral(
                                     "[\n"
                                     "  {\n"
                                     "    \"command\": \";;guiut\",\n"
                                     "    \"description\": \"GUI user template\",\n"
                                     "    \"body\": \"logic gui_slot;\",\n"
                                     "    \"slots\": [\n"
                                     "      {\"name\": \"signal\", \"start\": 6, \"length\": 8}\n"
                                     "    ]\n"
                                     "  }\n"
                                     "]\n")),
                   true);
        UserTemplateService::getInstance()->setWorkspaceTemplateFilePath(
            userTemplateGuiJson);
        UserTemplateService::getInstance()->reload();

        auto openInsertPalette =
            [&](GlobalControlCategory category,
                const QString& filter) -> GlobalControlPanel* {
                if (!window.globalControlCoordinator
                    || !window.globalControlCoordinator->panel) {
                    return nullptr;
                }
                window.globalControlCoordinator->panel->hide();
                window.activateWindow();
                editor->setFocus();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                window.globalControlCoordinator->open();
                GlobalControlPanel* panel =
                    window.globalControlCoordinator->panel.get();
                panel->setCategory(category);
                panel->searchEdit->setText(filter);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                return panel;
            };
        auto activatePaletteItem =
            [](GlobalControlPanel* panel,
               const std::function<bool(const GlobalControlItem&)>& predicate) {
                if (!panel || !panel->resultList)
                    return false;
                for (int row = 0; row < panel->currentItems.size(); ++row) {
                    if (!predicate(panel->currentItems.at(row)))
                        continue;
                    panel->resultList->setCurrentRow(row);
                    QTest::keyClick(panel->searchEdit, Qt::Key_Return);
                    QCoreApplication::processEvents(
                        QEventLoop::AllEvents, 100);
                    return true;
                }
                return false;
            };

        editor->setPlainText(QStringLiteral("module gui_top;\n"
                                            "    logic anchor_here;\n"
                                            "endmodule\n"));
        QTextCursor paletteAnchorCursor(
            editor->document()->findBlockByNumber(1));
        paletteAnchorCursor.movePosition(QTextCursor::EndOfBlock);
        editor->setTextCursor(paletteAnchorCursor);
        const QPoint expectedPaletteAnchor =
            editor->viewport()->mapToGlobal(
                editor->cursorRect().bottomLeft());
        GlobalControlPanel* positionedPalette =
            openInsertPalette(GlobalControlCategory::Symbols,
                              QStringLiteral("anchor"));
        const QRect positionedPaletteGeometry = positionedPalette
            ? positionedPalette->geometry()
            : QRect();
        QScreen* paletteScreen =
            QGuiApplication::screenAt(expectedPaletteAnchor);
        const QRect paletteAvailable = paletteScreen
            ? paletteScreen->availableGeometry()
            : QRect();
        const int expectedPaletteX = positionedPalette && paletteScreen
            ? qBound(
                  paletteAvailable.left(),
                  expectedPaletteAnchor.x(),
                  qMax(paletteAvailable.left(),
                       paletteAvailable.right()
                           - positionedPalette->width() + 1))
            : expectedPaletteAnchor.x();
        int expectedPaletteY = expectedPaletteAnchor.y() + 8;
        if (positionedPalette && paletteScreen
            && expectedPaletteY + positionedPalette->height()
                   > paletteAvailable.bottom() + 1) {
            expectedPaletteY = expectedPaletteAnchor.y()
                - positionedPalette->height() - 8;
        }
        if (positionedPalette && paletteScreen) {
            expectedPaletteY = qBound(
                paletteAvailable.top(),
                expectedPaletteY,
                qMax(paletteAvailable.top(),
                     paletteAvailable.bottom()
                         - positionedPalette->height() + 1));
        }
        expectBool("Ctrl+Space palette anchors at editor caret",
                   positionedPalette
                       && qAbs(positionedPaletteGeometry.left()
                               - expectedPaletteX) <= 2
                       && qAbs(positionedPaletteGeometry.top()
                               - expectedPaletteY) <= 2,
                   true);
        if (positionedPalette) {
            positionedPalette->searchEdit->setText(
                QStringLiteral("anchor"));
            positionedPalette->searchEdit->setCursorPosition(3);
            QTest::keyClick(positionedPalette->searchEdit, Qt::Key_Right);
            expectBool("Ctrl+Space Right moves query cursor",
                       positionedPalette->category()
                               == GlobalControlCategory::Symbols
                           && positionedPalette->queryText()
                                  == QStringLiteral("anchor")
                           && positionedPalette->searchEdit->cursorPosition()
                                  == 4
                           && positionedPalette->searchEdit->hasFocus(),
                       true);
            QTest::keyClick(positionedPalette->searchEdit, Qt::Key_Left);
            expectBool("Ctrl+Space Left moves query cursor",
                       positionedPalette->category()
                               == GlobalControlCategory::Symbols
                           && positionedPalette->queryText()
                                  == QStringLiteral("anchor")
                           && positionedPalette->searchEdit->cursorPosition()
                                  == 3,
                       true);
            QTest::keyClick(positionedPalette->searchEdit, Qt::Key_Tab);
            expectBool("Ctrl+Space Tab switches category",
                       positionedPalette->category()
                               == GlobalControlCategory::Templates,
                       true);
            QTest::keyClick(positionedPalette->searchEdit,
                            Qt::Key_Tab,
                            Qt::ShiftModifier);
            expectBool("Ctrl+Space Shift+Tab switches category",
                       positionedPalette->category()
                               == GlobalControlCategory::Symbols,
                       true);
            positionedPalette->hide();
        }

        editor->setPlainText(QStringLiteral("module gui_top;\n"
                                            "\n"
                                            "endmodule\n"));
        editor->setTextCursor(
            QTextCursor(editor->document()->findBlockByNumber(1)));
        GlobalControlPanel* palette =
            openInsertPalette(GlobalControlCategory::Templates,
                              QStringLiteral("guiut"));
        expectBool("Ctrl+Space exposes user templates",
                   activatePaletteItem(
                       palette,
                       [](const GlobalControlItem& item) {
                           return item.kind
                                      == GlobalControlItemKind::Template
                               && item.id == QStringLiteral(";;guiut");
                       }),
                   true);
        expectBool("Ctrl+Space user template enters Slot Mode",
                   editor->toPlainText().contains(
                       QStringLiteral("logic gui_slot;"))
                       && editor->templateSlotModeActive()
                       && editor->templateSlotModeSlotCount() == 1
                       && editor->textCursor().selectedText()
                              == QStringLiteral("gui_slot"),
                   true);
        sendWidgetKey(editor, Qt::Key_Escape);

        editor->setPlainText(QStringLiteral("module gui_top;\n"
                                            "\n"
                                            "endmodule\n"));
        editor->setTextCursor(
            QTextCursor(editor->document()->findBlockByNumber(1)));
        palette = openInsertPalette(GlobalControlCategory::Templates,
                                    QStringLiteral("import gui"));
        expectBool("Ctrl+Space package import uses semantic package records",
                   activatePaletteItem(
                       palette,
                       [](const GlobalControlItem& item) {
                           return item.operation
                                      == GlobalControlItemOperation::
                                             InsertPackageImport
                               && item.parameters
                                      .value(QStringLiteral("packageName"))
                                      .toString()
                                      == QStringLiteral("gui_pkg");
                       })
                       && editor->toPlainText().contains(
                           QStringLiteral("import gui_pkg::*;")),
                   true);

        editor->setPlainText(QStringLiteral("module gui_top;\n"
                                            "assign lhs = ;\n"
                                            "endmodule\n"));
        QTextBlock symbolBlock =
            editor->document()->findBlockByNumber(1);
        QTextCursor symbolCursor(symbolBlock);
        symbolCursor.setPosition(
            symbolBlock.position()
            + symbolBlock.text().indexOf(QLatin1Char(';')));
        editor->setTextCursor(symbolCursor);
        palette = openInsertPalette(GlobalControlCategory::Symbols,
                                    QStringLiteral("en_a"));
        bool foreignSymbolVisible = false;
        if (palette) {
            foreignSymbolVisible = std::any_of(
                palette->currentItems.cbegin(),
                palette->currentItems.cend(),
                [](const GlobalControlItem& item) {
                    return item.title == QStringLiteral("foreign_en");
                });
        }
        expectBool("Ctrl+Space symbol category preserves module scope",
                   !foreignSymbolVisible
                       && activatePaletteItem(
                           palette,
                           [](const GlobalControlItem& item) {
                               return item.kind
                                          == GlobalControlItemKind::Symbol
                                   && item.title
                                          == QStringLiteral("en_a");
                           })
                       && editor->toPlainText().contains(
                           QStringLiteral("assign lhs = en_a;")),
                   true);

        editor->setPlainText(QStringLiteral("module gui_top;\n"
                                            "\n"
                                            "endmodule\n"));
        editor->setTextCursor(
            QTextCursor(editor->document()->findBlockByNumber(1)));
        palette = openInsertPalette(GlobalControlCategory::Templates,
                                    QStringLiteral("module gui_target"));
        expectBool("Ctrl+Space module item inserts full semantic template",
                   activatePaletteItem(
                       palette,
                       [](const GlobalControlItem& item) {
                           return item.operation
                                      == GlobalControlItemOperation::InsertText
                               && item.title
                                      == QStringLiteral(
                                             "Instantiate gui_target");
                       }),
                   true);
        CompletionService::getInstance()->setSemanticIndex(
            SemanticIndex::getInstance());
        expectBool("Ctrl+Space module instantiation enters Slot Mode",
                   waitUntil([&]() {
                       return editor->templateSlotModeActive()
                           && editor->templateSlotModeSlotCount() == 4
                           && editor->textCursor().selectedText()
                                  == QStringLiteral("u_gui_target");
                   }, 2000)
                       && editor->toPlainText().contains(
                           QStringLiteral("gui_target #(\n"
                                          "    .GUI_WIDTH(GUI_WIDTH)\n"
                                          ") u_gui_target (\n"
                                          "    .clk(clk),\n"
                                          "    .rst_n(rst_n)\n"
                                          ");")),
                   true);
        sendWidgetKey(editor, Qt::Key_Escape);
    }

    NavigationWidget* navWidget = window.findChild<NavigationWidget*>();
    expectBool("navigation widget exists", navWidget != nullptr, true);
    runCommandLayerRegression(window);
    runGlobalControlRegression(window, navWidget);
    if (navWidget && editor) {
        navWidget->setActiveTab(NavigationWidget::DesignTab);
        window.navigationManager->setActiveView(NavigationManager::DesignHierarchyView);
        window.navigationManager->refreshCurrentView();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QSignalSpy analysisNavigationRefreshSpy(
            window.navigationManager.get(),
            &NavigationManager::dataRefreshed);
        window.navigationManager->onSymbolAnalysisCompleted(
            normalizedSymbolFixturePath, 0);
        expectBool("same snapshot file completion keeps Design cache",
                   analysisNavigationRefreshSpy.isEmpty(),
                   true);
        analysisNavigationRefreshSpy.clear();
        window.navigationManager->onBatchSymbolAnalysisCompleted(1, 0);
        expectBool("same snapshot batch completion keeps Design cache",
                   analysisNavigationRefreshSpy.isEmpty(),
                   true);

        navWidget->setActiveTab(NavigationWidget::FileTab);
        window.navigationManager->setActiveView(NavigationManager::FileHierarchyView);
        window.navigationManager->refreshCurrentView();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const QStringList navigationFiles =
            window.workspaceManager->getSystemVerilogFiles();
        const QString navigationFile =
            navigationFiles.isEmpty() ? QString() : navigationFiles.first();
        QTreeWidget* fileTree = navWidget->fileTreeWidget;
        QTreeWidgetItem* fileItem =
            navWidget->findFileItemByPath(navigationFile);
        expectBool("navigation file item exists", fileTree && fileItem, true);
        if (fileTree && fileItem) {
            fileTree->expandAll();
            fileTree->scrollToItem(fileItem);
            fileTree->setCurrentItem(fileItem);
            fileTree->setFocus();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QRect rect = fileTree->visualItemRect(fileItem);
            expectBool("navigation item has visual rect", rect.isValid(), true);
            bool fileDoubleClicked = false;
            QObject::connect(navWidget, &NavigationWidget::fileDoubleClicked,
                             &window, [&](const QString& filePath) {
                                 if (filePath == navigationFile)
                                     fileDoubleClicked = true;
                             });
            QTest::mouseClick(fileTree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
            QTest::mouseDClick(fileTree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            expectBool("navigation file double-click signal emitted", fileDoubleClicked, true);

            expectBool("navigation file double-click activates file",
                       waitUntil([&]() {
                           MyCodeEditor* current = window.tabManager->getCurrentEditor();
                           return current
                                  && window.tabManager
                                         ->getDocumentForEditor(current)
                                         .fileName == navigationFile;
                       }, 2000),
                       true);
        }
    }

    runRtlInsightsPanelRegression(window, normalizedSymbolFixturePath);
    runInsightFocusIntegrationRegression(window,
                                         normalizedSymbolFixturePath,
                                         workspacePath);
    expectBool("full app signal usage hotspot screenshot saved",
               saveFullAppSignalUsageHotspotScreenshot(window,
                                                       normalizedSymbolFixturePath),
               true);
    runRtlInsightsSemanticDiffRegression(window, normalizedSymbolFixturePath);

    drainRelationshipWork(window);

    if (problemsScopeCombo(window)) {
        const QString externalDiagnosticPath =
            QDir(diagnosticDir.path()).absoluteFilePath(
                QStringLiteral("external_workspace_probe.sv"));
        QFile externalDiagnosticFile(externalDiagnosticPath);
        if (externalDiagnosticFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            externalDiagnosticFile.write("module external_workspace_probe; endmodule\n");
            externalDiagnosticFile.close();
        }
        SemanticDiagnostic closeDiagnostic;
        closeDiagnostic.fileName = externalDiagnosticPath;
        closeDiagnostic.line = 3;
        closeDiagnostic.column = 1;
        closeDiagnostic.message = QStringLiteral("workspace close probe");
        closeDiagnostic.severity = SemanticDiagnostic::Error;
        SemanticIndex::getInstance()->setSnapshot(
            snapshotFromRecords(
                QList<SemanticSymbolRecord>{},
                QList<SemanticRelationship>{},
                QList<SemanticDiagnostic>{closeDiagnostic}));
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        semanticPanelRefresh(window)->updateProblemsPanel();
        expectBool("problems close probe visible",
                   navigableItemCount(problemsTree(window)) == 1,
                   true);
        window.workspaceManager->closeWorkspace();
        expectBool("problems clear on workspace close",
                   waitUntil([&]() {
                       return navigableItemCount(problemsTree(window)) == 0;
                   }, 2000),
                   true);
        SemanticIndex::getInstance()->setSnapshot(
            snapshotFromRecords(
                QList<SemanticSymbolRecord>{},
                QList<SemanticRelationship>{},
                QList<SemanticDiagnostic>{closeDiagnostic}));
        semanticPanelRefresh(window)->updateProblemsPanel();
        expectBool("problems reopen probe visible",
                   navigableItemCount(problemsTree(window)) == 1,
                   true);
        workspaceSymbolsDone = false;
        workspaceFilesScanned = false;
        if (problemsBandCombo(window))
            problemsBandCombo(window)->setCurrentIndex(0);
        expectBool("reopen workspace after close",
                   window.workspaceManager->openWorkspace(workspacePath), true);
        expectBool("reopened workspace file scan completes",
                   waitUntil([&]() { return workspaceFilesScanned; }, 10000),
                   true);
        expectBool("problems preserve external diagnostic on workspace analysis start",
                   waitUntil([&]() {
                       if (semanticPanelRefresh(window))
                           semanticPanelRefresh(window)->updateProblemsPanel();
                       return hasDiagnosticTreeItem(
                           problemsTree(window),
                           closeDiagnostic.fileName,
                           closeDiagnostic.message);
                   }, 2000),
                   true);
        expectBool("reopened workspace analysis completes",
                   waitUntil([&]() { return workspaceSymbolsDone; }, 60000),
                   true);
        expectBool("workspace config rebuild removes out-of-scope external diagnostic",
                   waitUntil([&]() {
                       const auto snapshot = SemanticIndex::getInstance()->snapshot();
                       return snapshot
                              && snapshot->getDiagnostics(
                                     closeDiagnostic.fileName).isEmpty();
                   }, 2000),
                   true);
        problemsScopeCombo(window)->setCurrentIndex(
            problemsScopeCombo(window)->findText(QStringLiteral("All Files")));
        semanticPanelRefresh(window)->updateProblemsPanel();
        expectBool("Problems drops external diagnostic after workspace rebuild",
                   waitUntil([&]() {
                       if (semanticPanelRefresh(window))
                           semanticPanelRefresh(window)->updateProblemsPanel();
                       return !hasDiagnosticTreeItem(
                           problemsTree(window),
                           closeDiagnostic.fileName,
                           closeDiagnostic.message);
                   }, 2000),
                   true);
        drainRelationshipWork(window);
    }

    runContextRailIconRegression(window);
    runLiveInsightSidebarRoutingRegression(
        window, normalizedSymbolFixturePath);

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
