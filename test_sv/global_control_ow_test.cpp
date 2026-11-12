// Integration regression for the real Global Control "ow 1" command path.
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFile>
#include <QFileSystemWatcher>
#include <QPointer>
#include <QQueue>
#include <QSettings>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QThreadPool>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QCryptographicHash>

#include <cstdio>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include "testuistyle.h"
#include "navigationrowstyle.h"

#define private public
#include "fixture_names.h"
#include "mainwindow.h"
#include "workspacesessioncoordinator.h"
#include "analysisscheduler.h"
#include "documentmodel.h"
#include "editorruntime.h"
#include "effectivevalueservice.h"
#include "filecommandcoordinator.h"
#include "globalcontrolcoordinator.h"
#include "ghostannotationservice.h"
#include "mycodeeditor.h"
#include "navigationmanager.h"
#include "navigationpanecoordinator.h"
#include "navigationservice.h"
#include "navigationwidget.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "settingscenterservice.h"
#include "symbolanalyzer.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#undef private
#include "designhierarchymodel.h"

namespace {
int failures = 0;

class ScopedThreadPoolMinimum
{
public:
    explicit ScopedThreadPoolMinimum(int minimum)
        : pool(QThreadPool::globalInstance()),
          originalMaximum(pool ? pool->maxThreadCount() : 0)
    {
        if (pool && originalMaximum < minimum)
            pool->setMaxThreadCount(minimum);
    }

    ~ScopedThreadPoolMinimum()
    {
        if (pool)
            pool->setMaxThreadCount(originalMaximum);
    }

