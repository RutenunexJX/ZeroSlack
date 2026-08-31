#include "suitecontextcatalog.h"

#include "pinloomcodelinkstore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QUrlQuery>

#include <algorithm>

namespace {

constexpr auto kSchema = "zeroslack.suite-references/v1";

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

QString resolvedPath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return {};
    const QString absolute = QFileInfo(path).absoluteFilePath();
    QFileInfo target(absolute);
    if (target.exists())
        return normalizedPath(absolute);

    QString cursor = absolute;
    QStringList suffix;
    while (!QFileInfo(cursor).exists()) {
        const QFileInfo current(cursor);
        const QString name = current.fileName();
        const QString parent = current.dir().absolutePath();
        if (name.isEmpty() || parent == cursor)
            return normalizedPath(absolute);
        suffix.prepend(name);
        cursor = parent;
    }
    QString base = QFileInfo(cursor).canonicalFilePath();
    if (base.isEmpty())
        base = QFileInfo(cursor).absoluteFilePath();
    for (const QString& part : suffix)
        base = QDir(base).filePath(part);
    return QDir::cleanPath(QDir::fromNativeSeparators(base));
}

QString pathKey(const QString& path)
{
    QString result = normalizedPath(path);
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

bool insideRoot(const QString& root, const QString& path)
{
    const QString rootValue = pathKey(root);
    const QString pathValue = pathKey(path);
    return !rootValue.isEmpty()
        && (pathValue == rootValue
            || pathValue.startsWith(rootValue + QLatin1Char('/')));
}

QString relativePath(const QString& root, const QString& path)
{
    if (!insideRoot(root, path))
        return normalizedPath(path);
    return QDir(normalizedPath(root)).relativeFilePath(
        normalizedPath(path));
}

QString digestId(const QString& value)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        value.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
}

QStringList jsonStrings(const QJsonValue& value, int maximum = 64)
{
    QStringList result;
    if (value.isString())
        result.append(value.toString().trimmed());
    else if (value.isArray()) {
        for (const QJsonValue& item : value.toArray()) {
            if (result.size() >= maximum)
                break;
            if (item.isString() && !item.toString().trimmed().isEmpty())
                result.append(item.toString().trimmed());
        }
    }
    result.removeAll(QString());
    result.removeDuplicates();
    return result;
}

QString boundedText(const QString& value, int maximum = 512)
{
    const QString trimmed = value.trimmed();
    return trimmed.size() <= maximum
        ? trimmed
        : trimmed.left(maximum) + QChar(0x2026);
}

QString pinloomSourceLabel(const PinloomSourceSelection& source)
{
    if (source.anchorKind == PinloomCodeAnchorKind::Symbol
        && !source.symbolName.trimmed().isEmpty()) {
        return boundedText(source.symbolName, 160);
    }
    const QString fileName = QFileInfo(
        source.absoluteFilePath).fileName();
    return boundedText(
        QStringLiteral("%1:%2")
            .arg(fileName.isEmpty()
                     ? QStringLiteral("RTL source") : fileName)
            .arg(qMax(1, source.startLine)),
        160);
}

