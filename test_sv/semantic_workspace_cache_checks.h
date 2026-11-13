// Included by the existing semantic pipeline contract executable.
ProjectSnapshot persistentProject(const QString& root, bool create)
{
    ProjectSnapshot project;
    project.workspaceRoot = root;
    project.systemVerilogFiles = {root + "/top.sv"};
    project.allFiles = project.systemVerilogFiles;
    project.includeDirs = {root + "/first", root + "/second"};
    if (create) {
        QDir().mkpath(project.includeDirs.first());
        write(root + "/second/width.svh", "`define CACHE_WIDTH 8\n");
        write(project.systemVerilogFiles.first(), R"(`include "width.svh"
package cache_types;
    typedef enum logic [1:0] {IDLE, ACTIVE} state_t;
    typedef struct packed {logic [3:0] data; state_t state;} packet_t;
    parameter int N = `CACHE_WIDTH;
endpackage
module cache_leaf #(parameter int W=4)(input logic [W-1:0] a, input cache_types::packet_t packet, output logic [W-1:0] y);
    assign y = a;
    function automatic int twice(input int value); return 2*value; endfunction
    localparam int RESULT=twice(W);
endmodule
module cache_top;
    import cache_types::*;
    logic [N-1:0] a, y;
    packet_t packet;
    cache_leaf #(.W(N)) first(.a(a),.packet(packet),.y(y));
    for (genvar i=0; i<2; ++i) begin: lanes
        cache_leaf #(.W(4)) child(.a(a[3:0]),.packet(packet),.y());
    end
endmodule
)");
    }
    return project;
}

QByteArray queryDigest(const PublishedWorkspaceSemanticState& state)
{
    // Query-visible values, independently of wire layout, local handles,
    // publication revisions and hash-container iteration order.
    QList<QByteArray> entries;
    for (const auto& record : state.snapshot->symbolRecordsView()) {
        QByteArray bytes; QDataStream out(&bytes, QIODevice::WriteOnly);
        out << record.stableKey.toString() << record.name << record.location.fileName
            << record.location.position << record.location.length << int(record.declarationKind)
            << int(record.usageRole) << int(record.sourceRole) << record.owner.stableKey.toString()
            << record.type.rawTypeText << record.type.resolvedTypeName << record.type.stableKey.toString()
            << record.presentation.declarationText << record.presentation.expressionText
            << record.presentation.callableQualifiedName << record.presentation.callableReturnType
            << record.presentation.elaboratedInstancePaths << record.analysisBand.label;
        auto detail = [&](const SemanticElaboratedSymbolInfo& info) {
            out << info.available << info.portDirection << info.bitWidth << info.signedIntegral
                << info.resolvedTypeText << info.valueText << info.packedDimensionsText
                << info.driverCount << info.continuousDriverCount << info.proceduralDriverCount;
            for (const auto& leaf : info.structuredLeaves)
                out << leaf.relativePath << leaf.canonicalTypeId << leaf.resolvedTypeText
                    << leaf.bitWidth << leaf.packedBitOffset;
            for (const auto& value : info.enumValues)
                out << value.name << value.valueText << value.expressionText;
        };
        detail(record.presentation.defaultInfo);
        for (const auto* map : {&record.presentation.instanceInfoByPath, &record.presentation.objectInfoByPath}) {
            auto paths=map->keys(); paths.sort();
            for (const auto& path : paths) { out << path; detail(map->value(path)); }
        }
        entries.append(bytes);
    }
    QHash<int,QString> keys;
    for (const auto& record : state.snapshot->symbolRecordsView()) keys.insert(record.localHandle,record.stableKey.toString());
    for (const auto& edge : state.snapshot->relationshipsView()) {
        QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly);
        out << keys.value(edge.fromId) << keys.value(edge.toId) << int(edge.type)
            << edge.fromAccessPath << edge.toAccessPath << int(edge.provenance)
            << edge.confidence << edge.evidenceText << edge.exactValueForward;
        if (edge.signalDependency)
            out << edge.signalDependency->fromInstance << edge.signalDependency->toInstance
                << edge.signalDependency->fromObject << edge.signalDependency->toObject
                << edge.signalDependency->complete << edge.signalDependency->processId;
        entries.append(bytes);
    }
    for (const auto& diagnostic : state.snapshot->rawDiagnostics()) {
        QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly);
        out << diagnostic.fileName << diagnostic.line << diagnostic.column << diagnostic.message
            << diagnostic.codeName << int(diagnostic.severity) << int(diagnostic.owner);
        for (const auto& range : diagnostic.ranges) out << range.fileName << range.position << range.length;
        entries.append(bytes);
    }
    const auto& contents = state.snapshot->fileContentsView();
    for (auto it=contents.cbegin(); it!=contents.cend(); ++it) {
        QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly);
        out << it.key() << it.value() << state.dependencyGraph.dependenciesOf({it.key()})
            << state.dependencyGraph.dependentsOf({it.key()});
        entries.append(bytes);
        for (const auto& fact : EffectiveValueService::capturedFacts(state.facts,it.key(),it.value())) {
            QByteArray value; QDataStream data(&value,QIODevice::WriteOnly);
            data << int(fact.kind) << int(fact.status) << fact.fileName << fact.startPosition << fact.endPosition
                << fact.expressionText << fact.valueText << fact.resolvedTypeText << fact.bitWidthText
                << fact.instancePath << fact.anchorInstancePath << fact.provenance << fact.failureReason;
            entries.append(value);
        }
    }
    const auto hierarchy = HierarchyService(SemanticSnapshotToken{state.snapshot,0}).automaticDesignHierarchy();
    for (const auto& node : hierarchy.nodes) {
        QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly);
        out << node.instancePath << node.moduleType << node.parentId << node.definitionFile;
        entries.append(bytes);
    }
    std::sort(entries.begin(),entries.end());
    QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly); out << entries;
    return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
}

QByteArray cacheBytes(const QString& path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
bool writeCacheBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size();
}
QByteArray editedCache(const QByteArray& original, const std::function<void(QJsonObject&,QByteArray&)>& edit)
{
    const auto headerSize = qFromLittleEndian<quint32>(original.constData()+8);
    auto header = QJsonDocument::fromJson(original.mid(12,headerSize)).object();
    auto payload = original.mid(12+headerSize);
    edit(header,payload);
    header["sha256"] = QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex());
    header["payloadBytes"] = payload.size();
    const auto json=QJsonDocument(header).toJson(QJsonDocument::Compact);
    QByteArray bytes=original.left(8); QDataStream out(&bytes,QIODevice::Append);
    out.setByteOrder(QDataStream::LittleEndian); out << quint32(json.size());
    bytes.append(json); bytes.append(payload); return bytes;
}

int persistentChild(const QString& root, const QString& expected)
{
    Harness harness;
    const bool ready=harness.run(persistentProject(root,false));
    const auto state=harness.analyzer.activeWorkspaceState;
    const bool same=state && queryDigest(*state)==expected.toLatin1();
    std::printf("cache_child_ready=%d slang=%d query_equal=%d\n",ready,harness.slang,same);
    return ready && !harness.slang && same ? 0 : 3;
}

void persistentCache(const QString& root)
{
    const auto project=persistentProject(root,true);
    const auto file=project.systemVerilogFiles.first();
    const auto path=SemanticWorkspaceCache::directory(root)+"/current.zscache";
    Harness cold;
    expect("persistent cold analysis commits with Slang",cold.run(project)&&cold.slang);
    const auto state=cold.analyzer.activeWorkspaceState;
    if (!state) return;
    QString reason;
    expect("publication asynchronously writes an atomic full semantic cache",waitFor([&]{return QFileInfo(path).size()>12;}));
    // Wait until the background writer has released its lock; no duplicate writer.
    expect("cache writer releases its lock",waitFor([&]{return !QFileInfo(SemanticWorkspaceCache::directory(root)+"/current.lock").exists();}));
    const auto original=cacheBytes(path);
    if (original.size()<12) {
        SemanticWorkspaceCache::store(*state,{}, {},&reason);
        std::printf("cache_write_reason=%s\n",qPrintable(reason));
        return;
    }
    const auto factCount=EffectiveValueService::capturedFacts(state->facts,file,state->snapshot->fileContentsView().value(SemanticInputCapture::pathKey(file))).size();
    int structured=0;
    for(const auto& record:state->snapshot->symbolRecordsView()) {
        structured+=record.presentation.defaultInfo.structuredLeaves.size();
        for(const auto& info:record.presentation.instanceInfoByPath) structured+=info.structuredLeaves.size();
        for(const auto& info:record.presentation.objectInfoByPath) structured+=info.structuredLeaves.size();
    }
    std::printf("cache_symbols=%lld relationships=%lld facts=%lld structured_leaves=%d\n",
        qlonglong(state->snapshot->symbolRecordCount()),qlonglong(state->snapshot->relationshipCount()),qlonglong(factCount),structured);
    expect("cache fixture contains structured symbols, relationships and effective facts",
        state->snapshot->symbolRecordCount()>20 && state->snapshot->relationshipCount()>0
        && factCount>0 && structured>0);
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {"--cache-child",root,QString::fromLatin1(queryDigest(*state))});
    const bool finished=waitFor([&]{return child.state()==QProcess::NotRunning;});
    if (!finished) { child.kill(); child.waitForFinished(1000); }
    std::printf("%s",child.readAllStandardOutput().constData());
    expect("fresh process restores equivalent semantic queries without Slang",finished&&child.exitStatus()==QProcess::NormalExit&&child.exitCode()==0);
    SemanticAnalysisRequest request; request.project=project;request.generation=777;request.runtimePolicy.maxDiagnostics=2;
    request.documentRevisions={{file,400}};
    auto restored=SemanticWorkspaceCache::restore(request,{});
    expect("restore reserves fresh computation and current document revisions",restored&&restored->analysisRevision!=state->analysisRevision
        && restored->input->generation==request.generation && restored->documentRevisionsByFile.value(SemanticInputCapture::pathKey(file))==400);
    if (restored) {
        PublishedWorkspaceSemanticState copy=*state;copy.snapshot=restored->preparedSnapshot;copy.facts=restored->preparedEffectiveFactsState;
        copy.dependencyGraph=restored->dependencyGraph;
        expect("restored relationship endpoints and full query values match cold state",queryDigest(copy)==queryDigest(*state));
    }
    const auto pristine=read(file);
    request.sourceOverrides={{file,pristine+"module unsaved_cache_member; endmodule\n"}};
    expect("dirty open document forbids disk restore",!SemanticWorkspaceCache::restore(request,{}));
    request.sourceOverrides={{file,pristine}};
    expect("clean open document may restore at its current revision",SemanticWorkspaceCache::restore(request,{}).has_value());
    request.sourceOverrides.clear();
    auto changed=request;changed.project.sourceDiscoveryComplete=false;
    expect("partial source discovery never restores authoritative data",!SemanticWorkspaceCache::restore(changed,{}));
    changed=request;changed.project.defines.insert("CACHE_MODE","1");
    expect("define changes invalidate persistent data",!SemanticWorkspaceCache::restore(changed,{}));
    changed=request;changed.project.topModule="cache_top";
    expect("top changes invalidate persistent data",!SemanticWorkspaceCache::restore(changed,{}));
    changed=request;std::reverse(changed.project.includeDirs.begin(),changed.project.includeDirs.end());
    expect("include search order invalidates persistent data",!SemanticWorkspaceCache::restore(changed,{}));
    changed=request;changed.project.systemVerilogFiles.append(root+"/extra.sv");write(root+"/extra.sv","module extra; endmodule\n");
    expect("source set addition invalidates persistent data",!SemanticWorkspaceCache::restore(changed,{}));
    changed=request;changed.project.systemVerilogFiles.clear();
    expect("source set deletion invalidates persistent data",!SemanticWorkspaceCache::restore(changed,{}));
    expect("new higher priority include invalidates negative evidence",write(root+"/first/width.svh","`define CACHE_WIDTH 9\n")&&!SemanticWorkspaceCache::restore(request,{}));
    QFile::remove(root+"/first/width.svh");
    expect("included content edit invalidates persistent data",write(root+"/second/width.svh","`define CACHE_WIDTH 16\n")&&!SemanticWorkspaceCache::restore(request,{}));
    QFile::remove(root+"/second/width.svh");
    expect("included file removal invalidates persistent data",!SemanticWorkspaceCache::restore(request,{}));
    write(root+"/second/width.svh","`define CACHE_WIDTH 8\n");
    expect("same-size source edits are content checked",write(file,QString(pristine).replace("cache_top","cache_tip"))&&!SemanticWorkspaceCache::restore(request,{}));
    write(file,pristine);
    expect("restored original inputs are eligible again",SemanticWorkspaceCache::restore(request,{}).has_value());
    const auto moved=persistentProject(root+"-moved",true);
    const auto movedPath=SemanticWorkspaceCache::directory(moved.workspaceRoot)+"/current.zscache";
    QDir().mkpath(QFileInfo(movedPath).absolutePath());writeCacheBytes(movedPath,original);
    changed=request;changed.project=moved;
    expect("copied cache cannot cross workspace identities",!SemanticWorkspaceCache::restore(changed,{}));

    Harness switching;
    const auto other=single(root+"-other","other_workspace");
    int phase=0;bool cachePrepared=false;
    QObject::connect(&switching.analyzer,&SymbolAnalyzer::semanticAnalysisTelemetry,[&](const auto& telemetry) {
        if(telemetry.stage!=SemanticAnalysisStage::Worker || phase>1) return;
        if(phase==0) cachePrepared=!telemetry.slangInvoked;
        switching.analyzer.clearSemanticIndex();
        SemanticAnalysisRequest next=request;next.generation=++switching.generation;
        next.reason=SemanticAnalysisReason::WorkspaceOpen;
        if(phase++==0) next.project=other;
        else next.sourceOverrides={{file,pristine+"module newest_after_aba; endmodule\n"}};
        switching.analyzer.startSemanticAnalysisAsync(next);
    });
    expect("decoded cache cannot publish after A-B-A switch and dirty replacement",switching.run(project)
        && phase==2 && cachePrepared && switching.slang && symbol("newest_after_aba")&&!symbol("other_workspace"));
    switching.analyzer.shutdown();
    const QList<QPair<const char*,QByteArray>> corruptions{
        {"truncated cache is a miss",original.left(original.size()/2)},
        {"unknown protocol is a miss",editedCache(original,[](auto& header,auto&){header["protocol"]=999;})},
        {"foreign generator is a miss",editedCache(original,[](auto& header,auto&){header["generator"]="foreign";})},
        {"incomplete cache is a miss",editedCache(original,[](auto& header,auto&){header["complete"]=false;})},
        {"bounded decoder rejects oversized string before allocation",editedCache(original,[](auto&,auto& bytes){qToLittleEndian<quint32>(0x7ffffffe,bytes.data());})},
        {"trailing payload is rejected",editedCache(original,[](auto&,auto& bytes){bytes.append('x');})}
    };
    for (const auto& [name,bytes] : corruptions) expect(name,writeCacheBytes(path,bytes)&&!SemanticWorkspaceCache::restore(request,{}));
    auto checksum=original;checksum[checksum.size()-1]^=1;
    expect("checksum corruption is rejected",writeCacheBytes(path,checksum)&&!SemanticWorkspaceCache::restore(request,{}));
    writeCacheBytes(path,original);
    QLockFile lock(SemanticWorkspaceCache::directory(root)+"/current.lock");
    expect("concurrent writer skips without replacing a complete cache",lock.tryLock(0)
        && !SemanticWorkspaceCache::store(*state,{}, {},&reason)&&cacheBytes(path)==original);
    lock.unlock();
    expect("cancelled IO leaves the previous atomic cache unchanged",!SemanticWorkspaceCache::store(*state,{},[]{return true;},&reason)&&cacheBytes(path)==original);
    expect("read cancellation is a miss",!SemanticWorkspaceCache::restore(request,{},[]{return true;}));
    expect("dirty published overlays cannot overwrite a disk cache",cold.run(project,{{file,pristine+"module unsaved_cache_member; endmodule\n"}})
        && !SemanticWorkspaceCache::store(*cold.analyzer.activeWorkspaceState,{}, {},&reason)&&cacheBytes(path)==original);
    cold.analyzer.shutdown();
    Harness fallback;
    expect("corrupt cache falls through to normal Slang publication",writeCacheBytes(path,checksum)&&fallback.run(project)&&fallback.slang&&symbol("cache_top"));
    fallback.analyzer.shutdown();

    if(!qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT").isEmpty()) {
        const auto unwritable=persistentProject(root+"-fallback",true);
        expect("blocked workspace cache location prepared",write(unwritable.workspaceRoot+"/.zeroslack","not a directory"));
        Harness local;
        const auto localPath=SemanticWorkspaceCache::directory(unwritable.workspaceRoot,true)+"/current.zscache";
        expect("unwritable workspace uses the isolated user cache without losing analysis",local.run(unwritable)
            && waitFor([&]{return QFileInfo(localPath).size()>12;}));
        changed=request;changed.project=unwritable;
        expect("workspace-bound user fallback restores without analysis",SemanticWorkspaceCache::restore(changed,{}).has_value());
        local.analyzer.shutdown();
        const auto blocked=persistentProject(root+"-write-failure",true);
        write(blocked.workspaceRoot+"/.zeroslack","not a directory");
        const auto unavailable=SemanticWorkspaceCache::directory(blocked.workspaceRoot,true);
        write(unavailable,"not a directory");
        Harness noWrite;
        expect("both cache locations unavailable still permit normal publication",noWrite.run(blocked)&&noWrite.slang&&symbol("cache_top"));
        expect("cache write failure is reported without replacing semantic state",!SemanticWorkspaceCache::store(*noWrite.analyzer.activeWorkspaceState,{}, {},&reason)
            && reason=="no writable cache location" && symbol("cache_top"));
        noWrite.analyzer.shutdown();
    }
}
