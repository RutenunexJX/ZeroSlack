#ifndef CLOCKRESETDOMAINSERVICE_H
#define CLOCKRESETDOMAINSERVICE_H

#include "rtlinsightlink.h"
#include "semanticindex.h"

#include <QList>
#include <QString>
#include <memory>

struct ClockResetDomainQuery {
    int moduleSymbolId = -1;
    QString moduleName;
    QString fileName;
};

struct ClockResetDomainMember {
    sym_list::SymbolInfo moduleSymbol = {};
    RtlInsightCodeLink moduleCodeLink;
    SemanticRelationshipResult relationship;
    QString sectionDisplayName;
    QString detailDisplayName;
};

struct ClockResetDomainEntry {
    sym_list::SymbolInfo domainSignal = {};
    RtlInsightCodeLink domainSignalCodeLink;
    QString sectionDisplayName;
    QString detailDisplayName;
    QList<ClockResetDomainMember> modules;
};

struct ClockResetDomainEvidenceRow {
    sym_list::SymbolInfo domainSignal = {};
    sym_list::SymbolInfo moduleSymbol = {};
    RtlInsightCodeLink signalCodeLink;
    RtlInsightCodeLink moduleCodeLink;
    SymbolRelationshipEngine::RelationType relationshipType =
        SymbolRelationshipEngine::CLOCKS;
    QString sectionDisplayName;
    QString signalDisplayName;
    QString moduleDisplayName;
    QString detailDisplayName;
    QString sourceRoleDisplayName;
};

struct ClockResetDomainReport {
    bool found = false;
    QString clockGroupDisplayName;
    QString resetGroupDisplayName;
    QString evidenceGroupDisplayName;
    QString ambiguityGroupDisplayName;
    QString unmappedGroupDisplayName;
    QList<ClockResetDomainEntry> clockDomains;
    QList<ClockResetDomainEntry> resetDomains;
    QList<ClockResetDomainEvidenceRow> evidenceRows;
    QList<ClockResetDomainEvidenceRow> ambiguityRows;
    QList<ClockResetDomainEvidenceRow> unmappedRows;
    int clockRelationshipCount = 0;
    int resetRelationshipCount = 0;
};

class ClockResetDomainService
{
public:
    static ClockResetDomainService* getInstance();

    explicit ClockResetDomainService(SemanticIndex* semanticIndex = nullptr);
    ~ClockResetDomainService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    ClockResetDomainReport buildClockResetDomainMap(
        const ClockResetDomainQuery& query = {}) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<ClockResetDomainService> instance;

    SemanticIndex* semanticIndex() const;
    QList<ClockResetDomainEntry> buildDomains(
        SymbolRelationshipEngine::RelationType type,
        const ClockResetDomainQuery& query,
        int* relationshipCount) const;
    QList<ClockResetDomainEvidenceRow> unmappedTimingRows(
        const ClockResetDomainQuery& query) const;

    static bool acceptsRelationship(const SemanticRelationshipResult& relationship,
                                    const ClockResetDomainQuery& query);
    static bool acceptsCandidate(const sym_list::SymbolInfo& symbol,
                                 const ClockResetDomainQuery& query,
                                 const sym_list::SymbolInfo& moduleSymbol);
    static bool isTimingCandidate(const sym_list::SymbolInfo& symbol,
                                  SymbolRelationshipEngine::RelationType* type);
    static bool hasMappedTimingRelationship(
        SemanticIndex* index,
        const sym_list::SymbolInfo& symbol,
        SymbolRelationshipEngine::RelationType type,
        const ClockResetDomainQuery& query);
    static sym_list::SymbolInfo moduleForCandidate(
        const sym_list::SymbolInfo& symbol,
        const QList<sym_list::SymbolInfo>& symbols);
    static QString normalizedFileName(const QString& fileName);
    static QString groupDisplayName(SymbolRelationshipEngine::RelationType type);
    static QString domainSectionDisplayName(SymbolRelationshipEngine::RelationType type);
    static QString domainDetailDisplayName(SymbolRelationshipEngine::RelationType type,
                                           int moduleCount);
    static QString memberDetailDisplayName(SymbolRelationshipEngine::RelationType type);
    static QList<ClockResetDomainEvidenceRow> evidenceRows(
        const QList<ClockResetDomainEntry>& clockDomains,
        const QList<ClockResetDomainEntry>& resetDomains);
    static QList<ClockResetDomainEvidenceRow> ambiguityRows(
        const QList<ClockResetDomainEntry>& clockDomains,
        const QList<ClockResetDomainEntry>& resetDomains);
    static ClockResetDomainEvidenceRow evidenceRow(
        const ClockResetDomainEntry& entry,
        const ClockResetDomainMember& member,
        SymbolRelationshipEngine::RelationType type);
    static QString evidenceDetailDisplayName(const QString& signalName,
                                             const QString& moduleName,
                                             SymbolRelationshipEngine::RelationType type);
    static QString ambiguityDetailDisplayName(const QString& moduleName,
                                              SymbolRelationshipEngine::RelationType type,
                                              int domainCount);
    static QString unmappedDetailDisplayName(const QString& signalName,
                                             SymbolRelationshipEngine::RelationType type);
    static QString sourceRoleDisplayName(SymbolTaxonomy::SourceRole role);
    static void fillEntryDisplayMetadata(ClockResetDomainEntry& entry,
                                         SymbolRelationshipEngine::RelationType type);
    static void sortEntries(QList<ClockResetDomainEntry>& entries);
    static void sortMembers(QList<ClockResetDomainMember>& members);
};

#endif // CLOCKRESETDOMAINSERVICE_H
