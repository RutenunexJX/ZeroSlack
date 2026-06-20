#ifndef SEMANTICDIFFSERVICE_H
#define SEMANTICDIFFSERVICE_H

#include "rtlinsightlink.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"

#include <QList>
#include <QString>
#include <memory>

enum class SemanticDiffChangeKind {
    Added,
    Removed,
    Modified
};

enum class SemanticDiffSymbolCategory {
    Port,
    Parameter,
    Instance,
    Signal,
    Package,
    Interface,
    Type
};

enum class SemanticDiffNotFoundReason {
    None,
    MissingSnapshot,
    NoChanges
};

struct SemanticDiffQuery {
    std::shared_ptr<const SemanticIndexSnapshot> beforeSnapshot;
    std::shared_ptr<const SemanticIndexSnapshot> afterSnapshot;
    QString moduleName;
    QString beforeFileName;
    QString afterFileName;
};

struct SemanticDiffSymbolChange {
    SemanticDiffChangeKind kind = SemanticDiffChangeKind::Added;
    SemanticDiffSymbolCategory category = SemanticDiffSymbolCategory::Signal;
    QString key;
    SemanticSymbolRecord beforeSymbolRecord;
    SemanticSymbolRecord afterSymbolRecord;
    SemanticSymbolRecord displaySymbolRecord;
    SymbolStableKey beforeStableKey;
    SymbolStableKey afterStableKey;
    SymbolStableKey displayStableKey;
    QString kindDisplayName;
    QString categoryDisplayName;
    QString categoryGroupDisplayName;
    QString symbolDisplayName;
    QString sourceRoleDisplayName;
    QString symbolTypeDisplayName;
    QString scopeDisplayName;
    QString beforeSymbolTypeDisplayName;
    QString afterSymbolTypeDisplayName;
    QString beforeScopeDisplayName;
    QString afterScopeDisplayName;
    QString beforeSourceRoleDisplayName;
    QString afterSourceRoleDisplayName;
    QString beforeDataTypeDisplayName;
    QString afterDataTypeDisplayName;
    QString detailDisplayName;
    RtlInsightCodeLink codeLink;
    RtlInsightCodeLink beforeCodeLink;
    RtlInsightCodeLink afterCodeLink;
};

struct SemanticDiffRelationshipChange {
    SemanticDiffChangeKind kind = SemanticDiffChangeKind::Added;
    QString key;
    SemanticRelationship beforeRelationship;
    SemanticRelationship afterRelationship;
    SemanticSymbolRecord beforeFromSymbolRecord;
    SemanticSymbolRecord beforeToSymbolRecord;
    SemanticSymbolRecord afterFromSymbolRecord;
    SemanticSymbolRecord afterToSymbolRecord;
    SemanticSymbolRecord displayFromSymbolRecord;
    SemanticSymbolRecord displayToSymbolRecord;
    SymbolStableKey beforeFromStableKey;
    SymbolStableKey beforeToStableKey;
    SymbolStableKey afterFromStableKey;
    SymbolStableKey afterToStableKey;
    SymbolStableKey displayFromStableKey;
    SymbolStableKey displayToStableKey;
    RtlInsightCodeLink fromCodeLink;
    RtlInsightCodeLink toCodeLink;
    RelationshipProvenance provenance = RelationshipProvenance::Unknown;
    int confidence = 0;
    QString evidenceText;
    QString kindDisplayName;
    QString relationshipTypeDisplayName;
    QString fromSymbolDisplayName;
    QString toSymbolDisplayName;
    QString provenanceDisplayName;
    QString confidenceDisplayName;
    QString evidenceDisplayName;
    QString categoryGroupDisplayName;
    QString sourceRoleDisplayName;
    QString detailDisplayName;
    RtlInsightCodeLink codeLink;
};

struct SemanticDiffDiagnosticChange {
    SemanticDiffChangeKind kind = SemanticDiffChangeKind::Added;
    QString key;
    SemanticDiagnostic beforeDiagnostic;
    SemanticDiagnostic afterDiagnostic;
    SemanticDiagnostic displayDiagnostic;
    QString kindDisplayName;
    QString severityDisplayName;
    QString categoryGroupDisplayName;
    QString sourceRoleDisplayName;
    QString detailDisplayName;
    RtlInsightCodeLink codeLink;
};

