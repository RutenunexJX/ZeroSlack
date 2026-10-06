#ifndef SEMANTICINDEX_H
#define SEMANTICINDEX_H

#include <zeroslack/semantic/semanticsourcerange.h>
#include <zeroslack/semantic/symbolrelationshipengine.h>
#include <zeroslack/semantic/symboltaxonomy.h>

#include <QHash>
#include <QMap>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <memory>
#include <functional>
#include <utility>

class SemanticIndexSnapshot;
class SemanticIndex;
class SlangManager;
class SmartRelationshipBuilder;
class QObject;
enum class CompletionCommandKind;

struct SemanticSnapshotToken {
    std::shared_ptr<const SemanticIndexSnapshot> snapshot;
    std::uint64_t revision = 0;

    bool isValid() const { return snapshot != nullptr; }
};

struct SemanticQueryContext {
    QString fileName;
    QString moduleName;
    QString packageName;
    QString prefix;
    int cursorLine = -1;      // 1-based
    int cursorPosition = -1;  // QTextDocument position, when available
    QSet<QString> importedPackageNames;
};

struct SymbolStableKey {
    QString fileName;
    QString symbolName;
    SymbolTaxonomy::DeclarationKind declarationKind =
        SymbolTaxonomy::DeclarationKind::Unknown;
    QString ownerScope;
    int sourcePosition = -1;
    int sourceLength = 0;

    bool isValid() const;
    QString toString() const;
    bool operator==(const SymbolStableKey& other) const;
};

struct SemanticSymbolLocation {
    QString fileName;
    int startLine = 0;
    int startColumn = 0;
    int endLine = 0;
    int endColumn = 0;
    int position = 0;
    int length = 0;

    bool isValid() const;
};

struct SemanticSymbolOwner {
    SymbolTaxonomy::SymbolOwnerScope kind =
        SymbolTaxonomy::SymbolOwnerScope::Unknown;
    QString name;
    SymbolStableKey stableKey;
    bool interfaceLike = false;

    bool isValid() const;
};

struct SemanticSymbolTypeReference {
    QString rawTypeText;
    QString resolvedTypeName;
    SymbolTaxonomy::DeclarationKind resolvedTypeKind =
        SymbolTaxonomy::DeclarationKind::Unknown;
    QString modportName;
    SymbolStableKey stableKey;

    bool isValid() const;
};

enum class SemanticDriverPresenceState : std::uint8_t {
    Unknown,
    ProvenZero,
    Present
};

enum class SemanticStructuredSelectorKind : std::uint8_t {
    StructMember,
    PackedIndex,
    UnpackedIndex,
    InterfaceMember
};

struct SemanticStructuredSelectorFact {
    SemanticStructuredSelectorKind kind =
        SemanticStructuredSelectorKind::StructMember;
    QString name;
    int sourceIndex = 0;
    int storageIndex = 0;
};

struct SemanticStructuredEnumValueFact {
    QString name;
    QString valueText;
    QString displayValueText;
    bool available = false;
};

struct SemanticStructuredLeafFact {
    QString relativePath;
    QList<SemanticStructuredSelectorFact> selectors;
    // Empty for ordinary data ports, which inherit the root port direction.
    // Interface leaves carry the selected modport direction explicitly.
    QString direction;
    QString canonicalTypeId;
    QString resolvedTypeText;
    bool fixedSize = false;
    bool integral = false;
    bool signedIntegral = false;
    std::uint64_t bitWidth = 0;
    bool packedBitOffsetValid = false;
    std::uint64_t packedBitOffset = 0;
    QList<SemanticStructuredEnumValueFact> enumValues;
};

