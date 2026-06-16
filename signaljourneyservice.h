#ifndef SIGNALJOURNEYSERVICE_H
#define SIGNALJOURNEYSERVICE_H

#include "rtlinsightlink.h"
#include "semanticindex.h"

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
    RtlInsightCodeLink peerCodeLink;
    bool outgoing = false;
    QString directionDisplayName;
    QString relationshipTypeDisplayName;
    QString peerSymbolDisplayName;
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
    QList<SignalJourneyItem> assignments;
    QList<SignalJourneyItem> reads;
    QList<SignalJourneyItem> portConnections;
    QList<SignalJourneyItem> interfaceConnections;
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

    bool isJourneyDeclaration(const sym_list::SymbolInfo& symbol) const;
    bool isInterfaceConnectionPeer(const sym_list::SymbolInfo& symbol) const;
    QSet<QString> interfaceNames() const;
    static QString directionDisplayName(bool outgoing);
    static QString relationshipTypeDisplayName(SymbolRelationshipEngine::RelationType type);
    static QString symbolDisplayName(const sym_list::SymbolInfo& symbol);
    static QString fileDisplayName(const QString& fileName);
    static QString lineDisplayName(int line);
    static RtlInsightCodeLink codeLink(const sym_list::SymbolInfo& symbol);
    static QString interfaceBaseName(const QString& dataType);
    static void fillDeclarationDisplayMetadata(SignalJourneyReport& report);
    static void fillDisplayMetadata(SignalJourneyItem& item);
    static void sortItems(QList<SignalJourneyItem>& items);
};

#endif // SIGNALJOURNEYSERVICE_H
