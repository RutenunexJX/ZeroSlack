#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "liveinsightscontextview.h"
#include "liveinsightsession.h"
#include "liveinsighttoolpage.h"

#include <QApplication>
#include <QCheckBox>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QStyleFactory>
#include <QThread>
#include <QVBoxLayout>

#include <array>
#include <cstdio>
#include <functional>
#include <utility>

namespace {
int gFailures = 0;

void expect(bool condition, const QString& message)
{
    const QByteArray encoded = message.toUtf8();
    std::printf("[%s] %s\n",
                condition ? "PASS" : "FAIL",
                encoded.constData());
    if (!condition)
        ++gFailures;
}

QString themeName(ThemeMode mode)
{
    return mode == ThemeMode::Dark
        ? QStringLiteral("dark")
        : QStringLiteral("light");
}

QString scenarioName(const QSize& size, ThemeMode mode)
{
    return QStringLiteral("%1x%2 %3")
        .arg(size.width())
        .arg(size.height())
        .arg(themeName(mode));
}

LiveInsightRequestKey requestKey(LiveInsightKind kind,
                                 quint64 revision)
{
    LiveInsightRequestKey key;
    key.kind = kind;
    key.workspaceId = QStringLiteral("snapshot-workspace");
    key.documentId = QStringLiteral("rtl/control_fsm.sv");
    key.documentRevision = revision;
    key.semanticRevision = 120 + revision;
    key.contextKey = QStringLiteral("soc_top/traffic_controller");
    return key;
}

bool pumpUntil(const std::function<bool()>& predicate,
               int timeoutMs = 1000)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QApplication::processEvents();
        if (predicate())
            return true;
        QThread::msleep(1);
    } while (timer.elapsed() < timeoutMs);
    QApplication::processEvents();
    return predicate();
}

bool fullyVisibleTo(QWidget* ancestor, QWidget* child)
{
    if (!ancestor || !child || !child->isVisibleTo(ancestor))
        return false;
    const QRect childRect(child->mapTo(ancestor, QPoint(0, 0)),
                          child->size());
    return ancestor->rect().contains(
        childRect.adjusted(0, 0, -1, -1));
}

bool buttonTextFits(const QPushButton* button)
{
    if (!button)
        return false;
    const int horizontalPadding = 18;
    return button->fontMetrics().horizontalAdvance(button->text())
        <= qMax(0, button->contentsRect().width() - horizontalPadding);
}

bool labelTextFits(const QLabel* label)
{
    if (!label)
        return false;
    if (label->wordWrap())
        return label->sizeHint().height() <= label->height();
    return label->fontMetrics().horizontalAdvance(label->text())
        <= label->contentsRect().width();
}

bool imageLooksNonBlank(const QImage& image)
{
    if (image.width() < 900 || image.height() < 700)
        return false;
    QSet<QRgb> colors;
    int opaqueSamples = 0;
    const int stepX = qMax(1, image.width() / 180);
    const int stepY = qMax(1, image.height() / 120);
    for (int y = 0; y < image.height(); y += stepY) {
        for (int x = 0; x < image.width(); x += stepX) {
            const QColor pixel = image.pixelColor(x, y);
            colors.insert(pixel.rgba());
            if (pixel.alpha() > 240)
                ++opaqueSamples;
        }
    }
    return colors.size() > 40 && opaqueSamples > 1000;
}

class LiveInsightsSnapshotWindow final : public QWidget
{
public:
    explicit LiveInsightsSnapshotWindow(const QSize& targetSize)
        : targetSizeValue(targetSize)
    {
        setObjectName(QStringLiteral("liveInsightsSnapshotWindow"));
        setWindowTitle(QStringLiteral("ZeroSlack Live Insights Snapshot"));
        setAttribute(Qt::WA_DontShowOnScreen, true);
        setFixedSize(targetSizeValue);
        buildUi();
        configureSession();
    }

