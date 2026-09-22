#include "workspacehubsession.h"

#include "editorlocation.h"
#include "liveinsightscontextprovider.h"
#include "pinloomcontextprovider.h"
#include "suitecontextcatalog.h"
#include "temporaryeditorcontextprovider.h"
#include "workspacehubsuitebridge.h"

#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QtConcurrent>

#include <algorithm>
#include <exception>
#include <utility>

namespace {

WorkspaceHubItemState hubState(SuiteContextAvailability state)
{
    switch (state) {
    case SuiteContextAvailability::Available:
        return WorkspaceHubItemState::Available;
    case SuiteContextAvailability::Stale:
        return WorkspaceHubItemState::Stale;
    case SuiteContextAvailability::Warning:
        return WorkspaceHubItemState::Warning;
    case SuiteContextAvailability::Missing:
        return WorkspaceHubItemState::Missing;
    case SuiteContextAvailability::Unavailable:
        return WorkspaceHubItemState::Unavailable;
    }
    return WorkspaceHubItemState::Unavailable;
}

QString insightTitle(LiveInsightKind kind)
{
    switch (kind) {
    case LiveInsightKind::Kernel:
        return QStringLiteral("Signal Kernel Graph");
    case LiveInsightKind::Module:
        return QStringLiteral("Module Block Diagram");
    case LiveInsightKind::Hotspot:
        return QStringLiteral("Signal Hotspot");
    case LiveInsightKind::State:
        return QStringLiteral("State Transition Graph");
    }
    return {};
}

QString insightSummary(LiveInsightKind kind,
                       const WorkspaceHubRequest& request)
{
    if (kind == LiveInsightKind::Module
        || kind == LiveInsightKind::State) {
        return request.moduleName.trimmed().isEmpty()
            ? QStringLiteral("Follow the active RTL scope")
            : request.moduleName;
    }
    return request.symbolName.trimmed().isEmpty()
        ? QStringLiteral("Follow the active RTL selection")
        : request.symbolName;
}

WorkspaceHubSection makeSection(const QString& id,
                                const QString& title,
                                const QString& emptyText)
{
    WorkspaceHubSection section;
    section.id = id;
    section.title = title;
    section.statusText = emptyText;
    return section;
}

WorkspaceHubSection* sectionNamed(
    QList<WorkspaceHubSection>* sections,
    const QString& id)
{
    if (!sections)
        return nullptr;
    for (WorkspaceHubSection& section : *sections) {
        if (section.id == id)
            return &section;
    }
    return nullptr;
}

QString workspaceRootKey(const QString& root)
{
    QString key = QDir::cleanPath(QDir::fromNativeSeparators(
        QFileInfo(root).absoluteFilePath()));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

int statePriority(WorkspaceHubItemState state)
{
    switch (state) {
    case WorkspaceHubItemState::Available: return 0;
    case WorkspaceHubItemState::Stale: return 1;
    case WorkspaceHubItemState::Warning: return 2;
    case WorkspaceHubItemState::Missing: return 3;
    case WorkspaceHubItemState::Unavailable: return 4;
    }
    return 0;
}

WorkspaceHubItemState providerFailureState(const QString& errorCode)
{
    const QString code = errorCode.trimmed().toLower();
    if (code.contains(QStringLiteral("not_found"))
        || code.contains(QStringLiteral("missing"))
        || code.contains(QStringLiteral("deleted"))) {
        return WorkspaceHubItemState::Missing;
    }
    if (code.contains(QStringLiteral("runtime"))
        || code.contains(QStringLiteral("transport"))
        || code.contains(QStringLiteral("timeout"))
        || code.contains(QStringLiteral("provider_unavailable"))) {
        return WorkspaceHubItemState::Unavailable;
    }
    return WorkspaceHubItemState::Warning;
}

QString providerFailureSummary(WorkspaceHubItemState state)
{
    switch (state) {
    case WorkspaceHubItemState::Missing:
        return QStringLiteral("Provider no longer contains this resource");
    case WorkspaceHubItemState::Unavailable:
        return QStringLiteral("Provider connection is unavailable");
    case WorkspaceHubItemState::Warning:
        return QStringLiteral("Provider could not verify this resource");
    default:
        return {};
    }
}

QString surfacePreview(const QString& provider,
                       const QJsonObject& descriptor)
{
    const QJsonObject model = descriptor.value(
        QStringLiteral("model")).toObject();
    QStringList rows;
    if (provider == QStringLiteral("wave")) {
        const QJsonArray scenarios = model.value(
            QStringLiteral("scenarios")).toArray();
        rows.append(QStringLiteral("%1 scenarios · %2 imported traces")
                        .arg(scenarios.size())
                        .arg(model.value(
                            QStringLiteral("traceCount")).toInt()));
        for (int index = 0; index < qMin(3, scenarios.size()); ++index) {
            const QJsonObject scenario = scenarios.at(index).toObject();
            QString name = scenario.value(
                QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty()) {
                name = scenario.value(
                    QStringLiteral("id")).toString().trimmed();
            }
            rows.append(QStringLiteral("%1 — %2 lanes")
                            .arg(name.left(160))
                            .arg(scenario.value(
                                QStringLiteral("laneCount")).toInt()));
        }
    } else if (provider == QStringLiteral("regmap")) {
        const QJsonObject object = model.value(
            QStringLiteral("object")).toObject();
        const QString kind = object.value(
            QStringLiteral("kind")).toString().trimmed();
        const QString name = object.value(
            QStringLiteral("name")).toString().trimmed();
        if (!kind.isEmpty() || !name.isEmpty()) {
            rows.append(QStringLiteral("%1 %2")
                            .arg(kind, name).trimmed().left(320));
        }
        rows.append(QStringLiteral("%1 registers · %2 fields · %3")
                        .arg(model.value(
                            QStringLiteral("registerCount")).toInt())
                        .arg(model.value(
                            QStringLiteral("fieldCount")).toInt())
                        .arg(model.value(QStringLiteral("valid")).toBool()
                                 ? QStringLiteral("valid")
                                 : QStringLiteral("validation issues")));
    }
    return rows.join(QLatin1Char('\n')).left(768);
}

} // namespace

WorkspaceHubSession::WorkspaceHubSession(QObject* parent)
    : QObject(parent)
    , builderValue(buildDefaultSnapshot)
{
    qRegisterMetaType<WorkspaceHubSnapshot>();
    debounceTimer.setSingleShot(true);
    debounceTimer.setInterval(140);
    connect(&debounceTimer, &QTimer::timeout,
            this, &WorkspaceHubSession::startPendingRequest);
}

WorkspaceHubSession::~WorkspaceHubSession()
{
    if (cancellation)
        cancellation->store(true);
}

WorkspaceHubSnapshot WorkspaceHubSession::snapshot() const
{
    return currentSnapshot;
}

quint64 WorkspaceHubSession::requestedGeneration() const
{
    return generationValue;
}

QString WorkspaceHubSession::requestedKey() const
{
    return requestKeyValue;
}

void WorkspaceHubSession::requestUpdate(
    const WorkspaceHubRequest& request)
{
    if (!request.isValid()) {
        clear();
        return;
    }
    const QString key = request.stableKey();
    if (key == requestKeyValue
        && (currentSnapshot.phase == WorkspaceHubPhase::Ready
            || currentSnapshot.phase == WorkspaceHubPhase::Updating)) {
        return;
    }
    if (cancellation)
        cancellation->store(true);
    const QString newWorkspaceRootKey = workspaceRootKey(
        request.workspaceRoot);
    if (newWorkspaceRootKey != workspaceRootKeyValue)
        currentSnapshot = {};
    cancellation = std::make_shared<std::atomic_bool>(false);
    pendingRequest = request;
    requestKeyValue = key;
    workspaceRootKeyValue = newWorkspaceRootKey;
    ++generationValue;

    WorkspaceHubSnapshot updating = currentSnapshot;
    updating.generation = generationValue;
    updating.requestKey = requestKeyValue;
    updating.phase = WorkspaceHubPhase::Updating;
    updating.failureReason.clear();
    updating.stale = !updating.sections.isEmpty();
    publish(updating);
    debounceTimer.start();
}

void WorkspaceHubSession::clear()
{
    debounceTimer.stop();
    if (cancellation)
        cancellation->store(true);
    cancellation.reset();
    pendingRequest = {};
    requestKeyValue.clear();
    workspaceRootKeyValue.clear();
    ++generationValue;
    WorkspaceHubSnapshot cleared;
    cleared.generation = generationValue;
    publish(cleared);
}

void WorkspaceHubSession::setBuilder(Builder builder)
{
    builderValue = builder ? std::move(builder) : buildDefaultSnapshot;
}

void WorkspaceHubSession::setSemanticCapture(
    SemanticCapture capture)
{
    semanticCapture = std::move(capture);
}

void WorkspaceHubSession::setDebounceIntervalForTesting(int milliseconds)
{
    debounceTimer.setInterval(qMax(0, milliseconds));
}

void WorkspaceHubSession::startPendingRequest()
{
    WorkspaceHubRequest request = pendingRequest;
    const quint64 generation = generationValue;
    const QString key = requestKeyValue;
    const CancellationFlag flag = cancellation;
    const Builder builder = builderValue;
    if (semanticCapture)
        semanticCapture(&request);
    if (!flag || flag->load()
        || generation != generationValue
        || key != requestKeyValue) {
        return;
    }
    auto* watcher = new QFutureWatcher<WorkspaceHubSnapshot>(this);
    connect(watcher, &QFutureWatcherBase::finished,
            this,
            [this, watcher, generation, key, flag]() {
                const WorkspaceHubSnapshot result = watcher->result();
                watcher->deleteLater();
                if (!flag || flag->load()
                    || generation != generationValue
                    || key != requestKeyValue
                    || !result.isCurrent(generation, key)) {
                    return;
                }
                publish(result);
            });
    watcher->setFuture(QtConcurrent::run(
        [request, generation, key, flag, builder]() {
            WorkspaceHubSnapshot result;
            try {
                result = builder(request, generation, flag);
            } catch (const std::exception& error) {
                result.phase = WorkspaceHubPhase::Error;
                result.failureReason = QString::fromUtf8(error.what())
                    .trimmed().left(768);
                if (result.failureReason.isEmpty()) {
                    result.failureReason = QStringLiteral(
                        "Workspace context construction failed.");
                }
            } catch (...) {
                result.phase = WorkspaceHubPhase::Error;
                result.failureReason = QStringLiteral(
                    "Workspace context construction failed.");
            }
            result.generation = generation;
            result.requestKey = key;
            return result;
        }));
}

void WorkspaceHubSession::publish(
    const WorkspaceHubSnapshot& snapshotValue)
{
    currentSnapshot = snapshotValue;
    emit snapshotChanged(currentSnapshot);
}

WorkspaceHubSnapshot WorkspaceHubSession::buildDefaultSnapshot(
    const WorkspaceHubRequest& request,
    quint64 generation,
    const CancellationFlag& cancellationFlag)
{
    WorkspaceHubSnapshot result;
    result.generation = generation;
    result.requestKey = request.stableKey();
    result.phase = WorkspaceHubPhase::Ready;
    result.sections = {
        makeSection(QStringLiteral("source"), QStringLiteral("Source"),
                    QStringLiteral("Open an RTL file to populate this section.")),
        makeSection(QStringLiteral("pinloom"), QStringLiteral("Pinloom"),
                    QStringLiteral("No Pinloom binding matches this source context.")),
        makeSection(QStringLiteral("wave"), QStringLiteral("Wave"),
                    QStringLiteral("No explicit Wave reference is registered.")),
        makeSection(QStringLiteral("regmap"), QStringLiteral("RegMap"),
                    QStringLiteral("No explicit RegMap reference is registered.")),
    };
    if (cancellationFlag && cancellationFlag->load())
        return result;

    WorkspaceHubSection* source = sectionNamed(
        &result.sections, QStringLiteral("source"));
    if (source && !request.filePath.trimmed().isEmpty()) {
        EditorLocation location;
        location.documentId = request.documentId;
        location.filePath = request.filePath;
        location.line = qMax(1, request.line);
        location.column = qMax(1, request.column);
        WorkspaceHubItem current;
        current.stableKey = QStringLiteral("source:current");
        current.providerId = QStringLiteral("source");
        current.title = request.symbolName.trimmed().isEmpty()
            ? QStringLiteral("Current source")
            : request.symbolName.trimmed();
        current.summary = QStringLiteral("%1:%2")
            .arg(request.filePath).arg(location.line);
        current.iconKey = QStringLiteral("document-edit");
        current.contextResource =
            TemporaryEditorContextProvider::resourceForLocation(
                location, request.workspaceRoot);
        source->items.append(current);

        for (LiveInsightKind kind : {
                 LiveInsightKind::Kernel, LiveInsightKind::Module,
                 LiveInsightKind::State, LiveInsightKind::Hotspot}) {
            WorkspaceHubItem insight;
            insight.stableKey = QStringLiteral("insight:%1")
                .arg(liveInsightKindId(kind));
            insight.providerId = QStringLiteral("source");
            insight.title = insightTitle(kind);
            insight.summary = insightSummary(kind, request);
            insight.iconKey =
                LiveInsightsContextProvider::iconKeyForKind(kind);
            insight.contextResource =
                LiveInsightsContextProvider::resourceForKind(
                    kind, request.workspaceRoot);
            source->items.append(insight);
        }
        source->statusText = QStringLiteral("%1 source tools")
            .arg(source->items.size());
    }
    if (cancellationFlag && cancellationFlag->load())
        return result;

    SuiteContextCatalogRequest catalogRequest;
    catalogRequest.workspaceRoot = request.workspaceRoot;
    catalogRequest.filePath = request.filePath;
    catalogRequest.documentText = request.documentText;
    catalogRequest.symbolName = request.symbolName;
    catalogRequest.cursorPosition = request.cursorPosition;
    catalogRequest.maxItemsPerProvider = 48;
    catalogRequest.semanticSymbols = request.semanticSymbols;
    catalogRequest.semanticSymbolsSupplied =
        request.semanticSymbolsSupplied;
    catalogRequest.isCancelled = [cancellationFlag]() {
        return cancellationFlag && cancellationFlag->load();
    };
    const SuiteContextSnapshot suite =
        SuiteContextCatalog::inspect(catalogRequest);
    if (cancellationFlag && cancellationFlag->load())
        return result;

    const QList<SuiteContextResource>& catalogResources = suite.resources;
    const bool hasExternalResource = std::any_of(
        catalogResources.cbegin(), catalogResources.cend(),
        [](const SuiteContextResource& resource) {
            return resource.provider == SuiteContextProvider::Wave
                || resource.provider == SuiteContextProvider::RegMap;
        });
    const WorkspaceHubSuiteRegistry registry = hasExternalResource
        ? WorkspaceHubSuiteBridge::registry(500)
        : WorkspaceHubSuiteRegistry{};
    if (cancellationFlag && cancellationFlag->load())
        return result;
    QSet<QString> unavailableProviders;
    QList<SuiteContextDiagnostic> providerDiagnostics;
    QElapsedTimer verificationTimer;
    verificationTimer.start();
    constexpr int kVerificationDeadlineMs = 1600;
    constexpr int kMaximumVerifiedItems = 8;
    int verifiedItems = 0;
    for (const SuiteContextResource& resource : catalogResources) {
        if (cancellationFlag && cancellationFlag->load())
            return result;
        if (resource.provider == SuiteContextProvider::Source)
            continue;
        WorkspaceHubSection* section = sectionNamed(
            &result.sections, resource.providerId());
        if (!section)
            continue;
        WorkspaceHubItem item;
        item.stableKey = resource.stableKey();
        item.providerId = resource.providerId();
        item.title = resource.title;
        item.summary = resource.summary;
        item.iconKey = resource.provider == SuiteContextProvider::Pinloom
            ? QStringLiteral("bookmark-new")
            : resource.provider == SuiteContextProvider::Wave
                ? QStringLiteral("wave")
                : QStringLiteral("register-map");
        item.state = hubState(resource.availability);
        item.metadata = resource.metadata;
        item.metadata.insert(QStringLiteral("filePath"), resource.filePath);
        if (resource.provider == SuiteContextProvider::Pinloom)
            item.contextResource = PinloomContextProvider::resourceForUri(
                resource.uri, request.workspaceRoot);
        else
            item.suiteUri = resource.uri;
        if ((resource.provider == SuiteContextProvider::Wave
             || resource.provider == SuiteContextProvider::RegMap)
            && item.state != WorkspaceHubItemState::Missing
            && item.state != WorkspaceHubItemState::Unavailable) {
            if (!registry.ok
                || !registry.contains(resource.providerId())) {
                item.state = WorkspaceHubItemState::Unavailable;
                item.metadata.insert(
                    QStringLiteral("providerErrorCode"),
                    registry.ok
                        ? QStringLiteral("provider_unavailable")
                        : registry.errorCode);
                unavailableProviders.insert(resource.providerId());
            } else if (verifiedItems >= kMaximumVerifiedItems
                       || verificationTimer.elapsed()
                              >= kVerificationDeadlineMs) {
                item.state = WorkspaceHubItemState::Stale;
                item.metadata.insert(
                    QStringLiteral("providerVerification"),
                    QStringLiteral("deferred"));
            } else {
                const int remaining = kVerificationDeadlineMs
                    - static_cast<int>(verificationTimer.elapsed());
                const WorkspaceHubSuiteResult resolution =
                    WorkspaceHubSuiteBridge::describeSurface(
                        item, qBound(100, remaining, 500));
                ++verifiedItems;
                if (resolution.ok) {
                    item.state = WorkspaceHubItemState::Available;
                    item.metadata.insert(
                        QStringLiteral("providerVerification"),
                        QStringLiteral("verified"));
                    item.metadata.insert(
                        QStringLiteral("surfaceId"),
                        resolution.model.value(
                            QStringLiteral("surfaceId")).toString().left(160));
                    item.metadata.insert(
                        QStringLiteral("surfaceMode"),
                        resolution.model.value(
                            QStringLiteral("mode")).toString().left(80));
                    item.metadata.insert(
                        QStringLiteral("surfaceFallback"),
                        resolution.model.value(
                            QStringLiteral("fallback")).toString().left(80));
                    item.metadata.insert(
                        QStringLiteral("surfacePreview"),
                        surfacePreview(resource.providerId(),
                                       resolution.model));
                } else {
                    item.state = providerFailureState(
                        resolution.errorCode);
                    item.metadata.insert(
                        QStringLiteral("providerErrorCode"),
                        resolution.errorCode.left(160));
                    item.metadata.insert(
                        QStringLiteral("providerErrorMessage"),
                        resolution.errorMessage.left(512));
                    const QString stateSummary =
                        providerFailureSummary(item.state);
                    if (!stateSummary.isEmpty()) {
                        item.summary = item.summary.trimmed().isEmpty()
                            ? stateSummary
                            : QStringLiteral("%1 · %2")
                                  .arg(item.summary, stateSummary);
                    }
                    providerDiagnostics.append({
                        resource.providerId(),
                        resolution.errorCode.isEmpty()
                            ? QStringLiteral("provider_resolve_failed")
                            : resolution.errorCode.left(160),
                        resolution.errorMessage.isEmpty()
                            ? stateSummary
                            : resolution.errorMessage.left(768),
                        resource.stableId});
                }
            }
        }
        if (item.isValid())
            section->items.append(item);
    }
    for (WorkspaceHubSection& section : result.sections) {
        if (!section.items.isEmpty()) {
            section.statusText = QStringLiteral("%1 item(s)")
                .arg(section.items.size());
            for (const WorkspaceHubItem& item : section.items) {
                if (statePriority(item.state)
                    > statePriority(section.state)) {
                    section.state = item.state;
                }
            }
        }
    }
    QList<SuiteContextDiagnostic> catalogDiagnostics = suite.diagnostics;
    catalogDiagnostics.append(providerDiagnostics);
    for (const QString& provider : unavailableProviders) {
        catalogDiagnostics.append({
            provider,
            registry.ok ? QStringLiteral("provider_unavailable")
                        : registry.errorCode,
            registry.ok
                ? QStringLiteral("The Suite provider is not running; local project metadata remains available.")
                : (registry.errorMessage.isEmpty()
                       ? QStringLiteral("Suite Runtime is unavailable; local project metadata remains available.")
                       : registry.errorMessage),
            {}});
    }
    for (const SuiteContextDiagnostic& diagnostic : catalogDiagnostics) {
        QList<WorkspaceHubSection*> affected;
        if (diagnostic.providerId == QStringLiteral("suite")) {
            affected = {
                sectionNamed(&result.sections, QStringLiteral("wave")),
                sectionNamed(&result.sections, QStringLiteral("regmap"))};
        } else {
            affected = {sectionNamed(
                &result.sections, diagnostic.providerId)};
        }
        for (WorkspaceHubSection* section : affected) {
            if (!section)
                continue;
            if (statePriority(section->state)
                < statePriority(WorkspaceHubItemState::Warning)) {
                section->state = WorkspaceHubItemState::Warning;
            }
            if (section->items.isEmpty())
                section->statusText = diagnostic.message;
        }
    }
    return result;
}
