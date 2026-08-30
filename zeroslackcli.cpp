#include "zeroslackcli.h"

#include "pinloomcodelinkstore.h"
#include "projectmodel.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "semanticstableidentity.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "symbolanalyzer.h"
#include "symboltaxonomy.h"
#include "workspaceconfigurationservice.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

#include <algorithm>

namespace {

constexpr auto kSchema = "zeroslack.cli/v1";
constexpr auto kCacheSchema = "ZeroSlack.CliSemanticIndex";
constexpr int kCacheVersion = 1;

struct SourceFileState {
    QString absolutePath;
    QString relativePath;
    QByteArray content;
    QString sha256;
    qint64 size = 0;
    qint64 modifiedMs = 0;
};

struct WorkspaceState {
    QString root;
    WorkspaceConfiguration configuration;
    WorkspaceConfigurationSource configurationSource =
        WorkspaceConfigurationSource::Default;
    QList<SourceFileState> files;
    QString revision;
    QString failureReason;

    bool isValid() const
    {
        return failureReason.isEmpty() && !root.isEmpty();
    }
};

struct PreparedIndex {
    WorkspaceState workspace;
    QString cachePath;
    QJsonObject index;
    bool cacheExists = false;
    bool cacheCurrent = false;
    bool rebuilt = false;
    QString failureReason;

    bool isValid() const
    {
        return failureReason.isEmpty() && workspace.isValid()
            && !index.isEmpty();
    }
};

QString normalizedPath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return {};
    const QFileInfo info(path);
    QString result = info.exists()
        ? info.canonicalFilePath()
        : info.absoluteFilePath();
    if (result.isEmpty())
        result = info.absoluteFilePath();
    return QDir::cleanPath(QDir::fromNativeSeparators(result));
}

QString pathKey(const QString& path)
{
    QString key = normalizedPath(path);
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

bool isInsideWorkspace(const QString& root, const QString& path)
{
    const QString rootKey = pathKey(root);
    const QString pathValue = pathKey(path);
    return pathValue == rootKey
        || pathValue.startsWith(rootKey + QLatin1Char('/'));
}

QString relativePath(const QString& root, const QString& path)
{
    if (!isInsideWorkspace(root, path))
        return normalizedPath(path);
    return QDir::cleanPath(QDir::fromNativeSeparators(
        QDir(normalizedPath(root)).relativeFilePath(normalizedPath(path))));
}

QString resolvedWorkspaceFile(const QString& root, const QString& path)
{
    if (path.trimmed().isEmpty())
        return {};
    const QFileInfo info(path);
    const QString absolute = normalizedPath(
        info.isAbsolute() ? path : QDir(root).absoluteFilePath(path));
    return isInsideWorkspace(root, absolute) ? absolute : QString();
}

QByteArray readFileBytes(const QString& path, QString* failureReason = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (failureReason) {
            *failureReason = QStringLiteral("Cannot read %1: %2")
                .arg(path, file.errorString());
        }
        return {};
    }
    return file.readAll();
}

QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        bytes, QCryptographicHash::Sha256).toHex());
}

bool pathIsIgnored(const QString& path, const QStringList& ignored)
{
    const QString candidate = pathKey(path);
    for (const QString& ignoredPath : ignored) {
        const QString key = pathKey(ignoredPath);
        if (candidate == key
            || candidate.startsWith(key + QLatin1Char('/'))) {
            return true;
        }
    }
    return false;
}

bool extensionMatches(const QFileInfo& info,
                      const QStringList& extensions)
{
    const QString suffix = QLatin1Char('.') + info.suffix().toLower();
    for (QString extension : extensions) {
        extension = extension.trimmed().toLower();
        if (!extension.startsWith(QLatin1Char('.')))
            extension.prepend(QLatin1Char('.'));
        if (suffix == extension)
            return true;
    }
    return false;
}