struct SemanticElaboratedSymbolInfo {
    bool available = false;
    // Machine-readable Slang type facts. Action planners consume these
    // directly and never infer propagatability from presentation strings.
    bool fixedSize = false;
    bool integral = false;
    bool unpackedArray = false;
    bool interfaceType = false;
    bool signedIntegral = false;
    std::uint64_t bitWidth = 0;
    // Unified Slang driver facts for this exact elaborated instance.
    // Unknown is deliberately distinct from ProvenZero: action planners
    // may reuse an existing endpoint only after Slang proved absence.
    SemanticDriverPresenceState driverPresence =
        SemanticDriverPresenceState::Unknown;
    std::uint64_t driverCount = 0;
    std::uint64_t continuousDriverCount = 0;
    std::uint64_t proceduralDriverCount = 0;
    std::uint64_t portConnectionDriverCount = 0;
    // True only when Slang proved that the direct value written at this
    // symbol's declaration anchor is numerically identical to the final
    // elaborated value. An override written elsewhere never sets this flag.
    bool sourceTextDisplaysEffectiveValue = false;
    // Lossless Slang value text used for semantic queries. Integral enum
    // values retain their bit width, signedness, and every X / Z bit here.
    QString valueText;
    // Optional UI-oriented rendering produced from the same Slang
    // ConstantValue. This never replaces valueText; enum Ghost annotations
    // use it to show known integral values as plain decimal.
    QString displayValueText;
    QString expressionText;
    QString valueSourceText;
    QString resolvedTypeText;
    QString packedDimensionsText;
    QString unpackedDimensionsText;
    QString unpackedElementCountText;
    QString bitWidthText;
    QString signednessText;
    QString interfaceName;
    QString modportName;
    bool structuredLeavesAvailable = false;
    QList<SemanticStructuredLeafFact> structuredLeaves;
    QString structuredFailureReason;
    QString failureReason;
};

enum class SemanticTypeDimensionKind : std::uint8_t {
    Packed,
    Unpacked
};

// These structures are the lossless machine-facing counterpart to the
// presentation strings above. They are populated directly from Slang's bound
// AST and resolved Type graph; consumers must not reconstruct them by parsing
// declarationText or any other renderer product.
struct SemanticConstantEvaluationNode {
    QString id;
    QString operationName;
    QString expressionText;
    QStringList operandIds;
    QStringList parameterDependencyIds;
    QString resultValueText;
    bool evaluated = false;
    QString evidenceId;
};

struct SemanticParameterDependencyNode {
    QString identity;
    QString name;
    bool localparam = false;
    QString rootEvaluationNodeId;
    QStringList dependencyIds;
    QString valueText;
    bool evaluated = false;
    QString evidenceId;
};

struct SemanticConstantDependencyGraph {
    QList<SemanticConstantEvaluationNode> evaluationNodes;
    QList<SemanticParameterDependencyNode> parameters;
    bool complete = false;
    QString evidenceId;
    QString failureReason;
};

struct SemanticTypeDimensionFact {
    SemanticTypeDimensionKind kind =
        SemanticTypeDimensionKind::Packed;
    QString canonicalId;
    QString declarationText;
    QString leftEvaluationNodeId;
    QString rightEvaluationNodeId;
    QString elementCountText;
    bool emitInDeclaration = true;
    bool complete = false;
    QString evidenceId;
};

struct SemanticTypedefResolutionStep {
    QString sourceTypeId;
    QString sourceTypeName;
    QString targetTypeId;
    QString declarationIdentity;
    QStringList introducedDimensionIds;
    QString evidenceId;
};

struct SemanticDeclaredTypeFacts {
    QString rootTypeId;
    QString canonicalTypeId;
    QString declarationShapeId;
    QString declarationBaseText;
    bool signednessKnown = false;
    bool signedIntegral = false;
    bool complete = false;
    QList<SemanticTypeDimensionFact> dimensions;
    QList<SemanticTypedefResolutionStep> typedefChain;
    SemanticConstantDependencyGraph constants;
    QString evidenceId;
    QString failureReason;
};

// Describes the elaboration domain in which a compile-time value is valid.
// This is intentionally independent of SemanticSymbolOwner: enum values owned
// by a typedef, for example, can still be package-scoped effective values.
enum class SemanticEffectiveScopeKind {
    Unknown,
    Instance,
    Package,
    CompilationUnit
};

