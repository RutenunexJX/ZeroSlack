#include "effectivevalueservice.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QReadLocker>
#include <QWriteLocker>

#include <slang/ast/ScriptSession.h>

#include <limits>

#include <memory>
#include <utility>

namespace {
std::unique_ptr<EffectiveValueService> instance;

QString normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString result = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

QString hierarchyRoot(const QString& instancePath)
{
    const int separator = instancePath.indexOf(QLatin1Char('.'));
    return separator < 0 ? instancePath : instancePath.left(separator);
}

bool isStaticScopeKind(SemanticEffectiveScopeKind kind)
{
    return kind == SemanticEffectiveScopeKind::Package
        || kind == SemanticEffectiveScopeKind::CompilationUnit;
}

QString effectiveFactAnchorPath(const EffectiveValueFact& fact)
{
    return fact.anchorInstancePath.isEmpty()
        ? fact.instancePath
        : fact.anchorInstancePath;
}

SemanticEffectiveScopeKind effectiveScopeKind(
    const SemanticSymbolRecord& symbol,
    const SemanticSymbolPresentation& presentation)
{
    if (presentation.effectiveScopeKind
        != SemanticEffectiveScopeKind::Unknown) {
        return presentation.effectiveScopeKind;
    }
    if (symbol.owner.kind
        == SymbolTaxonomy::SymbolOwnerScope::Package) {
        return SemanticEffectiveScopeKind::Package;
    }
    return SemanticEffectiveScopeKind::Instance;
}

void copyElaboratedInfo(const SemanticElaboratedSymbolInfo& info,
                        EffectiveValueResult* result)
{
    if (!result)
        return;
    result->sourceTextDisplaysEffectiveValue =
        info.sourceTextDisplaysEffectiveValue;
    result->valueText = info.valueText;
    result->displayValueText = info.displayValueText;
    result->expressionText = info.expressionText;
    result->resolvedTypeText = info.resolvedTypeText;
    result->packedDimensionsText = info.packedDimensionsText;
    result->unpackedDimensionsText = info.unpackedDimensionsText;
    result->unpackedElementCountText = info.unpackedElementCountText;
    result->bitWidthText = info.bitWidthText;
    result->signednessText = info.signednessText;
    result->interfaceName = info.interfaceName;
    result->modportName = info.modportName;
    result->failureReason = info.failureReason;
}

bool sameEffectiveFactValue(const EffectiveValueFact& left,
                            const EffectiveValueFact& right)
{
    return left.status == right.status
        && left.sourceTextDisplaysEffectiveValue
            == right.sourceTextDisplaysEffectiveValue
        && left.expressionText == right.expressionText
        && left.valueText == right.valueText
        && left.resolvedTypeText == right.resolvedTypeText
        && left.packedDimensionsText == right.packedDimensionsText
        && left.unpackedDimensionsText == right.unpackedDimensionsText
        && left.unpackedElementCountText
            == right.unpackedElementCountText
        && left.bitWidthText == right.bitWidthText
        && left.signednessText == right.signednessText
        && left.failureReason == right.failureReason;
}
}

struct EffectiveValueService::PreparedFactsState {
    QHash<QString, PublishedFacts> factsByFile;
    std::uint64_t computationRevision = 0;
};

struct EffectiveValueService::RetiredFactsState {
    QHash<QString, PublishedFacts> factsByFile;
};

struct EffectiveValueService::DocumentSnapshot {
    QString normalizedFileName;
    PublishedFacts publication;
    std::uint64_t requestedRevision = 0;
    bool hasPublication = false;
};

EffectiveValueService* EffectiveValueService::getInstance()
{
    if (!instance)
        instance = std::make_unique<EffectiveValueService>();
    return instance.get();
}

EffectiveValueService::EffectiveValueService(
    SemanticIndex* semanticIndex,
    std::shared_ptr<const DocumentSnapshot> documentSnapshot)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
    , readSnapshot(std::move(documentSnapshot))
{
}

void EffectiveValueService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

