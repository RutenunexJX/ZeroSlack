#include <zeroslack/semantic/semanticworkspacecache.h>
#include "semanticindexsnapshot.h"
#include <slang/util/VersionInfo.h>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtEndian>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
constexpr quint32 protocol = 1;
constexpr qint64 maximumPayload = 256 * 1024 * 1024;
constexpr qint64 maximumDecoded = 512 * 1024 * 1024;
constexpr qint64 maximumInputBytes = 128 * 1024 * 1024;
constexpr quint32 maximumItems = 2'000'000;
const QByteArray magic("ZSSEM001",8);

struct InvalidCache : std::runtime_error { InvalidCache() : std::runtime_error("Invalid semantic cache") {} };

// Every allocation is checked against both the remaining input and a shared
// budget. Qt's unbounded container/string extraction is deliberately avoided.
class Codec {
public:
    Codec(QByteArray& bytes, bool reading, std::function<bool()> cancelled = {})
        : buffer(&bytes), reading(reading), cancelled(std::move(cancelled)) {
        buffer.open(reading ? QIODevice::ReadOnly : QIODevice::WriteOnly);
        stream.setDevice(&buffer); stream.setVersion(QDataStream::Qt_6_0);
        stream.setByteOrder(QDataStream::LittleEndian);
    }
    template<class... T> void operator()(T&... values) { (value(values), ...); }
    bool atEnd() const { return buffer.atEnd(); }
    void check(qint64 allocation = 0) {
        if ((cancelled && cancelled()) || stream.status()!=QDataStream::Ok
            || buffer.pos()>maximumPayload || allocation<0 || allocation>budget) throw InvalidCache();
        budget -= allocation;
    }
    template<class T> void value(T& item) {
        check();
        if constexpr (std::is_enum_v<T>) {
            qint32 encoded = qint32(item); scalar(encoded);
            if(encoded<0 || encoded>4096) throw InvalidCache();
            if (reading) item=static_cast<T>(encoded);
        } else if constexpr (std::is_same_v<T,bool>) {
            quint8 encoded=item ? 1 : 0; scalar(encoded);
            if (encoded>1) throw InvalidCache();
            if (reading) item=encoded!=0;
        } else if constexpr (std::is_integral_v<T>) {
            using Wire = std::conditional_t<sizeof(T)<=4,
                std::conditional_t<std::is_signed_v<T>,qint32,quint32>,
                std::conditional_t<std::is_signed_v<T>,qint64,quint64>>;
            Wire encoded=Wire(item); scalar(encoded); if(reading) item=T(encoded);
        } else visit(*this,item);
    }
    void value(QString& text) {
        quint32 size=text.isNull() ? 0xffffffffu : quint32(text.size()*2);
        scalar(size);
        if(size==0xffffffffu) { if(reading) text=QString(); return; }
        if(size%2 || size>maximumPayload) throw InvalidCache();
        if(reading) {
            requireBytes(size); check(size); text=QString(size/2,Qt::Uninitialized);
            if(stream.readRawData(reinterpret_cast<char*>(text.data()),size)!=size) throw InvalidCache();
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
            qFromLittleEndian<quint16>(text.constData(),size/2,text.data());
#endif
        } else {
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
            QString little=text; qToLittleEndian<quint16>(text.constData(),size/2,little.data());
            stream.writeRawData(reinterpret_cast<const char*>(little.constData()),size);
#else
            stream.writeRawData(reinterpret_cast<const char*>(text.constData()),size);
#endif
        }
        check();
    }
    void value(QByteArray& bytes) {
        quint32 size=bytes.isNull()?0xffffffffu:quint32(bytes.size()); scalar(size);
        if(size==0xffffffffu) { if(reading) bytes=QByteArray(); return; }
        if(size>maximumPayload) throw InvalidCache();
        if(reading) { requireBytes(size); check(size); bytes.resize(size);
            if(stream.readRawData(bytes.data(),size)!=size) throw InvalidCache(); }
        else stream.writeRawData(bytes.constData(),size);
        check();
    }
    template<class T> void value(QList<T>& items) {
        const auto count=size(items.size(),sizeof(T));
        if(reading) { items.clear(); items.reserve(count);
            for(quint32 i=0;i<count;++i) { T item{}; value(item); items.append(std::move(item)); } }
        else for(auto it=items.cbegin();it!=items.cend();++it) value(const_cast<T&>(*it));
    }
    void value(QStringList& items) { value<QString>(static_cast<QList<QString>&>(items)); }
    template<class T> void value(QSet<T>& items) {
        const auto count=size(items.size(),sizeof(T)+48);
        if(reading) { items.clear(); for(quint32 i=0;i<count;++i) { T item{}; value(item);
            if(items.contains(item)) throw InvalidCache(); items.insert(std::move(item)); } }
        else { auto sorted=items.values(); std::sort(sorted.begin(),sorted.end());
            for(auto& item:sorted) value(item); }
    }
    template<class K,class V> void value(QHash<K,V>& items) { map(items); }
    template<class K,class V> void value(QMap<K,V>& items) { map(items); }
    template<class T> void value(std::shared_ptr<const T>& item) {
        bool present=bool(item); value(present);
        if(reading) { if(!present) item.reset(); else { check(sizeof(T)); auto decoded=std::make_shared<T>();
            value(*decoded); item=std::move(decoded); } }
        else if(present) value(const_cast<T&>(*item));
    }
private:
    QBuffer buffer;
    QDataStream stream;
    bool reading;
    std::function<bool()> cancelled;
    qint64 budget=maximumDecoded;
    void requireBytes(qint64 count) { if(count>buffer.bytesAvailable()) throw InvalidCache(); }
    template<class T> void scalar(T& item) {
        if(reading) { requireBytes(sizeof(T)); stream>>item; } else stream<<item;
        check();
    }
    quint32 size(qsizetype current, qsizetype cost) {
        quint32 count=quint32(current); scalar(count);
        if(count>maximumItems) throw InvalidCache();
        if(reading) { requireBytes(count); check(qint64(count)*cost); }
        return count;
    }
    template<class Map> void map(Map& items) {
        using K=typename Map::key_type; using V=typename Map::mapped_type;
        const auto count=size(items.size(),sizeof(K)+sizeof(V)+64);
        if(reading) { items.clear(); for(quint32 i=0;i<count;++i) { K key{}; V item{}; value(key); value(item);
            if(items.contains(key)) throw InvalidCache(); items.insert(std::move(key),std::move(item)); } }
        else { auto keys=items.keys(); std::sort(keys.begin(),keys.end());
            for(auto& key:keys) { value(key); value(const_cast<V&>(items.constFind(key).value())); } }
    }
};

