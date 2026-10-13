#include "xipsassetsync.h"
#include "editorfileidentity.h"
#include "slangmanager.h"
#include "xipscontextprovider.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <functional>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace
{
constexpr auto requestSchema = "xips.asset-adapter.request/v1";
constexpr auto responseSchema = "xips.asset-adapter.response/v1";
constexpr auto operationSchema = "zeroslack.xips-operation/v1";
QString hashFile(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}
bool within(const QString &path, const QString &root)
{
    const auto p = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const auto r = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    return p.compare(r, Qt::CaseInsensitive) == 0 || p.startsWith(r + '/', Qt::CaseInsensitive);
}
bool linked(QString path)
{
    path = QFileInfo(path).absoluteFilePath();
    for (;;)
    {
        const QFileInfo info(path);
        if (info.isSymLink() || info.isJunction())
            return true;
        if (info.absolutePath() == path)
            return false;
        path = info.absolutePath();
    }
}
bool cancelled(const std::shared_ptr<std::atomic_bool> &flag)
{
    return flag && flag->load();
}
QVariantMap failure(const QString &message, const QString &state = "failed",
                    const QString &publication = "none")
{
    return {{"ok", false},
            {"state", state},
            {"publication", publication},
            {"error", QVariantMap{{"code", "HOST_ERROR"}, {"message", message}}}};
}
QString operationPath(const QVariantMap &operation)
{
    const auto id = operation.value("requestId").toString();
    if (QUuid(id).isNull() || QUuid(id).toString(QUuid::WithoutBraces) != id)
        return {};
    const auto workspace = operation.value("workspace").toString();
    if (!QFileInfo(workspace).isDir() || linked(workspace))
        return {};
    const auto path = QDir(workspace).filePath(".zeroslack/asset-operations/" + id + ".json");
    return linked(path) ? QString() : path;
}
QString saveOperation(const QString &path, const QVariantMap &operation)
{
    if (path.isEmpty() || linked(path) || !QDir().mkpath(QFileInfo(path).absolutePath()))
        return QStringLiteral("The workspace cannot retain this asset operation for recovery.");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument::fromVariant(operation).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("The asset operation could not be retained. No new request was sent.");
    return {};
}
QVariantMap readOperation(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        *error = "Cannot read the saved asset operation.";
        return {};
    }
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto operation = json.toVariant().toMap();
    if (parse.error != QJsonParseError::NoError || !json.isObject() ||
        operation.value("schema").toString() != operationSchema)
    {
        *error = "The saved asset operation is invalid; it was preserved.";
        return {};
    }
    return operation;
}
QString verifyReceipt(const QVariantMap &operation, const QVariantMap &receipt)
{
    const auto request = operation.value("request").toMap();
    const auto preview = operation.value("preview").toMap();
    const QRegularExpression hash("^sha256:[0-9a-f]{64}$");
    if (receipt.value("schema") != "xips.asset-adapter.receipt/v1" ||
        receipt.value("requestId") != operation.value("requestId") ||
        !EditorFileIdentity::same(receipt.value("library").toString(), request.value("library").toString()) ||
        QUuid(receipt.value("assetId").toString()).isNull() ||
        QUuid(receipt.value("revision").toString()).isNull() ||
        !hash.match(receipt.value("contentHash").toString()).hasMatch() ||
        (!preview.value("assetId").toString().isEmpty() &&
         receipt.value("assetId") != preview.value("assetId")) ||
        (!request.value("assetId").toString().isEmpty() &&
         receipt.value("assetId") != request.value("assetId")))
        return "The published receipt does not match this asset operation. Its reference was not changed.";
    const auto expectedFiles = request.value("files").toList();
    const auto actualFiles = receipt.value("files").toList();
    if (expectedFiles.isEmpty() || expectedFiles.size() != actualFiles.size())
        return "The published receipt has a different dependency set.";
    for (int i = 0; i < expectedFiles.size(); ++i)
    {
        const auto expected = expectedFiles[i].toMap(), actual = actualFiles[i].toMap();
        if (expected.value("path") != actual.value("path") ||
            expected.value("sha256") != actual.value("sha256") ||
            expected.value("dependencies").toStringList() != actual.value("dependencies").toStringList() ||
            !EditorFileIdentity::same(expected.value("source").toString(), actual.value("source").toString()))
            return "The published receipt has different source files, hashes or dependency order.";
    }
    const auto cli =
        QDir(QFileInfo(operation.value("adapter").toString()).absolutePath()).filePath("xips-cli.exe");
    QProcess verify;
#ifdef Q_OS_WIN
    verify.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args)
                                             { args->flags |= CREATE_NO_WINDOW; });