std::uint64_t EffectiveValueService::beginComputation(
    const QStringList& affectedFiles)
{
    const std::uint64_t revision =
        nextComputationRevision.fetch_add(1, std::memory_order_acq_rel) + 1;
    registerComputation(revision, affectedFiles);
    return revision;
}

void EffectiveValueService::registerComputation(
    std::uint64_t computationRevision,
    const QStringList& affectedFiles)
{
    if (computationRevision == 0 || affectedFiles.isEmpty())
        return;
    QWriteLocker locker(&factsLock);
    for (const QString& fileName : affectedFiles) {
        const QString normalized = normalizedFileName(fileName);
        if (normalized.isEmpty())
            continue;
        std::uint64_t& requested = requestedRevisionByFile[normalized];
        requested = qMax(requested, computationRevision);
    }
}

bool EffectiveValueService::isComputationCurrent(
    const QString& fileName,
    std::uint64_t computationRevision) const
{
    const QString normalized = normalizedFileName(fileName);
    if (normalized.isEmpty() || computationRevision == 0)
        return false;
    QReadLocker locker(&factsLock);
    return requestedRevisionByFile.value(normalized, 0)
        <= computationRevision;
}

bool EffectiveValueService::isComputationCurrent(
    const QStringList& fileNames,
    std::uint64_t computationRevision) const
{
    if (computationRevision == 0)
        return false;
    QReadLocker locker(&factsLock);
    for (const QString& fileName : fileNames) {
        const QString normalized = normalizedFileName(fileName);
        if (!normalized.isEmpty()
            && requestedRevisionByFile.value(normalized, 0)
                   > computationRevision) {
            return false;
        }
    }
    return true;
}

std::uint64_t EffectiveValueService::requestedRevisionForDocument(
    const QString& fileName) const
{
    const QString normalized = normalizedFileName(fileName);
    if (normalized.isEmpty())
        return 0;
    if (readSnapshot) {
        return readSnapshot->normalizedFileName == normalized
            ? readSnapshot->requestedRevision : 0;
    }
    QReadLocker locker(&factsLock);
    return requestedRevisionByFile.value(normalized, 0);
}

SemanticIndex* EffectiveValueService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

QString EffectiveValueService::stableSourceIdentity(
    const SemanticSymbolRecord& symbol)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(normalizedFileName(symbol.location.fileName))
        .arg(symbol.location.position)
        .arg(symbol.location.length)
        .arg(symbol.name)
        .arg(static_cast<int>(symbol.declarationKind))
        .arg(static_cast<int>(symbol.collectorKind));
}

QString EffectiveValueService::documentContentFingerprint(
    const QString& text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex());
}

void EffectiveValueService::publishDocumentFacts(
    const QString& fileName,
    const QString& documentText,
    QList<EffectiveValueFact> facts,
    std::uint64_t computationRevision,
    std::uint64_t documentRevision,
    const QString& preparedContentFingerprint)
{
    const QString normalized = normalizedFileName(fileName);
    if (normalized.isEmpty())
        return;

    for (EffectiveValueFact& fact : facts) {
        fact.fileName = normalizedFileName(fact.fileName);
        fact.computationRevision = computationRevision;
        fact.documentRevision = documentRevision;
    }

    PublishedFacts publication;
    publication.contentFingerprint = preparedContentFingerprint.isEmpty()
        ? documentContentFingerprint(documentText)
        : preparedContentFingerprint;
    publication.facts = std::move(facts);
    publication.computationRevision = computationRevision;
    publication.documentRevision = documentRevision;

    QWriteLocker locker(&factsLock);
    if (requestedRevisionByFile.value(normalized, 0)
            > computationRevision) {
        return;
    }
    const auto existing = factsByFile.constFind(normalized);
    if (existing != factsByFile.constEnd()
        && existing->computationRevision > computationRevision) {
        return;
    }
    factsByFile.insert(normalized, std::move(publication));
    requestedRevisionByFile[normalized] = computationRevision;
}