// Structured bindings make an added aggregate field a compilation failure until
// the persistent value schema is reviewed. Handles are rebound, never persisted.
#define FIELDS(Type, ...) void visit(Codec& c, Type& item) { auto& [__VA_ARGS__]=item; c(__VA_ARGS__); }
FIELDS(SymbolStableKey,fileName,symbolName,declarationKind,ownerScope,sourcePosition,sourceLength)
FIELDS(SemanticSymbolLocation,fileName,startLine,startColumn,endLine,endColumn,position,length)
FIELDS(SemanticSymbolOwner,kind,name,stableKey,interfaceLike)
FIELDS(SemanticSymbolTypeReference,rawTypeText,resolvedTypeName,resolvedTypeKind,modportName,stableKey)
FIELDS(SemanticStructuredSelectorFact,kind,name,sourceIndex,storageIndex)
FIELDS(SemanticStructuredEnumValueFact,name,expressionText,valueText,displayValueText,available,expressionFailureReason,hierarchicalExpressionText,unqualifiedReferenceAvailable)
FIELDS(SemanticStructuredLeafFact,relativePath,selectors,direction,canonicalTypeId,resolvedTypeText,fixedSize,integral,signedIntegral,bitWidth,packedBitOffsetValid,packedBitOffset,enumValues)
FIELDS(SemanticSignalRoleEvidence,role,edge,activeLevel,rule,sourceSignal,sourceInstance,sourceFile,sourceLine,mappingChain,parameters,analysisIdentity,sourceHashes)
FIELDS(SemanticElaboratedSymbolInfo,available,portDirection,readable,writable,packedRank,unpackedRank,fixedSize,integral,unpackedArray,interfaceType,signedIntegral,bitWidth,enumValues,driverPresence,driverCount,continuousDriverCount,proceduralDriverCount,portConnectionDriverCount,signalRoles,signalRoleIssues,signalRoleConflict,sourceTextDisplaysEffectiveValue,valueText,displayValueText,expressionText,valueSourceText,resolvedTypeText,packedDimensionsText,unpackedDimensionsText,unpackedElementCountText,bitWidthText,signednessText,interfaceName,modportName,structuredLeavesAvailable,structuredLeaves,structuredFailureReason,failureReason)
FIELDS(SemanticConstantEvaluationNode,id,operationName,expressionText,operandIds,parameterDependencyIds,resultValueText,evaluated,evidenceId)
FIELDS(SemanticParameterDependencyNode,identity,name,localparam,rootEvaluationNodeId,dependencyIds,valueText,evaluated,evidenceId)
FIELDS(SemanticConstantDependencyGraph,evaluationNodes,parameters,complete,evidenceId,failureReason)
FIELDS(SemanticTypeDimensionFact,kind,canonicalId,declarationText,leftEvaluationNodeId,rightEvaluationNodeId,elementCountText,emitInDeclaration,complete,evidenceId)
FIELDS(SemanticTypedefResolutionStep,sourceTypeId,sourceTypeName,targetTypeId,declarationIdentity,introducedDimensionIds,evidenceId)
FIELDS(SemanticDeclaredTypeFacts,externalDeclarationBaseText,rootTypeId,canonicalTypeId,declarationShapeId,declarationBaseText,signednessKnown,signedIntegral,complete,dimensions,typedefChain,constants,evidenceId,failureReason)
FIELDS(SemanticCallableArgument,name,hasDefault,typeText,input)
FIELDS(SemanticSymbolPresentation,callableSignatureAvailable,callableArguments,callableQualifiedName,callableReturnType,callableReturnsVoid,directModuleVariable,declarationText,expressionText,enumTypeName,enumUnderlyingBitWidthText,packedDimensionsText,unpackedDimensionsText,effectiveScopeKind,qualifiedScopePath,computationRevision,documentRevision,defaultInfo,instanceInfoByPath,objectInfoByPath,elaboratedInstancePaths,defaultDeclaredTypeFacts,declaredTypeFactsByPath)
FIELDS(SemanticAnalysisBandMetadata,label,displayName,priority,publicationCheckpoint)
void visit(Codec& c,SemanticSymbolRecord& item) {
    auto& [stableKey,localHandle,name,location,declarationKind,usageRole,visibility,sourceRole,collectorKind,owner,type,presentation,analysisBand,compilationUnitFileName,compilationUnitSourceOrder,compilationUnitSourceOffset]=item;
    Q_UNUSED(localHandle)
    c(stableKey,name,location,declarationKind,usageRole,visibility,sourceRole,collectorKind,owner,type,presentation,analysisBand,compilationUnitFileName,compilationUnitSourceOrder,compilationUnitSourceOffset);
}
FIELDS(SemanticSourceRange,fileName,line,column,endLine,endColumn,position,length)
FIELDS(SignalBitRange,known,low,high)
FIELDS(SemanticSignalDependency,kind,boundary,fromInstance,toInstance,fromObject,toObject,fromBits,toBits,processId,processKind,boundaryEvidence,limitations,complete,referenceRole,sourceTrail,bitwiseForward)
void visit(Codec& c,SemanticRelationship& item) {
    auto& [fromId,toId,type,fromStableKey,toStableKey,fromAccessPath,toAccessPath,provenance,confidence,evidenceText,evidenceRange,exactValueForward,signalDependency]=item;
    Q_UNUSED(fromId) Q_UNUSED(toId)
    c(type,fromStableKey,toStableKey,fromAccessPath,toAccessPath,provenance,confidence,evidenceText,evidenceRange,exactValueForward,signalDependency);
}
FIELDS(SemanticDiagnostic,fileName,line,column,message,codeName,ranges,severity,owner,computationRevision,documentRevision)
FIELDS(EffectiveValueFact,kind,status,fileName,startPosition,endPosition,line,expressionText,valueText,resolvedTypeText,packedDimensionsText,unpackedDimensionsText,unpackedElementCountText,bitWidthText,signednessText,instancePath,anchorInstancePath,effectiveScopeKind,qualifiedScopePath,scopePath,provenance,failureReason,stableSourceIdentity,sourceTextDisplaysEffectiveValue,defaultEvaluation,computationRevision,documentRevision)
FIELDS(SemanticCapturedSource,text,readable,overridden,rawSha256)
FIELDS(SemanticAnalysisInput,projectIdentity,generation,sources,documentRevisions,watchFiles,watchDirectories)
FIELDS(ProjectSnapshot,revision,workspaceRoot,allFiles,systemVerilogFiles,includeDirs,defines,fileExtensions,sourceRoles,topModule,activeDesignProjectId,activeDesignTopModule,activeDesignTopFile,explicitDesignSelection,ignoredPaths,sourceDiscoveryComplete)
FIELDS(SemanticAnalysisRuntimePolicy,enabled,planningMode,maxDiagnostics)
FIELDS(SemanticModuleAssociationFact,name,position)
FIELDS(SemanticModuleInstantiationFact,ownerName,targetName,instanceName,constructKind,parameterAssociations,portAssociations,sourceLine,sourceColumn,syntaxComplete,failureReason)
FIELDS(SemanticFileDependencyFacts,fileName,includeNames,moduleDeclarations,packageDeclarations,macroDefinitions,apiDeclarations,instantiatedModules,moduleInstantiations,importedPackages,macroUses,symbolReferences,parseError)
#undef FIELDS