QStringList discoverSourceFiles(const WorkspaceConfiguration& configuration)
{
    QStringList result;
    QStringList pending{configuration.workspaceRoot};
    const QString gitDirectory = QDir(configuration.workspaceRoot)
        .absoluteFilePath(QStringLiteral(".git"));
    QStringList ignored = configuration.ignoredDirs;
    ignored.append(gitDirectory);

    while (!pending.isEmpty()) {
        const QString directoryPath = pending.takeLast();
        if (pathIsIgnored(directoryPath, ignored))
            continue;
        const QDir directory(directoryPath);
        const QFileInfoList entries = directory.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot
                | QDir::Readable | QDir::Hidden | QDir::System,
            QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& entry : entries) {
            if (entry.isSymLink())
                continue;
            if (entry.isDir()) {
                if (!pathIsIgnored(entry.absoluteFilePath(), ignored))
                    pending.append(entry.absoluteFilePath());
                continue;
            }
            if (entry.isFile()
                && extensionMatches(entry, configuration.fileExtensions)) {
                result.append(normalizedPath(entry.absoluteFilePath()));
            }
        }
    }

    std::sort(result.begin(), result.end(), [](const QString& left,
                                                const QString& right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    result.removeDuplicates();
    return result;
}

QJsonObject configurationJson(const WorkspaceConfiguration& configuration)
{
    QJsonArray includeDirs;
    for (const QString& path : configuration.includeDirs)
        includeDirs.append(relativePath(configuration.workspaceRoot, path));
    QJsonArray ignoredDirs;
    for (const QString& path : configuration.ignoredDirs)
        ignoredDirs.append(relativePath(configuration.workspaceRoot, path));
    QJsonArray extensions;
    for (const QString& extension : configuration.fileExtensions)
        extensions.append(extension.toLower());
    QJsonObject defines;
    QStringList defineNames = configuration.defines.keys();
    defineNames.sort(Qt::CaseSensitive);
    for (const QString& name : defineNames)
        defines.insert(name, configuration.defines.value(name));
    return {
        {QStringLiteral("includeDirs"), includeDirs},
        {QStringLiteral("ignoredDirs"), ignoredDirs},
        {QStringLiteral("fileExtensions"), extensions},
        {QStringLiteral("defines"), defines},
        {QStringLiteral("topModule"), configuration.topModule},
    };
}

WorkspaceState inspectWorkspace(const QString& requestedRoot)
{
    WorkspaceState state;
    state.root = normalizedPath(requestedRoot);
    const QFileInfo rootInfo(state.root);
    if (state.root.isEmpty() || !rootInfo.exists() || !rootInfo.isDir()) {
        state.failureReason = QStringLiteral(
            "Workspace root is not an existing directory.");
        return state;
    }

    WorkspaceConfigurationService configurationService;
    const WorkspaceConfigurationLoadResult loaded =
        configurationService.loadWithResult(state.root);
    state.configuration = loaded.configuration;
    state.configurationSource = loaded.source;
    if (!state.configuration.isValid()) {
        state.failureReason = loaded.message.isEmpty()
            ? QStringLiteral("Workspace configuration is invalid.")
            : loaded.message;
        return state;
    }

    const QStringList files = discoverSourceFiles(state.configuration);
    state.files.reserve(files.size());
    for (const QString& path : files) {
        QString failure;
        const QByteArray content = readFileBytes(path, &failure);
        if (!failure.isEmpty()) {
            state.failureReason = failure;
            return state;
        }
        const QFileInfo info(path);
        SourceFileState file;
        file.absolutePath = path;
        file.relativePath = relativePath(state.root, path);
        file.content = content;
        file.sha256 = sha256(content);
        file.size = info.size();
        file.modifiedMs = info.lastModified().toMSecsSinceEpoch();
        state.files.append(std::move(file));
    }

    QCryptographicHash revisionHash(QCryptographicHash::Sha256);
    revisionHash.addData(QByteArrayLiteral("ZeroSlack.CliWorkspace/v1\n"));
    revisionHash.addData(QJsonDocument(configurationJson(state.configuration))
                             .toJson(QJsonDocument::Compact));
    revisionHash.addData(QByteArrayLiteral("\n"));
    for (const SourceFileState& file : std::as_const(state.files)) {
        revisionHash.addData(file.relativePath.toUtf8());
        revisionHash.addData(QByteArrayLiteral("\0"));
        revisionHash.addData(file.sha256.toLatin1());
        revisionHash.addData(QByteArrayLiteral("\n"));
    }
    state.revision = QStringLiteral("sha256:")
        + QString::fromLatin1(revisionHash.result().toHex());
    return state;
}

QString configurationSourceName(WorkspaceConfigurationSource source)
{
    switch (source) {
    case WorkspaceConfigurationSource::ProjectFile:
        return QStringLiteral("project");
    case WorkspaceConfigurationSource::LegacySession:
        return QStringLiteral("legacy-zs-read-only");
    case WorkspaceConfigurationSource::Default:
    default:
        return QStringLiteral("default");
    }
}

QJsonArray sourceManifestJson(const WorkspaceState& state)
{
    QJsonArray files;
    for (const SourceFileState& file : state.files) {
        files.append(QJsonObject{
            {QStringLiteral("path"), file.relativePath},
            {QStringLiteral("sha256"), file.sha256},
            {QStringLiteral("size"), static_cast<double>(file.size)},
            {QStringLiteral("modifiedMs"),
             static_cast<double>(file.modifiedMs)},
        });
    }
    return files;
}

QString relationTypeName(SymbolRelationshipEngine::RelationType type)
{
    using Type = SymbolRelationshipEngine::RelationType;
    switch (type) {
    case Type::CONTAINS: return QStringLiteral("contains");
    case Type::REFERENCES: return QStringLiteral("references");
    case Type::INSTANTIATES: return QStringLiteral("instantiates");
    case Type::CALLS: return QStringLiteral("calls");
    case Type::INHERITS: return QStringLiteral("inherits");
    case Type::IMPLEMENTS: return QStringLiteral("implements");
    case Type::ASSIGNS_TO: return QStringLiteral("assigns-to");
    case Type::READS_FROM: return QStringLiteral("reads-from");
    case Type::CLOCKS: return QStringLiteral("clocks");
    case Type::RESETS: return QStringLiteral("resets");
    case Type::GENERATES: return QStringLiteral("generates");
    case Type::CONSTRAINS: return QStringLiteral("constrains");
    }
    return QStringLiteral("unknown");
}

QJsonObject sourceRangeJson(const SemanticSourceRange& range,
                            const QString& workspaceRoot)
{
    if (!range.isValid())
        return {};
    return {
        {QStringLiteral("file"), relativePath(workspaceRoot, range.fileName)},
        {QStringLiteral("line"), range.line},
        {QStringLiteral("column"), range.column},
        {QStringLiteral("endLine"), range.endLine},
        {QStringLiteral("endColumn"), range.endColumn},
    };
}

QJsonObject symbolJson(const SemanticSymbolRecord& record,
                       const QString& workspaceRoot)
{
    const SemanticStableIdentity identity =
        semanticStableIdentity(record, workspaceRoot);
    const QString file = relativePath(workspaceRoot,
                                      record.location.fileName);
    return {
        {QStringLiteral("id"), identity.stableId},
        {QStringLiteral("exactId"), identity.exactId},
        {QStringLiteral("idStability"), identity.stability},
        {QStringLiteral("uri"),
         semanticStableSymbolUri(record, workspaceRoot)},
        {QStringLiteral("name"), record.name},
        {QStringLiteral("kind"),
         SymbolTaxonomy::symbolTypeLabel(
             semanticMetadataForSymbolRecord(record))},
        {QStringLiteral("kindValue"),
         static_cast<int>(record.declarationKind)},
        {QStringLiteral("owner"), record.owner.name},
        {QStringLiteral("ownerKind"),
         static_cast<int>(record.owner.kind)},
        {QStringLiteral("file"), file},
        {QStringLiteral("line"), record.location.startLine},
        {QStringLiteral("column"), record.location.startColumn},
        {QStringLiteral("endLine"), record.location.endLine},
        {QStringLiteral("endColumn"), record.location.endColumn},
        {QStringLiteral("type"), record.type.rawTypeText},
        {QStringLiteral("resolvedType"), record.type.resolvedTypeName},
        {QStringLiteral("declaration"),
         record.presentation.declarationText.trimmed()},
        {QStringLiteral("exactKey"), record.stableKey.toString()},
    };
}

QString relationshipLookupKey(const QString& fileName)
{
    return pathKey(fileName);
}

QJsonObject buildSemanticIndex(const WorkspaceState& state,
                               QString* failureReason)
{
    ProjectSnapshot project;
    project.revision = 1;
    project.workspaceRoot = state.root;
    project.includeDirs = state.configuration.includeDirs;
    project.defines = state.configuration.defines;
    project.fileExtensions = state.configuration.fileExtensions;
    project.topModule = state.configuration.topModule;
    project.ignoredPaths = state.configuration.ignoredDirs;
    for (const SourceFileState& file : state.files) {
        project.allFiles.append(file.absolutePath);
        project.systemVerilogFiles.append(file.absolutePath);
        project.sourceRoles.insert(
            file.absolutePath,
            SymbolTaxonomy::sourceRoleForFileName(file.absolutePath));
    }

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    semanticIndex->clearSemanticState();
    SymbolAnalyzer analyzer;
    std::shared_ptr<const SemanticIndexSnapshot> snapshot;
    if (project.systemVerilogFiles.isEmpty()) {
        snapshot = std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords({}));
        semanticIndex->setSnapshot(snapshot);
    } else {
        analyzer.analyzeProject(project);
        snapshot = semanticIndex->snapshot();
    }
    if (!snapshot) {
        if (failureReason)
            *failureReason = QStringLiteral("Semantic analysis produced no snapshot.");
        return {};
    }

    QList<SemanticSymbolRecord> records = snapshot->getSymbolRecords();
    std::sort(records.begin(), records.end(), [](const auto& left,
                                                  const auto& right) {
        const int fileOrder = left.location.fileName.compare(
            right.location.fileName, Qt::CaseInsensitive);
        if (fileOrder != 0)
            return fileOrder < 0;
        if (left.location.startLine != right.location.startLine)
            return left.location.startLine < right.location.startLine;
        if (left.location.startColumn != right.location.startColumn)
            return left.location.startColumn < right.location.startColumn;
        return left.name < right.name;
    });

    QJsonArray symbols;
    QHash<int, QString> stableIdByHandle;
    for (const SemanticSymbolRecord& record : std::as_const(records)) {
        const SemanticStableIdentity identity =
            semanticStableIdentity(record, state.root);
        if (record.localHandle >= 0 && identity.isValid())
            stableIdByHandle.insert(record.localHandle, identity.stableId);
        symbols.append(symbolJson(record, state.root));
    }

    QJsonArray relationships;
    QSet<QString> relationshipKeys;
    SlangManager relationshipSlang;
    SmartRelationshipBuilder builder(nullptr, &relationshipSlang);
    const QHash<QString, RelationshipExtractionInfo> precomputed =
        builder.extractWorkspaceRelationshipInfo(
            project.systemVerilogFiles, project.includeDirs, project.defines);
    QHash<QString, RelationshipExtractionInfo> precomputedByKey;
    for (auto it = precomputed.cbegin(); it != precomputed.cend(); ++it)
        precomputedByKey.insert(relationshipLookupKey(it.key()), it.value());

    for (const SourceFileState& file : state.files) {
        const QList<SemanticSymbolRecord> fileRecords =
            snapshot->getSymbolRecords(file.absolutePath);
        const auto infoIt = precomputedByKey.constFind(
            relationshipLookupKey(file.absolutePath));
        const RelationshipExtractionInfo* info =
            infoIt == precomputedByKey.cend() ? nullptr : &infoIt.value();
        const QVector<RelationshipToAdd> computed =
            builder.computeRelationships(
                file.absolutePath,
                QString::fromUtf8(file.content),
                fileRecords,
                snapshot.get(),
                project.includeDirs,
                project.defines,
                info);
        for (const RelationshipToAdd& relationship : computed) {
            const QString fromId = stableIdByHandle.value(relationship.fromId);
            const QString toId = stableIdByHandle.value(relationship.toId);
            if (fromId.isEmpty() || toId.isEmpty())
                continue;
            const QString type = relationTypeName(relationship.type);
            const QString key = fromId + QLatin1Char('|') + toId
                + QLatin1Char('|') + type + QLatin1Char('|')
                + relationship.evidenceRange.fileName + QLatin1Char('|')
                + QString::number(relationship.evidenceRange.position);
            if (relationshipKeys.contains(key))
                continue;
            relationshipKeys.insert(key);
            relationships.append(QJsonObject{
                {QStringLiteral("from"), fromId},
                {QStringLiteral("to"), toId},
                {QStringLiteral("type"), type},
                {QStringLiteral("confidence"), relationship.confidence},
                {QStringLiteral("evidence"), relationship.context},
                {QStringLiteral("range"),
                 sourceRangeJson(relationship.evidenceRange, state.root)},
                {QStringLiteral("exactValueForward"),
                 relationship.exactValueForward},
            });
        }
    }

    QJsonArray diagnostics;
    for (const SemanticDiagnostic& diagnostic : snapshot->diagnostics()) {
        QString severity = QStringLiteral("info");
        if (diagnostic.severity == SemanticDiagnostic::Warning)
            severity = QStringLiteral("warning");
        else if (diagnostic.severity == SemanticDiagnostic::Error)
            severity = QStringLiteral("error");
        diagnostics.append(QJsonObject{
            {QStringLiteral("file"),
             relativePath(state.root, diagnostic.fileName)},
            {QStringLiteral("line"), diagnostic.line},
            {QStringLiteral("column"), diagnostic.column},
            {QStringLiteral("severity"), severity},
            {QStringLiteral("code"), diagnostic.codeName},
            {QStringLiteral("message"), diagnostic.message},
        });
    }

    analyzer.shutdown();
    return {
        {QStringLiteral("schema"), QString::fromLatin1(kCacheSchema)},
        {QStringLiteral("version"), kCacheVersion},
        {QStringLiteral("workspaceRoot"), state.root},
        {QStringLiteral("workspaceRevision"), state.revision},
        {QStringLiteral("generatedAtUtc"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("configurationSource"),
         configurationSourceName(state.configurationSource)},
        {QStringLiteral("configuration"),
         configurationJson(state.configuration)},
        {QStringLiteral("files"), sourceManifestJson(state)},
        {QStringLiteral("symbols"), symbols},
        {QStringLiteral("relationships"), relationships},
        {QStringLiteral("diagnostics"), diagnostics},
    };
}

QJsonObject readCache(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(
        file.readAll(), &error);
    const QJsonObject object = document.object();
    if (error.error != QJsonParseError::NoError
        || !document.isObject()
        || object.value(QStringLiteral("schema")).toString()
            != QString::fromLatin1(kCacheSchema)
        || object.value(QStringLiteral("version")).toInt()
            != kCacheVersion) {
        return {};
    }
    return object;
}

bool writeCache(const QString& path,
                const QJsonObject& index,
                QString* failureReason)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (failureReason)
            *failureReason = QStringLiteral("Cannot create CLI cache directory.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (failureReason)
            *failureReason = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(index).toJson(QJsonDocument::Compact)) < 0) {
        if (failureReason)
            *failureReason = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (failureReason)
            *failureReason = file.errorString();
        return false;
    }
    return true;
}