std::shared_ptr<EffectiveValueService::PreparedFactsState>
EffectiveValueService::prepareDocumentFactsState(
    QHash<QString, QList<EffectiveValueFact>> factsByFile,
    const QHash<QString, QString>& contentsByFile,
    const QHash<QString, QString>& contentFingerprintsByFile,
    const QHash<QString, std::uint64_t>& documentRevisionsByFile,
    std::uint64_t computationRevision)
{
    auto state = std::make_shared<PreparedFactsState>();
    state->computationRevision = computationRevision;

    QHash<QString, QString> normalizedContents;
    normalizedContents.reserve(contentsByFile.size());
    for (auto it = contentsByFile.constBegin();
         it != contentsByFile.constEnd(); ++it) {
        normalizedContents.insert(normalizedFileName(it.key()), it.value());
    }
    QHash<QString, QString> normalizedFingerprints;
    normalizedFingerprints.reserve(contentFingerprintsByFile.size());
    for (auto it = contentFingerprintsByFile.constBegin();
         it != contentFingerprintsByFile.constEnd(); ++it) {
        normalizedFingerprints.insert(normalizedFileName(it.key()),
                                      it.value());
    }

    state->factsByFile.reserve(factsByFile.size());
    for (auto it = factsByFile.begin(); it != factsByFile.end(); ++it) {
        const QString normalized = normalizedFileName(it.key());
        if (normalized.isEmpty())
            continue;

        PublishedFacts publication;
        publication.facts = std::move(it.value());
        publication.computationRevision = computationRevision;
        publication.documentRevision =
            documentRevisionsByFile.value(normalized, 0);
        publication.contentFingerprint =
            normalizedFingerprints.value(normalized);
        if (publication.contentFingerprint.isEmpty()) {
            publication.contentFingerprint = documentContentFingerprint(
                normalizedContents.value(normalized));
        }
        state->factsByFile.insert(normalized, std::move(publication));
    }
    return state;
}

std::shared_ptr<EffectiveValueService::RetiredFactsState>
EffectiveValueService::installPreparedDocumentFacts(
    std::shared_ptr<PreparedFactsState> state,
    bool replaceAll)
{
    if (!state || state->computationRevision == 0)
        return {};

    auto retired = std::make_shared<RetiredFactsState>();
    QWriteLocker locker(&factsLock);
    for (auto it = state->factsByFile.constBegin();
         it != state->factsByFile.constEnd(); ++it) {
        if (requestedRevisionByFile.value(it.key(), 0)
            > state->computationRevision) {
            return {};
        }
    }

    if (replaceAll) {
        retired->factsByFile = std::move(factsByFile);
        factsByFile = std::move(state->factsByFile);
        for (auto it = factsByFile.constBegin();
             it != factsByFile.constEnd(); ++it) {
            requestedRevisionByFile[it.key()] =
                state->computationRevision;
        }
        return retired;
    }

    for (auto it = state->factsByFile.begin();
         it != state->factsByFile.end(); ++it) {
        const auto existing = factsByFile.constFind(it.key());
        if (existing != factsByFile.constEnd()
            && existing->computationRevision
                   > state->computationRevision) {
            continue;
        }
        auto existingMutable = factsByFile.find(it.key());
        if (existingMutable != factsByFile.end()) {
            retired->factsByFile.insert(
                it.key(), std::move(existingMutable.value()));
            existingMutable.value() = std::move(it.value());
        } else {
            factsByFile.insert(it.key(), std::move(it.value()));
        }
        requestedRevisionByFile[it.key()] = state->computationRevision;
    }
    return retired;
}