    ScopedThreadPoolMinimum(const ScopedThreadPoolMinimum&) = delete;
    ScopedThreadPoolMinimum& operator=(const ScopedThreadPoolMinimum&) = delete;

private:
    QThreadPool* pool = nullptr;
    int originalMaximum = 0;
};

QString normalizedPath(const QString& path)
{
    QString normalized = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

void check(const char* label, bool condition)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
    std::fflush(stdout);
    if (!condition)
        ++failures;
}

void check(const QString& label, bool condition)
{
    const QByteArray utf8 = label.toUtf8();
    check(utf8.constData(), condition);
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (predicate())
            return true;
        QThread::msleep(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

bool remainsTrueFor(const std::function<bool()>& predicate,
                    int durationMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < durationMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!predicate())
            return false;
        QThread::msleep(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

GlobalControlItem openOneWorkspaceItem()
{
    GlobalControlItem item;
    item.kind = GlobalControlItemKind::Command;
    item.id = QStringLiteral("ow 1");
    item.title = QStringLiteral("ow 1");
    item.subtitle = QStringLiteral("Open 1 workspace");
    return item;
}

bool semanticRecordsBelongTo(const QString& workspaceRoot)
{
    const QString normalizedRoot = normalizedPath(workspaceRoot);
    const QString rootPrefix = normalizedRoot.endsWith(QLatin1Char('/'))
        ? normalizedRoot
        : normalizedRoot + QLatin1Char('/');
    const QList<SemanticSymbolRecord> records =
        SemanticIndex::getInstance()->getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        const QString fileName = normalizedPath(record.location.fileName);
        if (fileName == normalizedRoot || fileName.startsWith(rootPrefix))
            return true;
    }
    return false;
}

SemanticSymbolRecord semanticRecordNamed(const QString& workspaceRoot,
                                         const QString& name)
{
    const QString normalizedRoot = normalizedPath(workspaceRoot);
    const QString rootPrefix = normalizedRoot.endsWith(QLatin1Char('/'))
        ? normalizedRoot
        : normalizedRoot + QLatin1Char('/');
    const QList<SemanticSymbolRecord> records =
        SemanticIndex::getInstance()->getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        if (record.name != name
            || record.declarationKind
                   != SymbolTaxonomy::DeclarationKind::Module) {
            continue;
        }
        const QString fileName = normalizedPath(record.location.fileName);
        if ((fileName == normalizedRoot || fileName.startsWith(rootPrefix))
            && record.stableKey.isValid()) {
            return record;
        }
    }
    return {};
}

bool relationshipResultsQueryableFor(
    const QString& workspaceRoot,
    const SemanticSymbolRecord& preferredRecord)
{
    SemanticIndex* index = SemanticIndex::getInstance();
    SymbolRelationshipEngine* engine = index->relationshipEngine();
    if (!engine || engine->getRelationshipCount() <= 0)
        return false;

    auto hasResults = [index](const SemanticSymbolRecord& record) {
        return !index->getRelationshipResults(record.stableKey, true).isEmpty()
            || !index->getRelationshipResults(record.stableKey, false).isEmpty();
    };
    if (preferredRecord.stableKey.isValid() && hasResults(preferredRecord))
        return true;

    const QString normalizedRoot = normalizedPath(workspaceRoot);
    const QString rootPrefix = normalizedRoot.endsWith(QLatin1Char('/'))
        ? normalizedRoot
        : normalizedRoot + QLatin1Char('/');
    const QList<SemanticSymbolRecord> records = index->getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        if (!record.stableKey.isValid())
            continue;
        if (record.declarationKind != SymbolTaxonomy::DeclarationKind::Module
            && record.declarationKind
                   != SymbolTaxonomy::DeclarationKind::Instance) {
            continue;
        }
        const QString fileName = normalizedPath(record.location.fileName);
        if (fileName != normalizedRoot && !fileName.startsWith(rootPrefix))
            continue;
        if (hasResults(record))
            return true;
    }
    return false;
}

SemanticSymbolRecord semanticRecordInFile(
    const QString& fileName,
    const QString& name,
    SymbolTaxonomy::DeclarationKind declarationKind)
{
    const QString expectedFile = normalizedPath(fileName);
    const QList<SemanticSymbolRecord> records =
        SemanticIndex::getInstance()->getSymbolRecordsByName(name);
    for (const SemanticSymbolRecord& record : records) {
        if (record.declarationKind == declarationKind
            && normalizedPath(record.location.fileName) == expectedFile
            && record.stableKey.isValid()) {
            return record;
        }
    }
    return {};
}

QSet<QString> stableRelationshipKeys(
    const std::shared_ptr<const SemanticIndexSnapshot>& snapshot)
{
    QSet<QString> result;
    if (!snapshot)
        return result;
    for (const SemanticRelationship& relationship : snapshot->relationships()) {
        const QString key = semanticRelationshipStableKeyText(relationship);
        if (!key.isEmpty())
            result.insert(key);
    }
    return result;
}

QSet<QString> designInstanceKeys(const DesignHierarchyReport& report)
{
    QSet<QString> result;
    for (const DesignHierarchyNode& node : report.nodes) {
        result.insert(QStringLiteral("%1|%2|%3|%4")
                          .arg(node.instancePath,
                               node.moduleType,
                               normalizedPath(node.definitionFile),
                               node.unresolved
                                   ? QStringLiteral("unresolved")
                                   : QStringLiteral("resolved")));
    }
    return result;
}

bool designTreeContainsNestedModule(
    QTreeView* tree,
    const QString& moduleType)
{
    auto* model = tree ? qobject_cast<DesignHierarchyModel*>(tree->model()) : nullptr;
    if (!model)
        return false;
    for (const auto& node : model->report()->nodes) {
        if (node.moduleType != moduleType || node.parentId.isEmpty()) continue;
        const auto index = model->indexForId(node.id);
        if (index.isValid() && index.parent().isValid()
            && index.data(NavigationRowStyle::TypeRole).toString() == moduleType)
            return true;
    }
    return false;
}

bool designContainsModuleType(const DesignHierarchyReport& report,
                              const QString& moduleType)
{
    for (const DesignHierarchyNode& node : report.nodes) {
        if (node.moduleType == moduleType)
            return true;
    }
    return false;
}

QSet<QString> ghostAnnotationKeys(const QList<GhostAnnotation>& annotations)
{
    QSet<QString> result;
    for (const GhostAnnotation& annotation : annotations) {
        result.insert(QStringLiteral("%1|%2|%3|%4|%5")
                          .arg(static_cast<int>(annotation.kind))
                          .arg(annotation.line)
                          .arg(annotation.anchorPosition)
                          .arg(annotation.anchorLength)
                          .arg(annotation.text));
    }
    return result;
}

bool hasSemanticGhostAnnotation(const QList<GhostAnnotation>& annotations)
{
    for (const GhostAnnotation& annotation : annotations) {
        if (annotation.kind == GhostAnnotationKind::FormalPort
            || annotation.kind == GhostAnnotationKind::ParameterOverride) {
            return true;
        }
    }
    return false;
}

bool hasGhostAnnotationKind(const QList<GhostAnnotation>& annotations,
                            GhostAnnotationKind kind)
{
    return std::any_of(annotations.cbegin(), annotations.cend(),
                       [kind](const GhostAnnotation& annotation) {
                           return annotation.kind == kind;
                       });
}

bool hasRevisionBoundGhostAnnotation(
    const QList<GhostAnnotation>& annotations)
{
    for (const GhostAnnotation& annotation : annotations) {
        if (annotation.kind == GhostAnnotationKind::SignalWidth
            || annotation.kind
                   == GhostAnnotationKind::ConcatenationWidth) {
            return true;
        }
    }
    return false;
}

bool sameSourceIdentityAndPresentation(
    const SemanticSymbolRecord& before,
    const SemanticSymbolRecord& after)
{
    return before.isValid()
        && after.isValid()
        && before.stableKey == after.stableKey
        && before.location.position == after.location.position
        && before.location.length == after.location.length
        && before.location.startLine == after.location.startLine
        && before.location.startColumn == after.location.startColumn
        && before.location.endLine == after.location.endLine
        && before.location.endColumn == after.location.endColumn
        && before.type.rawTypeText == after.type.rawTypeText
        && before.type.resolvedTypeName == after.type.resolvedTypeName
        && (before.presentation.declarationText.isEmpty()
            || before.presentation.declarationText
                   == after.presentation.declarationText);
}

bool containsAll(const QSet<QString>& values,
                 const QSet<QString>& expected)
{
    for (const QString& item : expected) {
        if (!values.contains(item))
            return false;
    }
    return true;
}

void printSetDifference(const char* label,
                        const QSet<QString>& before,
                        const QSet<QString>& after)
{
    if (before == after)
        return;
    std::printf("[DIAG] %s before=%lld after=%lld\n",
                label,
                static_cast<long long>(before.size()),
                static_cast<long long>(after.size()));
    int printed = 0;
    for (const QString& item : before) {
        if (after.contains(item))
            continue;
        std::printf("[DIAG] %s missing-after: %s\n",
                    label,
                    item.toUtf8().constData());
        if (++printed >= 5)
            break;
    }
    printed = 0;
    for (const QString& item : after) {
        if (before.contains(item))
            continue;
        std::printf("[DIAG] %s added-after: %s\n",
                    label,
                    item.toUtf8().constData());
        if (++printed >= 5)
            break;
    }
    std::fflush(stdout);
}

class OwDesignTrace final : public QObject
{
public:
    OwDesignTrace(NavigationManager* navigation, AnalysisScheduler* scheduler,
                  TabManager* tabs, SemanticIndex* semantic, WorkspaceManager* workspace)
        : manager(navigation), widget(navigation->navigationWidget), index(semantic)
    {
        clock.start();
        if (widget) widget->installEventFilter(this);
        connections << connect(navigation, &NavigationManager::dataRefreshed, this,
            [this](NavigationManager::NavigationView view) {
                mark("dataRefreshed", {{"view", int(view)},
                    {"signalObservation", "direct"}, {"emitterSite", "unknown"},
                    {"emittingRequest", "unknown; active may already be cleared"},
                    {"triggerSource", "unknown"}});
            });
        connections << connect(navigation, &NavigationManager::navigationTelemetry, this,
            [this](const SemanticAnalysisTelemetry& telemetry) {
                mark("navigationTelemetry", {{"telemetry", telemetryJson(telemetry)},
                    {"observation", "direct telemetry; signal association requires separate evidence"}});
            });
        connections << connect(scheduler, &AnalysisScheduler::fileSymbolAnalysisStarted, this,
            [this](const QString& file) { mark("fileSymbolAnalysisStarted", {{"file", file}}); });
        connections << connect(scheduler, &AnalysisScheduler::semanticAnalysisTelemetry, this,
            [this](const SemanticAnalysisTelemetry& telemetry) {
                mark("semanticAnalysisTelemetry", {{"telemetry", telemetryJson(telemetry)}});
            });
        if (widget) {
            connections << connect(widget, &NavigationWidget::designViewStateChanged, this,
                [this](const QString& root, const QJsonObject&) {
                    mark("designViewStateChanged", {{"workspace", root}});
                });
        }
        if (workspace) {
            connections << connect(workspace, &WorkspaceManager::semanticInputsChanged, this,
                [this](const QString& root) { mark("semanticInputsChanged", {{"workspace", root}}); });
            connections << connect(workspace, &WorkspaceManager::projectChanged, this,
                [this](const ProjectSnapshot& project) {
                    mark("projectChanged", {{"workspace", project.workspaceRoot},
                        {"semanticIdentity", project.semanticIdentity()}});
                });
            for (auto* watcher : workspace->findChildren<QFileSystemWatcher*>()) {
                connections << connect(watcher, &QFileSystemWatcher::directoryChanged, this,
                    [this](const QString& path) { mark("watchedDirectoryChanged", {{"path", path}}); });
                connections << connect(watcher, &QFileSystemWatcher::fileChanged, this,
                    [this](const QString& path) { mark("watchedFileChanged", {{"path", path}}); });
            }
        }
        connections << connect(scheduler, &AnalysisScheduler::fileSymbolAnalysisFinished, this,
            [this](const QString& file, int symbols) {
                mark("fileSymbolAnalysisFinished", {{"file", file}, {"symbols", symbols}});
            });
        connections << connect(scheduler, &AnalysisScheduler::relationshipDataRefreshRequested, this,
            [this] { mark("relationshipDataRefreshRequested",
                {{"observation", "receiver delivery; other connected slots may already have run"}}); });
        connections << connect(tabs, &TabManager::activeDocumentChanged, this,
            [this](const DocumentSnapshot& document) {
                mark("activeDocumentChanged", {{"file", document.fileName},
                    {"textVersion", QString::number(document.textVersion)}});
            });
        connections << connect(tabs, &TabManager::tabCreated, this,
            [this](MyCodeEditor*) { mark("tabCreated"); });
        mark("observer-enabled-before-design-preparation");
    }

