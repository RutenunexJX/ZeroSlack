#ifndef SIGNALJOURNEYSERVICE_H
#define SIGNALJOURNEYSERVICE_H

#include "rtlinsightlink.h"
#include "semanticindex.h"
#include "symboltaxonomy.h"

#include <QList>
#include <QSet>
#include <QString>
#include <memory>

struct SignalJourneyQuery {
    int signalSymbolId = -1;
    QString signalName;
    QString fileName;
    QString moduleName;
};

struct SignalJourneyItem {
    SemanticRelationshipResult relationship;
    sym_list::SymbolInfo peerSymbol = {};
    sym_list::SymbolInfo fromSymbol = {};
    sym_list::SymbolInfo toSymbol = {};
    RtlInsightCodeLink peerCodeLink;
    RtlInsightCodeLink fromCodeLink;
    RtlInsightCodeLink toCodeLink;
    bool outgoing = false;
    QString directionDisplayName;
    QString relationshipTypeDisplayName;
    QString peerSymbolDisplayName;
    QString fromSymbolDisplayName;
    QString toSymbolDisplayName;
    QString connectionKindDisplayName;
    QString peerTypeDisplayName;
    QString peerSourceRoleDisplayName;
    QString interfaceBaseDisplayName;
    QString peerFileDisplayName;
    QString peerLineDisplayName;
    QString detailDisplayName;
};

struct SignalJourneyReport {
    bool found = false;
    sym_list::SymbolInfo declaration = {};
    RtlInsightCodeLink declarationCodeLink;
    QString declarationDisplayName;
    QString declarationTypeDisplayName;
    QString declarationFileDisplayName;
    QString declarationLineDisplayName;
    QString declarationSourceRoleDisplayName;
    QList<SignalJourneyItem> assignments;
    QList<SignalJourneyItem> reads;
    QList<SignalJourneyItem> portConnections;
    QList<SignalJourneyItem> interfaceConnections;
    QList<SignalJourneyItem> timingConnections;
};

class SignalJourneyService
{
public:
    static SignalJourneyService* getInstance();

    explicit SignalJourneyService(SemanticIndex* semanticIndex = nullptr);
    ~SignalJourneyService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    SignalJourneyReport buildSignalJourney(const SignalJourneyQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<SignalJourneyService> instance;

    SemanticIndex* semanticIndex() const;
    sym_list::SymbolInfo resolveSignal(const SignalJourneyQuery& query) const;
    QList<SignalJourneyItem> relationshipItems(
        const sym_list::SymbolInfo& signal,
        bool outgoing,
        const QList<SymbolRelationshipEngine::RelationType>& types) const;
    QList<SignalJourneyItem> portConnectionItems(
        const sym_list::SymbolInfo& signal) const;
    QList<SignalJourneyItem> interfaceConnectionItems(
        const sym_list::SymbolInfo& signal) const;
    QList<SignalJourneyItem> timingConnectionItems(
        const sym_list::SymbolInfo& signal) const;

    bool isJourneyDeclaration(const sym_list::SymbolInfo& symbol) const;
    bool isInterfaceConnectionPeer(const sym_list::SymbolInfo& symbol) const;
    QSet<QString> interfaceNames() const;
    static QString directionDisplayName(bool outgoing);
    static QString relationshipTypeDisplayName(SymbolRelationshipEngine::RelationType type);
    static QString symbolDisplayName(const sym_list::SymbolInfo& symbol);
    static QString interfaceBaseName(const QString& dataType);
    static QString interfaceConnectionKindDisplayName(const sym_list::SymbolInfo& symbol);
    static QString interfaceBaseDisplayName(const sym_list::SymbolInfo& symbol);
    static QString sourceRoleDisplayName(SymbolTaxonomy::SourceRole role);
    static void fillDeclarationDisplayMetadata(SignalJourneyReport& report);
    static void fillDisplayMetadata(SignalJourneyItem& item);
    static void fillInterfaceDisplayMetadata(SignalJourneyItem& item);
    static void sortItems(QList<SignalJourneyItem>& items);
};

#endif // SIGNALJOURNEYSERVICE_H