QList<EffectiveValueFact> EffectiveValueService::factsForDocument(
    const QString& fileName,
    const QString& documentText,
    const HierarchyInstanceContext& instanceContext,
    std::uint64_t documentRevision) const
{
    const QString normalized = normalizedFileName(fileName);
    if (normalized.isEmpty())
        return {};

    QReadLocker locker(readSnapshot ? nullptr : &factsLock);
    const PublishedFacts* publication = nullptr;
    std::uint64_t requestedRevision = 0;
    if (readSnapshot) {
        if (readSnapshot->normalizedFileName == normalized
            && readSnapshot->hasPublication) {
            publication = &readSnapshot->publication;
            requestedRevision = readSnapshot->requestedRevision;
        }
    } else {
        const auto found = factsByFile.constFind(normalized);
        if (found != factsByFile.constEnd())
            publication = &found.value();
        requestedRevision = requestedRevisionByFile.value(normalized, 0);
    }
    if (!publication
        || publication->contentFingerprint
               != documentContentFingerprint(documentText)
        || (documentRevision != 0
            && publication->documentRevision != documentRevision)
        || publication->computationRevision
               < requestedRevision) {
        return {};
    }

    QHash<QString, QList<EffectiveValueFact>> candidatesByKey;
    QStringList keyOrder;
    const bool bound = instanceContext.isBound();
    if (bound
        && hierarchyRoot(instanceContext.instancePath)
            != instanceContext.activeTopModule) {
        return {};
    }
    for (EffectiveValueFact fact : publication->facts) {
        if (normalizedFileName(fact.fileName) != normalized)
            continue;
        if (bound && !isStaticScopeKind(fact.effectiveScopeKind)) {
            // An override is evaluated in its child but written in its
            // parent's source declaration. Select by the instance that owns
            // the display anchor; retain instancePath as value provenance.
            if (effectiveFactAnchorPath(fact)
                != instanceContext.instancePath) {
                continue;
            }
        }

        fact.computationRevision = publication->computationRevision;
        fact.documentRevision = publication->documentRevision;
        const QString key = QStringLiteral("%1|%2|%3")
                                .arg(static_cast<int>(fact.kind))
                                .arg(fact.startPosition)
                                .arg(fact.endPosition);
        if (!candidatesByKey.contains(key))
            keyOrder.append(key);
        candidatesByKey[key].append(std::move(fact));
    }

    QList<EffectiveValueFact> result;
    result.reserve(keyOrder.size());
    for (const QString& key : std::as_const(keyOrder)) {
        const QList<EffectiveValueFact>& candidates =
            candidatesByKey.value(key);
        if (candidates.isEmpty())
            continue;

        if (bound) {
            int selected = 0;
            int selectedScore = -1;
            for (int i = 0; i < candidates.size(); ++i) {
                const EffectiveValueFact& fact = candidates.at(i);
                const int score = effectiveFactAnchorPath(fact)
                                          == instanceContext.instancePath
                    ? (fact.defaultEvaluation ? 2 : 3)
                    : 1;
                if (score > selectedScore) {
                    selected = i;
                    selectedScore = score;
                }
            }
            result.append(candidates.at(selected));
            continue;
        }

        QList<int> preferred;
        for (int i = 0; i < candidates.size(); ++i) {
            const EffectiveValueFact& fact = candidates.at(i);
            if (fact.defaultEvaluation || fact.instancePath.isEmpty())
                preferred.append(i);
        }
        if (preferred.isEmpty()) {
            for (int i = 0; i < candidates.size(); ++i)
                preferred.append(i);
        }

        const EffectiveValueFact& reference =
            candidates.at(preferred.first());
        bool consistent = true;
        for (int i : std::as_const(preferred)) {
            if (!sameEffectiveFactValue(reference, candidates.at(i))) {
                consistent = false;
                break;
            }
        }
        if (!consistent)
            continue;

        EffectiveValueFact selected = reference;
        if (!selected.defaultEvaluation
            && !selected.instancePath.isEmpty()) {
            selected.instancePath.clear();
            selected.anchorInstancePath.clear();
            selected.provenance += QStringLiteral(
                " (consistent across elaborated instances)");
        }
        result.append(std::move(selected));
    }
    return result;
}