struct SemanticSymbolPresentation {
    QString declarationText;
    QString expressionText;
    QString enumTypeName;
    QString enumUnderlyingBitWidthText;
    QString packedDimensionsText;
    QString unpackedDimensionsText;
    SemanticEffectiveScopeKind effectiveScopeKind =
        SemanticEffectiveScopeKind::Unknown;
    QString qualifiedScopePath;
    std::uint64_t computationRevision = 0;
    std::uint64_t documentRevision = 0;
    SemanticElaboratedSymbolInfo defaultInfo;
    // Usually one or two large values. QHash's minimum 48 value slots per
    // populated span dominated retained workspace memory for these rows.
    QMap<QString, SemanticElaboratedSymbolInfo> instanceInfoByPath;
    SemanticDeclaredTypeFacts defaultDeclaredTypeFacts;
    QMap<QString, SemanticDeclaredTypeFacts>
        declaredTypeFactsByPath;
};

struct SemanticAnalysisBandMetadata {
    QString label;
    QString displayName;
    bool priority = false;
    int publicationCheckpoint = 0;

    bool isValid() const;
};

struct SemanticAnalysisBandReportItem {
    QString label;
    QString displayName;
    bool priority = false;
    int publicationCheckpoint = 0;
    int symbolCount = 0;
    int fileCount = 0;
    QStringList files;

    bool isValid() const;
};

struct SemanticAnalysisBandReport {
    QList<SemanticAnalysisBandReportItem> bands;
    int totalSymbolCount = 0;
    int totalFileCount = 0;

    QString summaryText() const;
};

struct SemanticSymbolRecord {
    SymbolStableKey stableKey;
    int localHandle = -1;
    QString name;
    SemanticSymbolLocation location;
    SymbolTaxonomy::DeclarationKind declarationKind =
        SymbolTaxonomy::DeclarationKind::Unknown;
    SymbolTaxonomy::SymbolUsageRole usageRole =
        SymbolTaxonomy::SymbolUsageRole::Unknown;
    SymbolTaxonomy::SymbolVisibility visibility =
        SymbolTaxonomy::SymbolVisibility::Unknown;
    SymbolTaxonomy::SourceRole sourceRole =
        SymbolTaxonomy::SourceRole::Unknown;
    SymbolTaxonomy::CollectorKind collectorKind =
        SymbolTaxonomy::CollectorKind::User;
    SemanticSymbolOwner owner;
    SemanticSymbolTypeReference type;
    SemanticSymbolPresentation presentation;
    SemanticAnalysisBandMetadata analysisBand;
    // Slang import facts can originate in an included header. The identity
    // separates independent compilation units; source order and offset are
    // taken from Slang's root buffer / include expansion anchor so ownerless
    // imports flow forward through a workspace compilation unit, never back.
    QString compilationUnitFileName;
    std::uint64_t compilationUnitSourceOrder = 0;
    std::uint64_t compilationUnitSourceOffset = 0;

    bool isValid() const;
};

enum class RelationshipProvenance {
    Unknown,
    SlangExtracted,
    Inferred,
    LexicalFallback,
    OpenDocument,
    Workspace,
    FeatureGenerated
};

struct SemanticRelationship {
    int fromId = -1;
    int toId = -1;
    SymbolRelationshipEngine::RelationType type = SymbolRelationshipEngine::REFERENCES;
    SymbolStableKey fromStableKey;
    SymbolStableKey toStableKey;
    QString fromAccessPath;
    QString toAccessPath;
    RelationshipProvenance provenance = RelationshipProvenance::Unknown;
    int confidence = 0;
    QString evidenceText;
    SemanticSourceRange evidenceRange;
    bool exactValueForward = false;
};

struct SemanticRelationshipResult {
    SemanticRelationship relationship;
    SemanticSymbolRecord fromSymbolRecord;
    SemanticSymbolRecord toSymbolRecord;
    SymbolStableKey fromStableKey;
    SymbolStableKey toStableKey;
    QString fromAccessPath;
    QString toAccessPath;
    RelationshipProvenance provenance = RelationshipProvenance::Unknown;
    int confidence = 0;
    QString evidenceText;
    SemanticSourceRange evidenceRange;
    bool exactValueForward = false;
};

struct SemanticDiagnostic {
    enum Severity {
        Info,
        Warning,
        Error
    };

    enum Owner {
        UnknownOwner,
        SlangCompiler,
        SemanticIndexOwner
    };