    ~OwDesignTrace() override { finish(); }

    void mark(const char* label, QJsonObject details = {})
    {
        if (finished) return;
        details["sequence"] = ++sequence;
        details["microseconds"] = clock.nsecsElapsed() / 1000;
        details["label"] = QString::fromLatin1(label);
        details["insideOpenFileInTab"] = insideOpen;
        details["state"] = state();
        events.append(details);
        const auto bytes = QJsonDocument(details).toJson(QJsonDocument::Compact);
        std::printf("OW_DESIGN_EVENT %s\n", bytes.constData());
        std::fflush(stdout);
    }

    void beginOpen() { insideOpen = true; mark("openFileInTab-call-begin"); }
    void endOpen() { mark("openFileInTab-call-end"); insideOpen = false; }

    void sample(const char* name, const QSet<QString>& keys)
    {
        mark("saved-set-sample", {{"name", QString::fromLatin1(name)},
            {"identity", identity(keys.values())}, {"use", "original saved set; sampling point preserved"}});
    }

    void snapshotSample(const char* name, const void* snapshot)
    { mark("saved-snapshot-sample", {{"name", QString::fromLatin1(name)}, {"savedSnapshotAddress", pointer(snapshot)}}); }

    void preparationPoll()
    {
        // Observe only existing predicate evaluations; do not pump events or wait.
        const auto value = state();
        const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
        if (bytes != previousPoll) {
            previousPoll = bytes;
            mark("preparation-predicate-state-change");
        }
    }

    void finish()
    {
        if (finished) return;
        mark("observer-finish", {{"testFailuresAtFinish", failures}});
        if (widget) widget->removeEventFilter(this);
        for (const auto& connection : connections) QObject::disconnect(connection);
        finished = true;
        const auto directory = qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
        if (!directory.isEmpty()) {
            QFile output(QDir(directory).filePath("ow-design-trace.json"));
            if (output.open(QIODevice::WriteOnly))
                output.write(QJsonDocument(QJsonObject{{"events", events},
                    {"platform", QGuiApplication::platformName()},
                    {"requestIdentityLimit", "observed active/pending states are not a captured emitter call stack"}}).toJson());
            else std::printf("OW_DESIGN_ARTIFACT_ERROR %s\n", qPrintable(output.errorString()));
        }
        std::fflush(stdout);
    }