std::shared_ptr<const EffectiveValueService::DocumentSnapshot>
EffectiveValueService::snapshotForDocument(const QString& fileName) const
{
    const QString normalized = normalizedFileName(fileName);
    if (normalized.isEmpty())
        return {};

    auto snapshot = std::make_shared<DocumentSnapshot>();
    snapshot->normalizedFileName = normalized;
    QReadLocker locker(&factsLock);
    const auto publication = factsByFile.constFind(normalized);
    if (publication != factsByFile.constEnd()) {
        snapshot->publication = publication.value();
        snapshot->hasPublication = true;
    }
    snapshot->requestedRevision = requestedRevisionByFile.value(normalized, 0);
    return snapshot;
}

void EffectiveValueService::invalidateDocumentFacts(const QString& fileName)
{
    const QString normalized = normalizedFileName(fileName);
    if (normalized.isEmpty())
        return;
    QWriteLocker locker(&factsLock);
    factsByFile.remove(normalized);
}

void EffectiveValueService::clearPublishedFacts()
{
    QWriteLocker locker(&factsLock);
    factsByFile.clear();
    requestedRevisionByFile.clear();
}

EffectiveLiteralResult EffectiveValueService::evaluateLiteral(
    const QString& expressionText,
    bool stringLiteral)
{
    EffectiveLiteralResult result;
    if (expressionText.trimmed().isEmpty())
        return result;
    try {
        slang::ast::ScriptSession session;
        std::string source = expressionText.toStdString();
        if (stringLiteral)
            source = "string'(" + source + ")";
        const slang::ConstantValue value = session.eval(source);
        if (!value) {
            result.failureReason = QStringLiteral(
                "Slang did not produce a constant value.");
            return result;
        }
        result.available = true;
        result.valueText = QString::fromStdString(value.toString(
            std::numeric_limits<slang::bitwidth_t>::max(),
            true));
        if (value.isInteger()) {
            result.bitWidthText = QString::number(
                value.integer().getBitWidth());
            result.signednessText = value.integer().isSigned()
                ? QStringLiteral("signed")
                : QStringLiteral("unsigned");
        } else if (value.isString()) {
            result.bitWidthText = QString::number(
                value.str().size() * 8);
            result.signednessText = QStringLiteral("not applicable");
        }
    } catch (const std::exception& error) {
        result.failureReason = QString::fromUtf8(error.what());
    } catch (...) {
        result.failureReason = QStringLiteral(
            "Slang literal evaluation failed.");
    }
    return result;
}

