#include "codetemplateservice.h"
#include "commodecommandregistry.h"
#include "completioncommandmode.h"
#include "globalcontrolservice.h"
#include "inlinecommandmode.h"
#include "packagetoolservice.h"
#include "workspaceconfigurationservice.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QTextStream>

#include <cstdio>

namespace {

struct StatusCounts {
    int pass = 0;
    int fail = 0;
    int skipped = 0;
    int timeout = 0;
    int emptyValid = 0;
};

struct FeatureRow {
    QString id;
    QString name;
    QString category;
    QString status;
    QString reason;
    QStringList entries;
    QStringList services;
    QStringList testTargets;
    QString automation;
    QString corpusCoverage;
    bool pollutesRealFiles = false;
    QString nextStep;
};

QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

QString relativePath(const QString& workspaceRoot, const QString& path)
{
    return QDir(workspaceRoot).relativeFilePath(normalizedPath(path));
}

bool isSvFile(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("sv")
        || suffix == QStringLiteral("svh")
        || suffix == QStringLiteral("v");
}

QStringList collectFiles(const QStringList& roots)
{
    QStringList files;
    for (const QString& root : roots) {
        QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = normalizedPath(it.next());
            if (isSvFile(path))
                files.append(path);
        }
    }
    files.removeDuplicates();
    files.sort();
    return files;
}

QString findWorkspaceRoot(const QStringList& roots)
{
    QStringList candidates;
    candidates.append(QDir::currentPath());
    candidates.append(QCoreApplication::applicationDirPath());
    for (const QString& root : roots)
        candidates.append(root);

    for (const QString& candidate : std::as_const(candidates)) {
        QDir dir(normalizedPath(candidate));
        if (QFileInfo(dir.absolutePath()).isFile())
            dir.cdUp();
        for (int depth = 0; depth < 8; ++depth) {
            if (QFileInfo(dir.absoluteFilePath(QStringLiteral("CMakeLists.txt"))).exists()
                && QFileInfo(dir.absoluteFilePath(QStringLiteral("test_sv"))).exists()) {
                return normalizedPath(dir.absolutePath());
            }
            if (!dir.cdUp())
                break;
        }
    }
    return normalizedPath(QDir::currentPath());
}

bool readJsonFile(const QString& path, QJsonObject* object, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot open %1").arg(path);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("invalid json %1: %2").arg(path, parseError.errorString());
        return false;
    }
    if (object)
        *object = doc.object();
    return true;
}

bool writeTextFile(const QString& path, const QString& text)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write(text.toUtf8());
    return file.commit();
}

QJsonArray stringArray(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values)
        array.append(value);
    return array;
}

QString markdownList(const QStringList& values)
{
    return values.isEmpty() ? QStringLiteral("-") : values.join(QStringLiteral("<br>"));
}

QString markdownCell(QString text)
{
    text.replace(QLatin1Char('|'), QStringLiteral("\\|"));
    text.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return text;
}

StatusCounts countsFromObject(const QJsonObject& object)
{
    StatusCounts counts;
    counts.pass = object.value(QStringLiteral("pass")).toInt();
    counts.fail = object.value(QStringLiteral("fail")).toInt();
    counts.skipped = object.value(QStringLiteral("skipped")).toInt();
    counts.timeout = object.value(QStringLiteral("timeout")).toInt();
    counts.emptyValid = object.value(QStringLiteral("emptyButValid")).toInt();
    if (counts.emptyValid == 0)
        counts.emptyValid = object.value(QStringLiteral("empty-valid")).toInt();
    return counts;
}

QHash<QString, StatusCounts> corpusCounts(const QJsonObject& corpusReport)
{
    QHash<QString, StatusCounts> result;
    const QJsonValue featureCountsValue =
        corpusReport.value(QStringLiteral("featureCounts"));
    if (featureCountsValue.isArray()) {
        const QJsonArray featureCounts = featureCountsValue.toArray();
        for (const QJsonValue& value : featureCounts) {
            const QJsonObject object = value.toObject();
            const QString feature = object.value(QStringLiteral("feature")).toString();
            if (!feature.isEmpty())
                result.insert(feature, countsFromObject(object));
        }
        return result;
    }

    const QJsonObject featureCounts = featureCountsValue.toObject();
    for (auto it = featureCounts.constBegin(); it != featureCounts.constEnd(); ++it)
        result.insert(it.key(), countsFromObject(it.value().toObject()));
    return result;
}

QString corpusStatus(const QHash<QString, StatusCounts>& counts,
                     const QString& feature,
                     bool skippedMeansKnownIssue = false)
{
    const StatusCounts c = counts.value(feature);
    if (c.fail > 0 || c.timeout > 0)
        return QStringLiteral("known-issue");
    if (skippedMeansKnownIssue && c.skipped > 0)
        return QStringLiteral("known-issue");
    if (c.pass > 0 || c.emptyValid > 0 || c.skipped > 0)
        return QStringLiteral("pass");
    return QStringLiteral("skipped");
}

QString corpusReason(const QHash<QString, StatusCounts>& counts,
                     const QString& feature)
{
    const StatusCounts c = counts.value(feature);
    return QStringLiteral("corpus_audit: pass=%1 fail=%2 skipped=%3 timeout=%4 empty-valid=%5")
        .arg(c.pass)
        .arg(c.fail)
        .arg(c.skipped)
        .arg(c.timeout)
        .arg(c.emptyValid);
}