bool requested(const SuiteContextCatalogRequest& request,
               SuiteContextProvider provider)
{
    if (request.includedProviders.isEmpty())
        return true;
    const QString id = SuiteContextCatalog::providerId(provider);
    for (const QString& included : request.includedProviders) {
        if (included.trimmed().compare(id, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool cancelled(const SuiteContextCatalogRequest& request)
{
    return request.isCancelled && request.isCancelled();
}

void addDiagnostic(SuiteContextSnapshot* snapshot,
                   const QString& provider,
                   const QString& code,
                   const QString& message,
                   const QString& resourceId = {})
{
    if (!snapshot)
        return;
    snapshot->diagnostics.append(
        {provider, code, boundedText(message, 768), resourceId});
}

QString resourceFilePath(const QString& root,
                         const QJsonObject& object,
                         QUrl* normalizedUri)
{
    QString rawFile = object.value(QStringLiteral("file")).toString();
    QUrl uri(object.value(QStringLiteral("uri")).toString(),
             QUrl::StrictMode);
    const bool hasResourceUri = uri.isValid()
        && !uri.scheme().isEmpty();
    if (rawFile.isEmpty() && hasResourceUri) {
        rawFile = QUrlQuery(uri).queryItemValue(
            QStringLiteral("file"), QUrl::FullyDecoded);
    }
    if (rawFile.trimmed().isEmpty())
        return {};

    const QString absolute = resolvedPath(
        QFileInfo(rawFile).isAbsolute()
            ? rawFile
            : QDir(root).absoluteFilePath(rawFile));
    if (normalizedUri) {
        if (hasResourceUri) {
            QUrlQuery query(uri);
            query.removeAllQueryItems(QStringLiteral("file"));
            query.addQueryItem(QStringLiteral("file"), absolute);
            uri.setQuery(query);
        } else {
            uri = QUrl();
        }
        *normalizedUri = uri;
    }
    return absolute;
}

bool validReferenceShape(const QJsonObject& object,
                         QString* failure)
{
    static const QSet<QString> allowed{
        QStringLiteral("id"), QStringLiteral("provider"),
        QStringLiteral("file"), QStringLiteral("uri"),
        QStringLiteral("title"), QStringLiteral("summary"),
        QStringLiteral("objectId"), QStringLiteral("symbols"),
        QStringLiteral("metadata")};
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) {
            if (failure)
                *failure = QStringLiteral("Unknown field '%1'.").arg(it.key());
            return false;
        }
    }
    const auto boundedString = [&object, failure](const QString& key,
                                                   int maximum,
                                                   bool required) {
        if (!object.contains(key)) {
            if (required && failure)
                *failure = QStringLiteral("Missing string field '%1'.").arg(key);
            return !required;
        }
        const QJsonValue value = object.value(key);
        if (!value.isString()
            || value.toString().size() > maximum
            || (required && value.toString().trimmed().isEmpty())) {
            if (failure)
                *failure = QStringLiteral("Field '%1' is not a bounded string.").arg(key);
            return false;
        }
        return true;
    };
    if (!boundedString(QStringLiteral("id"),
                       SuiteContextCatalog::kMaximumReferenceIdLength, true)
        || !boundedString(QStringLiteral("provider"), 16, true)
        || !boundedString(QStringLiteral("file"),
                          SuiteContextCatalog::kMaximumReferencePathLength, true)
        || !boundedString(QStringLiteral("uri"),
                          SuiteContextCatalog::kMaximumReferenceUriLength, false)
        || !boundedString(QStringLiteral("title"), 160, false)
        || !boundedString(QStringLiteral("summary"), 512, false)
        || !boundedString(QStringLiteral("objectId"),
                          SuiteContextCatalog::kMaximumObjectIdLength, false)) {
        return false;
    }
    if (object.contains(QStringLiteral("symbols"))) {
        const QJsonValue value = object.value(QStringLiteral("symbols"));
        if (!value.isArray()
            || value.toArray().size() > SuiteContextCatalog::kMaximumSymbols) {
            if (failure)
                *failure = QStringLiteral("Field 'symbols' is not a bounded array.");
            return false;
        }
        for (const QJsonValue& symbol : value.toArray()) {
            if (!symbol.isString()
                || symbol.toString().size()
                       > SuiteContextCatalog::kMaximumSymbolLength) {
                if (failure)
                    *failure = QStringLiteral("Field 'symbols' contains an invalid item.");
                return false;
            }
        }
    }
    if (object.contains(QStringLiteral("metadata"))
        && !object.value(QStringLiteral("metadata")).isObject()) {
        if (failure)
            *failure = QStringLiteral("Field 'metadata' must be an object.");
        return false;
    }
    return true;
}

QString yamlContent(const QString& line)
{
    bool singleQuoted = false;
    bool doubleQuoted = false;
    for (int index = 0; index < line.size(); ++index) {
        const QChar character = line.at(index);
        if (character == QLatin1Char('\'') && !doubleQuoted)
            singleQuoted = !singleQuoted;
        else if (character == QLatin1Char('"') && !singleQuoted)
            doubleQuoted = !doubleQuoted;
        else if (character == QLatin1Char('#')
                 && !singleQuoted && !doubleQuoted) {
            return line.left(index).trimmed();
        }
    }
    return line.trimmed();
}

bool yamlScalar(const QString& trimmedLine,
                const QString& expectedKey,
                QString* value,
                bool listItem = false)
{
    QString candidate = yamlContent(trimmedLine);
    if (listItem) {
        if (!candidate.startsWith(QLatin1Char('-')))
            return false;
        candidate = candidate.mid(1).trimmed();
    }
    const int separator = candidate.indexOf(QLatin1Char(':'));
    if (separator < 1
        || candidate.left(separator).trimmed() != expectedKey) {
        return false;
    }
    if (value)
        *value = candidate.mid(separator + 1).trimmed();
    return true;
}

QVariantMap waveMetadata(const QString& path,
                         const QString& selectedSymbol,
                         QString* title,
                         QString* summary,
                         QStringList* symbols,
                         QString* failure,
                         const std::function<bool()>& isCancelled)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (failure)
            *failure = file.errorString();
        return {};
    }
    if (file.size() > 1024 * 1024) {
        if (failure) {
            *failure = QStringLiteral(
                "The Wave project exceeds the 1 MiB local-inspection limit.");
        }
        return {};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(
        file.read(1024 * 1024), &error);
    if (error.error != QJsonParseError::NoError
        || !document.isObject()) {
        if (failure)
            *failure = error.errorString();
        return {};
    }
    const QJsonObject root = document.object();
    const QJsonValue schemaValue = root.value(
        QStringLiteral("schemaVersion"));
    const int schemaVersion = schemaValue.toInt(-1);
    if (!schemaValue.isDouble()
        || (schemaVersion != 0 && schemaVersion != 1)
        || root.value(QStringLiteral("projectId"))
               .toString().trimmed().isEmpty()
        || root.value(QStringLiteral("name"))
               .toString().trimmed().isEmpty()) {
        if (failure) {
            *failure = QStringLiteral(
                "The Wave project schema identity is invalid or unsupported.");
        }
        return {};
    }
    if (!root.value(QStringLiteral("scenarios")).isArray()) {
        if (failure) {
            *failure = QStringLiteral(
                "The Wave project has no scenarios array.");
        }
        return {};
    }
    const QString projectName = boundedText(
        root.value(QStringLiteral("name")).toString(), 160);
    if (title && title->isEmpty())
        *title = projectName;
    QVariantList scenarios;
    int scenarioCount = 0;
    int totalLanes = 0;
    bool symbolMatch = false;
    for (const QJsonValue& value :
         root.value(QStringLiteral("scenarios")).toArray()) {
        if (isCancelled && isCancelled())
            return {};
        if (!value.isObject())
            continue;
        const QJsonObject scenario = value.toObject();
        const QJsonArray lanes = scenario.value(
            QStringLiteral("lanes")).toArray();
        ++scenarioCount;
        totalLanes += lanes.size();
        if (scenarios.size() < 24) {
            QVariantMap item{
                {QStringLiteral("id"), boundedText(
                     scenario.value(QStringLiteral("id")).toString(), 160)},
                {QStringLiteral("name"), boundedText(
                     scenario.value(QStringLiteral("name")).toString(), 160)},
                {QStringLiteral("laneCount"), lanes.size()},
                {QStringLiteral("durationTick"), boundedText(
                     scenario.value(QStringLiteral("durationTick"))
                         .toVariant().toString(), 80)}};
            scenarios.append(item);
        }
        for (const QJsonValue& laneValue : lanes) {
            if (isCancelled && isCancelled())
                return {};
            const QJsonObject lane = laneValue.toObject();
            const QString laneName = lane.value(
                QStringLiteral("name")).toString();
            if (symbols && !laneName.trimmed().isEmpty()
                && symbols->size() < 64) {
                symbols->append(laneName.trimmed());
            }
            if (!selectedSymbol.trimmed().isEmpty()
                && laneName.compare(selectedSymbol,
                                    Qt::CaseInsensitive) == 0) {
                symbolMatch = true;
            }
        }
    }
    if (symbols)
        symbols->removeDuplicates();
    if (summary) {
        *summary = QStringLiteral("%1 scenario(s), %2 lane(s)")
            .arg(scenarioCount).arg(totalLanes);
    }
    return {
        {QStringLiteral("projectId"), boundedText(
             root.value(QStringLiteral("projectId")).toString(), 160)},
        {QStringLiteral("schemaVersion"),
         schemaVersion},
        {QStringLiteral("scenarioCount"), scenarioCount},
        {QStringLiteral("laneCount"), totalLanes},
        {QStringLiteral("scenarioDetailOmittedCount"),
         qMax(0, scenarioCount - scenarios.size())},
        {QStringLiteral("symbolMatch"), symbolMatch},
        {QStringLiteral("scenarios"), scenarios},
    };
}

QVariantMap regMapMetadata(const QString& path,
                           const QString& selectedSymbol,
                           QString* title,
                           QString* summary,
                           QStringList* symbols,
                           QString* failure,
                           const std::function<bool()>& isCancelled)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (failure)
            *failure = file.errorString();
        return {};
    }
    if (file.size() > 1024 * 1024) {
        if (failure) {
            *failure = QStringLiteral(
                "The RegMap project exceeds the 1 MiB local-inspection limit.");
        }
        return {};
    }
    const QString text = QString::fromUtf8(file.read(1024 * 1024));
    if (text.trimmed().isEmpty()) {
        if (failure)
            *failure = QStringLiteral("The register-map file is empty.");
        return {};
    }
    const QStringList lines = text.split(QLatin1Char('\n'));
    int schemaVersion = -1;
    bool hasWorkspace = false;
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        QString value;
        if (yamlScalar(trimmed, QStringLiteral("schema_version"),
                       &value)) {
            bool validVersion = false;
            const int parsed = value.toInt(&validVersion);
            if (validVersion)
                schemaVersion = parsed;
        }
        if (yamlScalar(trimmed, QStringLiteral("workspace"), &value)
            && value.isEmpty()) {
            hasWorkspace = true;
        }
    }
    if ((schemaVersion != 1 && schemaVersion != 2)
        || !hasWorkspace) {
        if (failure) {
            *failure = QStringLiteral(
                "The RegMap project schema is invalid or unsupported.");
        }
        return {};
    }

    struct Collection { int indent = 0; QString key; };
    QList<Collection> collections;
    QHash<QString, int> counts;
    QString workspaceName;
    bool symbolMatch = false;
    const QSet<QString> collectionNames{
        QStringLiteral("address_spaces"), QStringLiteral("blocks"),
        QStringLiteral("registers"), QStringLiteral("fields"),
        QStringLiteral("members"), QStringLiteral("enum_values")};

    for (const QString& line : lines) {
        if (isCancelled && isCancelled())
            return {};
        int indent = 0;
        while (indent < line.size() && line.at(indent).isSpace())
            ++indent;
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        QString listId;
        const bool isListId = yamlScalar(
            trimmed, QStringLiteral("id"), &listId, true)
            && !listId.isEmpty();
        while (!collections.isEmpty()
               && indent <= collections.constLast().indent
               && !isListId) {
            collections.removeLast();
        }
        if (trimmed.endsWith(QLatin1Char(':'))) {
            const QString key = trimmed.left(trimmed.size() - 1).trimmed();
            if (collectionNames.contains(key)) {
                while (!collections.isEmpty()
                       && indent <= collections.constLast().indent) {
                    collections.removeLast();
                }
                collections.append({indent, key});
            }
            continue;
        }
        if (isListId && !collections.isEmpty()) {
            counts[collections.constLast().key] += 1;
            const QString id = listId;
            if (!selectedSymbol.isEmpty()
                && id.compare(selectedSymbol, Qt::CaseInsensitive) == 0) {
                symbolMatch = true;
            }
            continue;
        }
        QString name;
        if (yamlScalar(trimmed, QStringLiteral("name"), &name)
            && !name.isEmpty()) {
            if ((name.startsWith(QLatin1Char('"'))
                 && name.endsWith(QLatin1Char('"')))
                || (name.startsWith(QLatin1Char('\''))
                    && name.endsWith(QLatin1Char('\'')))) {
                name = name.mid(1, name.size() - 2);
            }
            if (workspaceName.isEmpty() && indent <= 4)
                workspaceName = name;
            if (symbols && symbols->size() < 64)
                symbols->append(name);
            if (!selectedSymbol.isEmpty()
                && name.compare(selectedSymbol, Qt::CaseInsensitive) == 0) {
                symbolMatch = true;
            }
        }
    }
    if (symbols)
        symbols->removeDuplicates();
    if (title && title->isEmpty())
        *title = workspaceName;
    if (summary) {
        *summary = QStringLiteral("%1 register(s), %2 field(s)")
            .arg(counts.value(QStringLiteral("registers")))
            .arg(counts.value(QStringLiteral("fields"))
                 + counts.value(QStringLiteral("members")));
    }
    return {
        {QStringLiteral("addressSpaceCount"),
         counts.value(QStringLiteral("address_spaces"))},
        {QStringLiteral("blockCount"),
         counts.value(QStringLiteral("blocks"))},
        {QStringLiteral("registerCount"),
         counts.value(QStringLiteral("registers"))},
        {QStringLiteral("fieldCount"),
         counts.value(QStringLiteral("fields"))
             + counts.value(QStringLiteral("members"))},
        {QStringLiteral("enumValueCount"),
         counts.value(QStringLiteral("enum_values"))},
        {QStringLiteral("symbolMatch"), symbolMatch},
    };
}