QString defaultCacheRoot()
{
    QString root = QStandardPaths::writableLocation(
        QStandardPaths::CacheLocation);
    if (root.isEmpty()) {
        root = QDir(QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation))
                   .filePath(QStringLiteral("cache"));
    }
    return QDir(root).filePath(QStringLiteral("semantic-index"));
}

QString cachePath(const QString& workspaceRoot,
                  const QString& overrideDirectory)
{
    const QString root = overrideDirectory.trimmed().isEmpty()
        ? defaultCacheRoot()
        : normalizedPath(overrideDirectory);
    const QByteArray workspaceDigest = QCryptographicHash::hash(
        pathKey(workspaceRoot).toUtf8(),
        QCryptographicHash::Sha256).toHex().left(24);
    return QDir(root).filePath(
        QString::fromLatin1(workspaceDigest)
        + QStringLiteral("/index-v1.json"));
}

PreparedIndex prepareIndex(const ZeroSlackCliRequest& request,
                           bool statusOnly)
{
    PreparedIndex prepared;
    prepared.workspace = inspectWorkspace(request.workspaceRoot);
    if (!prepared.workspace.isValid()) {
        prepared.failureReason = prepared.workspace.failureReason;
        return prepared;
    }
    prepared.cachePath = cachePath(prepared.workspace.root,
                                   request.cacheDirectory);
    prepared.cacheExists = QFileInfo::exists(prepared.cachePath);
    prepared.index = readCache(prepared.cachePath);
    prepared.cacheCurrent = !prepared.index.isEmpty()
        && pathKey(prepared.index.value(
               QStringLiteral("workspaceRoot")).toString())
            == pathKey(prepared.workspace.root)
        && prepared.index.value(
               QStringLiteral("workspaceRevision")).toString()
            == prepared.workspace.revision;

    if (statusOnly)
        return prepared;

    const bool mustBuild = request.forceRefresh
        || request.command == QStringLiteral("scan")
        || !prepared.cacheCurrent;
    if (!mustBuild)
        return prepared;
    if (!request.allowRefresh) {
        prepared.failureReason = QStringLiteral(
            "Semantic cache is missing or stale and --no-refresh was supplied.");
        return prepared;
    }

    QString failure;
    QJsonObject index = buildSemanticIndex(prepared.workspace, &failure);
    if (index.isEmpty()) {
        prepared.failureReason = failure.isEmpty()
            ? QStringLiteral("Semantic index build failed.") : failure;
        return prepared;
    }
    if (!writeCache(prepared.cachePath, index, &failure)) {
        prepared.failureReason = QStringLiteral("Semantic cache write failed: %1")
            .arg(failure);
        return prepared;
    }
    prepared.index = std::move(index);
    prepared.cacheExists = true;
    prepared.cacheCurrent = true;
    prepared.rebuilt = true;
    return prepared;
}