    QString fileName;
    int line = 0;
    int column = 0;
    QString message;
    QString codeName;
    QList<SemanticSourceRange> ranges;
    Severity severity = Info;
    Owner owner = UnknownOwner;
    std::uint64_t computationRevision = 0;
    std::uint64_t documentRevision = 0;
};

struct SemanticFileSymbolUpdate {
    QString fileName;
    QList<SemanticSymbolRecord> symbolRecords;
    QString content;
    bool removed = false;
};

struct SemanticSymbolSearchQuery {
    QString text;
    QString fileName;
    QList<SymbolTaxonomy::DeclarationKind> declarationKinds;
    SymbolTaxonomy::SymbolSearchIntent intent = SymbolTaxonomy::SymbolSearchIntent::Any;
    bool caseSensitive = false;
    bool exactMatch = false;
    int maxResults = -1;
};

struct SemanticSymbolSearchResult {
    SemanticSymbolRecord symbolRecord;
    SymbolStableKey symbolStableKey;
    int score = 0;
};

struct SemanticDefinitionQuery {
    QString symbolName;
    QString fileName;
    QString moduleName;
    QString structTypeNameForMember;
    int cursorLine = -1;
    int cursorPosition = -1;
    QSet<QString> importedPackageNames;
};

enum class SemanticDefinitionMissReason {
    None,
    EmptySymbolName,
    NoCandidateSymbols,
    NoMatchingName,
    StructMemberTypeMismatch,
    NotVisibleInContext,
    AmbiguousImportedPackageSymbol
};

struct SemanticDefinitionResult {
    bool found = false;
    bool localFile = false;
    SemanticSymbolRecord symbolRecord;
    SymbolStableKey symbolStableKey;
    int inspectedCandidateCount = 0;
    int matchingNameCandidateCount = 0;
    int typeCompatibleCandidateCount = 0;
    int visibleCandidateCount = 0;
    SemanticDefinitionMissReason missReason =
        SemanticDefinitionMissReason::NoCandidateSymbols;
};

SymbolTaxonomy::SemanticMetadata semanticMetadataForSymbolRecord(
    const SemanticSymbolRecord& record);
QString semanticAnalysisBandDisplayName(
    const SemanticAnalysisBandMetadata& metadata);
SemanticAnalysisBandReport semanticAnalysisBandReportForRecords(
    const QList<SemanticSymbolRecord>& records);
int semanticAnalysisBandSortPriority(
    const SemanticAnalysisBandMetadata& metadata);
int semanticSymbolAnalysisBandSortPriority(
    const SemanticSymbolRecord& record);
QString symbolStableKeyText(const SymbolStableKey& key);
QString semanticRelationshipStableKeyText(
    const SemanticRelationship& relationship);

struct SemanticIndexRetirementPayload {
    std::shared_ptr<const SemanticIndexSnapshot> snapshot;
    std::shared_ptr<SymbolRelationshipEngine::PreparedRelationshipState>
        relationshipState;

    bool isEmpty() const
    {
        return !snapshot && !relationshipState;
    }
};

// Query facade and compatibility adapter over one immutable publication.
class SemanticIndex
{
public:
    static SemanticIndex* getInstance();

    SemanticIndex();
    // Worker-local read facade. No singleton access, logging or mutable query
    // mirrors; every service bound to it reads the same immutable publication.
    explicit SemanticIndex(const SemanticSnapshotToken& token);
    ~SemanticIndex();

    void setSnapshot(std::shared_ptr<const SemanticIndexSnapshot> snapshot);
    SemanticIndexRetirementPayload installPreparedSnapshot(
        std::shared_ptr<const SemanticIndexSnapshot> snapshot,
        const QStringList& changedFiles,
        const QHash<QString, QSet<int>>& relationshipHandlesByFile = {},
        const QList<SemanticRelationship>& relationships = {},
        bool relationshipDeltaPrepared = false,
        std::shared_ptr<
            SymbolRelationshipEngine::PreparedRelationshipState>
            relationshipState = {},
        const SemanticAnalysisBandReport& analysisBandReport = {},
        bool notify = true);
    void notifyPreparedSnapshotPublished(std::uint64_t revision,
                                         const QStringList& changedFiles,
                                         bool notifyRelationships = true);
    // Clears the sole authoritative publication, including compatibility writes.
    void clearSnapshot();
    // Clears all index-owned state for a closed or replaced workspace.
    void clearSemanticState();
    std::shared_ptr<const SemanticIndexSnapshot> snapshot() const;
    bool hasSymbolRecords() const;
    std::uint64_t snapshotRevision() const;
    SemanticSnapshotToken snapshotToken() const;
    using PublicationObserver = std::function<void(std::uint64_t, int, int, const QStringList&)>;
    void setPublicationObserver(PublicationObserver observer) { m_publicationObserver = std::move(observer); }