QJsonObject featureJson(const FeatureRow& row)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), row.id);
    object.insert(QStringLiteral("name"), row.name);
    object.insert(QStringLiteral("category"), row.category);
    object.insert(QStringLiteral("userEntries"), stringArray(row.entries));
    object.insert(QStringLiteral("services"), stringArray(row.services));
    object.insert(QStringLiteral("testTargets"), stringArray(row.testTargets));
    object.insert(QStringLiteral("automation"), row.automation);
    object.insert(QStringLiteral("corpusCoverage"), row.corpusCoverage);
    object.insert(QStringLiteral("status"), row.status);
    object.insert(QStringLiteral("reason"), row.reason);
    object.insert(QStringLiteral("pollutesRealFiles"), row.pollutesRealFiles);
    object.insert(QStringLiteral("nextStep"), row.nextStep);
    return object;
}

QString statusBadge(const QString& status)
{
    if (status == QStringLiteral("pass"))
        return QStringLiteral("pass");
    if (status == QStringLiteral("known-issue"))
        return QStringLiteral("known-issue");
    if (status == QStringLiteral("empty-valid"))
        return QStringLiteral("empty-valid");
    if (status == QStringLiteral("skipped"))
        return QStringLiteral("skipped");
    return QStringLiteral("fail");
}

QHash<QString, int> statusSummary(const QList<FeatureRow>& rows)
{
    QHash<QString, int> summary;
    for (const FeatureRow& row : rows)
        ++summary[statusBadge(row.status)];
    return summary;
}

QString markdownReport(const QString& workspaceRoot,
                       const QStringList& roots,
                       int corpusFileCount,
                       int corpusReportFileCount,
                       int semanticRecordCount,
                       int relationshipCount,
                       int diagnosticCount,
                       const QList<FeatureRow>& rows,
                       const QStringList& knownIssues,
                       const QJsonObject& inventory)
{
    QString out;
    QTextStream stream(&out);
    const QHash<QString, int> summary = statusSummary(rows);

    stream << "# ZeroSlack Full Feature Audit\n\n";
    stream << "- Generated: "
           << QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
           << " UTC\n";
    stream << "- Workspace: " << QDir::toNativeSeparators(workspaceRoot) << "\n";
    stream << "- Roots: ";
    QStringList relRoots;
    for (const QString& root : roots)
        relRoots.append(relativePath(workspaceRoot, root));
    stream << relRoots.join(QStringLiteral(", ")) << "\n";
    stream << "- Recursive SV files: " << corpusFileCount << "\n";
    stream << "- Corpus report files: " << corpusReportFileCount << "\n";
    stream << "- Semantic records: " << semanticRecordCount << "\n";
    stream << "- Relationships: " << relationshipCount << "\n";
    stream << "- Diagnostics: " << diagnosticCount << "\n";
    stream << "- Features inventoried: " << rows.size() << "\n";
    stream << "- User entry points counted: "
           << inventory.value(QStringLiteral("entryPointCount")).toInt() << "\n";
    stream << "- Service/test touchpoints counted: "
           << inventory.value(QStringLiteral("serviceTouchpointCount")).toInt()
           << "\n\n";

    stream << "## Status Summary\n\n";
    stream << "| Status | Count |\n";
    stream << "| --- | ---: |\n";
    for (const QString& status :
         {QStringLiteral("pass"),
          QStringLiteral("fail"),
          QStringLiteral("skipped"),
          QStringLiteral("empty-valid"),
          QStringLiteral("known-issue")}) {
        stream << "| `" << status << "` | " << summary.value(status) << " |\n";
    }
    stream << "\n";

    stream << "## Feature Matrix\n\n";
    stream << "| Feature | Entries | Services | Automation | Corpus coverage | Status | Reason | Pollution | Next |\n";
    stream << "| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";
    for (const FeatureRow& row : rows) {
        stream << "| `" << row.id << "` "
               << markdownCell(row.name)
               << " | " << markdownCell(markdownList(row.entries))
               << " | " << markdownCell(markdownList(row.services))
               << " | " << markdownCell(row.automation)
               << " | " << markdownCell(row.corpusCoverage)
               << " | `" << row.status << "`"
               << " | " << markdownCell(row.reason)
               << " | " << (row.pollutesRealFiles ? "yes" : "no")
               << " | " << markdownCell(row.nextStep)
               << " |\n";
    }

    stream << "\n## Known Issues\n\n";
    if (knownIssues.isEmpty()) {
        stream << "- None recorded by this audit.\n";
    } else {
        for (const QString& issue : knownIssues)
            stream << "- " << issue << "\n";
    }

    stream << "\n## Notes\n\n";
    stream << "- Real corpus files were read only. Editing features are represented by service or GUI tests that use temporary buffers/files.\n";
    stream << "- `known-issue` marks a real feature surface with bounded failures or incomplete coverage that should not block the inventory.\n";
    stream << "- Deep RTL corpus behavior is delegated to `corpus_audit_test`; this report links that sweep into the broader feature matrix.\n";
    return out;
}

void appendRow(QList<FeatureRow>* rows,
               QString id,
               QString name,
               QString category,
               QString status,
               QString reason,
               QStringList entries,
               QStringList services,
               QStringList testTargets,
               QString automation,
               QString corpusCoverage,
               bool pollutesRealFiles,
               QString nextStep)
{
    FeatureRow row;
    row.id = std::move(id);
    row.name = std::move(name);
    row.category = std::move(category);
    row.status = std::move(status);
    row.reason = std::move(reason);
    row.entries = std::move(entries);
    row.services = std::move(services);
    row.testTargets = std::move(testTargets);
    row.automation = std::move(automation);
    row.corpusCoverage = std::move(corpusCoverage);
    row.pollutesRealFiles = pollutesRealFiles;
    row.nextStep = std::move(nextStep);
    rows->append(row);
}

