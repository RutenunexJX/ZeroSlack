#ifndef CLOCKRESETDOMAINSERVICE_H
#define CLOCKRESETDOMAINSERVICE_H

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
    SemanticRelationshipResult relationship;
};

struct ClockResetDomainEntry {
    sym_list::SymbolInfo domainSignal = {};
    QList<ClockResetDomainMember> modules;
};

struct ClockResetDomainReport {
    bool found = false;
    QList<ClockResetDomainEntry> clockDomains;
    QList<ClockResetDomainEntry> resetDomains;
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

    static bool acceptsRelationship(const SemanticRelationshipResult& relationship,
                                    const ClockResetDomainQuery& query);
    static QString normalizedFileName(const QString& fileName);
    static void sortEntries(QList<ClockResetDomainEntry>& entries);
    static void sortMembers(QList<ClockResetDomainMember>& members);
};

#endif // CLOCKRESETDOMAINSERVICE_H