    bool prepare()
    {
        sessionValue.setConsumerVisible(
            &moduleConsumer, LiveInsightKind::Module, true);
        sessionValue.setConsumerVisible(
            &stateConsumer, LiveInsightKind::State, true);
        sessionValue.setConsumerVisible(
            &hotspotConsumer, LiveInsightKind::Hotspot, true);
        sessionValue.setConsumerVisible(
            &kernelConsumer, LiveInsightKind::Kernel, true);

        bool ready = publishReady(
            LiveInsightKind::Module,
            31,
            QStringLiteral(
                "12 modules · 28 instances\nTop: traffic_controller"));
        ready = publishReady(
                    LiveInsightKind::State,
                    32,
                    QStringLiteral(
                        "4 states · 6 transitions\nCurrent state: RUN"))
            && ready;
        ready = publishReady(
                    LiveInsightKind::Hotspot,
                    33,
                    QStringLiteral(
                        "ctrl_state · 19 semantic uses\nPeak density: always_ff"))
            && ready;
        ready = publishReady(
                    LiveInsightKind::Kernel,
                    34,
                    QStringLiteral(
                        "Signal Kernel Graph\nStable data-flow inputs and outputs."))
            && ready;
        sessionValue.setConsumerVisible(
            &hotspotConsumer, LiveInsightKind::Hotspot, false);
        sessionValue.requestUpdate(
            requestKey(LiveInsightKind::Hotspot, 43),
            {{QStringLiteral("summary"),
              QStringLiteral("new hidden hotspot result")}});

        sessionValue.requestUpdate(
            requestKey(LiveInsightKind::State, 42),
            {{QStringLiteral("summary"),
              QStringLiteral("new state graph")}});
        sessionValue.flushPending(LiveInsightKind::State);
        ready = sessionValue.snapshot(LiveInsightKind::State).phase
                == LiveInsightPhase::Building
            && ready;
        return ready;
    }

    bool verifyAndSelectKernel(const QString& scenario)
    {
        const std::array<LiveInsightKind, 4> kinds = {
            LiveInsightKind::Module,
            LiveInsightKind::State,
            LiveInsightKind::Hotspot,
            LiveInsightKind::Kernel,
        };
        for (LiveInsightKind kind : kinds) {
            QPushButton* button = insightsView->kindButton(kind);
            button->click();
            QApplication::processEvents();
            expect(insightsView->selectedKind() == kind,
                   QStringLiteral("%1 switches to %2 insight")
                       .arg(scenario,
                            liveInsightKindDisplayName(kind)));
            expect(button->isChecked(),
                   QStringLiteral("%1 keeps %2 card selected")
                       .arg(scenario,
                            liveInsightKindDisplayName(kind)));
            expect(insightsView->kindSummaryLabel(kind)
                       ->isVisibleTo(insightsView),
                   QStringLiteral("%1 exposes %2 summary")
                       .arg(scenario,
                            liveInsightKindDisplayName(kind)));
        }
        QApplication::processEvents();

        expect(insightsView->kindStatusLabel(
                   LiveInsightKind::Module)->text()
                   == QStringLiteral("Current"),
               scenario + QStringLiteral(" shows Module freshness"));
        expect(insightsView->kindStatusLabel(
                   LiveInsightKind::State)->text()
                   == QStringLiteral("Stale · updating"),
               scenario + QStringLiteral(" shows State stale/update freshness"));
        expect(insightsView->kindStatusLabel(
                   LiveInsightKind::Hotspot)->text()
                   == QStringLiteral("Update pending"),
               scenario + QStringLiteral(" shows Hotspot hidden-dirty freshness"));
        expect(insightsView->kindStatusLabel(
                   LiveInsightKind::Kernel)->text()
                   == QStringLiteral("Current"),
               scenario + QStringLiteral(" shows Kernel freshness"));
        expect(insightsView->kindSummaryLabel(
                   LiveInsightKind::Kernel)->text()
                   .contains(QStringLiteral("Signal Kernel Graph"))
               && insightsView->kindSummaryLabel(
                          LiveInsightKind::Kernel)->text()
                          .contains(QStringLiteral("data-flow")),
               scenario + QStringLiteral(" labels the shared Kernel view"));

        verifyGeometry(scenario);
        return gFailures == 0;
    }

