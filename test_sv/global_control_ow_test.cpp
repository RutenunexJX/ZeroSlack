// Integration regression for the real Global Control "ow 1" command path.
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QThreadPool>
#include <QTreeWidget>

#include <cstdio>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#define private public
#include "mainwindow.h"
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
#include "symbolanalyzer.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#undef private

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
    return {GlobalControlItemKind::Command,
            QStringLiteral("ow 1"),
            QStringLiteral("ow 1"),
            QStringLiteral("Open 1 workspace")};
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
    QTreeWidgetItem* item,
    const QString& moduleType)
{
    if (!item)
        return false;
    if (item->text(1) == moduleType && item->parent())
        return true;
    for (int i = 0; i < item->childCount(); ++i) {
        if (designTreeContainsNestedModule(item->child(i), moduleType))
            return true;
    }
    return false;
}

bool designTreeContainsNestedModule(
    QTreeWidget* tree,
    const QString& moduleType)
{
    if (!tree)
        return false;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (designTreeContainsNestedModule(tree->topLevelItem(i), moduleType))
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

    const QString newWorkspace = argc > 1
        ? QString::fromLocal8Bit(argv[1])
        : QDir::current().absoluteFilePath(QStringLiteral("test_sv/new"));
    const QString hugeWorkspace = argc > 2
        ? QString::fromLocal8Bit(argv[2])
        : QDir::current().absoluteFilePath(QStringLiteral("test_sv/huge_prj"));
    check("new workspace fixture exists", QFileInfo(newWorkspace).isDir());
    check("huge workspace fixture exists", QFileInfo(hugeWorkspace).isDir());
    if (failures != 0)
        return failures;

    // The teardown regression intentionally holds both workspace workers in
    // cancellation gates. Reserve two global-pool slots for this test only,
    // then restore the process-wide setting after every worker has joined.
    ScopedThreadPoolMinimum threadPoolMinimum(2);

    const auto symbolGateEntered =
        std::make_shared<std::atomic_bool>(false);
    const auto symbolGateExited =
        std::make_shared<std::atomic_bool>(false);
    const auto relationshipGateEntered =
        std::make_shared<std::atomic_bool>(false);
    const auto relationshipGateExited =
        std::make_shared<std::atomic_bool>(false);

    {
    MainWindow window;
    window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(
        false);
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
    int relationshipStartedAttempt = -1;
    int relationshipFinishedAttempt = -1;
    std::uint64_t relationshipStartedGeneration = 0;
    std::uint64_t relationshipFinishedGeneration = 0;
    QString relationshipStartedProjectKey;
    int symbolFinishCount = 0;
    int relationshipFinishCount = 0;

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
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::workspaceRelationshipAnalysisStarted,
                     &window,
                     [&](const ProjectSnapshot& project, int) {
                          if (normalizedPath(project.workspaceRoot)
                              == expectedWorkspaceRoot) {
                              relationshipStartedAttempt = workspaceAttempt;
                              RelationshipAnalysisController* controller =
                                  window.analysisScheduler
                                      ? window.analysisScheduler
                                            ->relationshipAnalysis
                                      : nullptr;
                              relationshipStartedGeneration =
                                  controller
                                      ? controller
                                            ->activeWorkspaceRequestGeneration
                                      : 0;
                              relationshipStartedProjectKey =
                                  controller
                                      ? controller->activeWorkspaceProjectKey
                                      : QString();
                          }
                      });
    QObject::connect(
        window.analysisScheduler.get(),
        &AnalysisScheduler::workspaceRelationshipAnalysisFinished,
        &window,
        [&](const WorkspaceRelationshipAnalysisResult& result) {
            ++relationshipFinishCount;
            if (relationshipStartedAttempt == workspaceAttempt
                && result.requestGeneration == relationshipStartedGeneration
                && result.requestGeneration > relationshipFinishedGeneration
                && !result.projectKey.isEmpty()
                && result.projectKey == relationshipStartedProjectKey
                && normalizedPath(window.workspaceManager->getWorkspacePath())
                       == expectedWorkspaceRoot) {
                relationshipFinishedAttempt = workspaceAttempt;
                relationshipFinishedGeneration = result.requestGeneration;
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
        relationshipStartedAttempt = -1;
        relationshipFinishedAttempt = -1;
        relationshipStartedGeneration = 0;
        relationshipStartedProjectKey.clear();

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
        check(QString::fromLatin1(label)
                  + QStringLiteral(" relationship analysis starts"),
              waitStage([&]() {
                  return relationshipStartedAttempt == workspaceAttempt;
              }, 10000));
        check(QString::fromLatin1(label)
                  + QStringLiteral(" relationship analysis completes"),
              waitStage([&]() {
                  return relationshipFinishedAttempt == workspaceAttempt;
              }, 600000));
        const SemanticSymbolRecord knownRecord =
            semanticRecordNamed(workspace, knownSymbolName);
        check(QString::fromLatin1(label)
                  + QStringLiteral(" known semantic record is queryable"),
              knownRecord.isValid());
        check(QString::fromLatin1(label)
                  + QStringLiteral(" relationship results are queryable"),
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
    const std::shared_ptr<const SemanticIndexSnapshot> beforeOpenSnapshot =
        semanticIndex->snapshot();
    const std::uint64_t beforeOpenSnapshotRevision =
        semanticIndex->snapshotRevision();
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
    window.navigationManager->setDesignTop(QStringLiteral("rtl_top"));
    window.navigationManager->setActiveView(
        NavigationManager::DesignHierarchyView);
    window.navigationManager->refreshCurrentView();
    const QSet<QString> managerDesignKeysBefore = designInstanceKeys(
        window.navigationManager->caches.designHierarchy);
    QTreeWidget* designTree = window.navigationPane
        && window.navigationPane->navigationWidget
        ? window.navigationPane->navigationWidget->designTreeWidget
        : nullptr;

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
    QObject::connect(window.navigationManager.get(),
                     &NavigationManager::dataRefreshed,
                     &window,
                     [&](NavigationManager::NavigationView view) {
                         if (view == NavigationManager::DesignHierarchyView)
                             ++designRefreshesAfterOpen;
                     });

    check("open analyzed rtl_top through real TabManager",
          window.tabManager->openFileInTab(rtlTopPath));
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
    check("clean workspace file open skips all-open-tabs analysis",
          remainsTrueFor(
              [&]() {
                  return openFileAnalysisStarts == 0
                      && openFileAnalysisFinishes == 0
                      && semanticIndex->snapshotRevision()
                             == beforeOpenSnapshotRevision;
              },
              800));
    check("window survives post-analysis rtl_top open",
          windowGuard && window.isVisible());

    const std::shared_ptr<const SemanticIndexSnapshot> afterOpenSnapshot =
        semanticIndex->snapshot();
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
    const QSet<QString> managerDesignKeysAfter = designInstanceKeys(
        window.navigationManager->caches.designHierarchy);

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
    check("post-analysis rtl_top open preserves Design hierarchy instances",
          !designKeysAfter.isEmpty()
              && designKeysAfter == designKeysBefore);
    check("post-analysis rtl_top open keeps cached Navigation Design",
          designRefreshesAfterOpen == 0
              && !managerDesignKeysAfter.isEmpty()
              && managerDesignKeysAfter == managerDesignKeysBefore
              && window.navigationManager->caches.designSnapshotGeneration
                     == semanticIndex->snapshotRevision());
    check("post-analysis rtl_top open keeps NavigationWidget nested",
          designTree
              && designTreeContainsNestedModule(
                  designTree, QStringLiteral("top_ctrl")));

    const int workspaceCountBeforeRepeat =
        window.workspaceManager->workspaceEntries().size();
    const int symbolFinishCountBeforeRepeat = symbolFinishCount;
    const int relationshipFinishCountBeforeRepeat = relationshipFinishCount;
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
    check("repeated ow 1 does not restart completed relationship analysis",
          relationshipFinishCount == relationshipFinishCountBeforeRepeat);
    check("window survives repeated ow 1",
          windowGuard && window.isVisible());
    check("semantic records remain queryable after repeated ow 1",
          semanticRecordsBelongTo(newWorkspace));

    openAndAwaitAnalysis(hugeWorkspace,
                         840000,
                         "huge",
                         QStringLiteral("vendor_ip_ctl"));

    ++workspaceAttempt;
    expectedWorkspaceRoot = normalizedPath(hugeWorkspace);
    symbolStartedAttempt = -1;
    relationshipStartedAttempt = -1;
    relationshipFinishedAttempt = -1;
    window.analysisScheduler->symbolAnalyzer
        ->setWorkspaceWorkerStartGateForTesting(
            [symbolGateEntered, symbolGateExited](
                const std::function<bool()>& isCancelled) {
                symbolGateEntered->store(true, std::memory_order_release);
                while (!isCancelled())
                    QThread::msleep(1);
                symbolGateExited->store(true, std::memory_order_release);
            });
    window.analysisScheduler->relationshipAnalysis
        ->setWorkspaceWorkerStartGateForTesting(
            [relationshipGateEntered, relationshipGateExited](
                const std::function<bool()>& isCancelled) {
                relationshipGateEntered->store(true,
                                                std::memory_order_release);
                while (!isCancelled())
                    QThread::msleep(1);
                relationshipGateExited->store(true,
                                               std::memory_order_release);
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
    window.analysisScheduler->requestWorkspaceRelationshipAnalysis(
        window.workspaceManager->projectSnapshot());
    check("teardown stress relationship analysis restarts",
          waitUntil([&]() {
              return relationshipStartedAttempt == workspaceAttempt;
           }, 3000));
    check("teardown stress relationship worker enters deterministic gate",
          waitUntil([&]() {
              return relationshipGateEntered->load(
                  std::memory_order_acquire);
          }, 10000));
    check("relationship worker remains active at MainWindow teardown",
          relationshipGateEntered->load(std::memory_order_acquire)
              && window.analysisScheduler->relationshipAnalysis
              && window.analysisScheduler->relationshipAnalysis
                     ->workspaceWatcher
              && window.analysisScheduler->relationshipAnalysis
                     ->workspaceWatcher->isRunning());
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
    check("MainWindow shutdown cancels deterministic relationship gate",
          relationshipGateExited->load(std::memory_order_acquire));
    check("semantic runtime detaches relationship engine at teardown",
          SemanticIndex::getInstance()->relationshipEngine() == nullptr);

    std::printf("%d failure(s)\n", failures);
    std::fflush(stdout);
    return failures == 0 ? 0 : 1;
}