QString anchorKindName(PinloomCodeAnchorKind kind)
{
    switch (kind) {
    case PinloomCodeAnchorKind::Symbol: return QStringLiteral("symbol");
    case PinloomCodeAnchorKind::AlwaysBlock:
        return QStringLiteral("always-block");
    case PinloomCodeAnchorKind::ContinuousAssign:
        return QStringLiteral("continuous-assign");
    case PinloomCodeAnchorKind::LegacySelection:
    default:
        return QStringLiteral("legacy-selection");
    }
}

QString resolutionName(PinloomCodeLinkResolution resolution)
{
    switch (resolution) {
    case PinloomCodeLinkResolution::Exact: return QStringLiteral("exact");
    case PinloomCodeLinkResolution::Moved: return QStringLiteral("moved");
    case PinloomCodeLinkResolution::Ambiguous:
        return QStringLiteral("ambiguous");
    case PinloomCodeLinkResolution::Missing:
    default:
        return QStringLiteral("missing");
    }
}

QJsonArray targetLinksJson(const QList<PinloomCodeLinkRecord>& links)
{
    QJsonArray result;
    for (const PinloomCodeLinkRecord& link : links) {
        QJsonObject identity;
        for (auto it = link.identity.cbegin(); it != link.identity.cend(); ++it) {
            const QString key = it.key().toCaseFolded();
            if (key.contains(QStringLiteral("content"))
                || key.contains(QStringLiteral("selectedtext"))
                || key.contains(QStringLiteral("body"))
                || key.contains(QStringLiteral("html"))
                || key.contains(QStringLiteral("base64"))
                || key.contains(QStringLiteral("preview"))
                || key.contains(QStringLiteral("locatorjson"))) {
                continue;
            }
            QJsonValue value = QJsonValue::fromVariant(it.value());
            if (value.isString() && value.toString().size() > 512) {
                value = value.toString().left(512)
                    + QStringLiteral("…");
            }
            identity.insert(it.key(), value);
        }
        result.append(QJsonObject{
            {QStringLiteral("id"), link.id},
            {QStringLiteral("title"), link.title},
            {QStringLiteral("uri"), link.uri.toString(QUrl::FullyEncoded)},
            {QStringLiteral("identity"), identity},
        });
    }
    return result;
}

QJsonObject resolvedAnchorJson(const ResolvedPinloomCodeLink& resolved,
                               const QString& workspaceRoot)
{
    const PinloomSourceSelection& source = resolved.anchor.source;
    QString file = source.absoluteFilePath;
    if (file.isEmpty() && !source.relativeFilePath.isEmpty())
        file = QDir(workspaceRoot).absoluteFilePath(source.relativeFilePath);
    return {
        {QStringLiteral("anchorId"), resolved.anchor.id},
        {QStringLiteral("kind"), anchorKindName(source.anchorKind)},
        {QStringLiteral("resolution"), resolutionName(resolved.resolution)},
        {QStringLiteral("file"), relativePath(workspaceRoot, file)},
        {QStringLiteral("module"), source.moduleName},
        {QStringLiteral("symbol"), source.symbolName},
        {QStringLiteral("syntaxKind"), source.syntaxKind},
        {QStringLiteral("selectedTextHash"), source.selectedTextHash},
        {QStringLiteral("structuralFingerprint"),
         source.structuralFingerprint},
        {QStringLiteral("firstLine"), resolved.firstLine},
        {QStringLiteral("lastLine"), resolved.lastLine},
        {QStringLiteral("linkCount"), resolved.anchor.links.size()},
        {QStringLiteral("links"), targetLinksJson(resolved.anchor.links)},
    };
}

QJsonArray anchorsJson(const WorkspaceState& state,
                       const QString& requestedFile = {})
{
    PinloomCodeLinkStore store;
    store.setWorkspaceRoot(state.root);
    const QList<PinloomCodeLinkAnchorRecord> stored = store.anchors();
    QHash<QString, QString> fileByKey;
    for (const PinloomCodeLinkAnchorRecord& anchor : stored) {
        QString file = anchor.source.absoluteFilePath;
        if (file.isEmpty() && !anchor.source.relativeFilePath.isEmpty()) {
            file = QDir(state.root).absoluteFilePath(
                anchor.source.relativeFilePath);
        }
        if (!file.isEmpty())
            fileByKey.insert(pathKey(file), normalizedPath(file));
    }

    const QString requestedKey = requestedFile.isEmpty()
        ? QString() : pathKey(requestedFile);
    QJsonArray result;
    QSet<QString> emitted;
    for (auto it = fileByKey.cbegin(); it != fileByKey.cend(); ++it) {
        if (!requestedKey.isEmpty() && it.key() != requestedKey)
            continue;
        QString failure;
        const QByteArray bytes = readFileBytes(it.value(), &failure);
        if (!failure.isEmpty())
            continue;
        const QList<ResolvedPinloomCodeLink> resolved =
            store.linksForDocument(
                it.value(), QString::fromUtf8(bytes), nullptr);
        for (const ResolvedPinloomCodeLink& anchor : resolved) {
            emitted.insert(anchor.anchor.id);
            result.append(resolvedAnchorJson(anchor, state.root));
        }
    }

    for (const PinloomCodeLinkAnchorRecord& anchor : stored) {
        if (emitted.contains(anchor.id))
            continue;
        QString file = anchor.source.absoluteFilePath;
        if (file.isEmpty() && !anchor.source.relativeFilePath.isEmpty()) {
            file = QDir(state.root).absoluteFilePath(
                anchor.source.relativeFilePath);
        }
        if (!requestedKey.isEmpty() && pathKey(file) != requestedKey)
            continue;
        ResolvedPinloomCodeLink missing;
        missing.anchor = anchor;
        missing.resolution = PinloomCodeLinkResolution::Missing;
        missing.firstLine = anchor.source.startLine;
        missing.lastLine = anchor.source.endLine;
        result.append(resolvedAnchorJson(missing, state.root));
    }
    return result;
}