struct SemanticDiffReport {
    bool found = false;
    SemanticDiffNotFoundReason notFoundReason =
        SemanticDiffNotFoundReason::None;
    QString notFoundReasonDisplayName;
    QString symbolGroupDisplayName;
    QString relationshipGroupDisplayName;
    QString diagnosticGroupDisplayName;
    int symbolChangeCount = 0;
    int relationshipChangeCount = 0;
    int diagnosticChangeCount = 0;
    QList<SemanticDiffSymbolChange> symbolChanges;
    QList<SemanticDiffRelationshipChange> relationshipChanges;
    QList<SemanticDiffDiagnosticChange> diagnosticChanges;
};

class SemanticDiffService
{
public:
    static SemanticDiffService* getInstance();

    explicit SemanticDiffService(SemanticIndex* semanticIndex = nullptr);
    ~SemanticDiffService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    SemanticDiffReport buildSemanticDiff(const SemanticDiffQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<SemanticDiffService> instance;

    SemanticIndex* semanticIndex() const;

    static QList<SemanticDiffSymbolChange> symbolChanges(
        const SemanticDiffQuery& query);
    static QList<SemanticDiffRelationshipChange> relationshipChanges(
        const SemanticDiffQuery& query);
    static QList<SemanticDiffDiagnosticChange> diagnosticChanges(
        const SemanticDiffQuery& query);

    static bool symbolCategory(
        const SymbolTaxonomy::SemanticMetadata& metadata,
        SemanticDiffSymbolCategory* category);
    static bool symbolInScope(const SemanticSymbolRecord& record,
                              const QString& moduleName,
                              const QString& fileName);
    static bool relationshipInScope(
        const SemanticRelationship& relationship,
        const SemanticIndexSnapshot& snapshot,
        const SemanticDiffQuery& query,
        bool afterSide);
    static bool diagnosticInScope(const SemanticDiagnostic& diagnostic,
                                  const QString& fileName);

    static QString symbolKey(const SemanticSymbolRecord& record,
                             SemanticDiffSymbolCategory category);
    static QString symbolSignature(const SemanticSymbolRecord& record);
    static sym_list::SymbolInfo relationshipEndpointSymbol(
        const SemanticRelationship& relationship,
        const SemanticIndexSnapshot& snapshot,
        bool fromEndpoint);
    static SemanticSymbolRecord relationshipEndpointRecord(
        const SemanticRelationship& relationship,
        const SemanticIndexSnapshot& snapshot,
        bool fromEndpoint);
    static QString relationshipKey(const SemanticRelationship& relationship,
                                   const SemanticIndexSnapshot& snapshot);
    static QString diagnosticKey(const SemanticDiagnostic& diagnostic);
    static QString normalizedFileName(const QString& fileName);
    static QString changeKindName(SemanticDiffChangeKind kind);
    static QString changeKindDisplayName(SemanticDiffChangeKind kind);
    static QString symbolCategoryDisplayName(SemanticDiffSymbolCategory category);
    static QString symbolCategoryGroupDisplayName(SemanticDiffSymbolCategory category);
    static QString symbolScopeDisplayName(const sym_list::SymbolInfo& symbol);
    static QString relationshipTypeDisplayName(SymbolRelationshipEngine::RelationType type);
    static QString diagnosticSeverityDisplayName(SemanticDiagnostic::Severity severity);
    static QString notFoundReasonDisplayName(SemanticDiffNotFoundReason reason);
    static QString provenanceDisplayName(RelationshipProvenance provenance);
    static QString confidenceDisplayName(int confidence);
    static QString evidenceDisplayName(const QString& evidenceText);
    static void fillDisplayMetadata(SemanticDiffSymbolChange& change,
                                    const SemanticSymbolRecord& beforeRecord,
                                    const SemanticSymbolRecord& afterRecord);
    static void fillDisplayMetadata(SemanticDiffRelationshipChange& change,
                                    const sym_list::SymbolInfo& beforeFromSymbol,
                                    const sym_list::SymbolInfo& beforeToSymbol,
                                    const sym_list::SymbolInfo& afterFromSymbol,
                                    const sym_list::SymbolInfo& afterToSymbol);
    static void fillDisplayMetadata(SemanticDiffDiagnosticChange& change);

    static void sortSymbolChanges(QList<SemanticDiffSymbolChange>& changes);
    static void sortRelationshipChanges(
        QList<SemanticDiffRelationshipChange>& changes);
    static void sortDiagnosticChanges(QList<SemanticDiffDiagnosticChange>& changes);
};

#endif // SEMANTICDIFFSERVICE_H