    static QJsonObject identity(QStringList keys)
    {
        std::sort(keys.begin(), keys.end());
        const auto array = QJsonArray::fromStringList(keys);
        const auto bytes = QJsonDocument(array).toJson(QJsonDocument::Compact);
        return {{"size", keys.size()}, {"keys", array},
            {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
            {"encoding", "sorted QString keys; compact JSON UTF-8 array with element boundaries"}};
    }

protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (object == widget && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
            mark(event->type() == QEvent::Show ? "NavigationWidget.Show" : "NavigationWidget.Hide",
                {{"deliveryPhase", "event filter before widget handling; queued callback identity unknown"}});
        return false;
    }

private:
    static QString pointer(const void* value)
    { return QStringLiteral("0x") + QString::number(reinterpret_cast<quintptr>(value), 16); }

    static QJsonObject telemetryJson(const SemanticAnalysisTelemetry& value)
    {
        return {{"semanticRequestGeneration", QString::number(value.generation)},
            {"stage", int(value.stage)}, {"reason", int(value.reason)},
            {"detail", value.detail}, {"files", identity(value.files)},
            {"changedFiles", identity(value.changedFiles)}, {"uiRefreshMs", value.uiRefreshMs},
            {"workerMs", value.workerMs}, {"publicationMs", value.publicationMs}};
    }

    static QJsonArray rootsJson(const QList<DesignHierarchyRoot>& roots)
    {
        QJsonArray result;
        for (const auto& root : roots)
            result.append(QJsonObject{{"projectId", root.projectId}, {"name", root.name},
                {"module", root.module}, {"file", root.file}, {"expanded", root.expanded}});
        return result;
    }

    static QJsonObject requestJson(const std::optional<NavigationManager::DesignWorkRequest>& value)
    {
        if (!value) return {{"present", false}};
        const auto& request = *value;
        return {{"present", true}, {"observation", "direct fields at this sample"},
            {"generation", QString::number(request.generation)}, {"force", request.force},
            {"workspace", request.workspace}, {"selectedTop", request.selectedTop},
            {"roots", rootsJson(request.roots)}, {"files", identity(request.files)},
            {"filesOrdered", QJsonArray::fromStringList(request.files)},
            {"candidateFiles", identity(request.candidateFiles)},
            {"tokenAddress", pointer(request.token.snapshot.get())},
            {"tokenRevision", QString::number(request.token.revision)},
            {"previousStructure", QString::fromLatin1(request.previousFingerprint.toHex())},
            {"previousAnchors", QString::fromLatin1(request.previousAnchorFingerprint.toHex())},
            {"semanticTelemetry", telemetryJson(request.telemetry)}};
    }

    QJsonObject state() const
    {
        if (!manager || QThread::currentThread() != manager->thread())
            return {{"available", false}, {"reason", "manager unavailable or non-owner thread; no UI state read"}};
        const auto snapshot = index->snapshot();
        const auto& cache = manager->caches;
        return {{"available", true}, {"snapshotAddress", pointer(snapshot.get())},
            {"semanticRevision", QString::number(index->snapshotRevision())},
            {"cacheRevision", QString::number(cache.designSnapshotGeneration)},
            {"cacheValid", cache.designHierarchyValid}, {"widgetValid", manager->designHierarchyWidgetValid},
            {"currentView", int(manager->currentView)}, {"widgetVisible", widget && widget->isVisible()},
            {"designRequestGeneration", QString::number(manager->designRequestGeneration)},
            {"watcher", pointer(manager->designWatcher)},
            {"activeRequest", requestJson(manager->activeDesignRequest)},
            {"pendingRequest", requestJson(manager->pendingDesignRequest)},
            {"workspace", manager->context.currentWorkspacePath}, {"selectedTop", cache.activeProjectId},
            {"cacheRoots", rootsJson(cache.designRoots)}, {"cacheFiles", identity(cache.designFileScope)},
            {"cacheFilesOrdered", QJsonArray::fromStringList(cache.designFileScope)},
            {"cacheStructure", QString::fromLatin1(cache.designStructureFingerprint.toHex())},
            {"cacheAnchors", QString::fromLatin1(cache.designAnchorFingerprint.toHex())},
            {"liveManagerKeys", identity(designInstanceKeys(cache.designHierarchy).values())},
            {"liveKeysUse", "diagnostic only; never substitutes the saved sets used by original checks"}};
    }

    QPointer<NavigationManager> manager;
    QPointer<NavigationWidget> widget;
    SemanticIndex* index;
    QElapsedTimer clock;
    QList<QMetaObject::Connection> connections;
    QJsonArray events;
    QByteArray previousPoll;
    int sequence = 0;
    bool insideOpen = false, finished = false;
};

EffectiveValueResult resolveUnboundEffectiveValue(
    const SemanticSymbolRecord& record,
    const QString& documentText)
{
    EffectiveValueQuery query;
    query.symbol = record;
    query.documentText = documentText;
    query.documentRevision = record.presentation.documentRevision;
    return EffectiveValueService::getInstance()->resolve(query);
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const bool selfContained = argc > 1 && QString::fromLocal8Bit(argv[1]) == "--self-contained-only";
    QTemporaryDir originalProjects;
    QString newWorkspace, hugeWorkspace;
    if (selfContained) {
        check("self-contained workspace root exists", originalProjects.isValid());
        const auto write = [&](const QString& relative, const QString& source) {
            const QString path = originalProjects.filePath(relative);
            if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(source.toUtf8()) == source.toUtf8().size();
        };
        newWorkspace = originalProjects.filePath("workspace-a");
        hugeWorkspace = originalProjects.filePath("workspace-b");
        check("original package fixture written", write("workspace-a/PKG_global.sv",
            "package PKG_global; parameter int P_SW_NUM = 9 * 10; endpackage\n"));
        check("original hierarchy fixture written", write("workspace-a/elec_phy_import/top/rtl_top.sv",
            "module phy_top(input logic clk_main, output wire ready); assign ready=clk_main; endmodule\n"
            "module elec_top(input logic clk_main, output wire ready); phy_top phy(.clk_main(clk_main),.ready(ready)); endmodule\n"
            "module top_ctrl(input logic clk_main, output wire ready); elec_top elec(.clk_main(clk_main),.ready(ready)); endmodule\n"
            "module lite_top(input logic clk_main, output wire ready); top_ctrl control(.clk_main(clk_main),.ready(ready)); endmodule\n"
            "module rtl_top(input logic clk_main, output wire ready);\n"
            "  localparam int WIDTH = PKG_global::P_SW_NUM;\n"
            "  wire [WIDTH-1:0] bus_data;\n"
            "  assign bus_data = '0;\n"
            "  lite_top lite(.clk_main(clk_main), .ready(ready));\nendmodule\n"));
        check("original second workspace written", write("workspace-b/control.sv",
            QStringLiteral("module helper(input logic d, output wire q); assign q=d; endmodule\n"
                           "module %1(input logic d, output wire q); helper leaf(.d(d),.q(q)); endmodule\n")
                .arg(QStringLiteral(ZS_FIXTURE_TOP_CTL))));
    } else {
        newWorkspace = argc > 1 ? QString::fromLocal8Bit(argv[1])
                               : QDir::current().absoluteFilePath(QStringLiteral("test_sv/new"));
        hugeWorkspace = argc > 2 ? QString::fromLocal8Bit(argv[2])
                                : QDir::current().absoluteFilePath(QStringLiteral("test_sv/huge_prj"));
    }
    check("first workspace fixture exists", QFileInfo(newWorkspace).isDir());
    check("second workspace fixture exists", QFileInfo(hugeWorkspace).isDir());
    if (failures != 0) return failures;

    // The teardown regression intentionally holds the unified semantic worker
    // in a cancellation gate, then restores the process-wide setting after
    // the worker has joined.
    ScopedThreadPoolMinimum threadPoolMinimum(2);

    const auto symbolGateEntered =
        std::make_shared<std::atomic_bool>(false);
    const auto symbolGateExited =
        std::make_shared<std::atomic_bool>(false);

    QTemporaryDir sessionStateDir;
    check("session state isolation directory exists",
          sessionStateDir.isValid());
    if (!sessionStateDir.isValid())
        return failures;
    const QByteArray previousSessionStorage =
        qgetenv("ZEROSLACK_SESSION_STORAGE_PATH");
    qputenv(
        "ZEROSLACK_SESSION_STORAGE_PATH",
        QDir(sessionStateDir.path())
            .absoluteFilePath(
                QStringLiteral("workspace-sessions.ini"))
            .toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(
        QSettings::IniFormat,
        QSettings::UserScope,
        QDir(sessionStateDir.path())
            .absoluteFilePath(QStringLiteral("qsettings")));
    SettingsCenterService isolatedSettings;
    const SettingsCenterSnapshot initialSettings =
        isolatedSettings.load();
    QVariantMap globalSettings = initialSettings.globalValues;
    globalSettings.insert(
        QStringLiteral("layout.restoreWorkspaceSession"),
        false);
    check("workspace session auto-restore is disabled in isolation",
          isolatedSettings
              .saveGlobal(globalSettings,
                          initialSettings.globalRevision)
              .saved);
    check("maintained UI backend initializes", initializeUiStyleForTest());

    {
    MainWindow window;
    window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(
        false);
    // This test owns the clean "ow 1" scan path. Keep the real-project
    // fixtures and user-local session snapshots read-only so restored tabs or
    // cached scans cannot change the preconditions between runs.
    window.workspaceSessionCoordinator->setRestoreOnActivation(false);
    window.resize(960, 640);
    window.show();
    QPointer<MainWindow> windowGuard(&window);
    check("main window becomes visible",
          waitUntil([&]() { return window.isVisible(); }, 3000));
    check("global control coordinator exists",
          window.globalControlCoordinator != nullptr);
    check("file command coordinator exists",
          window.fileCommandCoordinator != nullptr);
    if (!window.globalControlCoordinator || !window.fileCommandCoordinator)
        return failures;

    QQueue<QString> selectedDirectories;
    selectedDirectories.enqueue(QString());
    selectedDirectories.enqueue(newWorkspace);
    selectedDirectories.enqueue(newWorkspace);
    selectedDirectories.enqueue(hugeWorkspace);
    int selectorCalls = 0;
    int aliasSelectorCalls = 0;
    window.fileCommandCoordinator->setWorkspaceDirectorySelector(
        [&](QWidget* dialogParent) {
            check("directory selector receives MainWindow parent",
                  dialogParent == &window);
            ++selectorCalls;
            return selectedDirectories.isEmpty()
                ? QString()
                : selectedDirectories.dequeue();
        });
    window.workspaceManager->setWorkspaceAliasSelector(
        [&](QWidget* dialogParent, const QString& suggestedAlias) {
            check("alias selector receives MainWindow parent",
                  dialogParent == &window);
            ++aliasSelectorCalls;
            return suggestedAlias;
        });

    QString expectedWorkspaceRoot;
    int workspaceAttempt = 0;
    int scanStartedAttempt = -1;
    int scanFinishedAttempt = -1;
    int symbolStartedAttempt = -1;
    int symbolFinishedAttempt = -1;
    int symbolFinishCount = 0;

    QObject::connect(window.workspaceManager.get(),
                     &WorkspaceManager::workspaceScanStarted,
                     &window,
                     [&](const QString& path) {
                         if (normalizedPath(path) == expectedWorkspaceRoot)
                             scanStartedAttempt = workspaceAttempt;
                     });
    QObject::connect(window.workspaceManager.get(),
                     &WorkspaceManager::workspaceScanFinished,
                     &window,
                     [&](const QString& path, int, int) {
                         if (normalizedPath(path) == expectedWorkspaceRoot)
                             scanFinishedAttempt = workspaceAttempt;
                     });
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::workspaceSymbolAnalysisStarted,
                     &window,
                     [&](const ProjectSnapshot& project, int) {
                         if (normalizedPath(project.workspaceRoot)
                             == expectedWorkspaceRoot) {
                             symbolStartedAttempt = workspaceAttempt;
                         }
                     });
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::workspaceSymbolAnalysisFinished,
                     &window,
                     [&](const ProjectSnapshot& project, int, int) {
                         ++symbolFinishCount;
                         if (normalizedPath(project.workspaceRoot)
                             == expectedWorkspaceRoot) {
                             symbolFinishedAttempt = workspaceAttempt;
                         }
                     });
    const GlobalControlItem owOne = openOneWorkspaceItem();

    window.globalControlCoordinator->dispatch(owOne);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    check("cancelled ow 1 calls injected selector", selectorCalls == 1);
    check("cancelled ow 1 leaves workspace closed",
          !window.workspaceManager->isWorkspaceOpen());
    check("window survives cancelled ow 1",
          windowGuard && window.isVisible());

    auto openAndAwaitAnalysis = [&](const QString& workspace,
                                     int timeoutMs,
                                     const char* label,
                                     const QString& knownSymbolName) {
        QElapsedTimer attemptBudget;
        attemptBudget.start();
        auto waitStage = [&](const std::function<bool()>& predicate,
                             int stageLimitMs) {
            const int remainingMs = qMax(
                0,
                timeoutMs
                    - static_cast<int>(attemptBudget.elapsed()));
            return waitUntil(predicate, qMin(stageLimitMs, remainingMs));
        };
        ++workspaceAttempt;
        const int selectorCallsBeforeDispatch = selectorCalls;
        expectedWorkspaceRoot = normalizedPath(workspace);
        scanStartedAttempt = -1;
        scanFinishedAttempt = -1;
        symbolStartedAttempt = -1;
        symbolFinishedAttempt = -1;

        window.globalControlCoordinator->dispatch(owOne);
        check(QString::fromLatin1(label) + QStringLiteral(" selector called"),
              selectorCalls == selectorCallsBeforeDispatch + 1);
        check(QString::fromLatin1(label)
                  + QStringLiteral(" workspace activated"),
              normalizedPath(window.workspaceManager->getWorkspacePath())
                  == expectedWorkspaceRoot);
        check(QString::fromLatin1(label) + QStringLiteral(" scan starts"),
              waitStage([&]() {
                  return scanStartedAttempt == workspaceAttempt;
              }, 5000));
        check(QString::fromLatin1(label) + QStringLiteral(" scan completes"),
              waitStage([&]() {
                  return scanFinishedAttempt == workspaceAttempt;
              },
                        120000));
        check(QString::fromLatin1(label)
                  + QStringLiteral(" symbol analysis starts"),
              waitStage(
                  [&]() { return symbolStartedAttempt == workspaceAttempt; },
                  10000));
        check(QString::fromLatin1(label)
                  + QStringLiteral(" symbol analysis completes"),
              waitStage(
                  [&]() { return symbolFinishedAttempt == workspaceAttempt; },
                  600000));
        const SemanticSymbolRecord knownRecord =
            semanticRecordNamed(workspace, knownSymbolName);
        check(QString::fromLatin1(label)
                  + QStringLiteral(" known semantic record is queryable"),
              knownRecord.isValid());
        check(QString::fromLatin1(label)
                  + QStringLiteral(" integrated relationship results are queryable"),
              relationshipResultsQueryableFor(workspace, knownRecord));
        check(QString::fromLatin1(label) + QStringLiteral(" window survives"),
              windowGuard && window.isVisible());
        check(QString::fromLatin1(label)
                  + QStringLiteral(" remains stable after analysis"),
              remainsTrueFor(
                   [&]() {
                       return windowGuard && windowGuard->isVisible()
                           && semanticRecordNamed(workspace,
                                                  knownSymbolName)
                                  .isValid();
                   },
                   2200));
    };

    openAndAwaitAnalysis(newWorkspace,
                         120000,
                         "new",
                         QStringLiteral("rtl_top"));

    // Regression for the exact post-analysis user sequence: workspace symbol
    // and automatic relationship analysis are already stable, then an
    // existing workspace member is opened without editing it. Disk content
    // already represented by the workspace snapshot must remain revision zero
    // and must not launch an equivalent all-open-tabs Slang transaction.
    const QString rtlTopPath = QDir(newWorkspace).absoluteFilePath(
        QStringLiteral("elec_phy_import/top/rtl_top.sv"));
    const QString packagePath = QDir(newWorkspace).absoluteFilePath(
        QStringLiteral("PKG_global.sv"));
    check("post-analysis rtl_top fixture exists",
          QFileInfo(rtlTopPath).isFile());
    check("post-analysis rtl_top is not already open",
          window.tabManager
              && window.tabManager->getDocumentModel()
              && !window.tabManager->getDocumentModel()
                       ->editorForFile(rtlTopPath));

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    OwDesignTrace designTrace(window.navigationManager.get(), window.analysisScheduler.get(),
                              window.tabManager.get(), semanticIndex, window.workspaceManager.get());
    designTrace.mark("before-setDesignTop");
    check("design project selection succeeds",
          window.navigationManager->setDesignTop(QStringLiteral("rtl_top")).isEmpty());
    designTrace.mark("after-setDesignTop-before-setActiveView");
    window.navigationManager->setActiveView(NavigationManager::DesignHierarchyView);
    designTrace.mark("after-setActiveView-before-refreshCurrentView");
    window.navigationManager->refreshCurrentView();
    designTrace.mark("after-preparation-refreshCurrentView");
    QTreeView* designTree = window.navigationPane
        && window.navigationPane->navigationWidget
        ? window.navigationPane->navigationWidget->designTreeWidget
        : nullptr;
    // Selecting a design project can publish a new semantic snapshot. Await
    // that transaction and its asynchronous projection before the clean-open baseline.
    constexpr qint64 designStableWindowMs = 200;
    constexpr qint64 designPreparationBudgetMs = 10000;
    QElapsedTimer designPreparationClock;
    qint64 designStableSinceMs = -1;
    std::uint64_t designStableGeneration = 0;
    int designStableResets = 0;
    bool designOriginalReadySeen = false;
    bool designPreviousReady = false;
    bool designPreparationReady = false;
    bool designPreparationDeadlineObserved = false;
    designTrace.mark("before-preparation-wait-10000",
        {{"windowMs", designStableWindowMs}, {"budgetMs", designPreparationBudgetMs},
         {"observationLimit", "sampled original readiness and request generation; not all queued work"}});
    designPreparationClock.start();
    check("design projection settles before clean-open baseline",
          waitUntil([&] {
              designTrace.preparationPoll();
              const bool originalReady = !window.analysisScheduler->isSemanticAnalysisActive()
                  && !window.navigationManager->designWatcher
                  && !window.navigationManager->pendingDesignRequest
                  && window.navigationManager->caches.designHierarchyValid
                  && window.navigationManager->caches.designSnapshotGeneration
                         == semanticIndex->snapshotRevision()
                  && designTree && designTreeContainsNestedModule(designTree, QStringLiteral("top_ctrl"));
              const qint64 elapsedMs = designPreparationClock.elapsed();
              const auto generation = window.navigationManager->designRequestGeneration;
              if (originalReady && !designOriginalReadySeen) {
                  designOriginalReadySeen = true;
                  designTrace.mark("preparation-original-first-ready",
                      {{"elapsedMs", elapsedMs}, {"generation", QString::number(generation)}});
              }
              if (designPreviousReady && !originalReady)
                  designTrace.mark("preparation-original-readiness-lost",
                      {{"elapsedMs", elapsedMs}, {"generation", QString::number(generation)}});
              designPreviousReady = originalReady;
              if (designStableSinceMs >= 0
                  && (!originalReady || generation != designStableGeneration)) {
                  QJsonArray reasons;
                  if (!originalReady) reasons.append("original-condition-false");
                  if (generation != designStableGeneration) reasons.append("generation-changed");
                  ++designStableResets;
                  designTrace.mark("preparation-stability-reset",
                      {{"elapsedMs", elapsedMs}, {"stableSinceMs", designStableSinceMs},
                       {"fromGeneration", QString::number(designStableGeneration)},
                       {"toGeneration", QString::number(generation)},
                       {"reasons", reasons}, {"resetCount", designStableResets}});
                  designStableSinceMs = -1;
              }
              if (originalReady && designStableSinceMs < 0) {
                  designStableSinceMs = elapsedMs;
                  designStableGeneration = generation;
                  designTrace.mark("preparation-stability-start",
                      {{"elapsedMs", elapsedMs}, {"generation", QString::number(generation)},
                       {"windowMs", designStableWindowMs}, {"resetCount", designStableResets}});
              }
              const qint64 observedMs = designPreparationClock.elapsed();
              if (observedMs >= designPreparationBudgetMs) {
                  if (!designPreparationDeadlineObserved) {
                      designPreparationDeadlineObserved = true;
                      designTrace.mark("preparation-stability-deadline",
                          {{"elapsedMs", observedMs}, {"budgetMs", designPreparationBudgetMs},
                           {"resetCount", designStableResets}});
                  }
                  return false;
              }
              if (originalReady && observedMs - designStableSinceMs >= designStableWindowMs) {
                  designTrace.mark("preparation-stability-ready",
                      {{"elapsedMs", observedMs}, {"stableSinceMs", designStableSinceMs},
                       {"stableElapsedMs", observedMs - designStableSinceMs},
                       {"windowMs", designStableWindowMs}, {"budgetMs", designPreparationBudgetMs},
                       {"generation", QString::number(generation)}, {"resetCount", designStableResets}});
                  designPreparationReady = designPreparationClock.elapsed() < designPreparationBudgetMs;
                  return designPreparationReady;
              }
              return false;
          }, 10000));
    designTrace.mark("after-preparation-wait-before-original-baseline",
        {{"preparationReady", designPreparationReady}, {"elapsedMs", designPreparationClock.elapsed()},
         {"stableSinceMs", designStableSinceMs}, {"generation", QString::number(designStableGeneration)},
         {"windowMs", designStableWindowMs}, {"budgetMs", designPreparationBudgetMs},
         {"resetCount", designStableResets}});
    const std::shared_ptr<const SemanticIndexSnapshot> beforeOpenSnapshot =
        semanticIndex->snapshot();
    designTrace.snapshotSample("beforeOpenSnapshot", beforeOpenSnapshot.get());
    const std::uint64_t beforeOpenSnapshotRevision =
        semanticIndex->snapshotRevision();
    designTrace.mark("saved-beforeOpenSnapshotRevision", {{"revision", QString::number(beforeOpenSnapshotRevision)}});
    const QSet<QString> beforeRelationshipKeys =
        stableRelationshipKeys(beforeOpenSnapshot);
    const SemanticSymbolRecord rtlTopBefore = semanticRecordInFile(
        rtlTopPath,
        QStringLiteral("rtl_top"),
        SymbolTaxonomy::DeclarationKind::Module);
    const SemanticSymbolRecord clockBefore = semanticRecordInFile(
        rtlTopPath,
        QStringLiteral("clk_main"),
        SymbolTaxonomy::DeclarationKind::Port);
    const SemanticSymbolRecord packageValueBefore = semanticRecordInFile(
        packagePath,
        QStringLiteral("P_SW_NUM"),
        SymbolTaxonomy::DeclarationKind::Parameter);
    const QString rtlTopTextBefore =
        semanticIndex->getCachedFileContent(rtlTopPath);
    const QString packageTextBefore =
        semanticIndex->getCachedFileContent(packagePath);
    const EffectiveValueResult packageEffectiveBefore =
        resolveUnboundEffectiveValue(packageValueBefore,
                                     packageTextBefore);
    GhostAnnotationQuery beforeGhostQuery;
    beforeGhostQuery.fileName = rtlTopPath;
    beforeGhostQuery.documentText = rtlTopTextBefore;
    beforeGhostQuery.documentRevision =
        rtlTopBefore.presentation.documentRevision;
    const GhostAnnotationReport beforeGhostReport =
        GhostAnnotationService::getInstance()->annotationsForDocument(
            beforeGhostQuery);
    const QSet<QString> beforeGhostKeys =
        ghostAnnotationKeys(beforeGhostReport.annotations);

    QSet<QString> designFileScope;
    const ProjectSnapshot newProject =
        window.workspaceManager->projectSnapshot();
    for (const QString& fileName : newProject.systemVerilogFiles)
        designFileScope.insert(fileName);
    NavigationService designService(semanticIndex);
    const DesignHierarchyReport designBefore =
        designService.findDesignHierarchy(QStringLiteral("rtl_top"),
                                          designFileScope);
    const QSet<QString> designKeysBefore =
        designInstanceKeys(designBefore);
    designTrace.sample("designKeysBefore", designKeysBefore);
    const QSet<QString> managerDesignKeysBefore = designInstanceKeys(
        window.navigationManager->caches.designHierarchy);
    designTrace.sample("managerDesignKeysBefore", managerDesignKeysBefore);

    check("post-analysis baseline snapshot has stable-key relationships",
          beforeOpenSnapshot && !beforeRelationshipKeys.isEmpty());
    check("post-analysis baseline has rtl_top and clk_main presentation",
          rtlTopBefore.isValid()
              && clockBefore.isValid()
              && !clockBefore.presentation.declarationText.isEmpty());
    check("post-analysis baseline package effective value is queryable",
          packageEffectiveBefore.current()
              && packageEffectiveBefore.effectiveScopeKind
                     == SemanticEffectiveScopeKind::Package
              && packageEffectiveBefore.valueText == QStringLiteral("90"));
    check("post-analysis baseline Ghost is semantic and non-empty",
          !beforeGhostKeys.isEmpty()
              && hasSemanticGhostAnnotation(
                  beforeGhostReport.annotations));
    check("post-analysis baseline includes formal port Ghost declarations",
          hasGhostAnnotationKind(beforeGhostReport.annotations,
                                 GhostAnnotationKind::FormalPort));
    check("post-analysis baseline Design hierarchy has expected instances",
          !designKeysBefore.isEmpty()
              && designContainsModuleType(designBefore,
                                          QStringLiteral("lite_top"))
              && designContainsModuleType(designBefore,
                                          QStringLiteral("top_ctrl"))
              && designContainsModuleType(designBefore,
                                          QStringLiteral("elec_top"))
              && designContainsModuleType(designBefore,
                                          QStringLiteral("phy_top")));
    check("real NavigationManager baseline matches Design service",
          !managerDesignKeysBefore.isEmpty()
              && containsAll(managerDesignKeysBefore,
                             designKeysBefore));
    // NavigationManager intentionally keeps other inferred roots beside the
    // selected top. A known selected-top descendant must nevertheless have a
    // parent; when INSTANTIATES edges disappear it becomes an inferred root.
    check("real NavigationWidget baseline renders nested Design hierarchy",
          designTree
              && designTreeContainsNestedModule(
                  designTree, QStringLiteral("top_ctrl")));

    int openFileAnalysisStarts = 0;
    int openFileAnalysisFinishes = 0;
    int designRefreshesAfterOpen = 0;
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::fileSymbolAnalysisStarted,
                     &window,
                     [&](const QString&) {
                         ++openFileAnalysisStarts;
                     });
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::fileSymbolAnalysisFinished,
                     &window,
                     [&](const QString&, int) {
                         ++openFileAnalysisFinishes;
                     });
    designTrace.mark("before-original-refresh-counter-connect");
    QObject::connect(window.navigationManager.get(),
                     &NavigationManager::dataRefreshed,
                     &window,
                     [&](NavigationManager::NavigationView view) {
                         if (view == NavigationManager::DesignHierarchyView)
                             ++designRefreshesAfterOpen;
                     });