EffectiveValueResult EffectiveValueService::resolve(
    const EffectiveValueQuery& query) const
{
    EffectiveValueResult result;
    const SemanticSymbolRecord& symbol = query.symbol;
    result.semanticSymbolKey = symbol.stableKey;
    result.sourceRange = symbol.location;
    result.stableSourceIdentity = stableSourceIdentity(symbol);
    result.symbolIdentity = result.stableSourceIdentity;
    result.symbolName = symbol.name;
    result.requestedDocumentRevision = query.documentRevision;

    SemanticIndex* semantic = semanticIndex();
    result.workspaceRevision = semantic ? semantic->snapshotRevision() : 0;

    const SemanticSymbolPresentation source =
        SymbolPresentationService::presentationForRecord(symbol);
    result.computedDocumentRevision = source.documentRevision;
    result.computationRevision = source.computationRevision != 0
        ? source.computationRevision : result.workspaceRevision;
    result.effectiveScopeKind = effectiveScopeKind(symbol, source);
    result.qualifiedScopePath = source.qualifiedScopePath;
    if (result.qualifiedScopePath.isEmpty()
        && isStaticScopeKind(result.effectiveScopeKind)) {
        result.qualifiedScopePath = symbol.owner.name;
        if (result.qualifiedScopePath.isEmpty()
            && result.effectiveScopeKind
                   == SemanticEffectiveScopeKind::CompilationUnit) {
            result.qualifiedScopePath = QStringLiteral("$unit");
        }
    }
    result.scopePath = result.qualifiedScopePath;
    result.declarationText = source.declarationText;
    result.expressionText = source.expressionText;
    result.packedDimensionsText = source.packedDimensionsText;
    result.unpackedDimensionsText = source.unpackedDimensionsText;
    result.enumTypeName = source.enumTypeName;
    result.enumUnderlyingBitWidthText =
        source.enumUnderlyingBitWidthText;

    const bool packageOrGlobal =
        isStaticScopeKind(result.effectiveScopeKind);
    const bool requestedInstance = query.instanceContext.isBound()
        && !packageOrGlobal;
    result.instanceBound = requestedInstance;

    const QString cachedText = semantic
        ? semantic->getCachedFileContent(symbol.location.fileName)
        : QString();
    const bool hasOverlay = !query.documentText.isNull();
    const bool revisionMismatch = query.documentRevision != 0
        && query.documentRevision != source.documentRevision;
    const std::uint64_t requestedRevision =
        requestedRevisionForDocument(symbol.location.fileName);
    const bool computationPending = requestedRevision != 0
        && source.computationRevision < requestedRevision;
    const bool stale = (hasOverlay && cachedText != query.documentText)
        || revisionMismatch
        || computationPending;

    const SemanticElaboratedSymbolInfo* selected = nullptr;
    if (requestedInstance) {
        result.instancePath = query.instanceContext.instancePath;
        if (hierarchyRoot(query.instanceContext.instancePath)
            != query.instanceContext.activeTopModule) {
            result.status = EffectiveValueStatus::Error;
            result.failureReason = QStringLiteral(
                "Bound instance path '%1' is outside active top '%2'.")
                .arg(query.instanceContext.instancePath,
                     query.instanceContext.activeTopModule);
            return result;
        }
        const auto it = source.instanceInfoByPath.constFind(
            query.instanceContext.instancePath);
        if (it == source.instanceInfoByPath.constEnd()) {
            result.status = stale
                ? EffectiveValueStatus::Stale
                : EffectiveValueStatus::Error;
            result.failureReason = stale
                ? QStringLiteral(
                      "The requested instance value is pending analysis for the current document revision.")
                : QStringLiteral(
                      "No Slang effective value exists at the exact bound instance '%1'.")
                      .arg(query.instanceContext.instancePath);
            return result;
        }
        selected = &it.value();
    } else {
        result.defaultEvaluation = !packageOrGlobal;
        result.instancePath = packageOrGlobal
            ? result.qualifiedScopePath
            : QStringLiteral("Unbound instance / \u672a\u7ed1\u5b9a\u5b9e\u4f8b");
        selected = &source.defaultInfo;
    }

    if (!selected) {
        result.status = EffectiveValueStatus::Unavailable;
        return result;
    }

    copyElaboratedInfo(*selected, &result);
    if (result.expressionText.isEmpty())
        result.expressionText = source.expressionText;
    if (result.packedDimensionsText.isEmpty())
        result.packedDimensionsText = source.packedDimensionsText;
    if (result.unpackedDimensionsText.isEmpty())
        result.unpackedDimensionsText = source.unpackedDimensionsText;

    result.provenance = selected->valueSourceText.isEmpty()
        ? (packageOrGlobal
               ? QStringLiteral("Slang package/global elaboration")
               : (requestedInstance
                      ? QStringLiteral("Slang instance elaboration")
                      : QStringLiteral("Slang default elaboration")))
        : selected->valueSourceText;

    if (!selected->available) {
        result.status = stale
            ? EffectiveValueStatus::Stale
            : EffectiveValueStatus::Error;
        if (result.failureReason.isEmpty()) {
            result.failureReason = stale
                ? QStringLiteral(
                      "The effective value belongs to an older document revision.")
                : QStringLiteral(
                      "Slang could not evaluate this declaration.");
        }
        return result;
    }

    result.status = stale
        ? EffectiveValueStatus::Stale
        : EffectiveValueStatus::Current;
    if (stale) {
        result.failureReason = QStringLiteral(
            "The effective value belongs to an older document revision.");
    }
    return result;
}