    void updateSymbolRecordsForFile(
        const QString& fileName,
        const QList<SemanticSymbolRecord>& records,
        const QString& content);
    void updateSymbolRecordsForFiles(
        const QList<SemanticFileSymbolUpdate>& updates,
        bool buildRelationships = true);
    // Accept uncapped diagnostics. -1 preserves the current display limit;
    // zero exposes the uncapped compatibility view, positive values select it.
    void publishSnapshotReplacingDiagnostics(
        const QStringList& fileNames,
        const QList<SemanticDiagnostic>& diagnostics,
        int diagnosticDisplayLimit = -1);
    std::shared_ptr<const SemanticIndexSnapshot> captureSnapshotPreservingDiagnostics() const;
    std::shared_ptr<const SemanticIndexSnapshot> captureSnapshotReplacingDiagnostics(
        const QStringList& fileNames,
        const QList<SemanticDiagnostic>& diagnostics,
        int diagnosticDisplayLimit = -1) const;
    SemanticSnapshotToken beginRelationshipAnalysisSnapshot();
    std::shared_ptr<const SemanticIndexSnapshot> snapshotWithAdditionalRelationships(
        std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot,
        const QList<SemanticRelationship>& relationships) const;
    bool publishSnapshotIfCurrent(
        const SemanticSnapshotToken& expectedCurrentSnapshot,
        std::shared_ptr<const SemanticIndexSnapshot> nextSnapshot);
    void setWorkspaceFileAnalysisBands(
        const QHash<QString, SemanticAnalysisBandMetadata>& bands);
    void clearWorkspaceFileAnalysisBands();
    std::uint64_t workspaceAnalysisBandRevision() const { return m_workspaceAnalysisBandRevision; }
    SemanticAnalysisBandMetadata analysisBandForFile(
        const QString& fileName) const;
    SemanticAnalysisBandReport analysisBandReport(
        const QString& fileName = QString()) const;
    bool hasPreparedAnalysisBandReport() const;