    bool saveSnapshot(const QString& outputPath,
                      const QString& scenario)
    {
        QImage image(targetSizeValue,
                     QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(1.0);
        image.fill(InsightVisualStyle::theme().appBackground);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        render(&painter, QPoint(), QRegion(rect()), QWidget::DrawChildren);
        painter.end();

        QDir().mkpath(QFileInfo(outputPath).absolutePath());
        const bool saved = image.save(outputPath, "PNG");
        const QImage reloaded(outputPath);
        expect(saved,
               scenario + QStringLiteral(" saves PNG"));
        expect(reloaded.size() == targetSizeValue,
               scenario + QStringLiteral(" preserves exact pixel dimensions"));
        expect(imageLooksNonBlank(reloaded),
               scenario + QStringLiteral(" screenshot is nonblank"));
        std::printf("snapshot %s %dx%d\n",
                    QFileInfo(outputPath)
                        .absoluteFilePath().toUtf8().constData(),
                    reloaded.width(),
                    reloaded.height());
        return saved && reloaded.size() == targetSizeValue
            && imageLooksNonBlank(reloaded);
    }

private:
    void buildUi()
    {
        const InsightTheme& theme = InsightVisualStyle::theme();
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* appHeader = new QFrame(this);
        appHeader->setObjectName(QStringLiteral("snapshotAppHeader"));
        appHeader->setFixedHeight(44);
        appHeader->setStyleSheet(QStringLiteral(
            "QFrame#snapshotAppHeader { background: %1; "
            "border-bottom: 1px solid %2; }").arg(
                theme.toolbarBackground.name(),
                theme.border.name()));
        auto* appHeaderLayout = new QHBoxLayout(appHeader);
        appHeaderLayout->setContentsMargins(14, 6, 14, 6);
        auto* productTitle = new QLabel(
            QStringLiteral("ZeroSlack  ·  RTL Workspace"), appHeader);
        InsightVisualStyle::applyLabel(productTitle, true);
        appHeaderLayout->addWidget(productTitle);
        appHeaderLayout->addStretch();
        auto* revision = new QLabel(
            QStringLiteral("STATE SHELL  ·  semantic revision 154"), appHeader);
        revision->setObjectName(QStringLiteral("snapshotRevisionStatus"));
        revision->setStyleSheet(
            InsightVisualStyle::statusChipStyleSheet(
                InsightStatusTone::Success,
                revision->objectName()));
        appHeaderLayout->addWidget(revision);
        root->addWidget(appHeader);

        auto* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->setObjectName(QStringLiteral("snapshotMainSplitter"));
        splitter->setHandleWidth(1);
        splitter->setChildrenCollapsible(false);

        auto* editorPanel = new QFrame(splitter);
        editorPanel->setObjectName(QStringLiteral("snapshotEditorPanel"));
        InsightVisualStyle::applyPanel(editorPanel);
        auto* editorLayout = new QVBoxLayout(editorPanel);
        editorLayout->setContentsMargins(10, 10, 10, 10);
        editorLayout->setSpacing(7);
        auto* editorHeader = new QHBoxLayout;
        auto* fileName = new QLabel(
            QStringLiteral("rtl/control_fsm.sv"), editorPanel);
        InsightVisualStyle::applyLabel(fileName, true);
        editorHeader->addWidget(fileName);
        editorHeader->addStretch();
        auto* sourceStatus = new QLabel(
            QStringLiteral("SystemVerilog  ·  Follow Editor"), editorPanel);
        sourceStatus->setObjectName(QStringLiteral("snapshotSourceStatus"));
        sourceStatus->setStyleSheet(
            InsightVisualStyle::statusChipStyleSheet(
                InsightStatusTone::Info,
                sourceStatus->objectName()));
        editorHeader->addWidget(sourceStatus);
        editorLayout->addLayout(editorHeader);

        auto* source = new QPlainTextEdit(editorPanel);
        source->setObjectName(QStringLiteral("snapshotSourceEditor"));
        source->setReadOnly(true);
        source->setLineWrapMode(QPlainTextEdit::NoWrap);
        source->setFont(QFontDatabase::systemFont(
            QFontDatabase::FixedFont));
        source->setPlainText(QStringLiteral(
            "  1  module traffic_controller (\n"
            "  2      input  logic       clk,\n"
            "  3      input  logic       rst_n,\n"
            "  4      input  logic       request,\n"
            "  5      output logic [1:0] phase\n"
            "  6  );\n"
            "  7\n"
            "  8    typedef enum logic [1:0] {\n"
            "  9      IDLE, ARBITRATE, RUN, ERROR\n"
            " 10    } state_t;\n"
            " 11\n"
            " 12    state_t state_q, state_d;\n"
            " 13\n"
            " 14    always_ff @(posedge clk or negedge rst_n) begin\n"
            " 15      if (!rst_n) state_q <= IDLE;\n"
            " 16      else        state_q <= state_d;\n"
            " 17    end\n"
            " 18\n"
            " 19    always_comb begin\n"
            " 20      state_d = state_q;\n"
            " 21      unique case (state_q)\n"
            " 22        IDLE:      if (request) state_d = ARBITRATE;\n"
            " 23        ARBITRATE:             state_d = RUN;\n"
            " 24        RUN:       if (!request) state_d = IDLE;\n"
            " 25        default:               state_d = ERROR;\n"
            " 26      endcase\n"
            " 27    end\n"
            " 28  endmodule\n"));
        source->setStyleSheet(QStringLiteral(
            "QPlainTextEdit#snapshotSourceEditor {"
            " background: %1; color: %2; border: 1px solid %3;"
            " selection-background-color: %4; padding: 10px; }").arg(
                theme.canvasBackground.name(),
                theme.textPrimary.name(),
                theme.border.name(),
                theme.input.selectionBackground.name()));
        editorLayout->addWidget(source, 1);

        auto* contextPanel = new QFrame(splitter);
        contextPanel->setObjectName(QStringLiteral("snapshotContextWorkspace"));
        contextPanel->setMinimumWidth(350);
        contextPanel->setStyleSheet(QStringLiteral(
            "QFrame#snapshotContextWorkspace { background: %1; "
            "border-left: 1px solid %2; }").arg(
                theme.panelBackground.name(),
                theme.borderStrong.name()));
        auto* contextRoot = new QVBoxLayout(contextPanel);
        contextRoot->setContentsMargins(8, 8, 8, 8);
        contextRoot->setSpacing(6);

        auto* contextTitle = new QLabel(
            QStringLiteral("CONTEXT WORKSPACE"), contextPanel);
        contextTitle->setObjectName(
            QStringLiteral("snapshotContextWorkspaceTitle"));
        InsightVisualStyle::applyTitleLabel(contextTitle);
        contextRoot->addWidget(contextTitle);

        auto* contextBody = new QHBoxLayout;
        contextBody->setContentsMargins(0, 0, 0, 0);
        contextBody->setSpacing(6);
        insightsView = new LiveInsightsContextView(
            &sessionValue, contextPanel);
        insightsView->setObjectName(
            QStringLiteral("snapshotLiveInsightsContextView"));
        insightsView->setWorkspaceId(
            QStringLiteral("snapshot-workspace"));
        contextBody->addWidget(insightsView, 1);

        auto* rail = new QFrame(contextPanel);
        rail->setObjectName(QStringLiteral("snapshotContextRail"));
        rail->setFixedWidth(46);
        rail->setStyleSheet(QStringLiteral(
            "QFrame#snapshotContextRail { background: %1; "
            "border-left: 1px solid %2; }").arg(
                theme.toolbarBackground.name(),
                theme.border.name()));
        auto* railLayout = new QVBoxLayout(rail);
        railLayout->setContentsMargins(5, 6, 5, 6);
        railLayout->setSpacing(5);
        const QStringList railLabels = {
            QStringLiteral("LI"),
            QStringLiteral("ED"),
            QStringLiteral("PL")
        };
        for (int index = 0; index < railLabels.size(); ++index) {
            auto* button = new QPushButton(railLabels.at(index), rail);
            button->setObjectName(
                QStringLiteral("snapshotRailButton_%1").arg(index));
            button->setCheckable(true);
            button->setChecked(index == 0);
            button->setFixedSize(34, 34);
            InsightVisualStyle::applyToolbarButton(button);
            railLayout->addWidget(button);
        }
        railLayout->addStretch();
        contextBody->addWidget(rail);
        contextRoot->addLayout(contextBody, 1);

        auto* freshness = new QLabel(
            QStringLiteral(
                "FRESHNESS  ·  Current  ·  Stale / updating  ·  Update pending"),
            contextPanel);
        freshness->setObjectName(
            QStringLiteral("snapshotFreshnessLegend"));
        freshness->setWordWrap(true);
        freshness->setStyleSheet(
            InsightVisualStyle::statusChipStyleSheet(
                InsightStatusTone::Info,
                freshness->objectName()));
        contextRoot->addWidget(freshness);

        symbolicPreviewLabel = new QLabel(
            QStringLiteral(
                "WORKBENCH  ·  Shared InsightGraphCore / InsightCanvas  ·  "
                "five independently routed views"),
            contextPanel);
        symbolicPreviewLabel->setObjectName(
            QStringLiteral("snapshotSymbolicPreviewNotice"));
        symbolicPreviewLabel->setWordWrap(true);
        symbolicPreviewLabel->setStyleSheet(
            InsightVisualStyle::statusChipStyleSheet(
                InsightStatusTone::Warning,
                symbolicPreviewLabel->objectName()));
        contextRoot->addWidget(symbolicPreviewLabel);

        splitter->addWidget(editorPanel);
        splitter->addWidget(contextPanel);
        const int contextWidth = qBound(
            370,
            qRound(targetSizeValue.width() * 0.38),
            520);
        splitter->setSizes(
            {targetSizeValue.width() - contextWidth, contextWidth});
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 0);
        root->addWidget(splitter, 1);

        auto* footer = new QLabel(
            QStringLiteral(
                "Ready  ·  workspace: demo_soc  ·  latest semantic snapshot accepted"),
            this);
        footer->setObjectName(QStringLiteral("snapshotFooter"));
        footer->setFixedHeight(28);
        footer->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        footer->setStyleSheet(QStringLiteral(
            "QLabel#snapshotFooter { background: %1; color: %2; "
            "border-top: 1px solid %3; padding-left: 12px; }").arg(
                theme.statusBar.background.name(),
                theme.statusBar.text.name(),
                theme.statusBar.border.name()));
        root->addWidget(footer);

        contextPanelValue = contextPanel;
    }