    designTrace.mark("after-original-refresh-counter-connect");
    designTrace.beginOpen();
    check("open analyzed rtl_top through real TabManager",
          window.tabManager->openFileInTab(rtlTopPath));
    designTrace.endOpen();
    MyCodeEditor* rtlTopEditor = window.tabManager->getCurrentEditor();
    const DocumentSnapshot rtlTopDocument =
        window.tabManager->getCurrentDocument();
    check("post-analysis rtl_top editor and document are active",
          rtlTopEditor
              && normalizedPath(rtlTopDocument.fileName)
                     == normalizedPath(rtlTopPath));
    check("clean workspace file opens at semantic revision zero",
          rtlTopDocument.textVersion == 0
              && rtlTopEditor
              && rtlTopEditor->semanticDocumentRevision() == 0);
    designTrace.mark("before-original-analysis-wait-800");
    check("clean workspace file open skips all-open-tabs analysis",
          remainsTrueFor(
              [&]() {
                  return openFileAnalysisStarts == 0
                      && openFileAnalysisFinishes == 0
                      && semanticIndex->snapshotRevision()
                             == beforeOpenSnapshotRevision;
              },
              800));
    designTrace.mark("after-original-analysis-wait-800");
    check("window survives post-analysis rtl_top open",
          windowGuard && window.isVisible());