QJsonObject errorEnvelope(const ZeroSlackCliRequest& request,
                          const QString& code,
                          const QString& message,
                          const WorkspaceState* workspace = nullptr)
{
    QJsonObject result{
        {QStringLiteral("schema"), QString::fromLatin1(kSchema)},
        {QStringLiteral("schemaVersion"),
         ZeroSlackCliService::kSchemaVersion},
        {QStringLiteral("command"), request.command},
        {QStringLiteral("ok"), false},
        {QStringLiteral("generatedAtUtc"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("error"), QJsonObject{
             {QStringLiteral("code"), code},
             {QStringLiteral("message"), message},
         }},
    };
    if (workspace && workspace->isValid()) {
        result.insert(QStringLiteral("workspaceRoot"), workspace->root);
        result.insert(QStringLiteral("workspaceRevision"),
                      workspace->revision);
    }
    return result;
}

QJsonObject successEnvelope(const ZeroSlackCliRequest& request,
                            const PreparedIndex& prepared,
                            const QJsonObject& data)
{
    return {
        {QStringLiteral("schema"), QString::fromLatin1(kSchema)},
        {QStringLiteral("schemaVersion"),
         ZeroSlackCliService::kSchemaVersion},
        {QStringLiteral("command"), request.command},
        {QStringLiteral("ok"), true},
        {QStringLiteral("generatedAtUtc"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("workspaceRoot"), prepared.workspace.root},
        {QStringLiteral("workspaceRevision"),
         prepared.workspace.revision},
        {QStringLiteral("cachePath"), prepared.cachePath},
        {QStringLiteral("cacheCurrent"), prepared.cacheCurrent},
        {QStringLiteral("cacheRebuilt"), prepared.rebuilt},
        {QStringLiteral("data"), data},
    };
}

QJsonObject countsByKey(const QJsonArray& values, const QString& key)
{
    QHash<QString, int> counts;
    for (const QJsonValue& value : values) {
        const QString name = value.toObject().value(key).toString();
        if (!name.isEmpty())
            ++counts[name];
    }
    QStringList names = counts.keys();
    names.sort(Qt::CaseInsensitive);
    QJsonObject result;
    for (const QString& name : names)
        result.insert(name, counts.value(name));
    return result;
}

bool sameFile(const QString& left, const QString& right)
{
    QString leftKey = QDir::cleanPath(QDir::fromNativeSeparators(left));
    QString rightKey = QDir::cleanPath(QDir::fromNativeSeparators(right));
#ifdef Q_OS_WIN
    leftKey = leftKey.toCaseFolded();
    rightKey = rightKey.toCaseFolded();
#endif
    return leftKey == rightKey;
}

const SourceFileState* findSourceFile(const WorkspaceState& state,
                                      const QString& requested)
{
    const QString absolute = resolvedWorkspaceFile(state.root, requested);
    if (absolute.isEmpty())
        return nullptr;
    for (const SourceFileState& file : state.files) {
        if (sameFile(file.absolutePath, absolute))
            return &file;
    }
    return nullptr;
}

QString numberedSnippet(const QByteArray& content,
                        int firstLine,
                        int lastLine)
{
    const QStringList lines = QString::fromUtf8(content).split(
        QLatin1Char('\n'));
    firstLine = qBound(1, firstLine, qMax(1, lines.size()));
    lastLine = qBound(firstLine, lastLine, qMax(firstLine, lines.size()));
    QStringList output;
    for (int line = firstLine; line <= lastLine; ++line) {
        output.append(QStringLiteral("%1 | %2")
                          .arg(line, 6)
                          .arg(lines.value(line - 1)));
    }
    return output.join(QLatin1Char('\n'));
}

QJsonArray matchingSymbols(const QJsonArray& symbols,
                           const QString& query)
{
    QJsonArray matches;
    const QString folded = query.trimmed().toCaseFolded();
    for (const QJsonValue& value : symbols) {
        const QJsonObject symbol = value.toObject();
        if (symbol.value(QStringLiteral("id")).toString() == query
            || symbol.value(QStringLiteral("exactId")).toString() == query
            || symbol.value(QStringLiteral("name")).toString()
                   .toCaseFolded() == folded) {
            matches.append(symbol);
        }
    }
    return matches;
}

QJsonArray relationshipsForIds(const QJsonArray& relationships,
                               const QSet<QString>& ids)
{
    QJsonArray result;
    for (const QJsonValue& value : relationships) {
        const QJsonObject relationship = value.toObject();
        if (ids.contains(relationship.value(
                QStringLiteral("from")).toString())
            || ids.contains(relationship.value(
                QStringLiteral("to")).toString())) {
            result.append(relationship);
        }
    }
    return result;
}

int queryScore(const QJsonObject& symbol, const QString& query)
{
    const QString folded = query.toCaseFolded().trimmed();
    if (folded.isEmpty())
        return 0;
    const QString name = symbol.value(QStringLiteral("name"))
        .toString().toCaseFolded();
    const QString owner = symbol.value(QStringLiteral("owner"))
        .toString().toCaseFolded();
    const QString file = symbol.value(QStringLiteral("file"))
        .toString().toCaseFolded();
    const QString declaration = symbol.value(QStringLiteral("declaration"))
        .toString().toCaseFolded();
    if (name == folded)
        return 1000;
    if (name.startsWith(folded))
        return 800;
    if (name.contains(folded))
        return 650;
    if ((owner + QLatin1Char('.') + name).contains(folded))
        return 550;
    if (owner.contains(folded))
        return 450;
    if (file.contains(folded))
        return 300;
    if (declaration.contains(folded))
        return 200;
    QStringList terms;
    QString currentTerm;
    for (const QChar character : folded) {
        if (character.isSpace()) {
            if (!currentTerm.isEmpty()) {
                terms.append(currentTerm);
                currentTerm.clear();
            }
        } else {
            currentTerm.append(character);
        }
    }
    if (!currentTerm.isEmpty())
        terms.append(currentTerm);
    int matched = 0;
    const QString haystack = name + QLatin1Char(' ') + owner
        + QLatin1Char(' ') + file + QLatin1Char(' ') + declaration;
    for (const QString& term : terms) {
        if (haystack.contains(term))
            ++matched;
    }
    return matched == terms.size() && !terms.isEmpty()
        ? 100 + matched * 10 : 0;
}

int estimatedTokens(const QString& text)
{
    return qMax(1, (text.toUtf8().size() + 3) / 4);
}

QStringList gitChangedFiles(const WorkspaceState& state,
                            const QString& baseRef,
                            QString* failureReason)
{
    if (baseRef.trimmed().isEmpty()) {
        if (failureReason)
            *failureReason = QStringLiteral("--base requires a Git ref.");
        return {};
    }
    const auto runGit = [&](const QStringList& arguments,
                            bool required) -> QStringList {
        QProcess process;
        process.setProgram(QStringLiteral("git"));
        QStringList fullArguments{QStringLiteral("-C"), state.root};
        fullArguments.append(arguments);
        process.setArguments(fullArguments);
        process.start();
        if (!process.waitForStarted(5000)
            || !process.waitForFinished(15000)
            || (required && process.exitCode() != 0)) {
            if (failureReason && failureReason->isEmpty()) {
                *failureReason = QString::fromUtf8(
                    process.readAllStandardError()).trimmed();
                if (failureReason->isEmpty())
                    *failureReason = QStringLiteral("Git query failed.");
            }
            return {};
        }
        return QString::fromUtf8(process.readAllStandardOutput())
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    };

    QStringList changed = runGit(
        {QStringLiteral("diff"), QStringLiteral("--name-only"),
         baseRef, QStringLiteral("--")}, true);
    if (failureReason && !failureReason->isEmpty())
        return {};
    changed.append(runGit(
        {QStringLiteral("ls-files"), QStringLiteral("--others"),
         QStringLiteral("--exclude-standard")}, false));
    for (QString& path : changed)
        path = QDir::cleanPath(QDir::fromNativeSeparators(path));
    changed.sort(Qt::CaseInsensitive);
    changed.removeDuplicates();
    return changed;
}

QJsonObject scanData(const PreparedIndex& prepared)
{
    return {
        {QStringLiteral("files"),
         prepared.index.value(QStringLiteral("files")).toArray().size()},
        {QStringLiteral("symbols"),
         prepared.index.value(QStringLiteral("symbols")).toArray().size()},
        {QStringLiteral("relationships"),
         prepared.index.value(
             QStringLiteral("relationships")).toArray().size()},
        {QStringLiteral("diagnostics"),
         prepared.index.value(
             QStringLiteral("diagnostics")).toArray().size()},
        {QStringLiteral("configurationSource"),
         prepared.index.value(QStringLiteral("configurationSource"))},
    };
}

QJsonObject summaryData(const PreparedIndex& prepared)
{
    const QJsonArray symbols = prepared.index.value(
        QStringLiteral("symbols")).toArray();
    const QJsonArray relationships = prepared.index.value(
        QStringLiteral("relationships")).toArray();
    const QJsonArray diagnostics = prepared.index.value(
        QStringLiteral("diagnostics")).toArray();
    const QJsonArray anchors = anchorsJson(prepared.workspace);
    return {
        {QStringLiteral("fileCount"), prepared.workspace.files.size()},
        {QStringLiteral("symbolCount"), symbols.size()},
        {QStringLiteral("relationshipCount"), relationships.size()},
        {QStringLiteral("diagnosticCount"), diagnostics.size()},
        {QStringLiteral("anchorCount"), anchors.size()},
        {QStringLiteral("symbolsByKind"),
         countsByKey(symbols, QStringLiteral("kind"))},
        {QStringLiteral("relationshipsByType"),
         countsByKey(relationships, QStringLiteral("type"))},
        {QStringLiteral("diagnosticsBySeverity"),
         countsByKey(diagnostics, QStringLiteral("severity"))},
        {QStringLiteral("topModule"),
         prepared.workspace.configuration.topModule},
        {QStringLiteral("configurationSource"),
         configurationSourceName(
             prepared.workspace.configurationSource)},
    };
}

QJsonObject contextData(const PreparedIndex& prepared,
                        const ZeroSlackCliRequest& request,
                        QString* failureReason)
{
    const SourceFileState* file = findSourceFile(
        prepared.workspace, request.filePath);
    if (!file) {
        if (failureReason)
            *failureReason = QStringLiteral(
                "--file must identify an RTL source inside the workspace.");
        return {};
    }
    if (request.line < 1) {
        if (failureReason)
            *failureReason = QStringLiteral("--line must be at least 1.");
        return {};
    }
    const int totalLines = QString::fromUtf8(file->content)
        .count(QLatin1Char('\n')) + 1;
    if (request.line > totalLines) {
        if (failureReason)
            *failureReason = QStringLiteral("--line exceeds the source length.");
        return {};
    }

    QJsonArray symbols;
    for (const QJsonValue& value : prepared.index.value(
             QStringLiteral("symbols")).toArray()) {
        const QJsonObject symbol = value.toObject();
        if (!sameFile(symbol.value(QStringLiteral("file")).toString(),
                      file->relativePath)) {
            continue;
        }
        const int start = symbol.value(QStringLiteral("line")).toInt();
        int end = symbol.value(QStringLiteral("endLine")).toInt(start);
        if (end < start)
            end = start;
        if ((request.line >= start && request.line <= end)
            || qAbs(request.line - start) <= 2) {
            symbols.append(symbol);
        }
    }
    const QJsonArray fileAnchors = anchorsJson(
        prepared.workspace, file->absolutePath);
    QJsonArray nearbyAnchors;
    for (const QJsonValue& value : fileAnchors) {
        const QJsonObject anchor = value.toObject();
        const int first = anchor.value(QStringLiteral("firstLine")).toInt();
        const int last = anchor.value(QStringLiteral("lastLine")).toInt(first);
        if ((first > 0 && request.line >= first && request.line <= qMax(first, last))
            || (first > 0 && qAbs(request.line - first) <= 3)) {
            nearbyAnchors.append(anchor);
        }
    }
    const int firstLine = qMax(1, request.line - 10);
    const int lastLine = qMin(totalLines, request.line + 10);
    return {
        {QStringLiteral("file"), file->relativePath},
        {QStringLiteral("fileSha256"), file->sha256},
        {QStringLiteral("line"), request.line},
        {QStringLiteral("firstLine"), firstLine},
        {QStringLiteral("lastLine"), lastLine},
        {QStringLiteral("snippet"),
         numberedSnippet(file->content, firstLine, lastLine)},
        {QStringLiteral("symbols"), symbols},
        {QStringLiteral("anchors"), nearbyAnchors},
    };
}

QJsonObject symbolData(const PreparedIndex& prepared,
                       const QString& query,
                       QString* failureReason)
{
    if (query.trimmed().isEmpty()) {
        if (failureReason)
            *failureReason = QStringLiteral("A symbol name or ID is required.");
        return {};
    }
    const QJsonArray matches = matchingSymbols(
        prepared.index.value(QStringLiteral("symbols")).toArray(), query);
    if (matches.isEmpty()) {
        if (failureReason)
            *failureReason = QStringLiteral("No matching symbol was found.");
        return {};
    }
    QSet<QString> ids;
    QSet<QString> files;
    for (const QJsonValue& value : matches) {
        const QJsonObject symbol = value.toObject();
        ids.insert(symbol.value(QStringLiteral("id")).toString());
        files.insert(symbol.value(QStringLiteral("file")).toString());
    }
    QJsonArray anchors;
    const QJsonArray allAnchors = anchorsJson(prepared.workspace);
    for (const QJsonValue& value : allAnchors) {
        const QJsonObject anchor = value.toObject();
        if (files.contains(anchor.value(QStringLiteral("file")).toString())
            && (anchor.value(QStringLiteral("symbol")).toString()
                    .compare(query, Qt::CaseInsensitive) == 0
                || ids.size() == 1)) {
            anchors.append(anchor);
        }
    }
    return {
        {QStringLiteral("matches"), matches},
        {QStringLiteral("relationships"),
         relationshipsForIds(
             prepared.index.value(
                 QStringLiteral("relationships")).toArray(), ids)},
        {QStringLiteral("anchors"), anchors},
    };
}

QJsonObject impactData(const PreparedIndex& prepared,
                       const ZeroSlackCliRequest& request,
                       QString* failureReason)
{
    const QJsonArray allSymbols = prepared.index.value(
        QStringLiteral("symbols")).toArray();
    const QJsonArray seeds = matchingSymbols(allSymbols, request.symbol);
    if (seeds.isEmpty()) {
        if (failureReason)
            *failureReason = QStringLiteral("No impact seed symbol was found.");
        return {};
    }
    QSet<QString> visited;
    QSet<QString> frontier;
    for (const QJsonValue& value : seeds) {
        const QString id = value.toObject().value(
            QStringLiteral("id")).toString();
        visited.insert(id);
        frontier.insert(id);
    }
    const QJsonArray allRelationships = prepared.index.value(
        QStringLiteral("relationships")).toArray();
    QJsonArray selectedRelationships;
    QSet<QString> edgeKeys;
    const int depth = qBound(1, request.depth, 8);
    for (int level = 0; level < depth && !frontier.isEmpty(); ++level) {
        QSet<QString> next;
        for (const QJsonValue& value : allRelationships) {
            const QJsonObject relationship = value.toObject();
            const QString from = relationship.value(
                QStringLiteral("from")).toString();
            const QString to = relationship.value(
                QStringLiteral("to")).toString();
            if (!frontier.contains(from) && !frontier.contains(to))
                continue;
            const QString key = from + QLatin1Char('|') + to
                + QLatin1Char('|') + relationship.value(
                    QStringLiteral("type")).toString();
            if (!edgeKeys.contains(key)) {
                edgeKeys.insert(key);
                selectedRelationships.append(relationship);
            }
            if (!visited.contains(from)) {
                visited.insert(from);
                next.insert(from);
            }
            if (!visited.contains(to)) {
                visited.insert(to);
                next.insert(to);
            }
        }
        frontier = std::move(next);
    }
    QJsonArray nodes;
    for (const QJsonValue& value : allSymbols) {
        if (visited.contains(value.toObject().value(
                QStringLiteral("id")).toString())) {
            nodes.append(value);
        }
    }
    return {
        {QStringLiteral("depth"), depth},
        {QStringLiteral("seeds"), seeds},
        {QStringLiteral("symbols"), nodes},
        {QStringLiteral("relationships"), selectedRelationships},
    };
}

QJsonObject changedData(const PreparedIndex& prepared,
                        const ZeroSlackCliRequest& request,
                        QString* failureReason)
{
    const QStringList changed = gitChangedFiles(
        prepared.workspace, request.baseRef, failureReason);
    if (failureReason && !failureReason->isEmpty())
        return {};
    QSet<QString> changedKeys;
    QJsonArray files;
    for (const QString& path : changed) {
        files.append(path);
        changedKeys.insert(QDir::cleanPath(
            QDir::fromNativeSeparators(path)).toCaseFolded());
    }
    QJsonArray symbols;
    for (const QJsonValue& value : prepared.index.value(
             QStringLiteral("symbols")).toArray()) {
        const QString file = value.toObject().value(
            QStringLiteral("file")).toString();
        if (changedKeys.contains(file.toCaseFolded()))
            symbols.append(value);
    }
    QJsonArray anchors;
    for (const QJsonValue& value : anchorsJson(prepared.workspace)) {
        const QString file = value.toObject().value(
            QStringLiteral("file")).toString();
        if (changedKeys.contains(file.toCaseFolded()))
            anchors.append(value);
    }
    return {
        {QStringLiteral("base"), request.baseRef},
        {QStringLiteral("files"), files},
        {QStringLiteral("symbols"), symbols},
        {QStringLiteral("anchors"), anchors},
    };
}

QJsonObject bundleData(const PreparedIndex& prepared,
                       const ZeroSlackCliRequest& request,
                       QString* failureReason)
{
    if (request.query.trimmed().isEmpty()) {
        if (failureReason)
            *failureReason = QStringLiteral("--query must not be empty.");
        return {};
    }
    const int budget = qBound(128, request.maxTokens, 200000);
    struct Candidate {
        QJsonObject symbol;
        int score = 0;
    };
    QList<Candidate> candidates;
    for (const QJsonValue& value : prepared.index.value(
             QStringLiteral("symbols")).toArray()) {
        const QJsonObject symbol = value.toObject();
        const int score = queryScore(symbol, request.query);
        if (score > 0)
            candidates.append(Candidate{symbol, score});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left,
                                                        const Candidate& right) {
        if (left.score != right.score)
            return left.score > right.score;
        return left.symbol.value(QStringLiteral("id")).toString()
            < right.symbol.value(QStringLiteral("id")).toString();
    });

    QString markdown = QStringLiteral(
        "# ZeroSlack context bundle\n\n"
        "- Query: `%1`\n"
        "- Workspace revision: `%2`\n"
        "- Token budget: %3 (UTF-8/4 estimate)\n\n")
        .arg(request.query,
             prepared.workspace.revision,
             QString::number(budget));
    int tokens = estimatedTokens(markdown);
    QJsonArray selected;
    QSet<QString> emittedRanges;
    for (const Candidate& candidate : std::as_const(candidates)) {
        if (selected.size() >= 64)
            break;
        const SourceFileState* file = findSourceFile(
            prepared.workspace,
            candidate.symbol.value(QStringLiteral("file")).toString());
        if (!file)
            continue;
        const int line = qMax(1, candidate.symbol.value(
            QStringLiteral("line")).toInt());
        const int first = qMax(1, line - 4);
        const int totalLines = QString::fromUtf8(file->content)
            .count(QLatin1Char('\n')) + 1;
        const int last = qMin(totalLines, qMax(
            line + 8,
            candidate.symbol.value(QStringLiteral("endLine")).toInt(line)));
        const QString rangeKey = file->relativePath.toCaseFolded()
            + QLatin1Char(':') + QString::number(first)
            + QLatin1Char('-') + QString::number(last);
        if (emittedRanges.contains(rangeKey))
            continue;
        const QString block = QStringLiteral(
            "## %1 — %2\n\n"
            "- Symbol ID: `%3`\n"
            "- Source: `%4:%5`\n\n"
            "```systemverilog\n%6\n```\n\n")
            .arg(candidate.symbol.value(QStringLiteral("kind")).toString(),
                 candidate.symbol.value(QStringLiteral("name")).toString(),
                 candidate.symbol.value(QStringLiteral("id")).toString(),
                 file->relativePath,
                 QString::number(line),
                 numberedSnippet(file->content, first, last));
        const int blockTokens = estimatedTokens(block);
        if (tokens + blockTokens > budget)
            continue;
        markdown += block;
        tokens += blockTokens;
        emittedRanges.insert(rangeKey);
        QJsonObject selectedSymbol = candidate.symbol;
        selectedSymbol.insert(QStringLiteral("score"), candidate.score);
        selectedSymbol.insert(QStringLiteral("firstLine"), first);
        selectedSymbol.insert(QStringLiteral("lastLine"), last);
        selected.append(selectedSymbol);
    }

    QJsonArray selectedAnchors;
    const QJsonArray allAnchors = anchorsJson(prepared.workspace);
    for (const QJsonValue& value : allAnchors) {
        const QJsonObject anchor = value.toObject();
        const QString haystack = anchor.value(QStringLiteral("symbol"))
            .toString() + QLatin1Char(' ')
            + anchor.value(QStringLiteral("module")).toString()
            + QLatin1Char(' ')
            + QString::fromUtf8(QJsonDocument(
                anchor.value(QStringLiteral("links")).toArray())
                    .toJson(QJsonDocument::Compact));
        if (!haystack.contains(request.query, Qt::CaseInsensitive))
            continue;
        const QString anchorLine = QStringLiteral(
            "- Anchor `%1` (%2), `%3:%4`, targets: %5\n")
            .arg(anchor.value(QStringLiteral("anchorId")).toString(),
                 anchor.value(QStringLiteral("resolution")).toString(),
                 anchor.value(QStringLiteral("file")).toString(),
                 QString::number(anchor.value(
                     QStringLiteral("firstLine")).toInt()),
                 QString::number(anchor.value(
                     QStringLiteral("linkCount")).toInt()));
        const int anchorTokens = estimatedTokens(anchorLine);
        if (tokens + anchorTokens > budget)
            break;
        if (selectedAnchors.isEmpty()) {
            const QString heading = QStringLiteral("## Pinloom links\n\n");
            if (tokens + estimatedTokens(heading) > budget)
                break;
            markdown += heading;
            tokens += estimatedTokens(heading);
        }
        markdown += anchorLine;
        tokens += anchorTokens;
        selectedAnchors.append(anchor);
    }
    return {
        {QStringLiteral("query"), request.query},
        {QStringLiteral("maxTokens"), budget},
        {QStringLiteral("estimatedTokens"), tokens},
        {QStringLiteral("symbols"), selected},
        {QStringLiteral("anchors"), selectedAnchors},
        {QStringLiteral("markdown"), markdown},
    };
}

QByteArray jsonLine(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact)
        + QByteArrayLiteral("\n");
}

} // namespace