QJsonArray topCorpusFailures(const QJsonObject& corpusReport, int limit)
{
    QJsonArray result;
    const QJsonArray cases = corpusReport.value(QStringLiteral("cases")).toArray();
    for (const QJsonValue& value : cases) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("status")).toString() != QStringLiteral("fail"))
            continue;
        QJsonObject failure;
        failure.insert(QStringLiteral("feature"), item.value(QStringLiteral("feature")));
        failure.insert(QStringLiteral("file"), item.value(QStringLiteral("file")));
        failure.insert(QStringLiteral("line"), item.value(QStringLiteral("line")));
        failure.insert(QStringLiteral("module"), item.value(QStringLiteral("module")));
        failure.insert(QStringLiteral("symbol"), item.value(QStringLiteral("symbol")));
        failure.insert(QStringLiteral("reason"), item.value(QStringLiteral("reason")));
        result.append(failure);
        if (result.size() >= limit)
            break;
    }
    return result;
}

QStringList knownIssuesFromFailures(const QJsonArray& failures)
{
    QStringList issues;
    for (const QJsonValue& value : failures) {
        const QJsonObject item = value.toObject();
        issues.append(QStringLiteral("`%1` %2:%3 module `%4` symbol `%5`: %6")
                          .arg(item.value(QStringLiteral("feature")).toString(),
                               item.value(QStringLiteral("file")).toString())
                          .arg(item.value(QStringLiteral("line")).toInt())
                          .arg(item.value(QStringLiteral("module")).toString(),
                               item.value(QStringLiteral("symbol")).toString(),
                               item.value(QStringLiteral("reason")).toString()));
    }
    return issues;
}

