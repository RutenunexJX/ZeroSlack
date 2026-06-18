#ifndef RELATIONSHIPSERVICE_H
#define RELATIONSHIPSERVICE_H

#include "semanticindex.h"

#include <QList>
#include <QMap>
#include <QString>
#include <memory>

struct RelationshipQuery {
    SymbolStableKey symbolStableKey;
    QString symbolName;
    QString fileName;
    QString moduleName;
    bool outgoing = true;
    QList<SymbolRelationshipEngine::RelationType> types;
};

using RelationshipResult = SemanticRelationshipResult;

struct RelationshipBrowseQuery {
    SymbolStableKey symbolStableKey;
    QString symbolName;
    QString fileName;
    QString moduleName;
    bool includeOutgoing = true;
    bool includeIncoming = true;
    QList<SymbolRelationshipEngine::RelationType> types;
};

enum class RelationshipPanelDirection {
    All,
    Outgoing,
    Incoming
};

enum class RelationshipReportNotFoundReason {
    None,
    NoSubjectSymbol,
    NoRelationships
};

struct RelationshipPanelQueryOptions {
    QString symbolName;
    QString fileName;
    QString moduleName;
    RelationshipPanelDirection direction = RelationshipPanelDirection::All;
    int typeFilter = -1;
};

struct RelationshipTypeFilterOption {
    QString displayName;
    int value = -1;
};

struct DirectedRelationshipResult {
    enum Direction {
        Outgoing,
        Incoming
    };

    RelationshipResult relationship;
    Direction direction = Outgoing;
    sym_list::SymbolInfo peerSymbol;
    SemanticSymbolRecord peerSymbolRecord;
    SymbolStableKey subjectStableKey;
    SymbolStableKey peerStableKey;
    QString directionDisplayName;
    QString typeDisplayName;
    QString subjectRole;
    QString peerRole;
    QString explanation;
    RelationshipProvenance provenance = RelationshipProvenance::Unknown;
    int confidence = 0;
    QString evidenceText;
    QString provenanceDisplayName;
    QString confidenceDisplayName;
    QString evidenceDisplayName;
    QString peerSymbolDisplayName;
    QString peerFileDisplayName;
    QString peerLineDisplayName;
};

struct RelationshipTypeGroup {
    SymbolRelationshipEngine::RelationType type = SymbolRelationshipEngine::REFERENCES;
    QString displayName;
    QList<DirectedRelationshipResult> relationships;
    int count = 0;
};

struct RelationshipDirectionGroup {
    DirectedRelationshipResult::Direction direction = DirectedRelationshipResult::Outgoing;
    QString displayName;
    QList<RelationshipTypeGroup> typeGroups;
    int count = 0;
};

struct RelationshipReport {
    sym_list::SymbolInfo subjectSymbol = {};
    SemanticSymbolRecord subjectSymbolRecord;
    SymbolStableKey subjectStableKey;
    RelationshipReportNotFoundReason notFoundReason =
        RelationshipReportNotFoundReason::None;
    QString notFoundReasonDisplayName;
    QList<DirectedRelationshipResult> relationships;
    QList<RelationshipDirectionGroup> directionGroups;
    int totalCount = 0;
    int outgoingCount = 0;
    int incomingCount = 0;
    QMap<DirectedRelationshipResult::Direction, int> directionCounts;
    QMap<SymbolRelationshipEngine::RelationType, int> typeCounts;
    QMap<DirectedRelationshipResult::Direction,
         QMap<SymbolRelationshipEngine::RelationType, int>> directionTypeCounts;
};

class RelationshipService
{
public:
    static RelationshipService* getInstance();

    explicit RelationshipService(SemanticIndex* semanticIndex = nullptr);
    ~RelationshipService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QList<RelationshipResult> findRelationships(const RelationshipQuery& query) const;
    QList<RelationshipResult> findOutgoingRelationships(const RelationshipQuery& query) const;
    QList<RelationshipResult> findIncomingRelationships(const RelationshipQuery& query) const;
    RelationshipReport findRelationshipReport(const RelationshipBrowseQuery& query) const;
    QList<SymbolStableKey> findRelatedSymbolKeys(const RelationshipQuery& query) const;
    RelationshipBrowseQuery queryForPanel(
        const RelationshipPanelQueryOptions& options) const;
    static QList<RelationshipTypeFilterOption> relationshipPanelTypeFilterOptions();
    static QList<RelationshipTypeFilterOption> referencePanelTypeFilterOptions();
    bool hasRelationship(const SymbolStableKey& fromStableKey,
                         const SymbolStableKey& toStableKey,
                         SymbolRelationshipEngine::RelationType type) const;
    bool hasNamedRelationship(const QString& fromSymbolName,
                              const QString& toSymbolName,
                              SymbolRelationshipEngine::RelationType type) const;
    bool hasRelationships(const RelationshipQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<RelationshipService> instance;

    SemanticIndex* semanticIndex() const;
    SemanticSymbolRecord resolveSubjectSymbolRecord(
        const RelationshipQuery& query) const;
    SemanticSymbolRecord resolveSubjectSymbolRecord(
        const RelationshipBrowseQuery& query) const;
    sym_list::SymbolInfo resolveSubjectSymbol(const RelationshipQuery& query) const;
    sym_list::SymbolInfo resolveSubjectSymbol(const RelationshipBrowseQuery& query) const;
    static RelationshipQuery normalizedQuery(const RelationshipQuery& query);
    static RelationshipBrowseQuery normalizedQuery(const RelationshipBrowseQuery& query);
    bool typeMatches(SymbolRelationshipEngine::RelationType type,
                     const QList<SymbolRelationshipEngine::RelationType>& allowedTypes) const;
};

#endif // RELATIONSHIPSERVICE_H
