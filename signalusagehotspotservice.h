#ifndef SIGNALUSAGEHOTSPOTSERVICE_H
#define SIGNALUSAGEHOTSPOTSERVICE_H

#include "rtlinsightlink.h"
#include "semanticindex.h"
#include "signaljourneyservice.h"

#include <QList>
#include <QString>
#include <memory>

struct SignalUsageHotspotQuery {
    SymbolStableKey signalStableKey;
    QString signalName;
    QString signalAccessPath;
    QString fileName;
    QString moduleName;
};

enum class SignalUsageHotspotRole {
    Write,
    Read,
    Port,
    Condition,
    Case,
    Timing,
    Unknown
};

enum class SignalUsageHotspotNotFoundReason {
    None,
    EmptySignalName,
    NoMatchingSignal,
    UnsupportedSymbolKind
};

struct SignalUsageHotspotItem {
    SignalUsageHotspotRole role = SignalUsageHotspotRole::Unknown;
    QString moduleName;
    QString fileName;
    int line = 0;
    int column = 0;
    int endLine = 0;
    int endColumn = 0;
    QString snippet;
    QString evidenceText;
    QString evidenceKindDisplayName;
    QString roleReasonDisplayName;
    SymbolRelationshipEngine::RelationType relationshipType =
        SymbolRelationshipEngine::REFERENCES;
    QString relationshipTypeDisplayName;
    RelationshipProvenance provenance = RelationshipProvenance::Unknown;
    int confidence = 0;
    bool preciseEvidence = false;
    bool outgoing = false;
    SemanticSourceRange evidenceRange;
    SemanticSymbolRecord subjectSymbolRecord;
    SemanticSymbolRecord peerSymbolRecord;
    SemanticSymbolRecord fromSymbolRecord;
    SemanticSymbolRecord toSymbolRecord;
    SymbolStableKey subjectStableKey;
    SymbolStableKey peerStableKey;
    SymbolStableKey fromStableKey;
    SymbolStableKey toStableKey;
    QString subjectAccessPath;
    QString peerAccessPath;
    QString fromAccessPath;
    QString toAccessPath;
    RtlInsightCodeLink codeLink;
};

struct SignalUsageHotspotRoleSummary {
    SignalUsageHotspotRole role = SignalUsageHotspotRole::Unknown;
    QString roleDisplayName;
    int count = 0;
};

struct SignalUsageHotspotModuleSummary {
    QString moduleName;
    int count = 0;
    QList<SignalUsageHotspotRoleSummary> roleCounts;
};

struct SignalUsageHotspotFileSummary {
    QString fileName;
    int count = 0;
    QList<SignalUsageHotspotRoleSummary> roleCounts;
};

struct SignalUsageHotspotMatrixCell {
    QString moduleName;
    QString fileName;
    SignalUsageHotspotRole role = SignalUsageHotspotRole::Unknown;
    QString roleDisplayName;
    int count = 0;
};

struct SignalUsageHotspotTrackPosition {
    int itemIndex = -1;
    SignalUsageHotspotRole role = SignalUsageHotspotRole::Unknown;
    QString roleDisplayName;
    int line = 0;
    int column = 0;
    int endLine = 0;
    int endColumn = 0;
};

struct SignalUsageHotspotTrackLane {
    QString moduleName;
    QString fileName;
    int startLine = 0;
    int endLine = 0;
    int count = 0;
    QList<SignalUsageHotspotTrackPosition> positions;
};

struct SignalUsageHotspotReport {
    bool found = false;
    SignalUsageHotspotNotFoundReason notFoundReason =
        SignalUsageHotspotNotFoundReason::None;
    QString notFoundReasonDisplayName;
    SemanticSymbolRecord declarationSymbolRecord;
    SymbolStableKey declarationStableKey;
    RtlInsightCodeLink declarationCodeLink;
    QString declarationDisplayName;
    QString declarationTypeDisplayName;
    QString declarationFileDisplayName;
    QString declarationLineDisplayName;
    QList<SignalUsageHotspotItem> items;
    QList<SignalUsageHotspotModuleSummary> moduleSummaries;
    QList<SignalUsageHotspotFileSummary> fileSummaries;
    QList<SignalUsageHotspotRoleSummary> roleSummaries;
    QList<SignalUsageHotspotMatrixCell> matrixCells;
    QList<SignalUsageHotspotTrackLane> trackLanes;
};

class SignalUsageHotspotService
{
public:
    static SignalUsageHotspotService* getInstance();

    explicit SignalUsageHotspotService(SemanticIndex* semanticIndex = nullptr);
    ~SignalUsageHotspotService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    SignalUsageHotspotReport buildSignalUsageHotspot(
        const SignalUsageHotspotQuery& query) const;

    static QString roleDisplayName(SignalUsageHotspotRole role);
    static QString notFoundReasonDisplayName(
        SignalUsageHotspotNotFoundReason reason);

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<SignalUsageHotspotService> instance;

    SemanticIndex* semanticIndex() const;
};

#endif // SIGNALUSAGEHOTSPOTSERVICE_H
