#include <zeroslack/semantic/signaltraceservice.h>
#include "semanticindexsnapshot.h"
#include "semanticanalysisinput.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

namespace {
QString digest(const QJsonObject& object) {
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
bool sameFile(const QString& a,const QString& b) { return SemanticInputCapture::pathKey(a)==SemanticInputCapture::pathKey(b); }
}

QJsonObject SignalTraceObject::toJson() const {
    return {{"workspace",workspaceId},{"project",projectId},{"instance",instancePath},{"object",objectPath},
        {"file",symbol.fileName},{"name",symbol.symbolName},{"kind",int(symbol.declarationKind)},
        {"owner",symbol.ownerScope},{"position",symbol.sourcePosition},{"length",symbol.sourceLength},
        {"access",accessPath},{"bitsKnown",bits.known},{"low",double(bits.low)},{"high",double(bits.high)},
        {"sourceHash",QString::fromLatin1(sourceHash.toHex())}};
}
SignalTraceObject SignalTraceObject::fromJson(const QJsonObject& value) {
    SignalTraceObject result;
    result.workspaceId=value["workspace"].toString(); result.projectId=value["project"].toString();
    result.instancePath=value["instance"].toString(); result.objectPath=value["object"].toString();
    result.symbol={value["file"].toString(),value["name"].toString(),SymbolTaxonomy::DeclarationKind(value["kind"].toInt()),
        value["owner"].toString(),value["position"].toInt(-1),value["length"].toInt()};
    result.accessPath=value["access"].toString();
    result.bits={value["bitsKnown"].toBool(),qint64(value["low"].toDouble()),qint64(value["high"].toDouble())};
    result.sourceHash=QByteArray::fromHex(value["sourceHash"].toString().toLatin1());
    return result;
}
QString SignalTraceObject::id() const {
    auto json=toJson(); json.remove("sourceHash");
    return digest(json);
}
QString SignalTraceObject::displayName() const { return accessPath.isEmpty() ? symbol.symbolName : accessPath; }

SignalTraceService::SignalTraceService(SignalTraceContext context) : m_context(std::move(context)) {}

std::shared_ptr<const SignalTraceService::SourceInfo> SignalTraceService::source(const QString& file) const {
    const auto key=SemanticInputCapture::pathKey(file);
    QMutexLocker lock(&m_sourceMutex);
    const auto found=m_sources.constFind(key);
    if (found!=m_sources.cend()) return *found;
    auto info=std::make_shared<SourceInfo>();
    if (m_context.token.snapshot) {
        const auto captured=m_context.token.snapshot->cachedFileSource(file);
        if (captured.exists) {
            info->text=captured.text;
            info->hash=SemanticSignalRoleEvidence::sourceHash(captured.text);
            info->current=true;
            for(auto it=m_context.currentSourceHashes.cbegin();it!=m_context.currentSourceHashes.cend();++it){
                const auto opened=m_context.token.snapshot->cachedFileSource(it.key());
                if((sameFile(it.key(),file) || (opened.exists && opened.fileKey==captured.fileKey)) && *it!=info->hash)
                    info->current=false;
            }
            info->lines.append(0);
            for (qsizetype index=0;index<captured.text.size();++index)
                if (captured.text.at(index)==QLatin1Char('\n')) info->lines.append(index+1);
        }
    }
    m_sources.insert(key,info);
    return info;
}

SignalTraceObject SignalTraceService::endpoint(const SemanticRelationship& edge,bool from) const {
    SignalTraceObject object;
    if (!m_context.token.snapshot || !edge.signalDependency) return object;
    const auto& fact=*edge.signalDependency;
    object.workspaceId=m_context.workspaceId; object.projectId=m_context.projectId;
    object.symbol=from ? edge.fromStableKey : edge.toStableKey;
    object.accessPath=from ? edge.fromAccessPath : edge.toAccessPath;
    object.instancePath=from ? fact.fromInstance : fact.toInstance;
    object.objectPath=from ? fact.fromObject : fact.toObject;
    object.bits=from ? fact.fromBits : fact.toBits;
    object.sourceHash=source(object.symbol.fileName)->hash;
    return object;
}

SignalTraceResolution SignalTraceService::resolve(const SymbolStableKey& key,const QString& name,
    const QString& file,const QString& module,const QString& accessPath) const {
    SignalTraceResolution result;
    if (!m_context.token.snapshot) { result.message=QStringLiteral("Semantic analysis is not available."); return result; }
    const auto& snapshot=*m_context.token.snapshot;
    auto record=snapshot.getSymbolRecordByStableKey(key);
    if (!record.isValid() && !key.isValid()) {
        auto records=snapshot.getSymbolRecordsByName(name);
        records.erase(std::remove_if(records.begin(),records.end(),[&](const auto& candidate) {
            return (!file.isEmpty() && !sameFile(candidate.location.fileName,file)) ||
                (!module.isEmpty() && candidate.owner.name!=module) ||
                (!SymbolTaxonomy::isSignalDeclaration(semanticMetadataForSymbolRecord(candidate))
                 && !SymbolTaxonomy::isPortDeclaration(semanticMetadataForSymbolRecord(candidate)));
        }),records.end());
        if (records.size()==1) record=records.first();
        else if (records.size()>1) { result.message=QStringLiteral("The declaration is ambiguous; select its source declaration."); return result; }
    }
    if (!record.isValid()) { result.message=QStringLiteral("The saved declaration is unavailable in this snapshot."); return result; }
    if (!SymbolTaxonomy::isSignalDeclaration(semanticMetadataForSymbolRecord(record))
        && !SymbolTaxonomy::isPortDeclaration(semanticMetadataForSymbolRecord(record))) {
        result.message=QStringLiteral("Select a signal declaration."); return result;
    }
    result.object.workspaceId=m_context.workspaceId; result.object.projectId=m_context.projectId;
    result.object.symbol=record.stableKey;
    result.object.accessPath=accessPath.isEmpty() ? record.name : accessPath;
    result.object.sourceHash=source(record.location.fileName)->hash;
    const bool specificAccess=!accessPath.isEmpty() && accessPath!=record.name;
    QSet<QString> objects;
    for (const auto& edge : snapshot.relationshipsForStableKey(record.stableKey,true)
                              + snapshot.relationshipsForStableKey(record.stableKey,false)) {
        if (!edge.signalDependency) continue;
        if(!specificAccess && edge.signalDependency->kind!=SignalDependencyKind::Identity)continue;
        for (bool from : {true,false}) {
            if(!((from?edge.fromStableKey:edge.toStableKey)==record.stableKey))continue;
            const auto& fact=*edge.signalDependency;
            const QString path=from?fact.fromObject:fact.toObject;
            if(!specificAccess && objects.contains(path))continue;
            if(specificAccess && (from?edge.fromAccessPath:edge.toAccessPath).simplified()!=accessPath.simplified())continue;
            auto object=endpoint(edge,from);
            if (!(object.symbol==record.stableKey) || !object.isConcrete()) continue;
            // Object choice identifies the whole declaration; sliced queries are explicit endpoints.
            if(!specificAccess){object.accessPath=record.name; object.bits={};}
            if (!m_context.instancePath.isEmpty() && object.instancePath!=m_context.instancePath) continue;
            const auto identity=specificAccess?object.id():path;
            if (objects.contains(identity)) continue;
            objects.insert(identity); result.choices.append(object);
        }
    }
    for (auto it=record.presentation.objectInfoByPath.cbegin();it!=record.presentation.objectInfoByPath.cend();++it) {
        if(specificAccess)break;
        const QString path=it.key();
        QString instance;
        for (auto owner=record.presentation.instanceInfoByPath.cbegin();owner!=record.presentation.instanceInfoByPath.cend();++owner)
            if (path.startsWith(owner.key()+'.') && owner.key().size()>instance.size()) instance=owner.key();
        if (instance.isEmpty()) continue;
        if (!m_context.instancePath.isEmpty() && instance!=m_context.instancePath) continue;
        auto object=result.object; object.instancePath=instance; object.objectPath=path; object.accessPath=record.name;
        if (!objects.contains(path)) { objects.insert(path); result.choices.append(object); }
    }
    std::sort(result.choices.begin(),result.choices.end(),[](const auto& a,const auto& b) { return a.objectPath<b.objectPath; });
    if (result.choices.size()==1) {
        result.object=result.choices.first();
        result.status=SignalTraceStatus::Ready;
    } else {
        result.status=SignalTraceStatus::DefinitionOnly;
        result.message=result.choices.isEmpty() ? QStringLiteral("Definition only. No actual instance is available.")
            : QStringLiteral("Definition only. Select an actual instance to follow dependencies.");
    }
    return result;
}

bool SignalTraceService::isSourceCurrent(const QString& file)const{return source(file)->current;}
bool SignalTraceService::validate(const SignalTraceObject& object,QString* reason) const {
    auto fail=[&](const QString& text) { if (reason) *reason=text; return false; };
    if (!m_context.token.snapshot || !object.isValid()) return fail(QStringLiteral("No valid semantic target."));
    if (object.workspaceId!=m_context.workspaceId || object.projectId!=m_context.projectId)
        return fail(QStringLiteral("The target belongs to another workspace or project."));
    const auto& snapshot=*m_context.token.snapshot;
    const auto record=snapshot.getSymbolRecordByStableKey(object.symbol);
    if (!record.isValid()) return fail(QStringLiteral("The declaration no longer matches."));
    if (object.sourceHash.isEmpty() || object.sourceHash!=source(object.symbol.fileName)->hash)
        return fail(QStringLiteral("Source changed. Refresh the target; saved paths remain invalid until reviewed."));
    if(!isSourceCurrent(object.symbol.fileName))return fail(QStringLiteral("Editor changes are not represented by this semantic snapshot yet."));
    if (object.isConcrete()) {
        bool present=record.presentation.objectInfoByPath.contains(object.objectPath)
            && record.presentation.instanceInfoByPath.contains(object.instancePath)
            && object.objectPath.startsWith(object.instancePath+QLatin1Char('.'));
        if(!present)for(const auto& edge:snapshot.relationshipsForStableKey(object.symbol,true)){
            if(!edge.signalDependency)continue;
            const auto& fact=*edge.signalDependency;
            if(edge.fromStableKey==object.symbol && fact.fromObject==object.objectPath && fact.fromInstance==object.instancePath){present=true;break;}
        }
        if (!present)
            return fail(QStringLiteral("The actual instance/object is no longer present."));
        const auto info=record.presentation.objectInfoByPath.value(object.objectPath);
        if(object.bits.known && (object.bits.low<0 || object.bits.high<object.bits.low
            || (info.fixedSize && info.bitWidth>0 && quint64(object.bits.high)>=info.bitWidth)))
            return fail(QStringLiteral("The saved bit range is outside this elaborated object."));
    }
    return true;
}

SignalTraceEvidence SignalTraceService::evidence(const SemanticRelationship& edge,const SignalTraceObject& subject,
    SignalTraceRole role,bool definitionOnly) const {
    SignalTraceEvidence item;
    item.role=role; item.range=edge.evidenceRange;
    item.link=RtlInsightLink::fromFileLine(item.range.fileName,item.range.line,item.range.column);
    item.objectId=subject.id(); item.definitionOnly=definitionOnly;
    if (m_context.token.snapshot) item.moduleName=m_context.token.snapshot->getSymbolRecordByStableKey(subject.symbol).owner.name;
    if (m_context.token.snapshot) {
        const auto captured=source(item.range.fileName);
        if (!captured->hash.isEmpty()) {
            item.sourceHash=captured->hash;
            item.fileLineCount=captured->lines.size();
            if (item.range.line>0 && item.range.line<=captured->lines.size()) {
                const auto start=captured->lines.at(item.range.line-1);
                const auto end=item.range.line<captured->lines.size() ? captured->lines.at(item.range.line) : captured->text.size();
                item.text=captured->text.mid(start,end-start).trimmed();
            }
        }
    }
    if (item.text.isEmpty()) item.text=edge.evidenceText;
    if (edge.signalDependency) item.dependency=*edge.signalDependency;
    item.id=digest({{"object",item.objectId},{"role",int(role)},{"file",SemanticInputCapture::pathKey(item.range.fileName)},
        {"line",item.range.line},{"column",item.range.column},{"endLine",item.range.endLine},{"endColumn",item.range.endColumn}});
    return item;
}

SignalTracePage SignalTraceService::oneHop(const SignalTraceObject& object,SignalTraceDirection direction,
    int cursor,int pageSize,const std::function<bool()>& cancelled) const {
    SignalTracePage result;
    result.subject=object; result.direction=direction; result.semanticRevision=m_context.token.revision;
    if(cancelled && cancelled()){result.status=SignalTraceStatus::Cancelled;return result;}
    QString reason;
    if (!validate(object,&reason)) { result.status=SignalTraceStatus::Stale; result.limitations << reason; return result; }
    pageSize=qBound(1,pageSize,250);
    const QString cacheKey=object.id()+QLatin1Char('/')+QString::number(int(direction))+QLatin1Char('/')+QString::number(cursor)+QLatin1Char('/')+QString::number(pageSize);
    {QMutexLocker lock(&m_pageMutex);if(const auto* cached=m_pages.object(cacheKey))return *cached;}
    const bool outgoing=direction==SignalTraceDirection::Downstream;
    const auto& snapshot=*m_context.token.snapshot;
    const auto edges=snapshot.relationshipsForStableKey(object.symbol,outgoing);
    QHash<QString,int> groupIndexes;
    QSet<QString> referenceIds;
    pageSize=qBound(1,pageSize,250);
    const int start=qBound(0,cursor,int(edges.size()));
    result.status=object.isConcrete() ? SignalTraceStatus::Ready : SignalTraceStatus::DefinitionOnly;
    for (int index=start;index<edges.size();++index) {
        if (cancelled && cancelled()) { result.status=SignalTraceStatus::Cancelled; result.groups.clear(); result.references.clear(); return result; }
        if (result.scanned>=2000 || result.groups.size()>=pageSize) {
            result.nextCursor=index; result.status=SignalTraceStatus::Partial; break;
        }
        ++result.scanned;
        const auto& edge=edges.at(index);
        if (!edge.signalDependency && !object.isConcrete()) {
            SignalTraceRole role=SignalTraceRole::Unknown;
            if (edge.type==SymbolRelationshipEngine::ASSIGNS_TO) role=outgoing ? SignalTraceRole::Read : SignalTraceRole::Write;
            else if (edge.type==SymbolRelationshipEngine::READS_FROM) role=SignalTraceRole::Control;
            else if (edge.type==SymbolRelationshipEngine::CLOCKS || edge.type==SymbolRelationshipEngine::RESETS) role=SignalTraceRole::Clock;
            else if (edge.type!=SymbolRelationshipEngine::REFERENCES) continue;
            auto item=evidence(edge,object,role,true);
            if (!referenceIds.contains(item.id)) { result.references.append(item); referenceIds.insert(item.id); }
            continue;
        }
        if (!object.isConcrete() || !edge.signalDependency) continue;
        const auto current=endpoint(edge,outgoing);
        if (current.instancePath!=object.instancePath || current.objectPath!=object.objectPath || !current.bits.overlaps(object.bits)) continue;
        const auto& fact=*edge.signalDependency;
        result.limitations += fact.limitations;
        if (fact.kind==SignalDependencyKind::Coverage || fact.kind==SignalDependencyKind::Identity
            || fact.kind==SignalDependencyKind::Reference) continue;
        const auto peer=endpoint(edge,!outgoing);
        const bool dataPath=fact.kind==SignalDependencyKind::Data || fact.kind==SignalDependencyKind::Port;
        const auto role=fact.kind==SignalDependencyKind::Port ? SignalTraceRole::Port
            : fact.kind==SignalDependencyKind::Control ? SignalTraceRole::Control
            : fact.kind==SignalDependencyKind::Clock ? SignalTraceRole::Clock
            : outgoing ? SignalTraceRole::Read : SignalTraceRole::Write;
        auto item=evidence(edge,object,role);
        if(!isSourceCurrent(item.range.fileName)){result.status=SignalTraceStatus::Stale;result.groups.clear();
            result.limitations<<QStringLiteral("Dependency source changed since analysis: %1").arg(item.range.fileName);return result;}
        if (!peer.isConcrete()) continue;
        const QString groupKey=peer.id()+(dataPath ? QStringLiteral("/data") : QStringLiteral("/related"));
        auto found=groupIndexes.constFind(groupKey);
        if (found==groupIndexes.cend()) {
            groupIndexes.insert(groupKey,result.groups.size());
            result.groups.append({peer,{item},dataPath,fact.limitations});
        } else {
            auto& group=result.groups[*found];
            if (std::none_of(group.evidence.cbegin(),group.evidence.cend(),[&](const auto& old) { return old.id==item.id; })) group.evidence.append(item);
            group.limitations += fact.limitations; group.limitations.removeDuplicates();
        }
    }
    result.limitations.removeDuplicates();
    int cost=1+result.groups.size()+result.references.size();for(const auto& group:result.groups)cost+=group.evidence.size();
    {QMutexLocker lock(&m_pageMutex);m_pages.insert(cacheKey,new SignalTracePage(result),cost);}
    return result;
}
SignalTracePage SignalTraceService::references(const SignalTraceObject& object,int cursor,int limit,
    const std::function<bool()>& cancelled) const {
    SignalTracePage result;
    result.subject=object; result.semanticRevision=m_context.token.revision;
    QString reason;
    if (!validate(object,&reason)) { result.status=SignalTraceStatus::Stale; result.limitations<<reason; return result; }
    const auto edges=m_context.token.snapshot->relationshipsForStableKey(object.symbol,true);
    result.status=object.isConcrete() ? SignalTraceStatus::Ready : SignalTraceStatus::DefinitionOnly;
    QSet<QString> seen;
    limit=qBound(1,limit,2000);
    for (int index=qBound(0,cursor,int(edges.size()));index<edges.size();++index) {
        if (cancelled && cancelled()) { result.references.clear(); result.status=SignalTraceStatus::Cancelled; return result; }
        if (result.references.size()>=limit || result.scanned>=10000) {
            result.nextCursor=index; result.status=SignalTraceStatus::Partial; break;
        }
        ++result.scanned;
        const auto& edge=edges[index];
        if (!edge.signalDependency || edge.signalDependency->kind!=SignalDependencyKind::Reference) continue;
        const auto target=endpoint(edge,true);
        if (object.isConcrete() && (target.instancePath!=object.instancePath || target.objectPath!=object.objectPath
            || !target.bits.overlaps(object.bits))) continue;
        auto item=evidence(edge,object,edge.signalDependency->referenceRole,!object.isConcrete());
        if(!isSourceCurrent(item.range.fileName)){result.status=SignalTraceStatus::Stale;result.references.clear();
            result.limitations<<QStringLiteral("Reference source changed since analysis: %1").arg(item.range.fileName);return result;}
        if (!seen.contains(item.id)) { seen.insert(item.id); result.references.append(item); }
    }
    return result;
}
QString SignalTraceService::roleName(SignalTraceRole role) {
    switch(role) {
    case SignalTraceRole::Write:return QStringLiteral("Write");
    case SignalTraceRole::Read:return QStringLiteral("Read");
    case SignalTraceRole::Port:return QStringLiteral("Port");
    case SignalTraceRole::Control:return QStringLiteral("Control");
    case SignalTraceRole::Clock:return QStringLiteral("Clock / reset");
    default:return QStringLiteral("Unknown");
    }
}
QString SignalTraceService::boundaryName(SignalStorageBoundary boundary) {
    switch(boundary) {
    case SignalStorageBoundary::Combinational:return QStringLiteral("Combinational");
    case SignalStorageBoundary::EdgeTriggered:return QStringLiteral("Edge-triggered storage");
    case SignalStorageBoundary::Latch:return QStringLiteral("Latch / incomplete assignment");
    default:return QStringLiteral("Unknown boundary");
    }
}