#endif
    verify.start(cli, {"--action", "resolve", "--library", receipt.value("library").toString(), "--asset",
                       receipt.value("assetId").toString(), "--asset-version",
                       receipt.value("revision").toString()});
    if (!verify.waitForStarted(10000) || !verify.waitForFinished(30000))
    {
        verify.kill();
        verify.waitForFinished(5000);
        return "The published version could not be verified through xIPs. Recover the operation before "
               "changing its reference.";
    }
    const auto resolved = QJsonDocument::fromJson(verify.readAllStandardOutput()).toVariant().toMap();
    const auto data = resolved.value("data").toMap();
    if (verify.exitStatus() != QProcess::NormalExit || verify.exitCode() != 0 ||
        !resolved.value("ok").toBool() || data.value("id") != receipt.value("assetId") ||
        data.value("resolvedVersion") != receipt.value("revision") ||
        data.value("resolvedStrictContentHash") != receipt.value("contentHash") ||
        data.value("resolvedRelativeFiles").toStringList() != receipt.value("archiveFiles").toStringList())
        return "The published asset identity or stored content is no longer verifiable. Its project "
               "reference was preserved.";
    return {};
}
} // namespace

QString XipsAssetSync::executable()
{
    const auto override = qEnvironmentVariable("XIPS_ASSET_ADAPTER");
    return override.isEmpty() ? QDir(QCoreApplication::applicationDirPath())
                                    .filePath("components/xips-asset-adapter/xips-asset-adapter.exe")
                              : override;
}
QString XipsAssetSync::stateDirectory()
{
    const auto override = qEnvironmentVariable("XIPS_ASSET_ADAPTER_STATE");
    return override.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                                    .filePath("ZeroSlack/xips-asset-operations")
                              : override;
}
XipsAssetSyncPlan XipsAssetSync::prepare(const QString &workspace, const QStringList &selectedFiles,
                                         const SimulationCommandRequest &context, const QVariantMap &binding,
                                         const QString &library, const QString &name,
                                         const std::shared_ptr<std::atomic_bool> &flag)
{
    XipsAssetSyncPlan plan;
    const auto reject = [&](const QString &error)
    {
        plan.error = error;
        return plan;
    };
    if (!QFileInfo(workspace).isDir() || linked(workspace))
        return reject("Open a local workspace first.");
    if (!QFileInfo(library).isDir() || linked(library))
        return reject("Choose an existing local xIPs library.");
    QStringList inputs = selectedFiles;
    const bool updating = !binding.isEmpty();
    const bool single = binding.value("singleFile").toBool();
    QString root;
    if (updating)
    {
        if (!EditorFileIdentity::same(library, binding.value("library").toString()))
            return reject("Use the owning library recorded for this package.");
        root = QDir(workspace).filePath(binding.value("path").toString());
        inputs.clear();
        for (const auto &relative : binding.value("files").toStringList())
        {
            if (relative == "xips-asset-inputs.json")
                continue;
            inputs << (single ? root : QDir(root).filePath(relative));
        }
        // Retain actual compilation order when the binding originated in an ordinary export.
        QStringList ordered;
        for (const auto &source : selectedFiles)
            if (inputs.contains(source))
                ordered << source;
        for (const auto &source : inputs)
            if (!ordered.contains(source))
                ordered << source;
        inputs = ordered;
    }
    inputs.removeDuplicates();
    if (inputs.isEmpty())
        return reject("Choose package inputs before saving an asset.");
    QStringList compilation;
    for (auto &file : inputs)
    {
        file = QFileInfo(file).absoluteFilePath();
        if (!within(file, workspace) || linked(file) || !QFileInfo(file).isFile())
            return reject("All package inputs must be saved regular files inside this workspace.");
        const auto suffix = QFileInfo(file).suffix().toLower();
        if (suffix == "sv" || suffix == "v")
            compilation << file;
    }
    if (compilation.isEmpty())
        for (const auto &file : inputs)
            if (QStringList{"svh", "vh"}.contains(QFileInfo(file).suffix().toLower()))
                compilation << file;
    if (compilation.isEmpty())
        return reject("The package contains no SystemVerilog compilation input.");
    SemanticAnalysisRequest request;
    SemanticInputCapture capture(request);
    SlangManager slang;
    const auto stopped = [&] { return cancelled(flag); };
    const auto analysis = slang.analyzeCapturedWorkspace(capture, compilation, context.includeDirectories,
                                                         context.defines, {}, stopped, {true, true, false});
    if (analysis.cancelled || stopped())
        return reject("Asset preparation cancelled.");
    if (!analysis.error.isEmpty())
        return reject(analysis.error);
    for (const auto &diagnostic : analysis.diagnostics)
        if (diagnostic.severity == SemanticDiagnostic::Error)
            return reject("Resolve the package dependency error first: " + diagnostic.message);
    if (!capture.stillMatchesDisk(stopped))
        return reject("Package inputs changed during dependency validation.");
    const auto captured = capture.seal();
    QStringList includes;
    QVariantMap fingerprints;
    for (auto it = captured->sources.cbegin(); it != captured->sources.cend(); ++it)
    {
        fingerprints.insert(it.key(), QVariantMap{{"readable", it->readable},
                                                  {"sha256", QString::fromLatin1(it->rawSha256.toHex())}});
        if (!it->readable)
            continue;
        if (!within(it.key(), workspace) || linked(it.key()))
            return reject("Copy external include dependencies into the workspace before publishing.");
        if (!inputs.contains(it.key(), Qt::CaseInsensitive))
            includes << it.key();
        for (auto open = context.contents.cbegin(); open != context.contents.cend(); ++open)
            if (EditorFileIdentity::same(open.key(), it.key()) &&
                QString(open.value()).replace("\r\n", "\n") != QString(it->text).replace("\r\n", "\n"))
                return reject("Save open package dependencies before publishing an asset.");
    }
    includes.sort();
    inputs = includes + inputs;
    QHash<QString, QStringList> dependencies;
    for (auto it = analysis.includesByFile.cbegin(); it != analysis.includesByFile.cend(); ++it)
        for (const auto &include : it.value())
            dependencies[SemanticInputCapture::pathKey(it.key())] << SemanticInputCapture::pathKey(include);
    // Slang's combined compilation can resolve a later package. A saved compile
    // list must still place every imported package before its use in Questa.
    QHash<QString, SemanticSymbolRecord> packageDeclarations;
    for (const auto &symbol : analysis.symbols)
        if (symbol.declarationKind == SymbolTaxonomy::DeclarationKind::Package)
            packageDeclarations.insert(symbol.name, symbol);
    for (const auto &symbol : analysis.symbols)
    {
        if (symbol.collectorKind != SymbolTaxonomy::CollectorKind::PackageImport)
            continue;
        const auto target = packageDeclarations.constFind(symbol.name);
        if (target == packageDeclarations.cend())
            continue; // Slang diagnoses unresolved imports.
        if (qMakePair(target->compilationUnitSourceOrder, target->compilationUnitSourceOffset) >
            qMakePair(symbol.compilationUnitSourceOrder, symbol.compilationUnitSourceOffset))
            return reject("Move the imported package before its consumer in Package inputs: " +
                          target->location.fileName);
        const auto source = SemanticInputCapture::pathKey(symbol.location.fileName);
        const auto dependency = SemanticInputCapture::pathKey(target->location.fileName);
        if (source != dependency)
            dependencies[source] << dependency;
    }
    QStringList priorRoots;
    for (const auto &file : compilation)
    {
        dependencies[SemanticInputCapture::pathKey(file)] += priorRoots;
        priorRoots << SemanticInputCapture::pathKey(file);
    }
    QHash<QString, QString> paths;
    for (const auto &file : inputs)
        paths.insert(SemanticInputCapture::pathKey(file), file);
    QSet<QString> active, visited;
    QStringList ordered;
    std::function<bool(const QString &)> visit = [&](const QString &key)
    {
        if (active.contains(key))
            return false;
        if (visited.contains(key))
            return true;
        if (!paths.contains(key))
            return false;
        active.insert(key);
        for (const auto &dependency : dependencies.value(key))
            if (!visit(dependency))
                return false;
        active.remove(key);
        visited.insert(key);
        ordered << paths.value(key);
        return true;
    };
    for (const auto &file : inputs)
        if (!visit(SemanticInputCapture::pathKey(file)))
            return reject("The package dependency order contains a cycle or an unresolved source.");
    inputs = ordered;
    if (!updating)
    {
        root = QFileInfo(inputs.first()).absolutePath();
        for (const auto &file : inputs)
            while (!within(file, root))
            {
                const auto parent = QFileInfo(root).absolutePath();
                if (parent == root)
                    return reject("Package inputs have no common workspace directory.");
                root = parent;
            }
    }
    if (!within(root, workspace) || linked(root))
        return reject("The package binding is outside this workspace.");
    QVariantList files;
    QStringList previous, relativeCompilation;
    QSet<QString> names;
    QVariantMap baseline;
    for (const auto &source : inputs)
    {
        if (stopped())
            return reject("Asset preparation cancelled.");
        QString relative;
        if (single)
        {
            if (inputs.size() != 1)
                return reject(
                    "Export this single-file asset to a package directory before adding dependencies.");
            relative = binding.value("files").toStringList().value(0);
        }
        else
        {
            if (!within(source, root))
                return reject("A dependency is outside the bound package directory. Register a complete "
                              "package instead.");
            relative = QDir(root).relativeFilePath(source);
        }
        if (relative == "xips-asset-inputs.json")
            return reject("xips-asset-inputs.json is reserved for asset input metadata.");
        if (names.contains(relative.toCaseFolded()))
            return reject("Package archive paths must be unique.");
        const auto hash = hashFile(source);
        if (hash.isEmpty())
            return reject("A package input cannot be read: " + source);
        names.insert(relative.toCaseFolded());
        baseline.insert(relative, hash);
        files << QVariantMap{
            {"path", relative}, {"source", source}, {"sha256", hash}, {"dependencies", previous}};
        previous << relative;
        if (compilation.contains(source, Qt::CaseInsensitive))
            relativeCompilation << relative;
    }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVariantMap previewRequest{{"schema", requestSchema},
                               {"action", updating ? "previewSave" : "previewCreate"},
                               {"requestId", id},
                               {"library", QFileInfo(library).canonicalFilePath()},
                               {"dependencyMode", "explicit-complete"},
                               {"files", files}};
    if (updating)
    {
        previewRequest.insert("assetId", binding.value("assetId"));
        previewRequest.insert("baseVersion", QVariantMap{{"revision", binding.value("revision")},
                                                         {"contentHash", binding.value("contentHash")}});
        previewRequest.insert("expectedHeads", QStringList{binding.value("revision").toString()});
    }
    else
    {
        if (name.trimmed().isEmpty())
            return reject("Give the asset a name.");
        previewRequest.insert("name", name.trimmed());
        previewRequest.insert("category", "module");
    }
    auto draft = binding;
    draft.remove("localState");
    draft.insert("path", QDir(workspace).relativeFilePath(root));
    draft.insert("singleFile", single);
    draft.insert("files", previous);
    draft.insert("baseFiles", baseline);
    draft.insert("compileOrder", relativeCompilation);
    plan.operation = {{"schema", operationSchema},    {"requestId", id},
                      {"workspace", workspace},       {"request", previewRequest},
                      {"expectedReference", binding}, {"reference", draft},
                      {"adapter", executable()},      {"stateDirectory", stateDirectory()},
                      {"fingerprints", fingerprints}, {"status", "prepared"}};
    QVariantMap defines;
    for (auto it = context.defines.cbegin(); it != context.defines.cend(); ++it)
        defines.insert(it.key(), it.value());
    plan.operation.insert("projectContext", QVariantMap{{"includeDirectories", context.includeDirectories},
                                                        {"defines", defines}});
    plan.error = inputsCurrent(plan.operation);
    return plan;
}
QString XipsAssetSync::inputsCurrent(const QVariantMap &operation)
{
    const auto workspace = operation.value("workspace").toString();
    const auto inputs = operation.value("request").toMap().value("files").toList();
    for (const auto &entry : inputs)
    {
        const auto file = entry.toMap();
        if (!within(file.value("source").toString(), workspace) || linked(file.value("source").toString()) ||
            hashFile(file.value("source").toString()) != file.value("sha256").toString())
            return "A package input changed after preparation. Create a new preview.";
    }
    const auto fingerprints = operation.value("fingerprints").toMap();
    for (auto it = fingerprints.cbegin(); it != fingerprints.cend(); ++it)
    {
        const auto fact = it.value().toMap();
        const auto actual = hashFile(it.key());
        if (fact.value("readable").toBool() ? actual != fact.value("sha256").toString() : !actual.isEmpty())
            return "An include dependency changed after preparation. Create a new preview.";
    }
    return {};
}
QVariantMap XipsAssetSync::invoke(const QString &program, const QString &stateRoot,
                                  const QVariantMap &request, const std::shared_ptr<std::atomic_bool> &flag)
{
    if (!QFileInfo(program).isFile())
        return failure("The xIPs asset adapter is unavailable.");
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args)
                                              { args->flags |= CREATE_NO_WINDOW; });