    const std::shared_ptr<const SemanticIndexSnapshot> afterOpenSnapshot =
        semanticIndex->snapshot();
    designTrace.snapshotSample("afterOpenSnapshot", afterOpenSnapshot.get());
    const QSet<QString> afterRelationshipKeys =
        stableRelationshipKeys(afterOpenSnapshot);
    const SemanticSymbolRecord rtlTopAfter = semanticRecordInFile(
        rtlTopPath,
        QStringLiteral("rtl_top"),
        SymbolTaxonomy::DeclarationKind::Module);
    const SemanticSymbolRecord clockAfter = semanticRecordInFile(
        rtlTopPath,
        QStringLiteral("clk_main"),
        SymbolTaxonomy::DeclarationKind::Port);
    const SemanticSymbolRecord packageValueAfter = semanticRecordInFile(
        packagePath,
        QStringLiteral("P_SW_NUM"),
        SymbolTaxonomy::DeclarationKind::Parameter);
    const EffectiveValueResult packageEffectiveAfter =
        resolveUnboundEffectiveValue(
            packageValueAfter,
            semanticIndex->getCachedFileContent(packagePath));
    GhostAnnotationQuery afterGhostQuery;
    afterGhostQuery.fileName = rtlTopPath;
    afterGhostQuery.documentText = rtlTopEditor
        ? rtlTopEditor->toPlainText()
        : QString();
    afterGhostQuery.documentRevision =
        static_cast<std::uint64_t>(rtlTopDocument.textVersion);
    const GhostAnnotationReport afterGhostReport =
        GhostAnnotationService::getInstance()->annotationsForDocument(
            afterGhostQuery);
    const QSet<QString> afterGhostKeys =
        ghostAnnotationKeys(afterGhostReport.annotations);
    const DesignHierarchyReport designAfter =
        designService.findDesignHierarchy(QStringLiteral("rtl_top"),
                                          designFileScope);
    const QSet<QString> designKeysAfter =
        designInstanceKeys(designAfter);
    designTrace.sample("designKeysAfter", designKeysAfter);
    const QSet<QString> managerDesignKeysAfter = designInstanceKeys(
        window.navigationManager->caches.designHierarchy);
    designTrace.sample("managerDesignKeysAfter", managerDesignKeysAfter);

