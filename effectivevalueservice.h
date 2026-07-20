#ifndef EFFECTIVEVALUESERVICE_H
#define EFFECTIVEVALUESERVICE_H

#include "semanticindex.h"
#include "symbolpresentationservice.h"

#include <QList>
#include <QHash>
#include <QReadWriteLock>
#include <QString>
#include <QStringList>
#include <atomic>
#include <cstdint>

enum class EffectiveValueStatus {
    Unavailable,
    Current,
    Stale,
    Error
};

struct EffectiveValueQuery {
    SemanticSymbolRecord symbol;
    HierarchyInstanceContext instanceContext;
    QString documentText;
    std::uint64_t documentRevision = 0;
};

struct EffectiveValueResult {
    EffectiveValueStatus status = EffectiveValueStatus::Unavailable;
    // Slang-provided declaration-anchor equivalence. Consumers may suppress
    // a redundant value only when this is true.
    bool sourceTextDisplaysEffectiveValue = false;
    QString symbolIdentity;
    SymbolStableKey semanticSymbolKey;
    SemanticSymbolLocation sourceRange;
    QString stableSourceIdentity;
    QString symbolName;
    QString declarationText;
    QString expressionText;
    // Lossless semantic value from Slang.
    QString valueText;
    // Optional consumer-facing rendering from the same Slang value. For
    // enum members this is plain decimal when fully known, otherwise the
    // exact X / Z-preserving representation.
    QString displayValueText;
    QString resolvedTypeText;
    QString packedDimensionsText;
    QString unpackedDimensionsText;
    QString unpackedElementCountText;
    QString bitWidthText;
    QString signednessText;
    QString interfaceName;
    QString modportName;
    QString enumTypeName;
    QString enumUnderlyingBitWidthText;
    SemanticEffectiveScopeKind effectiveScopeKind =
        SemanticEffectiveScopeKind::Unknown;
    QString qualifiedScopePath;
    QString scopePath;
    QString instancePath;
    QString provenance;
    QString failureReason;
    bool instanceBound = false;
    bool defaultEvaluation = false;
    std::uint64_t computationRevision = 0;
    std::uint64_t requestedDocumentRevision = 0;
    std::uint64_t computedDocumentRevision = 0;
    std::uint64_t workspaceRevision = 0;

    bool available() const
    {
        return status == EffectiveValueStatus::Current
            || status == EffectiveValueStatus::Stale;
    }

    bool current() const
    {
        return status == EffectiveValueStatus::Current;
    }
};

enum class EffectiveValueFactKind {
    ParameterOverride,
    PartSelectWidth,
    GenerateCount,
    ConcatenationWidth
};

struct EffectiveValueFact {
    EffectiveValueFactKind kind =
        EffectiveValueFactKind::ParameterOverride;
    EffectiveValueStatus status = EffectiveValueStatus::Unavailable;
    QString fileName;
    int startPosition = -1;
    int endPosition = -1;
    int line = 0;
    QString expressionText;
    QString valueText;
    QString resolvedTypeText;
    QString packedDimensionsText;
    QString unpackedDimensionsText;
    QString unpackedElementCountText;
    QString bitWidthText;
    QString signednessText;
    // The elaborated object whose value was evaluated. For a parameter
    // override this is the child instance receiving the override.
    QString instancePath;
    // The instance whose source declaration contains the display anchor. It
    // differs from instancePath for an override written in a parent module.
    QString anchorInstancePath;
    SemanticEffectiveScopeKind effectiveScopeKind =
        SemanticEffectiveScopeKind::Unknown;
    QString qualifiedScopePath;
    QString scopePath;
    QString provenance;
    QString failureReason;
    QString stableSourceIdentity;
    // Slang-provided equivalence for the expression at this fact's own
    // source anchor (for example a parameter override expression).
    bool sourceTextDisplaysEffectiveValue = false;
    bool defaultEvaluation = false;
    std::uint64_t computationRevision = 0;
    std::uint64_t documentRevision = 0;

    bool isValid() const
    {
        return !fileName.isEmpty()
            && startPosition >= 0
            && endPosition >= startPosition;
    }
};

struct EffectiveLiteralResult {
    bool available = false;
    QString valueText;
    QString bitWidthText;
    QString signednessText;
    QString failureReason;
};

class EffectiveValueService
{
public:
    static EffectiveValueService* getInstance();

    explicit EffectiveValueService(SemanticIndex* semanticIndex = nullptr);

    void setSemanticIndex(SemanticIndex* semanticIndex);
    std::uint64_t beginComputation(
        const QStringList& affectedFiles = {});
    bool isComputationCurrent(
        const QString& fileName,
        std::uint64_t computationRevision) const;
    bool isComputationCurrent(
        const QStringList& fileNames,
        std::uint64_t computationRevision) const;
    EffectiveValueResult resolve(const EffectiveValueQuery& query) const;
    void publishDocumentFacts(
        const QString& fileName,
        const QString& documentText,
        QList<EffectiveValueFact> facts,
        std::uint64_t computationRevision,
        std::uint64_t documentRevision = 0);
    QList<EffectiveValueFact> factsForDocument(
        const QString& fileName,
        const QString& documentText,
        const HierarchyInstanceContext& instanceContext = {},
        std::uint64_t documentRevision = 0) const;
    void invalidateDocumentFacts(const QString& fileName);
    void clearPublishedFacts();
    static EffectiveLiteralResult evaluateLiteral(
        const QString& expressionText,
        bool stringLiteral = false);

    static QString stableSourceIdentity(
        const SemanticSymbolRecord& symbol);

private:
    struct PublishedFacts {
        QString contentFingerprint;
        QList<EffectiveValueFact> facts;
        std::uint64_t computationRevision = 0;
        std::uint64_t documentRevision = 0;
    };

    SemanticIndex* index = nullptr;
    mutable QReadWriteLock factsLock;
    QHash<QString, PublishedFacts> factsByFile;
    QHash<QString, std::uint64_t> requestedRevisionByFile;
    std::atomic<std::uint64_t> nextComputationRevision{0};

    SemanticIndex* semanticIndex() const;
    void registerComputation(
        std::uint64_t computationRevision,
        const QStringList& affectedFiles);
    std::uint64_t requestedRevisionForDocument(
        const QString& fileName) const;
    static QString contentFingerprint(const QString& text);
};

#endif // EFFECTIVEVALUESERVICE_H