    QList<SemanticSymbolRecord> getSymbolRecords(
        const QString& fileName = QString()) const;
    QList<SemanticSymbolRecord> getSymbolRecordsByName(
        const QString& name) const;
    QList<SemanticSymbolRecord> getSymbolRecordsByOwner(
        const QString& ownerName) const;
    QList<SemanticSymbolRecord> getSymbolRecordsByDeclarationKind(
        SymbolTaxonomy::DeclarationKind declarationKind) const;
    QList<SemanticSymbolSearchResult> searchSymbols(
        const SemanticSymbolSearchQuery& query) const;
    QList<SemanticSymbolRecord> getCommandCompletionSymbolRecords(
        const QString& moduleName,
        CompletionCommandKind commandKind,
        const QString& prefix = QString()) const;
    QList<SemanticSymbolRecord> getCommandCompletionSymbolRecords(
        const SemanticQueryContext& context,
        CompletionCommandKind commandKind,
        const QString& prefix = QString()) const;
    QList<QList<SemanticSymbolRecord>> getCommandCompletionSymbolRecordGroups(
        const SemanticQueryContext& context,
        const QList<CompletionCommandKind>& commandKinds,
        const QString& prefix = QString()) const;
    SemanticSymbolRecord getSymbolRecordByStableKey(
        const SymbolStableKey& key) const;
    QList<SemanticSymbolRecord> findDefinitionRecords(
        const QString& name,
        const SemanticQueryContext& context = {}) const;
    SemanticDefinitionResult resolveDefinition(
        const SemanticDefinitionQuery& query) const;
    QString getCachedFileContent(const QString& fileName) const;
    QStringList getScopeSymbolNames(const QString& fileName, int cursorLine) const;
    QSet<QString> activeImportedPackageNames(
        const SemanticQueryContext& context) const;
    QList<SemanticSymbolRecord> getVisibleImportedPackageRecords(
        const SemanticQueryContext& context) const;
    bool packageVisibleRecordImported(
        const SemanticSymbolRecord& record,
        const SemanticQueryContext& context) const;
    QString getStructTypeForVariable(const QString& variableName,
                                     const QString& moduleName = QString()) const;
    QList<SemanticSymbolRecord> getStructMemberRecords(
        const QString& structTypeName = QString()) const;
    // Identity-bearing queries are shared by completion, navigation and insights.
    QList<SemanticSymbolRecord> getDeclarationMemberRecords(
        const SemanticSymbolRecord& declaration) const;
    QList<SemanticSymbolRecord> getTypeMemberRecords(
        const SemanticSymbolRecord& subject,
        SymbolTaxonomy::DeclarationKind memberKind) const;
    SemanticSymbolRecord resolveVisibleValueRecord(
        const QString& name, const SemanticQueryContext& context) const;
    SemanticSymbolRecord resolveScopeRecord(const SemanticQueryContext& context) const;
    SemanticSymbolRecord resolveMemberPath(
        const SemanticSymbolRecord& root, const QStringList& memberPath) const;
    QList<SemanticSymbolRecord> getModuleContextSymbolRecordsByType(
        const QString& moduleName,
        const QString& fileName,
        CompletionCommandKind commandKind,
        const QString& prefix = QString()) const;
    QList<QList<SemanticSymbolRecord>> getModuleContextSymbolRecordGroups(
        const QString& moduleName,
        const QString& fileName,
        const QList<CompletionCommandKind>& commandKinds,
        const QString& prefix = QString()) const;
    QString currentModuleAt(const QString& fileName, int cursorPosition) const;
    bool isValidModuleName(const QString& name) const;
    bool contentAffectsSymbols(const QString& fileName, const QString& content) const;
    void attachRelationshipEngine(SymbolRelationshipEngine* engine);
    SymbolRelationshipEngine* relationshipEngine() const;
    std::unique_ptr<SmartRelationshipBuilder> createRelationshipBuilder(
        SymbolRelationshipEngine* engine,
        SlangManager* slangManager,
        QObject* parent = nullptr) const;

    QList<SemanticRelationship> relationshipsForStableKey(
        const SymbolStableKey& key,
        bool outgoing = true) const;
    QList<SemanticRelationshipResult> getRelationshipResults(
        const SymbolStableKey& key,
        bool outgoing = true) const;

    QList<SemanticDiagnostic> getDiagnostics(const QString& fileName = QString()) const;

private:
    PublicationObserver m_publicationObserver;
    SemanticIndexSnapshot captureSnapshotWithRawDiagnostics() const;
    SymbolRelationshipEngine* m_relationshipEngine = nullptr;
    std::shared_ptr<const SemanticIndexSnapshot> m_snapshot;
    bool m_preparedAnalysisBandReportValid = false;
    SemanticAnalysisBandReport m_preparedAnalysisBandReport;
    std::uint64_t m_snapshotRevision = 0;
    QHash<QString, SemanticAnalysisBandMetadata>
        m_workspaceFileAnalysisBands;
    std::uint64_t m_workspaceAnalysisBandRevision = 0;
    static std::unique_ptr<SemanticIndex> instance;

    SemanticSymbolRecord recordWithAnalysisBand(
        SemanticSymbolRecord record) const;
    QList<SemanticSymbolRecord> recordsWithAnalysisBands(
        QList<SemanticSymbolRecord> records) const;

    SemanticDefinitionResult bestDefinitionFromCandidates(
        const QList<SemanticSymbolRecord>& candidates,
        const SemanticDefinitionQuery& query,
        bool localFile) const;
    QList<SymbolRelationshipEngine::RelationType> relationshipTypes() const;
};

#endif // SEMANTICINDEX_H