    printSetDifference("relationships",
                       beforeRelationshipKeys,
                       afterRelationshipKeys);
    printSetDifference("ghost",
                       beforeGhostKeys,
                       afterGhostKeys);
    printSetDifference("design",
                       designKeysBefore,
                       designKeysAfter);
    printSetDifference("navigation-design",
                       managerDesignKeysBefore,
                       managerDesignKeysAfter);

    check("post-analysis rtl_top open preserves stable-key relationships",
          !afterRelationshipKeys.isEmpty()
              && afterRelationshipKeys == beforeRelationshipKeys);
    check("post-analysis rtl_top open preserves module presentation identity",
          sameSourceIdentityAndPresentation(rtlTopBefore, rtlTopAfter)
              && rtlTopAfter.presentation.documentRevision
                     == static_cast<std::uint64_t>(
                         rtlTopDocument.textVersion)
              && rtlTopAfter.presentation.computationRevision
                     >= rtlTopBefore.presentation.computationRevision);
    check("post-analysis rtl_top open preserves port presentation identity",
          sameSourceIdentityAndPresentation(clockBefore, clockAfter)
              && clockAfter.presentation.documentRevision
                     == static_cast<std::uint64_t>(
                         rtlTopDocument.textVersion));
    check("post-analysis rtl_top open preserves package effective value",
          packageEffectiveAfter.current()
              && packageEffectiveAfter.valueText
                     == packageEffectiveBefore.valueText
              && packageEffectiveAfter.resolvedTypeText
                     == packageEffectiveBefore.resolvedTypeText
              && packageEffectiveAfter.semanticSymbolKey
                     == packageEffectiveBefore.semanticSymbolKey);
    check("post-analysis rtl_top open preserves Ghost service annotations",
          !afterGhostKeys.isEmpty()
              && afterGhostKeys == beforeGhostKeys
              && hasSemanticGhostAnnotation(
                  afterGhostReport.annotations));
    check("post-analysis rtl_top open preserves formal port Ghost declarations",
          hasGhostAnnotationKind(afterGhostReport.annotations,
                                 GhostAnnotationKind::FormalPort));
    designTrace.mark("before-original-Ghost-wait-5000");
    check("post-analysis rtl_top open refreshes actual editor Ghost",
          waitUntil(
              [&]() {
                  return rtlTopEditor
                      && rtlTopEditor->state
                      && hasSemanticGhostAnnotation(
                          rtlTopEditor->state->ghostAnnotations)
                      && hasRevisionBoundGhostAnnotation(
                          rtlTopEditor->state->ghostAnnotations);
              },
              5000));
    designTrace.mark("after-original-Ghost-wait-5000");
    designTrace.mark("before-original-Design-instances-check",
        {{"savedAfterNonEmpty", !designKeysAfter.isEmpty()},
         {"savedSetsEqual", designKeysAfter == designKeysBefore}});
    check("post-analysis rtl_top open preserves Design hierarchy instances",
          !designKeysAfter.isEmpty()
              && designKeysAfter == designKeysBefore);
    designTrace.mark("after-original-Design-instances-check");
    std::printf("[DIAG] clean-open design refreshes=%d keys-before=%lld keys-after=%lld cache-revision=%llu semantic-revision=%llu\n",
                designRefreshesAfterOpen, static_cast<long long>(managerDesignKeysBefore.size()),
                static_cast<long long>(managerDesignKeysAfter.size()),
                static_cast<unsigned long long>(window.navigationManager->caches.designSnapshotGeneration),
                static_cast<unsigned long long>(semanticIndex->snapshotRevision()));
    designTrace.mark("before-original-cached-Design-check",
        {{"originalRefreshCount", designRefreshesAfterOpen}, {"zeroRefresh", designRefreshesAfterOpen == 0},
         {"savedAfterNonEmpty", !managerDesignKeysAfter.isEmpty()},
         {"savedSetsEqual", managerDesignKeysAfter == managerDesignKeysBefore},
         {"liveRevisionsEqual", window.navigationManager->caches.designSnapshotGeneration == semanticIndex->snapshotRevision()}});
    check("post-analysis rtl_top open keeps cached Navigation Design",
          designRefreshesAfterOpen == 0
              && !managerDesignKeysAfter.isEmpty()
              && managerDesignKeysAfter == managerDesignKeysBefore
              && window.navigationManager->caches.designSnapshotGeneration
                     == semanticIndex->snapshotRevision());
    designTrace.mark("after-original-cached-Design-check", {{"testFailures", failures}});
    check("post-analysis rtl_top open keeps NavigationWidget nested",
          designTree
              && designTreeContainsNestedModule(
                  designTree, QStringLiteral("top_ctrl")));