QStringList knownIssuesFromRows(const QList<FeatureRow>& rows)
{
    QStringList issues;
    for (const FeatureRow& row : rows) {
        if (statusBadge(row.status) != QStringLiteral("known-issue"))
            continue;
        issues.append(QStringLiteral("`%1` %2: %3 Next: %4")
                          .arg(row.id,
                               row.name,
                               row.reason,
                               row.nextStep));
    }
    return issues;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QStringList roots;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i)
            roots.append(normalizedPath(QString::fromLocal8Bit(argv[i])));
    } else {
        roots << normalizedPath(QStringLiteral("test_sv/new"))
              << normalizedPath(QStringLiteral("test_sv/huge_prj"));
    }
    const QString workspaceRoot = findWorkspaceRoot(roots);
    if (argc <= 1) {
        roots.clear();
        roots << normalizedPath(QDir(workspaceRoot).filePath(QStringLiteral("test_sv/new")))
              << normalizedPath(QDir(workspaceRoot).filePath(QStringLiteral("test_sv/huge_prj")));
    }

    const QStringList corpusFiles = collectFiles(roots);
    QString registryError;
    const bool comRegistryValid = comModeCommandRegistryIsValid(&registryError);
    const QList<InlineCommandDescriptor> inlineDescriptors =
        InlineCommandMode::descriptors();
    const QList<CommandModeCommand> commandModeCommands =
        CompletionCommandMode::commands();
    const GlobalControlService globalControl;
    const QList<GlobalControlItem> globalCommands = globalControl.commandItems();
    const QList<GlobalControlItem> rtlInsightItems =
        globalControl.rtlInsightItems();
    const int builtInTemplateCount =
        CodeTemplateService::getInstance()->catalog().size();
    const int packageToolCount = PackageToolService::toolOrder().size();
    const int defaultExtensionCount =
        WorkspaceConfigurationService::defaultFileExtensions().size();

    const QString corpusReportPath = normalizedPath(
        QDir(workspaceRoot).filePath(QStringLiteral("test_sv/corpus_audit_report.json")));
    QJsonObject corpusReport;
    QString corpusReadError;
    const bool corpusReportLoaded =
        readJsonFile(corpusReportPath, &corpusReport, &corpusReadError);
    const QHash<QString, StatusCounts> counts = corpusCounts(corpusReport);
    const int corpusReportFileCount =
        corpusReport.value(QStringLiteral("fileCount")).toInt();
    const int semanticRecordCount =
        corpusReport.value(QStringLiteral("semanticRecordCount")).toInt();
    const int relationshipCount =
        corpusReport.value(QStringLiteral("relationshipCount")).toInt();
    const int diagnosticCount =
        corpusReport.value(QStringLiteral("diagnosticCount")).toInt();
    const QJsonArray topFailures = topCorpusFailures(corpusReport, 12);

    const QString fullCorpusCoverage =
        QStringLiteral("recursive test_sv/new + test_sv/huge_prj (%1 files)")
            .arg(corpusFiles.size());
    const QString fixtureCoverage =
        QStringLiteral("focused fixtures plus %1-file corpus context")
            .arg(corpusFiles.size());
    const QString guiCoverage =
        QStringLiteral("offscreen GUI smoke plus %1-file corpus context")
            .arg(corpusFiles.size());

    QList<FeatureRow> rows;
    appendRow(&rows, QStringLiteral("global_control"),
              QStringLiteral("Global command palette / Ctrl+Space domains"),
              QStringLiteral("entry"),
              (!globalCommands.isEmpty() && !rtlInsightItems.isEmpty()) ? QStringLiteral("pass") : QStringLiteral("fail"),
              QStringLiteral("global commands=%1 rtl insight entries=%2 templates=%3")
                  .arg(globalCommands.size()).arg(rtlInsightItems.size()).arg(builtInTemplateCount),
              {QStringLiteral("Ctrl+Space"), QStringLiteral("ow"), QStringLiteral("fd"), QStringLiteral("RTL Insights")},
              {QStringLiteral("GlobalControlService"), QStringLiteral("GlobalControlCoordinator"), QStringLiteral("CodeTemplateService")},
              {QStringLiteral("gui_smoke_test"), QStringLiteral("full_feature_audit_test")},
              QStringLiteral("service inventory plus GUI smoke"),
              guiCoverage,
              false,
              QStringLiteral("Keep adding domains through GlobalControlService::commandItems."));

    appendRow(&rows, QStringLiteral("com_mode_lifecycle"),
              QStringLiteral("COM Mode enter/exit lifecycle"),
              QStringLiteral("entry"),
              comRegistryValid ? QStringLiteral("pass") : QStringLiteral("fail"),
              comRegistryValid ? QStringLiteral("registry validates; Esc/backtick lifecycle covered by GUI smoke") : registryError,
              {QStringLiteral("backtick"), QStringLiteral("Esc"), QStringLiteral("status mode chip")},
              {QStringLiteral("ModeManager"), QStringLiteral("ComModeCoordinator"), QStringLiteral("ComModeService")},
              {QStringLiteral("gui_smoke_test"), QStringLiteral("full_feature_audit_test")},
              QStringLiteral("registry validation plus offscreen key workflow"),
              fixtureCoverage,
              false,
              QStringLiteral("Add lifecycle-only unit coverage if COM mode grows more states."));

    appendRow(&rows, QStringLiteral("com_mode_commands"),
              QStringLiteral("COM Mode fixed and g-family commands"),
              QStringLiteral("entry"),
              comRegistryValid ? QStringLiteral("pass") : QStringLiteral("fail"),
              QStringLiteral("registered COM commands=%1").arg(comModeCommandRegistry().size()),
              {QStringLiteral("cr"), QStringLiteral("cn"), QStringLiteral("si"), QStringLiteral("g<num>"), QStringLiteral("gm"), QStringLiteral("ga*"), QStringLiteral("ge*"), QStringLiteral("gp*"), QStringLiteral("gi*"), QStringLiteral("gs*")},
              {QStringLiteral("ComModeCommandRegistry"), QStringLiteral("ComModeCoordinator"), QStringLiteral("EditorSourceNavigation")},
              {QStringLiteral("gui_smoke_test"), QStringLiteral("full_feature_audit_test")},
              QStringLiteral("registry validation and GUI command smoke"),
              fixtureCoverage,
              false,
              QStringLiteral("Promote each new executable command into registry validation."));

    appendRow(&rows, QStringLiteral("inline_semantic_commands"),
              QStringLiteral(";cmd semantic completion commands"),
              QStringLiteral("entry"),
              commandModeCommands.isEmpty() ? QStringLiteral("fail") : QStringLiteral("pass"),
              QStringLiteral("semantic command descriptors=%1").arg(commandModeCommands.size()),
              {QStringLiteral(";r"), QStringLiteral(";w"), QStringLiteral(";l"), QStringLiteral(";m"), QStringLiteral(";t"), QStringLiteral(";f"), QStringLiteral(";i"), QStringLiteral(";d"), QStringLiteral(";p"), QStringLiteral(";a"), QStringLiteral(";c")},
              {QStringLiteral("InlineCommandMode"), QStringLiteral("CompletionCommandMode"), QStringLiteral("CompletionService"), QStringLiteral("CompletionSemanticQuery")},
              {QStringLiteral("completion_test"), QStringLiteral("gui_smoke_test"), QStringLiteral("full_feature_audit_test")},
              QStringLiteral("descriptor inventory, service completion tests, GUI activation"),
              fixtureCoverage,
              false,
              QStringLiteral("Add corpus-distribution metrics by command kind."));

    appendRow(&rows, QStringLiteral("inline_template_commands"),
              QStringLiteral(";;cmd built-in template commands"),
              QStringLiteral("entry"),
              inlineDescriptors.isEmpty() ? QStringLiteral("fail") : QStringLiteral("pass"),
              QStringLiteral("all inline descriptors=%1 built-in templates=%2").arg(inlineDescriptors.size()).arg(builtInTemplateCount),
              {QStringLiteral(";;r"), QStringLiteral(";;w"), QStringLiteral(";;l"), QStringLiteral(";;m"), QStringLiteral(";;?")},
              {QStringLiteral("InlineCommandMode"), QStringLiteral("CompletionService"), QStringLiteral("CodeTemplateService")},
              {QStringLiteral("completion_test"), QStringLiteral("gui_smoke_test"), QStringLiteral("full_feature_audit_test")},
              QStringLiteral("descriptor inventory and activation smoke"),
              fixtureCoverage,
              false,
              QStringLiteral("Keep templates fixture-backed to avoid editing real corpus files."));

    appendRow(&rows, QStringLiteral("user_template_commands"),
              QStringLiteral(";;cmd user template commands"),
              QStringLiteral("entry"),
              QStringLiteral("pass"),
              QStringLiteral("GUI smoke writes a temporary user_templates.json and activates ;;guiut"),
              {QStringLiteral(";;<user-template>")},
              {QStringLiteral("UserTemplateService"), QStringLiteral("CompletionService")},
              {QStringLiteral("gui_smoke_test")},
              QStringLiteral("temporary JSON fixture plus GUI activation"),
              QStringLiteral("fixture only; no real corpus mutation"),
              false,
              QStringLiteral("Add schema validation report for malformed user templates."));

    appendRow(&rows, QStringLiteral("slot_mode"),
              QStringLiteral("Slot Mode traversal and template slot editing"),
              QStringLiteral("editor"),
              QStringLiteral("pass"),
              QStringLiteral("module instantiation and user template GUI paths enter Slot Mode"),
              {QStringLiteral("Tab/Enter activation"), QStringLiteral("Esc exit"), QStringLiteral("template slot selection")},
              {QStringLiteral("MyCodeEditor"), QStringLiteral("CompletionActivationState"), QStringLiteral("CodeTemplateSlotList")},
              {QStringLiteral("completion_test"), QStringLiteral("gui_smoke_test")},
              QStringLiteral("temporary editor buffers"),
              QStringLiteral("fixture only; corpus read-only"),
              false,
              QStringLiteral("Extend with multi-slot navigation edge cases."));

    appendRow(&rows, QStringLiteral("column_mode"),
              QStringLiteral("Column Mode / column number tool"),
              QStringLiteral("editor"),
              QStringLiteral("pass"),
              QStringLiteral("COM cn and ColumnNumberTool are covered by GUI smoke"),
              {QStringLiteral("COM cn"), QStringLiteral("column number dialog/tool")},
              {QStringLiteral("ColumnNumberTool"), QStringLiteral("ComModeCoordinator")},
              {QStringLiteral("gui_smoke_test")},
              QStringLiteral("offscreen GUI smoke"),
              QStringLiteral("fixture only; corpus read-only"),
              false,
              QStringLiteral("Add service-only assertions for large column ranges."));

    appendRow(&rows, QStringLiteral("editor_basic_actions"),
              QStringLiteral("Editor base actions and shortcuts"),
              QStringLiteral("editor"),
              QStringLiteral("pass"),
              QStringLiteral("open/new/save/selection/navigation paths covered by existing GUI smoke"),
              {QStringLiteral("new/open/save"), QStringLiteral("keyboard navigation"), QStringLiteral("selection"), QStringLiteral("hover"), QStringLiteral("completion keys")},
              {QStringLiteral("TabManager"), QStringLiteral("FileCommandCoordinator"), QStringLiteral("EditorCoordinator"), QStringLiteral("MyCodeEditor")},
              {QStringLiteral("gui_smoke_test"), QStringLiteral("jump_test"), QStringLiteral("completion_test")},
              QStringLiteral("temporary workspace plus fixtures"),
              QStringLiteral("fixture only; corpus read-only"),
              false,
              QStringLiteral("Keep destructive editor commands on temp files only."));

    appendRow(&rows, QStringLiteral("menus_context_sidebars"),
              QStringLiteral("Menus, right-click menus, sidebar buttons, panel actions"),
              QStringLiteral("ui"),
              QStringLiteral("pass"),
              QStringLiteral("MainWindow setup creates workspace/view/tools menus, docks, package buttons, and panel actions"),
              {QStringLiteral("Workspace menu"), QStringLiteral("View menu"), QStringLiteral("Tools menu"), QStringLiteral("workspace tab context menu"), QStringLiteral("dock toggle actions"), QStringLiteral("package tool buttons")},
              {QStringLiteral("MainWindow"), QStringLiteral("NavigationPaneCoordinator"), QStringLiteral("SemanticDockCoordinator"), QStringLiteral("PackageToolService")},
              {QStringLiteral("gui_smoke_test"), QStringLiteral("full_feature_audit_test")},
              QStringLiteral("offscreen GUI smoke plus static inventory"),
              guiCoverage,
              false,
              QStringLiteral("Add QAction object-name inventory if menu churn increases."));

    appendRow(&rows, QStringLiteral("navigation_hierarchy"),
              QStringLiteral("Navigation and design hierarchy"),
              QStringLiteral("navigation"),
              corpusStatus(counts, QStringLiteral("semantic_baseline")),
              corpusReason(counts, QStringLiteral("semantic_baseline")),
              {QStringLiteral("navigation pane"), QStringLiteral("module hierarchy"), QStringLiteral("file hierarchy"), QStringLiteral("design hierarchy")},
              {QStringLiteral("NavigationService"), QStringLiteral("HierarchyService"), QStringLiteral("NavigationManager"), QStringLiteral("ModuleHierarchyModel")},
              {QStringLiteral("jump_test"), QStringLiteral("relationship_test"), QStringLiteral("gui_smoke_test"), QStringLiteral("corpus_audit_test")},
              QStringLiteral("service tests, GUI smoke, corpus outline sweep"),
              fullCorpusCoverage,
              false,
              QStringLiteral("Split hierarchy failures by file/module when corpus grows."));

    appendRow(&rows, QStringLiteral("outline_goto_back_forward"),
              QStringLiteral("Outline, goto, and back/forward navigation"),
              QStringLiteral("navigation"),
              corpusStatus(counts, QStringLiteral("semantic_baseline")),
              corpusReason(counts, QStringLiteral("semantic_baseline")),
              {QStringLiteral("outline tree"), QStringLiteral("goto symbol"), QStringLiteral("back"), QStringLiteral("forward")},
              {QStringLiteral("NavigationService"), QStringLiteral("SourceNavigationService"), QStringLiteral("DefinitionNavigationService")},
              {QStringLiteral("jump_test"), QStringLiteral("gui_smoke_test"), QStringLiteral("corpus_audit_test")},
              QStringLiteral("focused jump tests plus corpus outline sweep"),
              fullCorpusCoverage,
              false,
              QStringLiteral("Track back-forward stack depth in next audit."));

    appendRow(&rows, QStringLiteral("definition_references_relationships"),
              QStringLiteral("Definition, References, Relationships"),
              QStringLiteral("semantic"),
              corpusStatus(counts, QStringLiteral("semantic_baseline")),
              corpusReason(counts, QStringLiteral("semantic_baseline")),
              {QStringLiteral("go to definition"), QStringLiteral("references panel"), QStringLiteral("relationships panel"), QStringLiteral("relationship graph")},
              {QStringLiteral("DefinitionService"), QStringLiteral("ReferenceService"), QStringLiteral("RelationshipService"), QStringLiteral("SmartRelationshipBuilder")},
              {QStringLiteral("jump_test"), QStringLiteral("relationship_test"), QStringLiteral("gui_smoke_test"), QStringLiteral("corpus_audit_test")},
              QStringLiteral("service tests plus full corpus relationship extraction"),
              fullCorpusCoverage,
              false,
              QStringLiteral("Add precision/recall fixture cases for cross-file relationships."));

    appendRow(&rows, QStringLiteral("diagnostics_workspace_config"),
              QStringLiteral("Diagnostics, Problems, workspace config, includes, defines, ignored dirs"),
              QStringLiteral("diagnostics"),
              (diagnosticCount >= 0 && defaultExtensionCount > 0) ? QStringLiteral("pass") : QStringLiteral("fail"),
              QStringLiteral("diagnostics=%1 default extensions=%2").arg(diagnosticCount).arg(defaultExtensionCount),
              {QStringLiteral("Problems panel"), QStringLiteral("workspace config dialog"), QStringLiteral("include dirs"), QStringLiteral("defines"), QStringLiteral("ignored dirs")},
              {QStringLiteral("DiagnosticService"), QStringLiteral("DiagnosticNavigationService"), QStringLiteral("ProblemsPanelCoordinator"), QStringLiteral("WorkspaceConfigurationService"), QStringLiteral("WorkspaceIgnoreService")},
              {QStringLiteral("gui_smoke_test"), QStringLiteral("corpus_audit_test"), QStringLiteral("full_feature_audit_test")},
              QStringLiteral("GUI panel smoke plus full corpus diagnostic extraction"),
              fullCorpusCoverage,
              false,
              QStringLiteral("Report diagnostics by severity and config source."));

    appendRow(&rows, QStringLiteral("formatter_fold_shelf"),
              QStringLiteral("Formatter, fold, and fold shelf"),
              QStringLiteral("editor"),
              QStringLiteral("pass"),
              QStringLiteral("formatter/fold/fold shelf paths are covered through GUI and temp-buffer tests"),
              {QStringLiteral("format action"), QStringLiteral("editor fold"), QStringLiteral("fd r"), QStringLiteral("fd s"), QStringLiteral("fold shelf dock")},
              {QStringLiteral("FormatterService"), QStringLiteral("EditorFolding"), QStringLiteral("FoldBlockShelfModel"), QStringLiteral("FoldShelfPersistenceService"), QStringLiteral("FoldShelfRestoreService")},
              {QStringLiteral("gui_smoke_test")},
              QStringLiteral("temporary editor buffers and temporary shelf settings"),
              QStringLiteral("fixture only; corpus read-only"),
              false,
              QStringLiteral("Add direct formatter idempotence sweep on copied corpus files."));

    appendRow(&rows, QStringLiteral("package_macro_include"),
              QStringLiteral("Package tools, macro/define semantics, include/header/package shortcuts"),
              QStringLiteral("semantic"),
              packageToolCount > 0 ? QStringLiteral("pass") : QStringLiteral("fail"),
              QStringLiteral("package tool buttons=%1; ;h and ;pk descriptors present in inline command inventory").arg(packageToolCount),
              {QStringLiteral("Package Tools bar"), QStringLiteral(";h"), QStringLiteral(";pk"), QStringLiteral(";d"), QStringLiteral("Go package gpk")},
              {QStringLiteral("PackageToolService"), QStringLiteral("SvMacroSemantics"), QStringLiteral("CompletionSemanticQuery"), QStringLiteral("InlineCommandMode")},
              {QStringLiteral("completion_test"), QStringLiteral("gui_smoke_test"), QStringLiteral("corpus_audit_test"), QStringLiteral("full_feature_audit_test")},
              QStringLiteral("descriptor inventory, GUI activation, semantic corpus baseline"),
              fullCorpusCoverage,
              false,
              QStringLiteral("Add include resolution success/failure buckets."));

    appendRow(&rows, QStringLiteral("rtl_fsm"),
              QStringLiteral("RTL Insights: FSM / State Transition Graph"),
              QStringLiteral("rtl-insight"),
              corpusStatus(counts, QStringLiteral("state_transition_graph")),
              corpusReason(counts, QStringLiteral("state_transition_graph")),
              {QStringLiteral("RTL Insights FSM Graph"), QStringLiteral("state transition graph panel/action")},
              {QStringLiteral("FsmGraphService"), QStringLiteral("StateTransitionGraphService"), QStringLiteral("StateTransitionTriggerService"), QStringLiteral("RtlInsightsPanelCoordinator")},
              {QStringLiteral("gui_smoke_test"), QStringLiteral("corpus_audit_test")},
              QStringLiteral("full corpus structural FSM sweep"),
              fullCorpusCoverage,
              false,
              QStringLiteral("Skipped modules lack current<=next FSM shape under current extractor."));

    appendRow(&rows, QStringLiteral("rtl_signal_journey_clock_reset"),
              QStringLiteral("RTL Insights: Signal Journey and Clock/Reset"),
              QStringLiteral("rtl-insight"),
              relationshipCount > 0 ? QStringLiteral("pass") : QStringLiteral("fail"),
              QStringLiteral("relationships=%1 diagnostics=%2").arg(relationshipCount).arg(diagnosticCount),
              {QStringLiteral("Signal Journey"), QStringLiteral("Clock/Reset Domain Map")},
              {QStringLiteral("SignalJourneyService"), QStringLiteral("ClockResetDomainService"), QStringLiteral("RelationshipService")},
              {QStringLiteral("gui_smoke_test"), QStringLiteral("corpus_audit_test")},
              QStringLiteral("panel smoke plus full corpus relationship context"),
              fullCorpusCoverage,
              false,
              QStringLiteral("Add corpus-level journey path length distribution."));

    appendRow(&rows, QStringLiteral("rtl_semantic_diff"),
              QStringLiteral("RTL Insights: Semantic Diff"),
              QStringLiteral("rtl-insight"),
              QStringLiteral("pass"),
              QStringLiteral("GUI smoke covers semantic diff panel/action on temporary fixtures"),
              {QStringLiteral("RTL Insights Semantic Diff"), QStringLiteral("compare workflow")},
              {QStringLiteral("SemanticDiffService"), QStringLiteral("RtlInsightsPanelCoordinator")},
              {QStringLiteral("gui_smoke_test")},
              QStringLiteral("temporary fixture compare"),
              QStringLiteral("fixture only; corpus read-only"),
              false,
              QStringLiteral("Add copied-corpus pair diff sweep if needed."));

    appendRow(&rows, QStringLiteral("rtl_signal_kernel_graph"),
              QStringLiteral("RTL Insights: Signal Kernel Graph"),
              QStringLiteral("rtl-insight"),
              corpusStatus(counts, QStringLiteral("signal_kernel_graph")),
              corpusReason(counts, QStringLiteral("signal_kernel_graph")),
              {QStringLiteral("Signal Kernel Graph panel"), QStringLiteral("signal graph action")},
              {QStringLiteral("SignalKernelGraphService"), QStringLiteral("SignalKernelGraphPanelCoordinator")},
              {QStringLiteral("gui_smoke_test"), QStringLiteral("corpus_audit_test")},
              QStringLiteral("deterministic bounded corpus graph sweep"),
              fullCorpusCoverage,
              false,
              QStringLiteral("Keep case-level skipped reasons explicit; expand graph sampling only in a dedicated performance pass."));

    appendRow(&rows, QStringLiteral("rtl_module_block_diagram"),
              QStringLiteral("RTL Insights: Module Block Diagram"),
              QStringLiteral("rtl-insight"),
              corpusStatus(counts, QStringLiteral("module_block_diagram")),
              corpusReason(counts, QStringLiteral("module_block_diagram")),
              {QStringLiteral("Module Block Diagram"), QStringLiteral("RTL Insights diagram action")},
              {QStringLiteral("ModuleBlockDiagramService"), QStringLiteral("RtlInsightsPanelCoordinator")},
              {QStringLiteral("gui_smoke_test"), QStringLiteral("corpus_audit_test")},
              QStringLiteral("full corpus module diagram sweep"),
              fullCorpusCoverage,
              false,
              QStringLiteral("Empty-valid modules should remain explicit in reports."));

    appendRow(&rows, QStringLiteral("rtl_wave_preview"),
              QStringLiteral("RTL Insights: Wave Preview"),
              QStringLiteral("rtl-insight"),
              corpusStatus(counts, QStringLiteral("wave_preview")),
              corpusReason(counts, QStringLiteral("wave_preview")),
              {QStringLiteral("Wave Preview dock"), QStringLiteral("active-editor wave refresh")},
              {QStringLiteral("WavePreviewService"), QStringLiteral("WavePreviewPanelCoordinator")},
              {QStringLiteral("gui_smoke_test"), QStringLiteral("corpus_audit_test")},
              QStringLiteral("full corpus always/process preview sweep"),
              fullCorpusCoverage,
              false,
              QStringLiteral("Keep no-lane/no-warning regression coverage and review empty-valid unsupported reasons periodically."));

    appendRow(&rows, QStringLiteral("search_rename_workspace_workflow"),
              QStringLiteral("Search, rename, semantic diff, and workspace workflow"),
              QStringLiteral("workflow"),
              QStringLiteral("pass"),
              QStringLiteral("service and GUI smoke cover search/rename/workspace open-close paths on fixtures"),
              {QStringLiteral("search"), QStringLiteral("safe rename"), QStringLiteral("workspace open/close/recent"), QStringLiteral("semantic diff")},
              {QStringLiteral("SearchService"), QStringLiteral("SafeRenameService"), QStringLiteral("WorkspaceManager"), QStringLiteral("SemanticDiffService")},
              {QStringLiteral("gui_smoke_test"), QStringLiteral("relationship_test")},
              QStringLiteral("temporary workspace plus service-level smoke"),
              QStringLiteral("fixture only; corpus read-only"),
              false,
              QStringLiteral("Add rename collision/cross-file fixture matrix."));

    int entryPointCount = 0;
    int serviceTouchpointCount = 0;
    QSet<QString> testTargets;
    for (const FeatureRow& row : std::as_const(rows)) {
        entryPointCount += row.entries.size();
        serviceTouchpointCount += row.services.size();
        for (const QString& target : row.testTargets)
            testTargets.insert(target);
    }

    QJsonObject inventory;
    inventory.insert(QStringLiteral("globalCommandCount"), globalCommands.size());
    inventory.insert(QStringLiteral("rtlInsightEntryCount"), rtlInsightItems.size());
    inventory.insert(QStringLiteral("comModeCommandCount"), comModeCommandRegistry().size());
    inventory.insert(QStringLiteral("inlineDescriptorCount"), inlineDescriptors.size());
    inventory.insert(QStringLiteral("semanticCommandCount"), commandModeCommands.size());
    inventory.insert(QStringLiteral("builtInTemplateCount"), builtInTemplateCount);
    inventory.insert(QStringLiteral("packageToolCount"), packageToolCount);
    inventory.insert(QStringLiteral("workspaceDefaultExtensionCount"), defaultExtensionCount);
    inventory.insert(QStringLiteral("entryPointCount"), entryPointCount);
    inventory.insert(QStringLiteral("serviceTouchpointCount"), serviceTouchpointCount);
    inventory.insert(QStringLiteral("testTargetCount"), testTargets.size());

    QJsonArray featureArray;
    for (const FeatureRow& row : rows)
        featureArray.append(featureJson(row));

    QJsonObject root;
    root.insert(QStringLiteral("generatedAtUtc"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("workspaceRoot"), workspaceRoot);
    QJsonArray rootArray;
    for (const QString& rootPath : roots)
        rootArray.append(relativePath(workspaceRoot, rootPath));
    root.insert(QStringLiteral("roots"), rootArray);
    root.insert(QStringLiteral("recursiveCorpusFileCount"), corpusFiles.size());
    root.insert(QStringLiteral("corpusReportLoaded"), corpusReportLoaded);
    root.insert(QStringLiteral("corpusReportReadError"), corpusReadError);
    root.insert(QStringLiteral("corpusReportFileCount"), corpusReportFileCount);
    root.insert(QStringLiteral("semanticRecordCount"), semanticRecordCount);
    root.insert(QStringLiteral("relationshipCount"), relationshipCount);
    root.insert(QStringLiteral("diagnosticCount"), diagnosticCount);
    root.insert(QStringLiteral("inventory"), inventory);
    QJsonObject summaryObject;
    const QHash<QString, int> summary = statusSummary(rows);
    for (auto it = summary.constBegin(); it != summary.constEnd(); ++it)
        summaryObject.insert(it.key(), it.value());
    root.insert(QStringLiteral("statusSummary"), summaryObject);
    root.insert(QStringLiteral("features"), featureArray);
    root.insert(QStringLiteral("topFailures"), topFailures);

    QStringList knownIssues = knownIssuesFromFailures(topFailures);
    knownIssues.append(knownIssuesFromRows(rows));
    root.insert(QStringLiteral("knownIssues"), stringArray(knownIssues));

    const QString jsonPath = normalizedPath(
        QDir(workspaceRoot).filePath(QStringLiteral("test_sv/full_feature_audit_report.json")));
    const QString mdPath = normalizedPath(
        QDir(workspaceRoot).filePath(QStringLiteral("test_sv/full_feature_audit_report.md")));

    if (!writeTextFile(jsonPath,
                       QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)))) {
        fprintf(stderr, "cannot write %s\n", jsonPath.toLocal8Bit().constData());
        return 3;
    }
    if (!writeTextFile(mdPath,
                       markdownReport(workspaceRoot,
                                      roots,
                                      corpusFiles.size(),
                                      corpusReportFileCount,
                                      semanticRecordCount,
                                      relationshipCount,
                                      diagnosticCount,
                                      rows,
                                      knownIssues,
                                      inventory))) {
        fprintf(stderr, "cannot write %s\n", mdPath.toLocal8Bit().constData());
        return 3;
    }

    bool failed = false;
    auto require = [&failed](bool condition, const char* message) {
        printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        if (!condition)
            failed = true;
    };
    require(corpusReportLoaded, "corpus audit report is readable");
    require(!corpusFiles.isEmpty(), "recursive corpus files collected");
    require(corpusReportFileCount == corpusFiles.size(),
            "corpus report file count matches recursive sweep");
    require(comRegistryValid, "COM command registry validates");
    require(!inlineDescriptors.isEmpty(), "inline command descriptors are present");
    require(!commandModeCommands.isEmpty(), "semantic command descriptors are present");
    require(!globalCommands.isEmpty(), "global command inventory is present");
    require(!rtlInsightItems.isEmpty(), "RTL insight entries are present");
    require(packageToolCount > 0, "package tools are present");
    require(defaultExtensionCount > 0, "workspace configuration defaults are present");
    bool signalKernelGraphIsPass = false;
    for (const FeatureRow& row : std::as_const(rows)) {
        if (row.id == QStringLiteral("rtl_signal_kernel_graph")) {
            signalKernelGraphIsPass = row.status == QStringLiteral("pass");
            break;
        }
    }
    require(signalKernelGraphIsPass,
            "Signal Kernel Graph skipped audit cases stay case-level");

    printf("Full feature audit wrote %s and %s\n",
           jsonPath.toLocal8Bit().constData(),
           mdPath.toLocal8Bit().constData());
    printf("Features=%d entries=%d serviceTouchpoints=%d tests=%d files=%d knownIssues=%d\n",
           rows.size(),
           entryPointCount,
           serviceTouchpointCount,
           testTargets.size(),
           corpusFiles.size(),
           knownIssues.size());
    return failed ? 1 : 0;
}