QString generator()
{
    return QStringLiteral("zeroslack-semantic/") + QStringLiteral(ZEROSLACK_SEMANTIC_GENERATOR_VERSION)
        + QStringLiteral("/slang-%1.%2.%3/").arg(slang::VersionInfo::getMajor()).arg(slang::VersionInfo::getMinor()).arg(slang::VersionInfo::getPatch())
        + QString::fromUtf8(slang::VersionInfo::getHash().data(),slang::VersionInfo::getHash().size());
}
QByteArray sha(const QByteArray& bytes) { return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(); }
QString normalized(const QString& path) { return SemanticInputCapture::pathKey(path); }

QByteArray configuration(const ProjectSnapshot& project, const SemanticAnalysisRuntimePolicy& policy,
    const QHash<QString, SemanticAnalysisBandMetadata>& bands)
{
    QByteArray bytes; Codec codec(bytes,false);
    auto identity=project.semanticIdentity(); auto roles=project.sourceRoles;
    auto normalizedPolicy=policy.normalized(); auto valueBands=bands;
    codec(identity,roles,normalizedPolicy,valueBands);
    return sha(bytes);
}

bool cleanInputs(const SemanticAnalysisRequest& request, const SemanticAnalysisInput& saved,
                 SemanticInputCapture& disk, const std::function<bool()>& cancelled)
{
    if(!request.project.sourceDiscoveryComplete || saved.projectIdentity!=request.project.semanticIdentity()) return false;
    QStringList paths=saved.sources.keys(); paths.append(request.project.systemVerilogFiles);
    paths.removeDuplicates();
    qint64 inputBytes=0;
    for(const auto& path:paths) {
        inputBytes += qMax(qint64(0),QFileInfo(path).size());
        if((cancelled && cancelled()) || inputBytes>maximumInputBytes
            || SemanticWorkspaceCache::ownsPath(request.project.workspaceRoot,path)) return false;
    }
    if(!disk.capture(paths,cancelled)) return false;
    for(auto it=saved.sources.cbegin();it!=saved.sources.cend();++it) {
        const auto& current=disk.source(it.key());
        if(current.readable!=it->readable || current.text!=it->text) return false;
        if(!it->overridden && current.rawSha256!=it->rawSha256) return false;
    }
    for(auto it=request.sourceOverrides.cbegin();it!=request.sourceOverrides.cend();++it) {
        const auto& source=disk.source(it.key());
        if(!source.readable || source.text!=it.value()) return false;
    }
    for(const auto& source:request.project.systemVerilogFiles)
        if(!disk.source(source).readable) return false;
    return true;
}