    designTrace.finish();

    const int workspaceCountBeforeRepeat =
        window.workspaceManager->workspaceEntries().size();
    const int symbolFinishCountBeforeRepeat = symbolFinishCount;
    window.globalControlCoordinator->dispatch(owOne);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
    check("repeated ow 1 calls injected selector", selectorCalls == 3);
    check("repeated ow 1 keeps one workspace entry",
          window.workspaceManager->workspaceEntries().size()
              == workspaceCountBeforeRepeat);
    check("repeated ow 1 keeps the active workspace",
          normalizedPath(window.workspaceManager->getWorkspacePath())
              == normalizedPath(newWorkspace));
    check("repeated ow 1 does not restart completed symbol analysis",
          symbolFinishCount == symbolFinishCountBeforeRepeat);
    check("repeated ow 1 does not launch a second semantic transaction",
          symbolFinishCount == symbolFinishCountBeforeRepeat);
    check("window survives repeated ow 1",
          windowGuard && window.isVisible());
    check("semantic records remain queryable after repeated ow 1",
          semanticRecordsBelongTo(newWorkspace));

    openAndAwaitAnalysis(hugeWorkspace,
                         840000,
                         "huge",
                         QStringLiteral(ZS_FIXTURE_TOP_CTL));

    ++workspaceAttempt;
    expectedWorkspaceRoot = normalizedPath(hugeWorkspace);
    symbolStartedAttempt = -1;
    window.analysisScheduler->symbolAnalyzer
        ->setWorkspaceWorkerStartGateForTesting(
            [symbolGateEntered, symbolGateExited](
                const std::function<bool()>& isCancelled) {
                symbolGateEntered->store(true, std::memory_order_release);
                while (!isCancelled())
                    QThread::msleep(1);
                symbolGateExited->store(true, std::memory_order_release);
            });
    if (window.analysisScheduler->workspaceSymbolAnalysis) {
        window.analysisScheduler->workspaceSymbolAnalysis
            ->completedWorkspaceAnalysisKeys.clear();
    }
    window.analysisScheduler->requestWorkspaceAnalysis(
        window.workspaceManager->projectSnapshot());
    check("teardown stress symbol analysis restarts",
          waitUntil([&]() {
              return symbolStartedAttempt == workspaceAttempt;
           }, 3000));
    check("teardown stress symbol worker enters deterministic gate",
          waitUntil([&]() {
              return symbolGateEntered->load(std::memory_order_acquire);
          }, 10000));
    check("workspace symbol worker remains active at MainWindow teardown",
          symbolGateEntered->load(std::memory_order_acquire)
              && window.analysisScheduler->symbolAnalyzer
                  && window.analysisScheduler->symbolAnalyzer
                         ->workspaceAnalysisWatcher
                  && window.analysisScheduler->symbolAnalyzer
                         ->workspaceAnalysisWatcher->isRunning());
    check("window is alive immediately before teardown",
          windowGuard && window.isVisible());

    check("all injected selections consumed", selectedDirectories.isEmpty());
    check("alias selector runs only for newly opened workspaces",
          aliasSelectorCalls == 2);
    check("both workspaces remain registered",
          window.workspaceManager->workspaceEntries().size() == 2);

    }

    check("MainWindow shutdown cancels deterministic symbol gate",
          symbolGateExited->load(std::memory_order_acquire));
    check("semantic runtime detaches relationship engine at teardown",
          SemanticIndex::getInstance()->relationshipEngine() == nullptr);

    if (previousSessionStorage.isEmpty())
        qunsetenv("ZEROSLACK_SESSION_STORAGE_PATH");
    else
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH",
                previousSessionStorage);

    std::printf("%d failure(s)\n", failures);
    std::fflush(stdout);
    return failures == 0 ? 0 : 1;
}
