#include "projectmodel.h"
#include "navigationservice.h"
#include "relationshipanalysisworker.h"
#include "relationshipresultpublisher.h"
#include "relationshipservice.h"
#include "semanticindex.h"
#include "slangmanager.h"
#include "symbolanalyzer.h"
#include "symbolrelationshipengine.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

namespace {
QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

QStringList scanWorkspaceFiles(const QString& workspaceRoot)
{
    QStringList files;
    QDirIterator it(workspaceRoot, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(normalizedPath(it.next()));
    files.sort(Qt::CaseInsensitive);
    return files;
}

qint64 totalBytes(const QStringList& files)
{
    qint64 total = 0;
    for (const QString& fileName : files)
        total += QFileInfo(fileName).size();
    return total;
}

void printMetric(const QString& name, qint64 value)
{
    std::printf("perf.%s=%lld\n",
                name.toLocal8Bit().constData(),
                static_cast<long long>(value));
    std::fflush(stdout);
}

void printMetric(const QString& name, int value)
{
    std::printf("perf.%s=%d\n", name.toLocal8Bit().constData(), value);
    std::fflush(stdout);
}

void printEvent(const QString& name)
{
    std::printf("perf.event=%s\n", name.toLocal8Bit().constData());
    std::fflush(stdout);
}

void printTextMetric(const QString& name, const QString& value)
{
    std::printf("perf.%s=%s\n",
                name.toLocal8Bit().constData(),
                value.toLocal8Bit().constData());
    std::fflush(stdout);
}

QString normalizedFileKey(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QList<SemanticSymbolRecord> fileSymbolRecords(
    const SemanticSnapshotToken& snapshotToken,
    const QString& fileName)
{
    if (!snapshotToken.isValid())
        return {};
    return snapshotToken.snapshot->getSymbolRecords(fileName);
}

QList<SemanticRelationship> toSemanticRelationships(
    const QVector<RelationshipToAdd>& relationships)
{
    QList<SemanticRelationship> result;
    result.reserve(relationships.size());
    for (const RelationshipToAdd& relationship : relationships) {
        if (relationship.fromId < 0 || relationship.toId < 0)
            continue;
        SemanticRelationship item;
        item.fromId = relationship.fromId;
        item.toId = relationship.toId;
        item.type = relationship.type;
        item.confidence = relationship.confidence;
        item.evidenceText = relationship.context;
        item.evidenceRange = relationship.evidenceRange;
        item.fromAccessPath = relationship.fromAccessPath;
        item.toAccessPath = relationship.toAccessPath;
        item.provenance = RelationshipProvenance::Inferred;
        result.append(item);
    }
    return result;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    bool verboseFiles = false;
    QString workspaceArg;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--verbose-files")) {
            verboseFiles = true;
            continue;
        }
        if (workspaceArg.isEmpty())
            workspaceArg = arg;
    }

    const QString workspaceRoot = !workspaceArg.isEmpty()
        ? normalizedPath(workspaceArg)
        : normalizedPath(QDir::current().absoluteFilePath(
              QStringLiteral("test_sv/huge_prj")));
    if (!QFileInfo(workspaceRoot).isDir()) {
        std::fprintf(stderr,
                     "workspace does not exist: %s\n",
                     workspaceRoot.toLocal8Bit().constData());
        return 2;
    }

    QElapsedTimer timer;
    timer.start();
    const QStringList allFiles = scanWorkspaceFiles(workspaceRoot);
    const qint64 scanMs = timer.elapsed();

    ProjectModel projectModel;
    projectModel.setWorkspaceRoot(workspaceRoot);
    projectModel.setScannedFiles(allFiles);
    const ProjectSnapshot project = projectModel.snapshot();
    if (!project.isOpen() || project.systemVerilogFiles.isEmpty()) {
        std::fprintf(stderr,
                     "workspace has no HDL files: %s\n",
                     workspaceRoot.toLocal8Bit().constData());
        return 3;
    }
    printMetric(QStringLiteral("all_files"), allFiles.size());
    printMetric(QStringLiteral("hdl_files"), project.systemVerilogFiles.size());
    printMetric(QStringLiteral("hdl_bytes"), totalBytes(project.systemVerilogFiles));
    printMetric(QStringLiteral("scan_ms"), scanMs);

    printEvent(QStringLiteral("symbol_start"));
    timer.restart();
    SymbolAnalyzer symbolAnalyzer;
    WorkspaceAnalysisTelemetry symbolTelemetry;
    QEventLoop symbolLoop;
    QTimer symbolTimeout;
    symbolTimeout.setSingleShot(true);
    int symbolExitCode = 0;
    QObject::connect(
        &symbolAnalyzer,
        &SymbolAnalyzer::workspaceAnalysisTelemetry,
        &symbolAnalyzer,
        [&](const WorkspaceAnalysisTelemetry& telemetry) {
            symbolTelemetry = telemetry;
        });
    QObject::connect(
        &symbolAnalyzer,
        &SymbolAnalyzer::analysisCompleted,
        &symbolAnalyzer,
        [&](const QString& fileName, int) {
            if (normalizedPath(fileName) == project.workspaceRoot)
                symbolLoop.quit();
        });
    QObject::connect(
        &symbolAnalyzer,
        &SymbolAnalyzer::workspaceAnalysisExpired,
        &symbolAnalyzer,
        [&]() {
            symbolExitCode = 5;
            symbolLoop.quit();
        });
    QObject::connect(&symbolTimeout,
                     &QTimer::timeout,
                     &symbolLoop,
                     [&]() {
                         symbolExitCode = 6;
                         symbolLoop.quit();
                     });
    symbolTimeout.start(180000);
    symbolAnalyzer.startAnalyzeProjectAsync(project);
    symbolLoop.exec();
    if (symbolExitCode != 0) {
        std::fprintf(stderr,
                     "workspace symbol analysis failed: %d\n",
                     symbolExitCode);
        return symbolExitCode;
    }
    const qint64 symbolMs = timer.elapsed();

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    const auto baseSnapshot = semanticIndex->beginRelationshipAnalysisSnapshot();
    if (!baseSnapshot.isValid()) {
        std::fprintf(stderr, "semantic base snapshot is not available\n");
        return 4;
    }
    const int symbolCount = baseSnapshot.snapshot
        ? baseSnapshot.snapshot->getSymbolRecords().size()
        : 0;
    printMetric(QStringLiteral("symbol_ms"), symbolMs);
    printMetric(QStringLiteral("symbol_worker_ms"),
                symbolTelemetry.workerElapsedMs);
    printMetric(QStringLiteral("symbol_extraction_ms"),
                symbolTelemetry.symbolExtractionMs);
    printMetric(QStringLiteral("symbol_result_assembly_ms"),
                symbolTelemetry.resultAssemblyMs);
    printMetric(QStringLiteral("symbol_diagnostics_ms"),
                symbolTelemetry.diagnosticsExtractionMs);
    printMetric(QStringLiteral("symbol_publication_ms"),
                symbolTelemetry.publicationMs);
    printMetric(QStringLiteral("symbol_publication_update_ms"),
                symbolTelemetry.publicationUpdateMs);
    printMetric(QStringLiteral("symbol_final_snapshot_ms"),
                symbolTelemetry.finalSnapshotMs);
    printMetric(QStringLiteral("symbol_total_telemetry_ms"),
                symbolTelemetry.totalElapsedMs);
    printMetric(QStringLiteral("diagnostics"),
                symbolTelemetry.diagnostics);
    printMetric(QStringLiteral("symbols"), symbolCount);

    SlangManager slangManager;
    SymbolRelationshipEngine relationshipEngine;
    auto relationshipBuilder =
        semanticIndex->createRelationshipBuilder(&relationshipEngine,
                                                 &slangManager);

    printEvent(QStringLiteral("relationship_extraction_start"));
    QElapsedTimer relationshipTimer;
    relationshipTimer.start();
    QElapsedTimer stageTimer;
    stageTimer.start();
    WorkspaceRelationshipAnalysisResult result;
    result.baseSnapshot = baseSnapshot;
    result.semanticSnapshot = baseSnapshot.snapshot;
    result.totalFiles = project.systemVerilogFiles.size();
    const QHash<QString, RelationshipExtractionInfo> relationshipInfoByFile =
        relationshipBuilder->extractWorkspaceRelationshipInfo(
            project.systemVerilogFiles,
            project.includeDirs,
            project.defines);
    result.extractionMs = stageTimer.elapsed();
    printMetric(QStringLiteral("relationship_extraction_ms"),
                result.extractionMs);
    printMetric(QStringLiteral("relationship_extraction_buckets"),
                relationshipInfoByFile.size());

    printEvent(QStringLiteral("relationship_compute_start"));
    result.fileRelationships.reserve(project.systemVerilogFiles.size());
    QList<SemanticRelationship> newRelationships;
    int progressSincePrint = 0;
    for (const QString& filePath : project.systemVerilogFiles) {
        QElapsedTimer fileTimer;
        fileTimer.start();
        stageTimer.restart();
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString content = QTextStream(&file).readAll();
        result.fileReadMs += stageTimer.elapsed();

        const QList<SemanticSymbolRecord> fileSymbols =
            fileSymbolRecords(baseSnapshot, filePath);
        const auto infoIt =
            relationshipInfoByFile.constFind(normalizedFileKey(filePath));
        const RelationshipExtractionInfo* relationshipInfo =
            infoIt == relationshipInfoByFile.constEnd()
                ? nullptr
                : &infoIt.value();
        const int infoItems = relationshipInfo
            ? relationshipInfo->moduleInstantiations.size()
                + relationshipInfo->assignments.size()
                + relationshipInfo->conditionReferences.size()
                + relationshipInfo->subroutineCalls.size()
                + relationshipInfo->timingSignals.size()
            : 0;

        stageTimer.restart();
        const QVector<RelationshipToAdd> relationships =
            relationshipBuilder->computeRelationships(
                filePath,
                content,
                fileSymbols,
                baseSnapshot.snapshot.get(),
                project.includeDirs,
                project.defines,
                relationshipInfo);
        result.computeMs += stageTimer.elapsed();

        stageTimer.restart();
        const QList<SemanticRelationship> semanticRelationships =
            toSemanticRelationships(relationships);
        result.fileRelationships.append({filePath, relationships});
        ++result.processedFiles;
        result.relationshipCount += semanticRelationships.size();
        newRelationships.append(semanticRelationships);
        result.conversionMs += stageTimer.elapsed();

        ++progressSincePrint;
        if (verboseFiles) {
            printMetric(QStringLiteral("relationship_progress_files"),
                        result.processedFiles);
            printMetric(QStringLiteral("relationship_progress_last_file_ms"),
                        fileTimer.elapsed());
            printMetric(QStringLiteral("relationship_progress_last_file_bytes"),
                        QFileInfo(filePath).size());
            printMetric(QStringLiteral("relationship_progress_info_items"),
                        infoItems);
            printMetric(QStringLiteral("relationship_progress_symbols"),
                        fileSymbols.size());
            printMetric(QStringLiteral("relationship_progress_relationships"),
                        relationships.size());
            printMetric(QStringLiteral("relationship_progress_ms"),
                        relationshipTimer.elapsed());
            printTextMetric(QStringLiteral("relationship_progress_file"),
                            filePath);
        } else if (progressSincePrint >= 25
                   || result.processedFiles == result.totalFiles) {
            printMetric(QStringLiteral("relationship_progress_files"),
                        result.processedFiles);
            printMetric(QStringLiteral("relationship_progress_ms"),
                        relationshipTimer.elapsed());
            progressSincePrint = 0;
        }
    }

    printEvent(QStringLiteral("relationship_snapshot_merge_start"));
    stageTimer.restart();
    result.semanticSnapshot =
        semanticIndex->snapshotWithAdditionalRelationships(
            baseSnapshot.snapshot,
            newRelationships);
    result.snapshotMergeMs = stageTimer.elapsed();
    result.elapsedMs = relationshipTimer.elapsed();
    printMetric(QStringLiteral("relationship_snapshot_merge_ms"),
                result.snapshotMergeMs);

    printEvent(QStringLiteral("relationship_publish_start"));
    RelationshipResultPublisher publisher;
    publisher.setRelationshipEngine(&relationshipEngine);
    timer.restart();
    const bool published = publisher.applyWorkspaceResult(result);
    const qint64 publishMs = timer.elapsed();
    if (!published) {
        std::fprintf(stderr, "failed to publish relationship result\n");
        return 5;
    }

    printMetric(QStringLiteral("relationship_worker_ms"), result.elapsedMs);
    printMetric(QStringLiteral("relationship_file_read_ms"), result.fileReadMs);
    printMetric(QStringLiteral("relationship_compute_ms"), result.computeMs);
    printMetric(QStringLiteral("relationship_conversion_ms"), result.conversionMs);
    printMetric(QStringLiteral("relationship_publish_ms"), publishMs);
    printMetric(QStringLiteral("relationship_total_ms"), result.elapsedMs + publishMs);
    printMetric(QStringLiteral("relationship_files"), result.processedFiles);
    printMetric(QStringLiteral("relationships"), result.relationshipCount);
    printMetric(QStringLiteral("engine_relationships"),
                relationshipEngine.getRelationshipCount());

    RelationshipService relationshipService(semanticIndex);
    const QList<SemanticSymbolRecord> ctlDefs =
        semanticIndex->findDefinitionRecords(QStringLiteral("vendor_ip_ctl"));
    if (!ctlDefs.isEmpty()) {
        RelationshipQuery ctlIncomingQuery;
        ctlIncomingQuery.symbolStableKey = ctlDefs.first().stableKey;
        ctlIncomingQuery.outgoing = false;
        ctlIncomingQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
        const QList<RelationshipResult> ctlParents =
            relationshipService.findRelationships(ctlIncomingQuery);
        QStringList parentNames;
        for (const RelationshipResult& parent : ctlParents)
            parentNames.append(parent.fromSymbolRecord.name);
        parentNames.removeDuplicates();
        printMetric(QStringLiteral("design_ctl_parent_count"), parentNames.size());
        printTextMetric(QStringLiteral("design_ctl_parents"),
                        parentNames.mid(0, 12).join(QLatin1Char(',')));
    }

    NavigationService navigationService(semanticIndex);
    printEvent(QStringLiteral("design_top_infer_start"));
    timer.restart();
    const QStringList designTops = navigationService.inferDesignTopModules();
    const QString designTop = designTops.isEmpty() ? QString() : designTops.first();
    const qint64 designTopInferMs = timer.elapsed();
    printMetric(QStringLiteral("design_top_infer_ms"), designTopInferMs);
    printTextMetric(QStringLiteral("design_top"), designTop);
    printMetric(QStringLiteral("design_top_count"), designTops.size());
    printTextMetric(QStringLiteral("design_top_roots"),
                    designTops.mid(0, 12).join(QLatin1Char(',')));

    printEvent(QStringLiteral("design_hierarchy_start"));
    timer.restart();
    const DesignHierarchyReport designReport =
        navigationService.findDesignHierarchy(designTop);
    const qint64 designHierarchyMs = timer.elapsed();
    printMetric(QStringLiteral("design_hierarchy_ms"), designHierarchyMs);
    printMetric(QStringLiteral("design_hierarchy_nodes"),
                designReport.nodes.size());
    printMetric(QStringLiteral("design_hierarchy_files"),
                designReport.participatingFiles.size());
    printMetric(QStringLiteral("design_unresolved_modules"),
                designReport.unresolvedModules.size());
    const DesignHierarchyReport ctlReport =
        navigationService.findDesignHierarchy(QStringLiteral("vendor_ip_ctl"));
    printMetric(QStringLiteral("design_ctl_nodes"), ctlReport.nodes.size());
    printMetric(QStringLiteral("design_ctl_files"), ctlReport.participatingFiles.size());
    const DesignHierarchyReport gphyReport =
        navigationService.findDesignHierarchy(QStringLiteral("vendor_ip_gphy"));
    printMetric(QStringLiteral("design_gphy_nodes"), gphyReport.nodes.size());
    printMetric(QStringLiteral("design_gphy_files"), gphyReport.participatingFiles.size());

    return 0;
}