ZeroSlackCliResult ZeroSlackCliService::execute(
    const ZeroSlackCliRequest& request) const
{
    ZeroSlackCliResult result;
    static const QSet<QString> commands{
        QStringLiteral("scan"), QStringLiteral("status"),
        QStringLiteral("summary"), QStringLiteral("context"),
        QStringLiteral("symbol"), QStringLiteral("anchors"),
        QStringLiteral("impact"), QStringLiteral("changed"),
        QStringLiteral("bundle")};
    if (!commands.contains(request.command)) {
        result.exitCode = 2;
        result.envelope = errorEnvelope(
            request, QStringLiteral("unknown_command"),
            QStringLiteral("Unknown command. Use --help for usage."));
        result.rendered = render(result.envelope, request.format);
        return result;
    }

    const bool statusOnly = request.command == QStringLiteral("status");
    PreparedIndex prepared = prepareIndex(request, statusOnly);
    if (!prepared.workspace.isValid() || !prepared.failureReason.isEmpty()) {
        result.exitCode = 3;
        result.envelope = errorEnvelope(
            request, QStringLiteral("workspace_or_cache_error"),
            prepared.failureReason.isEmpty()
                ? prepared.workspace.failureReason
                : prepared.failureReason,
            &prepared.workspace);
        result.rendered = render(result.envelope, request.format);
        return result;
    }

    if (statusOnly) {
        const QJsonObject data{
            {QStringLiteral("cacheExists"), prepared.cacheExists},
            {QStringLiteral("cacheCurrent"), prepared.cacheCurrent},
            {QStringLiteral("cachedRevision"),
             prepared.index.value(QStringLiteral("workspaceRevision"))},
            {QStringLiteral("currentRevision"),
             prepared.workspace.revision},
            {QStringLiteral("fileCount"), prepared.workspace.files.size()},
            {QStringLiteral("anchorCount"),
             anchorsJson(prepared.workspace).size()},
        };
        result.envelope = successEnvelope(request, prepared, data);
        if (request.requireCurrent && !prepared.cacheCurrent) {
            result.exitCode = 4;
            result.envelope.insert(QStringLiteral("ok"), false);
            result.envelope.insert(
                QStringLiteral("error"),
                QJsonObject{{QStringLiteral("code"),
                             QStringLiteral("cache_stale")},
                            {QStringLiteral("message"),
                             QStringLiteral("Semantic cache is not current.")}});
        }
        result.rendered = render(result.envelope, request.format);
        return result;
    }

    QJsonObject data;
    QString failure;
    if (request.command == QStringLiteral("scan"))
        data = scanData(prepared);
    else if (request.command == QStringLiteral("summary"))
        data = summaryData(prepared);
    else if (request.command == QStringLiteral("context"))
        data = contextData(prepared, request, &failure);
    else if (request.command == QStringLiteral("symbol"))
        data = symbolData(prepared, request.symbol, &failure);
    else if (request.command == QStringLiteral("anchors")) {
        QString file;
        if (!request.filePath.isEmpty()) {
            file = resolvedWorkspaceFile(prepared.workspace.root,
                                         request.filePath);
            if (file.isEmpty()) {
                failure = QStringLiteral(
                    "--file must be inside the workspace.");
            }
        }
        if (failure.isEmpty()) {
            const QJsonArray anchors = anchorsJson(prepared.workspace, file);
            data = {{QStringLiteral("count"), anchors.size()},
                    {QStringLiteral("anchors"), anchors}};
        }
    } else if (request.command == QStringLiteral("impact"))
        data = impactData(prepared, request, &failure);
    else if (request.command == QStringLiteral("changed"))
        data = changedData(prepared, request, &failure);
    else if (request.command == QStringLiteral("bundle"))
        data = bundleData(prepared, request, &failure);

    if (!failure.isEmpty()) {
        result.exitCode = 5;
        result.envelope = errorEnvelope(
            request, QStringLiteral("query_error"), failure,
            &prepared.workspace);
    } else {
        result.envelope = successEnvelope(request, prepared, data);
    }
    result.rendered = render(result.envelope, request.format);
    return result;
}