    void configureSession()
    {
        sessionValue.setTaskExecutor(
            [this](LiveInsightSession::Task task) {
                pendingTasks.append(std::move(task));
            });
        const auto builder = [](
            const LiveInsightBuildRequest& request,
            const LiveInsightCancellationToken& cancellation) {
            if (cancellation.isCancellationRequested())
                return LiveInsightBuildResult::cancellation(request);
            return LiveInsightBuildResult::success(
                request,
                {{QStringLiteral("summary"),
                  request.input.value(QStringLiteral("summary"))},
                 {QStringLiteral("kernelGraph"),
                  request.key.kind == LiveInsightKind::Kernel}});
        };
        sessionValue.setBuilder(LiveInsightKind::Module, builder);
        sessionValue.setBuilder(LiveInsightKind::State, builder);
        sessionValue.setBuilder(LiveInsightKind::Hotspot, builder);
        sessionValue.setBuilder(LiveInsightKind::Kernel, builder);
    }

    bool publishReady(LiveInsightKind kind,
                      quint64 revision,
                      const QString& summary)
    {
        sessionValue.requestUpdate(
            requestKey(kind, revision),
            {{QStringLiteral("summary"), summary}});
        sessionValue.flushPending(kind);
        if (pendingTasks.isEmpty())
            return false;
        LiveInsightSession::Task task = pendingTasks.takeFirst();
        task();
        return pumpUntil(
            [this, kind]() {
                return sessionValue.snapshot(kind).phase
                    == LiveInsightPhase::Ready;
            });
    }

