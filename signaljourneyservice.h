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
    SymbolStableKey signalStableKey;
    QString signalName;
    QString fileName;
    QString moduleName;
};

enum class SignalJourneyNotFoundReason {
    None,
    EmptySignalName,
    NoMatchingSignal,
    UnsupportedSymbolKind
};

struct SignalJourneyItem {
    SemanticRelationshipResult relationship;
    SemanticSymbolRecord peerSymbolRecord;
    SemanticSymbolRecord fromSymbolRecord;
    SemanticSymbolRecord toSymbolRecord;
    SymbolStableKey peerStableKey;
    SymbolStableKey fromStableKey;
    SymbolStableKey toStableKey;
    RtlInsightCodeLink peerCodeLink;
    RtlInsightCodeLink fromCodeLink;
    RtlInsightCodeLink toCodeLink;
    bool outgoing = false;
    RelationshipProvenance provenance = RelationshipProvenance::Unknown;
    int confidence = 0;
    QString evidenceText;
    QString directionDisplayName;
    QString relationshipTypeDisplayName;
    QString provenanceDisplayName;
    QString confidenceDisplayName;
    QString evidenceDisplayName;
    QString peerSymbolDisplayName;
    QString fromSymbolDisplayName;
    QString toSymbolDisplayName;
    QString fromTypeDisplayName;
    QString toTypeDisplayName;
    QString fromSourceRoleDisplayName;
    QString toSourceRoleDisplayName;
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
    SignalJourneyNotFoundReason notFoundReason =
        SignalJourneyNotFoundReason::None;
    sym_list::SymbolInfo declaration = {};
    SemanticSymbolRecord declarationSymbolRecord;
    SymbolStableKey declarationStableKey;
    RtlInsightCodeLink declarationCodeLink;
    QString notFoundReasonDisplayName;
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
    sym_list::SymbolInfo resolveSignal(
        const SignalJourneyQuery& query,
        SignalJourneyNotFoundReason* reason) const;
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
    static QString notFoundReasonDisplayName(SignalJourneyNotFoundReason reason);
    static QString provenanceDisplayName(RelationshipProvenance provenance);
    static QString confidenceDisplayName(int confidence);
    static QString evidenceDisplayName(const QString& evidenceText);
    static QString interfaceConnectionKindDisplayName(
        const SemanticSymbolRecord& record,
        const sym_list::SymbolInfo& fallback);
    static QString interfaceBaseDisplayName(
        const SemanticSymbolRecord& record,
        const sym_list::SymbolInfo& fallback);
    static void fillDeclarationDisplayMetadata(SignalJourneyReport& report);
    static void fillDisplayMetadata(SignalJourneyItem& item,
                                    const sym_list::SymbolInfo& peerSymbol);
    static void fillInterfaceDisplayMetadata(
        SignalJourneyItem& item,
        const sym_list::SymbolInfo& peerSymbol);
    static void sortItems(QList<SignalJourneyItem>& items);
};

#endif // SIGNALJOURNEYSERVICE_H