void appendSource(const SuiteContextCatalogRequest& request,
                  SuiteContextSnapshot* snapshot)
{
    if (!snapshot || !requested(request, SuiteContextProvider::Source))
        return;
    const QString file = normalizedPath(request.filePath);
    if (file.isEmpty())
        return;
    SuiteContextResource resource;
    resource.provider = SuiteContextProvider::Source;
    resource.filePath = file;
    resource.title = request.symbolName.trimmed().isEmpty()
        ? QFileInfo(file).fileName()
        : request.symbolName.trimmed();
    resource.summary = request.symbolName.trimmed().isEmpty()
        ? relativePath(request.workspaceRoot, file)
        : QStringLiteral("%1 · %2")
              .arg(relativePath(request.workspaceRoot, file),
                   request.symbolName.trimmed());
    resource.stableId = QStringLiteral("source:%1:%2")
        .arg(digestId(pathKey(file)), request.symbolName.trimmed());
    QUrl uri(QStringLiteral("zeroslack://source"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("file"), file);
    if (!request.symbolName.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("symbol"),
                           request.symbolName.trimmed());
    uri.setQuery(query);
    resource.uri = uri;
    resource.symbols = {request.symbolName.trimmed()};
    resource.symbols.removeAll(QString());
    snapshot->resources.append(resource);
}

void appendPinloom(const SuiteContextCatalogRequest& request,
                   SuiteContextSnapshot* snapshot)
{
    if (!snapshot || !requested(request, SuiteContextProvider::Pinloom))
        return;
    if (cancelled(request))
        return;
    PinloomCodeLinkStore store;
    store.setWorkspaceRoot(request.workspaceRoot);
    if (!store.loadFailureReason().isEmpty()) {
        addDiagnostic(snapshot, QStringLiteral("pinloom"),
                      QStringLiteral("pinloom_links_invalid"),
                      store.loadFailureReason());
        return;
    }
    QList<ResolvedPinloomCodeLink> resolved;
    if (!request.filePath.trimmed().isEmpty()) {
        const QList<SemanticSymbolRecord>* semanticSymbols =
            request.semanticSymbolsSupplied
            ? &request.semanticSymbols : nullptr;
        if (request.cursorPosition >= 0) {
            resolved = store.linksAtPosition(
                request.filePath, request.documentText,
                request.cursorPosition, nullptr, semanticSymbols);
        }
        if (resolved.isEmpty()) {
            resolved = store.linksForDocument(
                request.filePath, request.documentText,
                nullptr, semanticSymbols);
        }
    }

    const bool hasSourceContext =
        !request.filePath.trimmed().isEmpty();
    const int maximum = qBound(1, request.maxItemsPerProvider, 128);
    int emitted = 0;
    for (const PinloomCodeLinkAnchorRecord& anchor : store.anchors()) {
        if (cancelled(request))
            return;
        const QString relativeSource = QDir::cleanPath(
            QDir::fromNativeSeparators(
                anchor.source.relativeFilePath.trimmed()));
        const bool usableRelative = !relativeSource.isEmpty()
            && relativeSource != QStringLiteral("..")
            && !relativeSource.startsWith(QStringLiteral("../"))
            && !QDir::isAbsolutePath(relativeSource);
        const QString sourceCandidate = usableRelative
            ? QDir(request.workspaceRoot).absoluteFilePath(relativeSource)
            : anchor.source.absoluteFilePath;
        const QString sourcePath = resolvedPath(sourceCandidate);
        if (sourcePath.isEmpty()
            || !insideRoot(request.workspaceRoot, sourcePath)) {
            addDiagnostic(snapshot, QStringLiteral("pinloom"),
                          QStringLiteral("anchor_source_outside_workspace"),
                          QStringLiteral("A Pinloom source anchor resolves outside the active workspace."),
                          anchor.id);
            continue;
        }
        bool selected = !hasSourceContext;
        SuiteContextAvailability availability =
            SuiteContextAvailability::Available;
        for (const ResolvedPinloomCodeLink& item : resolved) {
            if (item.anchor.id != anchor.id)
                continue;
            selected = true;
            if (item.resolution == PinloomCodeLinkResolution::Moved)
                availability = SuiteContextAvailability::Stale;
            else if (item.resolution == PinloomCodeLinkResolution::Ambiguous)
                availability = SuiteContextAvailability::Warning;
            else if (item.resolution == PinloomCodeLinkResolution::Missing)
                availability = SuiteContextAvailability::Missing;
            break;
        }
        if (!selected)
            continue;
        if (!request.symbolName.trimmed().isEmpty()
            && !anchor.source.symbolName.trimmed().isEmpty()
            && anchor.source.symbolName.compare(
                   request.symbolName, Qt::CaseInsensitive) != 0) {
            continue;
        }
        for (const PinloomCodeLinkRecord& link : anchor.links) {
            if (cancelled(request))
                return;
            snapshot->discoveredCounts[QStringLiteral("pinloom")] += 1;
            if (emitted >= maximum)
                continue;
            SuiteContextResource resource;
            resource.provider = SuiteContextProvider::Pinloom;
            resource.availability = availability;
            resource.stableId = QStringLiteral("pinloom:%1:%2")
                .arg(anchor.id, link.id);
            const QString sourceLabel = pinloomSourceLabel(
                anchor.source);
            resource.title = boundedText(
                link.title.isEmpty()
                    ? sourceLabel
                    : link.title, 160);
            resource.summary = QStringLiteral("%1 · %2")
                .arg(sourceLabel,
                     anchor.links.size() == 1
                         ? QStringLiteral("1 target")
                         : QStringLiteral("%1 targets")
                               .arg(anchor.links.size()));
            resource.filePath = sourcePath;
            resource.uri = link.uri;
            resource.symbols = {anchor.source.symbolName};
            resource.symbols.removeAll(QString());
            resource.metadata = {
                {QStringLiteral("anchorId"), anchor.id},
                {QStringLiteral("linkId"), link.id},
                {QStringLiteral("anchorKind"),
                 static_cast<int>(anchor.source.anchorKind)},
                {QStringLiteral("module"), anchor.source.moduleName},
            };
            snapshot->resources.append(resource);
            ++emitted;
        }
    }
}

void appendReferences(const SuiteContextCatalogRequest& request,
                      SuiteContextSnapshot* snapshot)
{
    if (!snapshot)
        return;
    const QString path = SuiteContextCatalog::referenceFilePath(
        request.workspaceRoot);
    if (!QFileInfo::exists(path))
        return;
    if (!insideRoot(request.workspaceRoot, path)) {
        addDiagnostic(snapshot, QStringLiteral("suite"),
                      QStringLiteral("reference_file_outside_workspace"),
                      QStringLiteral("Suite reference storage resolves outside the workspace."));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        addDiagnostic(snapshot, QStringLiteral("suite"),
                      QStringLiteral("reference_file_unreadable"),
                      file.errorString());
        return;
    }
    if (file.size() > SuiteContextCatalog::kMaximumManifestBytes) {
        addDiagnostic(snapshot, QStringLiteral("suite"),
                      QStringLiteral("reference_file_too_large"),
                      QStringLiteral("Suite references exceed the 256 KiB inspection limit."));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        file.read(SuiteContextCatalog::kMaximumManifestBytes), &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        addDiagnostic(snapshot, QStringLiteral("suite"),
                      QStringLiteral("reference_file_invalid"),
                      parseError.errorString());
        return;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toString()
        != QString::fromLatin1(kSchema)) {
        addDiagnostic(snapshot, QStringLiteral("suite"),
                      QStringLiteral("reference_schema_unsupported"),
                      QStringLiteral("Expected schema %1.")
                          .arg(QString::fromLatin1(kSchema)));
        return;
    }
    if (!root.value(QStringLiteral("resources")).isArray()) {
        addDiagnostic(snapshot, QStringLiteral("suite"),
                      QStringLiteral("reference_resources_invalid"),
                      QStringLiteral("Suite reference resources must be an array."));
        return;
    }

    QHash<QString, int> emitted;
    QSet<QString> stableKeys;
    const int maximum = qBound(1, request.maxItemsPerProvider, 128);
    const QJsonArray references = root.value(
        QStringLiteral("resources")).toArray();
    if (references.size() > SuiteContextCatalog::kMaximumReferences) {
        addDiagnostic(snapshot, QStringLiteral("suite"),
                      QStringLiteral("reference_limit_exceeded"),
                      QStringLiteral("Only the first 512 suite references are inspected."));
        snapshot->catalogOmittedCount +=
            references.size() - SuiteContextCatalog::kMaximumReferences;
        for (int tail = SuiteContextCatalog::kMaximumReferences;
             tail < references.size(); ++tail) {
            const QJsonObject object = references.at(tail).toObject();
            SuiteContextProvider provider;
            const QString rawProvider = object.value(
                QStringLiteral("provider")).toString().trimmed();
            const QString providerText = rawProvider.toLower();
            if (rawProvider == providerText
                && SuiteContextCatalog::providerFromId(providerText, &provider)
                && (provider == SuiteContextProvider::Wave
                    || provider == SuiteContextProvider::RegMap)
                && requested(request, provider)) {
                snapshot->discoveredCounts[providerText] += 1;
            }
        }
    }
    int index = 0;
    for (const QJsonValue& value : references) {
        if (cancelled(request))
            return;
        ++index;
        if (index > SuiteContextCatalog::kMaximumReferences)
            break;
        if (!value.isObject()) {
            addDiagnostic(snapshot, QStringLiteral("suite"),
                          QStringLiteral("invalid_reference"),
                          QStringLiteral("Reference %1 is not an object.")
                              .arg(index));
            continue;
        }
        const QJsonObject object = value.toObject();
        QString shapeFailure;
        if (!validReferenceShape(object, &shapeFailure)) {
            addDiagnostic(snapshot, QStringLiteral("suite"),
                          QStringLiteral("invalid_reference"),
                          QStringLiteral("Reference %1 is invalid: %2")
                              .arg(index).arg(shapeFailure));
            continue;
        }
        SuiteContextProvider provider;
        const QString rawProvider = object.value(
            QStringLiteral("provider")).toString().trimmed();
        const QString providerText = rawProvider.toLower();
        if (!SuiteContextCatalog::providerFromId(providerText, &provider)
            || rawProvider != providerText
            || (provider != SuiteContextProvider::Wave
                && provider != SuiteContextProvider::RegMap)) {
            addDiagnostic(snapshot, providerText,
                          QStringLiteral("invalid_reference_provider"),
                          QStringLiteral("Reference %1 must use wave or regmap.")
                              .arg(index));
            continue;
        }
        if (!requested(request, provider)) {
            continue;
        }
        snapshot->discoveredCounts[providerText] += 1;
        QUrl uri;
        const QString encodedUri = object.value(
            QStringLiteral("uri")).toString();
        if (!encodedUri.isEmpty()) {
            const QUrl suppliedUri(encodedUri, QUrl::StrictMode);
            if (!suppliedUri.isValid() || suppliedUri.scheme().isEmpty()) {
                addDiagnostic(snapshot, providerText,
                              QStringLiteral("invalid_reference_uri"),
                              QStringLiteral("Reference %1 has an invalid URI.")
                                  .arg(index));
                continue;
            }
        }
        const QString resourcePath = resourceFilePath(
            request.workspaceRoot, object, &uri);
        const QString id = object.value(QStringLiteral("id"))
            .toString().trimmed();
        if (resourcePath.isEmpty() || id.isEmpty()
            || id.size() > SuiteContextCatalog::kMaximumReferenceIdLength
            || encodedUri.size()
                   > SuiteContextCatalog::kMaximumReferenceUriLength) {
            addDiagnostic(snapshot, providerText,
                          QStringLiteral("invalid_reference"),
                          QStringLiteral("Reference %1 requires bounded id, file, and URI values.")
                              .arg(index), id);
            continue;
        }
        if (!insideRoot(request.workspaceRoot, resourcePath)) {
            addDiagnostic(snapshot, providerText,
                          QStringLiteral("resource_outside_workspace"),
                          QStringLiteral("Referenced files must remain inside the workspace."),
                          id);
            continue;
        }
        if (uri.isEmpty()) {
            uri = QUrl(provider == SuiteContextProvider::Wave
                           ? QStringLiteral("wave://project")
                           : QStringLiteral("regmap://project"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("file"), resourcePath);
            const QString objectId = object.value(
                QStringLiteral("objectId")).toString().trimmed();
            if (!objectId.isEmpty())
                query.addQueryItem(QStringLiteral("object"), objectId);
            uri.setQuery(query);
        }
        const bool validScheme = provider == SuiteContextProvider::Wave
            ? uri.scheme().compare(QStringLiteral("wave"),
                                   Qt::CaseInsensitive) == 0
              && uri.host().compare(QStringLiteral("project"),
                                    Qt::CaseInsensitive) == 0
            : uri.scheme().compare(QStringLiteral("regmap"),
                                   Qt::CaseInsensitive) == 0
              && (uri.host().compare(QStringLiteral("project"),
                                     Qt::CaseInsensitive) == 0
                  || uri.host().compare(QStringLiteral("register"),
                                        Qt::CaseInsensitive) == 0);
        if (!validScheme) {
            addDiagnostic(snapshot, providerText,
                          QStringLiteral("invalid_reference_uri"),
                          QStringLiteral("The resource URI does not match its provider."),
                          id);
            continue;
        }
        snapshot->revisionFiles.insert(
            resourcePath, SuiteContextCatalog::kMaximumProjectBytes);
        if (emitted.value(providerText) >= maximum)
            continue;

        SuiteContextResource resource;
        resource.provider = provider;
        resource.stableId = QStringLiteral("%1:%2")
            .arg(providerText, id);
        resource.title = boundedText(object.value(
            QStringLiteral("title")).toString(), 160);
        resource.summary = boundedText(object.value(
            QStringLiteral("summary")).toString(), 512);
        resource.filePath = resourcePath;
        resource.uri = uri;
        resource.symbols = jsonStrings(
            object.value(QStringLiteral("symbols")));
        resource.metadata = object.value(
            QStringLiteral("metadata")).toObject().toVariantMap();
        resource.metadata.insert(QStringLiteral("referenceId"), id);
        resource.metadata.insert(QStringLiteral("relativePath"),
                                 relativePath(request.workspaceRoot,
                                              resourcePath));
        if (!QFileInfo(resourcePath).isFile()) {
            resource.availability = SuiteContextAvailability::Missing;
            addDiagnostic(snapshot, providerText,
                          QStringLiteral("resource_missing"),
                          QStringLiteral("The referenced project file does not exist."),
                          id);
        } else {
            QString parseFailure;
            QStringList discoveredSymbols;
            QVariantMap parsed = provider == SuiteContextProvider::Wave
                ? waveMetadata(resourcePath, request.symbolName,
                               &resource.title, &resource.summary,
                               &discoveredSymbols, &parseFailure,
                               request.isCancelled)
                : regMapMetadata(resourcePath, request.symbolName,
                                 &resource.title, &resource.summary,
                                 &discoveredSymbols, &parseFailure,
                                 request.isCancelled);
            if (cancelled(request))
                return;
            resource.symbols.append(discoveredSymbols);
            resource.symbols.removeDuplicates();
            for (auto it = parsed.cbegin(); it != parsed.cend(); ++it)
                resource.metadata.insert(it.key(), it.value());
            snapshot->catalogOmittedCount += parsed.value(
                QStringLiteral("scenarioDetailOmittedCount")).toInt();
            if (!parseFailure.isEmpty()) {
                resource.availability = SuiteContextAvailability::Warning;
                addDiagnostic(snapshot, providerText,
                              QStringLiteral("resource_parse_warning"),
                              parseFailure, id);
            } else {
                resource.availability = SuiteContextAvailability::Stale;
                resource.metadata.insert(
                    QStringLiteral("localInspection"),
                    QStringLiteral("heuristic"));
            }
        }
        if (resource.title.isEmpty())
            resource.title = QFileInfo(resourcePath).completeBaseName();
        if (!stableKeys.contains(resource.stableKey())) {
            stableKeys.insert(resource.stableKey());
            snapshot->resources.append(resource);
            emitted[providerText] += 1;
        }
    }
}

} // namespace

QJsonObject SuiteContextDiagnostic::toJson() const
{
    QJsonObject result{
        {QStringLiteral("provider"), providerId},
        {QStringLiteral("code"), code},
        {QStringLiteral("message"), message},
    };
    if (!resourceId.isEmpty())
        result.insert(QStringLiteral("resourceId"), resourceId);
    return result;
}

bool SuiteContextResource::isValid() const
{
    return !stableId.trimmed().isEmpty()
        && !providerId().isEmpty();
}

QString SuiteContextResource::providerId() const
{
    return SuiteContextCatalog::providerId(provider);
}

QString SuiteContextResource::stableKey() const
{
    return providerId() + QLatin1Char(':') + stableId;
}

QJsonObject SuiteContextResource::toJson(
    const QString& workspaceRoot) const
{
    QJsonArray symbolValues;
    for (const QString& symbol : symbols)
        symbolValues.append(symbol);
    QJsonObject result{
        {QStringLiteral("id"), stableId},
        {QStringLiteral("provider"), providerId()},
        {QStringLiteral("availability"),
         SuiteContextCatalog::availabilityId(availability)},
        {QStringLiteral("title"), title},
        {QStringLiteral("summary"), summary},
        {QStringLiteral("uri"), uri.toString(QUrl::FullyEncoded)},
        {QStringLiteral("symbols"), symbolValues},
        {QStringLiteral("metadata"), QJsonObject::fromVariantMap(metadata)},
    };
    if (!filePath.isEmpty()) {
        result.insert(QStringLiteral("file"),
                      workspaceRoot.isEmpty()
                          ? filePath
                          : relativePath(workspaceRoot, filePath));
    }
    return result;
}

QList<SuiteContextResource> SuiteContextSnapshot::resourcesFor(
    SuiteContextProvider provider) const
{
    QList<SuiteContextResource> result;
    for (const SuiteContextResource& resource : resources) {
        if (resource.provider == provider)
            result.append(resource);
    }
    return result;
}

SuiteContextSnapshot SuiteContextCatalog::inspect(
    const SuiteContextCatalogRequest& request)
{
    SuiteContextSnapshot snapshot;
    snapshot.workspaceRoot = normalizedPath(request.workspaceRoot);
    if (snapshot.workspaceRoot.isEmpty()
        || !QFileInfo(snapshot.workspaceRoot).isDir()) {
        addDiagnostic(&snapshot, QStringLiteral("suite"),
                      QStringLiteral("workspace_unavailable"),
                      QStringLiteral("The workspace root is unavailable."));
        return snapshot;
    }
    SuiteContextCatalogRequest normalized = request;
    normalized.workspaceRoot = snapshot.workspaceRoot;
    normalized.maxItemsPerProvider = qBound(
        1, request.maxItemsPerProvider, 128);
    appendSource(normalized, &snapshot);
    if (cancelled(normalized))
        return snapshot;
    appendPinloom(normalized, &snapshot);
    if (cancelled(normalized))
        return snapshot;
    if (requested(normalized, SuiteContextProvider::Wave)
        || requested(normalized, SuiteContextProvider::RegMap)) {
        appendReferences(normalized, &snapshot);
    }

    std::stable_sort(
        snapshot.resources.begin(), snapshot.resources.end(),
        [](const SuiteContextResource& lhs,
           const SuiteContextResource& rhs) {
            if (lhs.provider != rhs.provider)
                return static_cast<int>(lhs.provider)
                    < static_cast<int>(rhs.provider);
            const bool lhsMatch = lhs.metadata.value(
                QStringLiteral("symbolMatch")).toBool();
            const bool rhsMatch = rhs.metadata.value(
                QStringLiteral("symbolMatch")).toBool();
            if (lhsMatch != rhsMatch)
                return lhsMatch;
            const int titleOrder = QString::compare(
                lhs.title, rhs.title, Qt::CaseInsensitive);
            return titleOrder == 0
                ? lhs.stableKey() < rhs.stableKey()
                : titleOrder < 0;
        });
    return snapshot;
}

QString SuiteContextCatalog::referenceFilePath(
    const QString& workspaceRoot)
{
    if (workspaceRoot.trimmed().isEmpty())
        return {};
    return QDir(normalizedPath(workspaceRoot)).filePath(
        QStringLiteral(".zeroslack/suite-references.json"));
}

QString SuiteContextCatalog::providerId(
    SuiteContextProvider provider)
{
    switch (provider) {
    case SuiteContextProvider::Source: return QStringLiteral("source");
    case SuiteContextProvider::Pinloom: return QStringLiteral("pinloom");
    case SuiteContextProvider::Wave: return QStringLiteral("wave");
    case SuiteContextProvider::RegMap: return QStringLiteral("regmap");
    }
    return {};
}

QString SuiteContextCatalog::availabilityId(
    SuiteContextAvailability availability)
{
    switch (availability) {
    case SuiteContextAvailability::Available:
        return QStringLiteral("available");
    case SuiteContextAvailability::Stale:
        return QStringLiteral("stale");
    case SuiteContextAvailability::Warning:
        return QStringLiteral("warning");
    case SuiteContextAvailability::Missing:
        return QStringLiteral("missing");
    case SuiteContextAvailability::Unavailable:
        return QStringLiteral("unavailable");
    }
    return {};
}

bool SuiteContextCatalog::providerFromId(
    const QString& id,
    SuiteContextProvider* provider)
{
    if (!provider)
        return false;
    const QString normalized = id.trimmed().toLower();
    if (normalized == QStringLiteral("source"))
        *provider = SuiteContextProvider::Source;
    else if (normalized == QStringLiteral("pinloom"))
        *provider = SuiteContextProvider::Pinloom;
    else if (normalized == QStringLiteral("wave"))
        *provider = SuiteContextProvider::Wave;
    else if (normalized == QStringLiteral("regmap"))
        *provider = SuiteContextProvider::RegMap;
    else
        return false;
    return true;
}