QByteArray readPayload(const QString& path, const QByteArray& config)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly) || file.size()<12 || file.size()>maximumPayload+16396) throw InvalidCache();
    if(file.read(8)!=magic) throw InvalidCache();
    QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
    quint32 headerSize=0; stream>>headerSize;
    if(headerSize>16384 || headerSize>file.bytesAvailable()) throw InvalidCache();
    const auto manifest=QJsonDocument::fromJson(file.read(headerSize)).object();
    if(manifest.value("protocol").toInt()!=int(protocol) || manifest.value("generator").toString()!=generator()
        || !manifest.value("complete").toBool() || manifest.value("configuration").toString().toLatin1()!=config
        || manifest.value("payloadBytes").toInteger()!=file.bytesAvailable()) throw InvalidCache();
    auto bytes=file.readAll();
    if(file.error()!=QFileDevice::NoError || bytes.size()>maximumPayload
        || sha(bytes)!=manifest.value("sha256").toString().toLatin1()) throw InvalidCache();
    return bytes;
}

void validateRecords(const QList<SemanticSymbolRecord>& records,
    const QList<SemanticRelationship>& relationships, const SemanticAnalysisInput& input)
{
    QSet<QString> identities;
    for(const auto& record:records) {
        if(!record.stableKey.isValid() || !input.sources.contains(normalized(record.location.fileName))
            || int(record.declarationKind)<0 || int(record.declarationKind)>int(SymbolTaxonomy::DeclarationKind::User)
            || int(record.usageRole)<0 || int(record.usageRole)>int(SymbolTaxonomy::SymbolUsageRole::Process)
            || int(record.collectorKind)<0 || int(record.collectorKind)>int(SymbolTaxonomy::CollectorKind::PackageImport)) throw InvalidCache();
        identities.insert(record.stableKey.toString());
    }
    for(const auto& edge:relationships)
        if(!identities.contains(edge.fromStableKey.toString()) || !identities.contains(edge.toStableKey.toString())) throw InvalidCache();
}
}