QString ZeroSlackCliService::cachePathForWorkspace(
    const QString& workspaceRoot,
    const QString& cacheDirectory) const
{
    return cachePath(workspaceRoot, cacheDirectory);
}

QByteArray ZeroSlackCliService::render(const QJsonObject& envelope,
                                       const QString& requestedFormat)
{
    const QString format = requestedFormat.trimmed().toLower();
    if (format == QStringLiteral("markdown")) {
        const QJsonObject data = envelope.value(
            QStringLiteral("data")).toObject();
        const QString markdown = data.value(
            QStringLiteral("markdown")).toString();
        if (!markdown.isEmpty())
            return markdown.toUtf8();
        QString result = QStringLiteral("# ZeroSlack %1\n\n")
            .arg(envelope.value(QStringLiteral("command")).toString());
        result += QStringLiteral("- Status: %1\n")
            .arg(envelope.value(QStringLiteral("ok")).toBool()
                     ? QStringLiteral("ok") : QStringLiteral("failed"));
        if (envelope.contains(QStringLiteral("workspaceRevision"))) {
            result += QStringLiteral("- Workspace revision: `%1`\n")
                .arg(envelope.value(
                    QStringLiteral("workspaceRevision")).toString());
        }
        result += QStringLiteral("\n```json\n")
            + QString::fromUtf8(QJsonDocument(data).toJson(
                QJsonDocument::Indented))
            + QStringLiteral("```\n");
        return result.toUtf8();
    }
    if (format == QStringLiteral("jsonl")) {
        QJsonObject meta = envelope;
        const QJsonObject data = meta.take(
            QStringLiteral("data")).toObject();
        meta.insert(QStringLiteral("recordType"), QStringLiteral("meta"));
        QByteArray output = jsonLine(meta);
        QJsonObject scalars;
        for (auto it = data.begin(); it != data.end(); ++it) {
            if (!it.value().isArray()) {
                scalars.insert(it.key(), it.value());
                continue;
            }
            for (const QJsonValue& value : it.value().toArray()) {
                output += jsonLine(QJsonObject{
                    {QStringLiteral("recordType"), it.key()},
                    {QStringLiteral("value"), value},
                });
            }
        }
        if (!scalars.isEmpty()) {
            output += jsonLine(QJsonObject{
                {QStringLiteral("recordType"), QStringLiteral("data")},
                {QStringLiteral("value"), scalars},
            });
        }
        return output;
    }
    return QJsonDocument(envelope).toJson(QJsonDocument::Indented);
}

QString ZeroSlackCliService::usageText()
{
    return QStringLiteral(
        "ZeroSlack read-only AI workspace CLI\n\n"
        "Usage:\n"
        "  zeroslack-cli scan <workspace> [--format json|jsonl|markdown]\n"
        "  zeroslack-cli status <workspace> [--require-current]\n"
        "  zeroslack-cli summary <workspace>\n"
        "  zeroslack-cli context <workspace> --file <path> --line <n>\n"
        "  zeroslack-cli symbol <workspace> <name-or-id>\n"
        "  zeroslack-cli anchors <workspace> [--file <path>]\n"
        "  zeroslack-cli impact <workspace> --symbol <name-or-id> [--depth <n>]\n"
        "  zeroslack-cli changed <workspace> --base <git-ref>\n"
        "  zeroslack-cli bundle <workspace> --query <text> --max-tokens <n>\n\n"
        "Global options:\n"
        "  --cache-dir <path>   Override the user cache directory.\n"
        "  --refresh            Rebuild a current cache.\n"
        "  --no-refresh         Fail instead of rebuilding a stale cache.\n"
        "  --format <format>     json (default), jsonl, or markdown.\n");
}