#endif
    process.setProgram(program);
    process.setArguments({"--state-dir", stateRoot, "--stdin"});
    process.setWorkingDirectory(QFileInfo(program).absolutePath());
    process.start();
    if (!process.waitForStarted(10000))
        return failure("The asset adapter could not start: " + process.errorString());
    process.write(QJsonDocument::fromVariant(request).toJson(QJsonDocument::Compact));
    process.closeWriteChannel();
    QElapsedTimer timer;
    timer.start();
    QByteArray output, errors;
    bool sentCancel = false;
    while (process.state() != QProcess::NotRunning)
    {
        process.waitForFinished(100);
        output += process.readAllStandardOutput();
        errors += process.readAllStandardError();
        if (cancelled(flag) && !sentCancel && request.value("action") != "operationStatus" &&
            request.value("action") != "cancel")
        {
            sentCancel = true;
            invoke(
                program, stateRoot,
                {{"schema", requestSchema}, {"action", "cancel"}, {"requestId", request.value("requestId")}});
        }
        if (output.size() > 32 * 1024 * 1024 || errors.size() > 2 * 1024 * 1024 ||
            timer.elapsed() >
                (QStringList{"cancel", "operationStatus"}.contains(request.value("action").toString())
                     ? 30000
                     : 180000))
        {
            process.kill();
            process.waitForFinished(5000);
            return failure(
                "The adapter stopped without a confirmed result. Use Recover operation before retrying.",
                "indeterminate", "unknown");
        }
    }
    output += process.readAllStandardOutput();
    errors += process.readAllStandardError();
    if (output.size() > 32 * 1024 * 1024)
        return failure("The adapter response exceeded its size limit.", "indeterminate", "unknown");
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(output, &parse);
    auto result = json.toVariant().toMap();
    if (process.exitStatus() != QProcess::NormalExit || parse.error != QJsonParseError::NoError ||
        !json.isObject() || result.value("schema") != responseSchema ||
        result.value("protocolVersion").toInt() != 1 ||
        result.value("adapterId") != "xips-asset-adapter-2142" ||
        result.value("action") != request.value("action") ||
        (!request.value("requestId").toString().isEmpty() &&
         result.value("requestId") != request.value("requestId")))
        return failure(
            "The asset adapter returned no verifiable result. Recover this request before retrying.",
            "indeterminate", "unknown");
    result.insert("processExitCode", process.exitCode());
    return result;
}
QVariantMap XipsAssetSync::execute(const QVariantMap &provided, const QString &action,
                                   const std::shared_ptr<std::atomic_bool> &flag)
{
    const auto path = operationPath(provided);
    if (path.isEmpty())
        return failure("The asset operation has no valid workspace identity.");
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return failure("Cannot retain asset recovery information.");
    QLockFile lock(path + ".lock");
    if (!lock.tryLock(0))
        return failure("This asset operation is already active.", "committing", "pending");
    QString error;
    auto operation = QFileInfo::exists(path) ? readOperation(path, &error) : provided;
    if (!error.isEmpty())
        return failure(error, "indeterminate", "unknown");
    if (QJsonValue::fromVariant(operation.value("request")) !=
            QJsonValue::fromVariant(provided.value("request")) ||
        operationPath(operation) != path)
        return failure("The saved asset request identity changed.", "indeterminate", "unknown");
    if (!EditorFileIdentity::same(operation.value("adapter").toString(), executable()) ||
        QDir::cleanPath(operation.value("stateDirectory").toString()) != QDir::cleanPath(stateDirectory()))
        return failure("The adapter or recovery directory changed. Restore its original configured location "
                       "before recovery.",
                       "indeterminate", "unknown");
    QVariantMap request;
    if (action == "preview")
        request = operation.value("request").toMap();
    else if (QStringList{"commit", "operationStatus", "cancel"}.contains(action))
    {
        request = {
            {"schema", requestSchema}, {"action", action}, {"requestId", operation.value("requestId")}};
        if (action == "commit")
        {
            const auto state = operation.value("lastResponse").toMap().value("state").toString();
            if (state != "previewed" && state != "interrupted")
                return failure("Preview or recover the operation before committing.");
            request.insert("previewId", operation.value("preview").toMap().value("previewId"));
        }
    }
    else
        return failure("Unknown asset operation.");
    if (action == "preview" || action == "commit")
    {
        error = inputsCurrent(operation);
        if (!error.isEmpty())
            return failure(error, "conflict");
        if (cancelled(flag))
            return failure("Asset preparation cancelled.", "cancelled");
    }
    operation.insert("lastAction", action);
    error = saveOperation(path, operation);
    if (!error.isEmpty())
        return failure(error);
    auto result = invoke(operation.value("adapter").toString(), operation.value("stateDirectory").toString(),
                         request, flag);
    operation.insert("lastResponse", result);
    if (result.contains("preview"))
        operation.insert("preview", result.value("preview"));
    const auto state = result.value("state").toString();
    const auto publication = result.value("publication").toString();
    operation.insert("status", state);
    if ((state == "committed" && publication == "published") ||
        (state == "reused" && publication == "reused"))
    {
        const auto receipt = result.value("receipt").toMap();
        QVariantMap saveFailure;
        error = verifyReceipt(operation, receipt);
        if (error.isEmpty())
        {
            auto reference = operation.value("reference").toMap();
            for (const auto *field : {"library", "assetId", "revision", "contentHash", "name", "category"})
                reference.insert(field, receipt.value(field));
            error = XipsHostBridge::recordPublishedVersion(
                operation.value("workspace").toString(), operation.value("expectedReference").toMap(),
                reference, operation.value("requestId").toString(), &saveFailure);
        }
        if (error.isEmpty())
        {
            operation.insert("status", "applied");
            operation.remove("referenceError");
            result.insert("referenceApplied", true);
        }
        else
        {
            operation.insert("status", "published_pending_reference");
            operation.insert("referenceError", error);
            result.insert("referenceError", error);
            if (!saveFailure.isEmpty())
            {
                result.insert("referenceFailure", saveFailure);
                operation.insert("lastReferenceFailure", saveFailure);
            }
        }
    }
    const auto retained = saveOperation(path, operation);
    if (!retained.isEmpty())
        result.insert("recoveryWarning", retained);
    result.insert("operation", operation);
    return result;
}
QVariantList XipsAssetSync::recoverable(const QString &workspace, QString *error)
{
    QVariantList result;
    const auto folder = QDir(workspace).filePath(".zeroslack/asset-operations");
    if (linked(folder))
    {
        if (error)
            *error = "Asset recovery directory contains a linked path.";
        return {};
    }
    for (const auto &entry : QDir(folder).entryInfoList({"*.json"}, QDir::Files, QDir::Time))
    {
        QString issue;
        const auto operation = readOperation(entry.absoluteFilePath(), &issue);
        if (!issue.isEmpty() || operationPath(operation) != entry.absoluteFilePath())
        {
            if (error)
                *error = "An asset recovery record is invalid; it was preserved.";
            continue;
        }
        if (!QStringList{"applied", "cancelled", "failed", "conflict"}.contains(
                operation.value("status").toString()))
            result << operation;
    }
    return result;
}