QString SemanticWorkspaceCache::directory(const QString& workspace, bool localFallback)
{
    if(workspace.trimmed().isEmpty()) return {};
    if(!localFallback) return QDir(workspace).filePath(QStringLiteral(".zeroslack/cache/semantic"));
    const auto isolatedProfile=qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT");
    const auto cacheRoot=isolatedProfile.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        : QDir(isolatedProfile).filePath("cache");
    return QDir(cacheRoot)
        .filePath(QStringLiteral("ZeroSlack/semantic/")+QString::fromLatin1(sha(normalized(workspace).toUtf8())));
}

bool SemanticWorkspaceCache::ownsPath(const QString& workspace, const QString& path)
{
    if(workspace.isEmpty() || path.isEmpty()) return false;
    const auto root=normalized(directory(workspace)), candidate=normalized(path);
    return candidate==root || candidate.startsWith(root+'/');
}

std::optional<WorkspaceAnalysisResult> SemanticWorkspaceCache::restore(
    const SemanticAnalysisRequest& request, const QHash<QString, SemanticAnalysisBandMetadata>& bands,
    const std::function<bool()>& cancelled)
{
    if(!request.project.isOpen() || !request.project.sourceDiscoveryComplete || request.triviaOnlyGate) return {};
    QElapsedTimer total; total.start();
    for(bool fallback:{false,true}) try {
        if(cancelled && cancelled()) return {};
        QElapsedTimer phase; phase.start();
        auto bytes=readPayload(directory(request.project.workspaceRoot,fallback)+"/current.zscache",
            configuration(request.project,request.runtimePolicy,bands));
        Codec codec(bytes,true,cancelled);
        SemanticAnalysisInput saved; codec(saved);
        for(const auto& source:saved.sources) if(source.overridden) throw InvalidCache();
        WorkspaceAnalysisResult result;
        result.cacheReadMs=phase.restart();
        SemanticAnalysisRequest diskRequest=request; diskRequest.sourceOverrides.clear(); diskRequest.documentRevisions.clear();
        SemanticInputCapture disk(diskRequest);
        if(!cleanInputs(request,saved,disk,cancelled)) throw InvalidCache();
        for(const auto& root:request.project.systemVerilogFiles)
            if(!saved.sources.contains(normalized(root))) throw InvalidCache();
        result.cacheValidationMs=phase.restart();
        QStringList contentFiles;
        QList<SemanticSymbolRecord> records;
        QList<SemanticRelationship> relationships;
        QList<SemanticDiagnostic> diagnostics;
        QHash<QString,QList<EffectiveValueFact>> facts;
        QHash<QString,SemanticFileDependencyFacts> dependencyFacts;
        QHash<QString,QStringList> observed;
        codec(contentFiles,records,relationships,diagnostics,facts,dependencyFacts,observed,result.fileAnalysisBands);
        if(!codec.atEnd()) throw InvalidCache();
        validateRecords(records,relationships,saved);
        QHash<QString,QString> contents;
        for(const auto& file:contentFiles) {
            const auto source=saved.sources.constFind(normalized(file));
            if(source==saved.sources.cend() || !source->readable) throw InvalidCache();
            contents.insert(file,source->text);
        }
        result.dependencyGraph=SemanticDependencyGraph::fromFacts(request.project,std::move(dependencyFacts),std::move(observed),cancelled);
        if(!disk.stillMatchesDisk(cancelled)) throw InvalidCache();
        // Fresh local identities and computation/document revisions belong to
        // this process. The persistent generation is never installed directly.
        const auto revision=request.computationRevision ? request.computationRevision
            : EffectiveValueService::getInstance()->beginComputation(contents.keys());
        QHash<QString,std::uint64_t> revisions;
        for(auto it=request.documentRevisions.cbegin();it!=request.documentRevisions.cend();++it)
            revisions.insert(normalized(it.key()),it.value());
        int handle=1;
        QHash<QString,int> handles;
        for(auto& record:records) {
            record.localHandle=handle++; handles.insert(record.stableKey.toString(),record.localHandle);
            record.presentation.computationRevision=revision;
            record.presentation.documentRevision=revisions.value(normalized(record.location.fileName));
        }
        for(auto& edge:relationships) { edge.fromId=handles.value(edge.fromStableKey.toString(),-1); edge.toId=handles.value(edge.toStableKey.toString(),-1); }
        for(auto& diagnostic:diagnostics) {
            diagnostic.computationRevision=revision; diagnostic.documentRevision=revisions.value(normalized(diagnostic.fileName));
        }
        for(auto it=facts.begin();it!=facts.end();++it) for(auto& fact:it.value()) {
            fact.computationRevision=revision; fact.documentRevision=revisions.value(normalized(fact.fileName));
        }
        result.request=request; result.request.computationRevision=revision;
        result.generation=request.generation; result.analysisRevision=revision;
        result.documentRevisionsByFile=revisions;
        auto captured=std::make_shared<SemanticAnalysisInput>(*disk.seal());
        captured->documentRevisions=revisions;
        for(auto it=request.sourceOverrides.cbegin();it!=request.sourceOverrides.cend();++it) {
            auto& source=captured->sources[normalized(it.key())]; source.overridden=true; source.rawSha256.clear();
        }
        result.input=std::move(captured);
        result.preparedSnapshot=std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(
            std::move(records),std::move(relationships),std::move(diagnostics),std::move(contents)));
        result.preparedEffectiveFactsState=EffectiveValueService::prepareDocumentFactsState(std::move(facts),
            result.preparedSnapshot->fileContentsView(),{},revisions,revision);
        result.preparedRelationshipState=std::make_shared<SymbolRelationshipEngine::PreparedRelationshipState>();
        result.preparedRelationshipState->snapshot=result.preparedSnapshot; result.relationshipDeltaPrepared=true;
        result.totalSymbols=result.preparedSnapshot->symbolRecordCount();
        result.incrementalPlan.reason=request.reason; result.incrementalPlan.impact=SemanticChangeImpact::WorkspaceConfig;
        result.incrementalPlan.affectedFiles=request.project.systemVerilogFiles;
        result.incrementalPlan.changedFiles=request.changedFiles;
        result.incrementalPlan.authoritativeWorkspaceReplace=true;
        result.reusedWorkspace=true; result.persistentCacheHit=true;
        result.cachePreparationMs=phase.elapsed(); result.workerElapsedMs=total.elapsed();
        if(cancelled && cancelled()) return {};
        return result;
    } catch(const std::exception&) { if(cancelled && cancelled()) return {}; }
    return {};
}