    void verifyGeometry(const QString& scenario)
    {
        expect(size() == targetSizeValue,
               scenario + QStringLiteral(" keeps requested window size"));
        expect(fullyVisibleTo(this, contextPanelValue),
               scenario + QStringLiteral(" keeps Context Workspace visible"));
        expect(fullyVisibleTo(contextPanelValue, insightsView),
               scenario + QStringLiteral(" keeps Live Insights inside Context Workspace"));
        expect(fullyVisibleTo(contextPanelValue, symbolicPreviewLabel),
               scenario + QStringLiteral(" keeps Workbench notice visible"));

        const std::array<LiveInsightKind, 4> kinds = {
            LiveInsightKind::Kernel,
            LiveInsightKind::Module,
            LiveInsightKind::State,
            LiveInsightKind::Hotspot,
        };
        for (LiveInsightKind kind : kinds) {
            QPushButton* button = insightsView->kindButton(kind);
            QLabel* status = insightsView->kindStatusLabel(kind);
            const QString name = liveInsightKindDisplayName(kind);
            expect(fullyVisibleTo(insightsView, button),
                   QStringLiteral("%1 keeps %2 card visible")
                       .arg(scenario, name));
            expect(fullyVisibleTo(insightsView, status),
                   QStringLiteral("%1 keeps %2 freshness visible")
                       .arg(scenario, name));
            expect(buttonTextFits(button),
                   QStringLiteral("%1 does not clip %2 card label")
                       .arg(scenario, name));
            expect(labelTextFits(status),
                   QStringLiteral("%1 does not clip %2 freshness text")
                       .arg(scenario, name));
        }
        expect(!insightsView->saveState().contains(QStringLiteral("followEditor"))
                   && !insightsView->saveState().contains(QStringLiteral("pinned")),
               scenario + QStringLiteral(" removes Follow Editor and Pin state"));
        expect(fullyVisibleTo(
                   insightsView, insightsView->openFullViewButton())
                   && buttonTextFits(
                       insightsView->openFullViewButton()),
               scenario + QStringLiteral(" keeps Open Full View unclipped"));
    }

