#include <zeroslack/semantic/signalfeedbackservice.h>
#include "semanticindexsnapshot.h"
#include <QCryptographicHash>
#include <QQueue>
#include <QSet>
#include <algorithm>

namespace {
QString hash(const QString& text) {
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(),QCryptographicHash::Sha256).toHex());
}
bool within(const QString& value,const QString& scope) {
    return value==scope || value.startsWith(scope+QLatin1Char('.'));
}
bool dependency(const SemanticSignalDependency& fact) {
    return fact.kind==SignalDependencyKind::Data || fact.kind==SignalDependencyKind::Port || fact.kind==SignalDependencyKind::Control;
}
struct Edge { int from,to; SignalFeedbackEdge value; SignalStorageBoundary boundary; bool certain; };
}

QString SignalFeedbackService::kindName(SignalFeedbackKind kind) {
    switch (kind) {
    case SignalFeedbackKind::Combinational: return QStringLiteral("Static combinational feedback");
    case SignalFeedbackKind::Sequential: return QStringLiteral("Feedback through edge-triggered storage");
    default: return QStringLiteral("Uncertain feedback");
    }
}

SignalFeedbackReport SignalFeedbackService::analyze(const QString& instancePath,
    const std::function<bool()>& cancelled) const {
    SignalFeedbackReport report; report.context=m_context; report.context.instancePath=instancePath;
    if (!m_context.token.snapshot || instancePath.isEmpty()) {
        report.limitations<<QStringLiteral("Choose an actual elaborated instance before analyzing feedback."); return report;
    }
    SignalTraceService trace(m_context);
    const auto& snapshot=*m_context.token.snapshot;
    QList<SemanticRelationship> facts;
    QHash<QString,QList<SemanticSignalDependency>> driverFacts;
    QHash<QString,QStringList> unresolvedObjects;
    QSet<QString> seen;
    bool foundInstance=false;
    auto stop=[&] { return cancelled && cancelled(); };
    auto partial=[&](const QString& message) { report.status=SignalTraceStatus::Partial; report.limitations<<message; };
    report.status=SignalTraceStatus::Ready;
    // Adjacency indexes own the facts. Visit each declaration once, retaining evidence identities.
    for (const auto& symbol:snapshot.getSymbolRecords()) {
        if (stop()) { report.status=SignalTraceStatus::Cancelled; return report; }
        for (const auto& edge:snapshot.relationshipsForStableKey(symbol.stableKey,true)) {
            if (!edge.signalDependency) continue;
            const auto& fact=*edge.signalDependency;
            if (!within(fact.toInstance,instancePath)) continue;
            foundInstance=true;
            if(!trace.isSourceCurrent(edge.evidenceRange.fileName)){
                report.status=SignalTraceStatus::Stale;
                report.limitations<<QStringLiteral("Feedback source changed since analysis: %1").arg(edge.evidenceRange.fileName);
                return report;
            }
            if (fact.kind==SignalDependencyKind::Coverage) {
                driverFacts[fact.toObject].append(fact);
                report.limitations+=fact.limitations;
                if(fact.boundary==SignalStorageBoundary::Unknown)unresolvedObjects[fact.toObject]+=fact.limitations;
                if (fact.boundary==SignalStorageBoundary::Latch || fact.boundary==SignalStorageBoundary::Unknown)
                    partial(QStringLiteral("%1: %2").arg(fact.toObject,SignalTraceService::boundaryName(fact.boundary)));
            }
            if(fact.kind==SignalDependencyKind::Port)driverFacts[fact.toObject].append(fact);
            if (!dependency(fact) || !within(fact.fromInstance,instancePath)) continue;
            const QString key=semanticRelationshipStableKeyText(edge);
            if (seen.contains(key)) continue;
            seen.insert(key); facts.append(edge);
            if (facts.size()>=8192) { partial(QStringLiteral("Dependency limit reached (8192); results are incomplete.")); break; }
        }
        if (facts.size()>=8192) break;
    }
    if (!foundInstance) {
        report.status=SignalTraceStatus::Unavailable; report.limitations<<QStringLiteral("This instance is absent from the captured elaboration."); return report;
    }
    std::sort(facts.begin(),facts.end(),[](const auto& a,const auto& b) {
        return semanticRelationshipStableKeyText(a)<semanticRelationshipStableKeyText(b);
    });
    QHash<QString,int> nodes;
    QList<SignalTraceObject> objects;
    QList<Edge> edges;
    QHash<int,QList<int>> outgoing;
    auto node=[&](SignalTraceObject object,qint64 bit,bool known) {
        object.accessPath=object.symbol.symbolName; object.bits={known,bit,bit};
        const QString key=object.id();
        if (nodes.contains(key)) return nodes[key];
        if (nodes.size()>=4096) return -1;
        const int index=objects.size(); nodes.insert(key,index); objects.append(object); return index;
    };
    bool budget=false;
    for (const auto& relation:facts) {
        if (stop()) { report.status=SignalTraceStatus::Cancelled; return report; }
        const auto& fact=*relation.signalDependency;
        auto from=trace.endpoint(relation,true), to=trace.endpoint(relation,false);
        if (!from.isConcrete() || !to.isConcrete()) { partial(QStringLiteral("An endpoint is unresolved.")); continue; }
        bool certain=fact.complete;
        QStringList issues=fact.limitations;
        issues+=unresolvedObjects.value(fact.toObject);issues+=unresolvedObjects.value(fact.fromObject);
        if(!issues.isEmpty())certain=false;
        for (const auto& other:driverFacts.value(fact.toObject)) {
            if (other.processId!=fact.processId && other.toBits.overlaps(fact.toBits)) {
                certain=false; issues<<QStringLiteral("Overlapping drivers from distinct processes or ports."); break;
            }
        }
        const bool fine=from.bits.known && to.bits.known
            && from.bits.high-from.bits.low<256 && to.bits.high-to.bits.low<256;
        if (!fine) { certain=false; issues<<QStringLiteral("Unknown bounds or more than 256 bits; range-level feedback is uncertain."); }
        const qint64 fromLow=fine ? from.bits.low : 0, fromHigh=fine ? from.bits.high : 0;
        const qint64 toLow=fine ? to.bits.low : 0, toHigh=fine ? to.bits.high : 0;
        const bool direct=fine && fact.bitwiseForward
            && fromHigh-fromLow==toHigh-toLow;
        if(fact.kind!=SignalDependencyKind::Control && !direct && (fromHigh>fromLow || toHigh>toLow)){
            certain=false;issues<<QStringLiteral("Vector expression has conservative bit dependencies; an exact bit mapping is unavailable.");
        }
        for (qint64 a=fromLow;a<=fromHigh && !budget;++a) {
            const int source=node(from,a,fine);
            for (qint64 b=direct ? toLow+a-fromLow : toLow;b<=(direct ? toLow+a-fromLow : toHigh);++b) {
                const int target=node(to,b,fine);
                if (source<0 || target<0 || edges.size()>=16384) { budget=true; break; }
                auto evidence=trace.evidence(relation,to,fact.kind==SignalDependencyKind::Control ? SignalTraceRole::Control : SignalTraceRole::Write);
                evidence.dependency.limitations=issues;
                SignalFeedbackEdge value{hash(semanticRelationshipStableKeyText(relation)),objects[source],objects[target],evidence};
                outgoing[source].append(edges.size()); edges.append({source,target,value,fact.boundary,certain});
            }
        }
        if (budget) { partial(QStringLiteral("Graph limit reached (4096 bit nodes / 16384 edges); results are incomplete.")); break; }
    }
    report.nodeCount=nodes.size(); report.edgeCount=edges.size();
    QSet<QString> findingIds;
    // Separate passes retain pure combinational subcycles inside mixed storage SCCs.
    for (const auto kind:{SignalFeedbackKind::Combinational,SignalFeedbackKind::Sequential,SignalFeedbackKind::Uncertain}) {
        auto uncertain=[](const Edge& edge) { return !edge.certain || edge.boundary==SignalStorageBoundary::Unknown || edge.boundary==SignalStorageBoundary::Latch; };
        auto eligible=[&](const Edge& edge) {
            if (kind==SignalFeedbackKind::Combinational) return edge.certain && edge.boundary==SignalStorageBoundary::Combinational;
            if (kind==SignalFeedbackKind::Sequential) return !uncertain(edge);
            return true;
        };
        for (int first=0;first<edges.size();++first) {
            if (stop()) { report.status=SignalTraceStatus::Cancelled; report.findings.clear(); return report; }
            const auto& start=edges[first];
            if (!eligible(start) || (kind==SignalFeedbackKind::Sequential && start.boundary!=SignalStorageBoundary::EdgeTriggered)
                || (kind==SignalFeedbackKind::Uncertain && !uncertain(start))) continue;
            QQueue<int> pending; QHash<int,int> parent;
            pending.enqueue(start.to); parent[start.to]=-1;
            while (!pending.isEmpty() && !parent.contains(start.from) && report.operations<200000) {
                const int current=pending.dequeue();
                for (int index:outgoing.value(current)) {
                    ++report.operations;
                    const auto& next=edges[index];
                    if (!eligible(next) || parent.contains(next.to)) continue;
                    parent[next.to]=index; pending.enqueue(next.to);
                }
            }
            if (report.operations>=200000) { partial(QStringLiteral("Cycle search limit reached (200000 edge visits).")); break; }
            if (!parent.contains(start.from)) continue;
            QList<int> cycle{first}; QList<int> reverse;
            for (int at=start.from;at!=start.to;) { const int index=parent[at]; reverse.prepend(index); at=edges[index].from; }
            cycle.append(reverse);
            QStringList identity;
            for (int index:cycle) identity<<edges[index].value.id;
            identity.removeDuplicates(); identity.sort();
            const QString id=hash(m_context.workspaceId+QLatin1Char('|')+m_context.projectId+QLatin1Char('|')+instancePath
                +QString::number(int(kind))+identity.join(QLatin1Char('|')));
            if (findingIds.contains(id)) continue;
            findingIds.insert(id);
            SignalFeedbackFinding finding; finding.id=id; finding.kind=kind;
            for (int index:cycle) {
                finding.edges.append(edges[index].value); finding.limitations+=edges[index].value.evidence.dependency.limitations;
            }
            finding.limitations.removeDuplicates();
            finding.limitations<<QStringLiteral("Static dependencies do not establish runtime oscillation or condition reachability.");
            report.findings.append(finding);
            if (report.findings.size()>=32) { partial(QStringLiteral("Showing at most 32 representative cycles.")); break; }
        }
        if (report.operations>=200000 || report.findings.size()>=32) break;
    }
    report.limitations.removeDuplicates();
    if (!report.limitations.isEmpty() && report.status==SignalTraceStatus::Ready) report.status=SignalTraceStatus::Partial;
    return report;
}