bool SemanticWorkspaceCache::store(const PublishedWorkspaceSemanticState& state,
    const QHash<QString, SemanticAnalysisBandMetadata>& bands,
    const std::function<bool()>& cancelled, QString* reason)
{
    const auto fail=[&](const QString& detail) { if(reason) *reason=detail; return false; };
    try {
        if(!state.project.isOpen() || !state.project.sourceDiscoveryComplete || !state.input || !state.snapshot
            || state.logicalBytes>maximumDecoded || !state.dependencyGraph.isValidFor(state.project)
            || (cancelled && cancelled())) return fail("ineligible");
        SemanticAnalysisRequest request; request.project=state.project; request.runtimePolicy=state.policy;
        SemanticInputCapture disk(request);
        if(!cleanInputs(request,*state.input,disk,cancelled)) return fail("inputs changed or contain unsaved edits");
        auto input=*disk.seal(); input.generation=0; input.documentRevisions.clear();
        const auto& contents=state.snapshot->fileContentsView();
        auto contentFiles=contents.keys(); contentFiles.sort();
        for(const auto& file:contentFiles)
            if(!input.sources.contains(normalized(file)) || !input.sources.value(normalized(file)).readable
                || input.sources.value(normalized(file)).text!=contents.value(file)) return fail("snapshot/input mismatch");
        auto records=state.snapshot->symbolRecordsView();
        auto relationships=state.snapshot->relationshipsView();
        QHash<int,SymbolStableKey> identities;
        for(const auto& record:std::as_const(records)) identities.insert(record.localHandle,record.stableKey);
        for(auto& edge:relationships) {
            if(!edge.fromStableKey.isValid()) edge.fromStableKey=identities.value(edge.fromId);
            if(!edge.toStableKey.isValid()) edge.toStableKey=identities.value(edge.toId);
        }
        validateRecords(records,relationships,input);
        auto diagnostics=state.snapshot->rawDiagnostics();
        QHash<QString,QList<EffectiveValueFact>> facts;
        for(const auto& file:contentFiles) facts.insert(file,EffectiveValueService::capturedFacts(state.facts,file,contents.value(file)));
        auto dependencyFacts=state.dependencyGraph.fileFacts(); auto observed=state.dependencyGraph.observedIncludeFiles(); auto valueBands=bands;
        QByteArray payload; Codec codec(payload,false,cancelled);
        codec(input,contentFiles,records,relationships,diagnostics,facts,dependencyFacts,observed,valueBands);
        const auto manifest=QJsonDocument(QJsonObject{{"protocol",int(protocol)},{"generator",generator()},
            {"configuration",QString::fromLatin1(configuration(state.project,state.policy,bands))},
            {"complete",true},{"payloadBytes",payload.size()},{"sha256",QString::fromLatin1(sha(payload))}}).toJson(QJsonDocument::Compact);
        for(bool fallback:{false,true}) {
            const auto folder=directory(state.project.workspaceRoot,fallback);
            if(!QDir().mkpath(folder)) continue;
            QLockFile lock(folder+"/current.lock"); lock.setStaleLockTime(30000);
            if(!lock.tryLock(0)) {
                if(lock.error()==QLockFile::LockFailedError) return fail("another cache writer is active");
                continue;
            }
            QSaveFile file(folder+"/current.zscache"); file.setDirectWriteFallback(false);
            if(!file.open(QIODevice::WriteOnly)) continue;
            QDataStream output(&file); output.setByteOrder(QDataStream::LittleEndian);
            if(file.write(magic)!=magic.size()) return fail("write failed");
            output<<quint32(manifest.size());
            if(file.write(manifest)!=manifest.size() || file.write(payload)!=payload.size()
                || output.status()!=QDataStream::Ok || !disk.stillMatchesDisk(cancelled)
                || (cancelled && cancelled())) return fail("write cancelled or inputs changed");
            if(!file.commit()) return fail("atomic commit failed");
            if(reason) reason->clear(); return true;
        }
        return fail("no writable cache location");
    } catch(const std::exception&) { return fail("cache limit, invalid values, or cancellation"); }
}