    QSize targetSizeValue;
    LiveInsightSession sessionValue;
    QObject moduleConsumer;
    QObject stateConsumer;
    QObject hotspotConsumer;
    QObject kernelConsumer;
    QList<LiveInsightSession::Task> pendingTasks;
    LiveInsightsContextView* insightsView = nullptr;
    QFrame* contextPanelValue = nullptr;
    QLabel* symbolicPreviewLabel = nullptr;
};

bool renderScenario(const QSize& size,
                    ThemeMode mode,
                    const QString& outputDirectory)
{
    ApplicationThemeManager::instance().setMode(mode);
    LiveInsightsSnapshotWindow window(size);
    const QString scenario = scenarioName(size, mode);
    expect(window.prepare(),
           scenario + QStringLiteral(" prepares deterministic session states"));
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();
    window.verifyAndSelectKernel(scenario);

    const QString outputPath = QDir(outputDirectory).filePath(
        QStringLiteral("live_insights_context_%1x%2_%3.png")
            .arg(size.width())
            .arg(size.height())
            .arg(themeName(mode)));
    return window.saveSnapshot(outputPath, scenario);
}


}

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("live_insights_ui_snapshot_test"));
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        app.setStyle(fusion);

    const int fontId = QFontDatabase::addApplicationFont(
        QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
    if (fontId >= 0) {
        const QStringList families =
            QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty())
            app.setFont(QFont(families.constFirst(), 9));
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Render deterministic ZeroSlack Live Insights UI snapshots."));
    parser.addHelpOption();
    const QCommandLineOption outputOption(
        {QStringLiteral("o"), QStringLiteral("output-dir")},
        QStringLiteral("Directory for generated PNG files."),
        QStringLiteral("directory"));
    parser.addOption(outputOption);
    parser.addPositionalArgument(
        QStringLiteral("output-dir"),
        QStringLiteral("Optional output directory when --output-dir is omitted."),
        QStringLiteral("[output-dir]"));
    parser.process(app);

    QString outputDirectory = parser.value(outputOption).trimmed();
    if (outputDirectory.isEmpty()
        && !parser.positionalArguments().isEmpty()) {
        outputDirectory = parser.positionalArguments().constFirst();
    }
    if (outputDirectory.isEmpty()) {
        outputDirectory = QDir::current().filePath(
            QStringLiteral("artifacts/ui/live-insights"));
    }
    outputDirectory = QFileInfo(outputDirectory).absoluteFilePath();
    if (!QDir().mkpath(outputDirectory)) {
        std::fprintf(stderr,
                     "Unable to create output directory: %s\n",
                     outputDirectory.toUtf8().constData());
        return 2;
    }

    const std::array<QSize, 2> sizes = {
        QSize(960, 720),
        QSize(1440, 900)
    };
    const std::array<ThemeMode, 2> modes = {
        ThemeMode::Light,
        ThemeMode::Dark
    };
    bool savedAll = true;
    for (const QSize& size : sizes) {
        for (ThemeMode mode : modes)
            savedAll = renderScenario(size, mode, outputDirectory) && savedAll;
    }

    std::printf("output_dir %s\n",
                QDir(outputDirectory).absolutePath().toUtf8().constData());
    std::printf("assertions %s failures=%d\n",
                gFailures == 0 ? "passed" : "failed",
                gFailures);
    return savedAll && gFailures == 0 ? 0 : 1;
}
