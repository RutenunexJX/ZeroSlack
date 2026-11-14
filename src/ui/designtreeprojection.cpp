#include "designtreeprojection.h"
#include "semanticdependencygraph.h"
#include "semanticindexsnapshot.h"
#include "editorfileidentity.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace {
QString key(const QString& file) { return QDir::cleanPath(QDir::fromNativeSeparators(file)).toCaseFolded(); }
DesignTreeRow instance(const DesignHierarchyNode& node, const QString& id, const QString& parent) {
    DesignTreeRow row;
    row.id=id; row.parentId=parent; row.name=node.instanceName; row.type=node.moduleType;
    row.file=node.definitionFile; row.status=node.status; row.semantic=node;
    row.unresolved=node.unresolved || node.status.contains("incomplete", Qt::CaseInsensitive);
    if (row.name==row.type) row.type.clear();
    return row;
}
}
void DesignTreeProjection::append(DesignTreeRow row) {
    const int index=nodes.size(); nodeById.insert(row.id,index); childrenById[row.parentId].append(index);
    if(!row.semantic.id.isEmpty() && !firstPresentationBySemanticId.contains(row.semantic.id))
        firstPresentationBySemanticId.insert(row.semantic.id,row.id);
    nodes.append(std::move(row));
}
std::shared_ptr<DesignTreeProjection> DesignTreeProjection::hierarchy(const DesignHierarchyReport& report) {
    auto result=std::make_shared<DesignTreeProjection>();
    for(const auto& node:report.nodes)result->append(instance(node,node.id,node.parentId));
    result->structureFingerprint=report.structureFingerprint;
    return result;
}
std::shared_ptr<DesignTreeProjection> DesignTreeProjection::build(const DesignHierarchyReport& report,
    const SemanticSnapshotToken& token, const ProjectSnapshot& project,
    const QPair<QString,QString>& source, const QPair<QString,QString>& simulation,
    const std::function<bool()>& cancelled) {
    auto result=std::make_shared<DesignTreeProjection>();
    for(const auto& title:{QStringLiteral("Sources"),QStringLiteral("Simulation"),QStringLiteral("Headers"),QStringLiteral("Unreferenced")}) {
        DesignTreeRow row; row.id="section/"+title; row.name=title; row.category=true; result->append(row);
    }
    // Work runs on the navigation worker against one immutable publication.
    const auto contents=token.snapshot?token.snapshot->fileContents():QHash<QString,QString>();
    const auto dependencies=SemanticDependencyGraph::build(project,contents,cancelled);
    const auto facts=dependencies.fileFacts();
    QSet<QString> known, uncertain;
    for(auto it=facts.cbegin();it!=facts.cend();++it) {
        known.insert(key(it.key()));
        if(it->parseError)uncertain.insert(key(it.key()));
    }
    for(const auto& file:project.systemVerilogFiles)
        if(!known.contains(key(file)))uncertain.insert(key(file));
    const auto locate=[&](const QPair<QString,QString>& target) {
        int found=-1;
        for(int i=0;i<report.nodes.size();++i) {
            const auto& node=report.nodes[i];
            if(node.moduleType!=target.second || !EditorFileIdentity::same(node.definitionFile,target.first))continue;
            if(found<0 || node.isTop)found=i;
            if(node.isTop)break;
        }
        return found;
    };
    QSet<QString> used, shown;
    QHash<QString,QList<DesignHierarchyNode>> definitionsByFile;
    QHash<QString,QSet<QString>> identitiesByFile;
    for(const auto& node:report.nodes) {
        const auto file=key(node.definitionFile), identity=node.definitionId.isEmpty()?node.moduleType:node.definitionId;
        if(file.isEmpty() || identity.isEmpty() || identitiesByFile[file].contains(identity))continue;
        identitiesByFile[file].insert(identity);definitionsByFile[file].append(node);
    }
    const auto addFile=[&](const QString& file,const QString& section,const QString& status) {
        const QString id=section+"/file/"+key(file);
        if(result->nodeById.contains(id))return;
        DesignTreeRow row; row.id=id;row.parentId=section;row.file=file;
        row.name=QDir(project.workspaceRoot).relativeFilePath(file);row.status=status;
        result->append(row);shown.insert(key(file));
        // Definitions remain selectable even in dependency and unused files.
        for(const auto& node:definitionsByFile.value(key(file))) {
            const auto identity=node.definitionId.isEmpty()?node.moduleType:node.definitionId;
            auto entry=instance(node,id+"/definition/"+identity,id);
            entry.name=node.moduleType;entry.type.clear();result->append(entry);
        }
    };
    const auto addContext=[&](const QString& section,const QPair<QString,QString>& target,bool src) {
        QList<int> roots;
        if(target.second.isEmpty()) {
            if(src)for(int i=0;i<report.nodes.size();++i)if(report.nodes[i].parentId.isEmpty())roots.append(i);
        } else {
            const int found=locate(target);
            if(found>=0) roots.append(found);
            else {
                DesignTreeRow missing;missing.id=section+"/missing";missing.parentId=section;
                missing.name=target.second;missing.file=target.first;missing.unresolved=true;
                missing.source=src;missing.simulation=!src;missing.status="Selected top is missing or unresolved";
                result->append(missing);
            }
        }
        QSet<QString> files;
        for(int root:roots) {
            QList<QPair<int,QString>> pending{{root,section}};
            for(int i=0;i<pending.size();++i) {
                if(cancelled && cancelled())return;
                const auto node=report.nodes[pending[i].first];
                auto row=instance(node,section+"/instance/"+node.id,pending[i].second);
                if(pending[i].first==root) {
                    row.name=node.moduleType;row.type.clear();
                    row.source=src && !target.second.isEmpty();row.simulation=!src;
                }
                result->append(row);
                if(!node.definitionFile.isEmpty())files.insert(node.definitionFile);
                for(int child:report.childrenById.value(node.id))pending.append({child,row.id});
            }
        }
        const auto required=dependencies.dependenciesOf(files.values());
        for(const auto& file:required)files.insert(file);
        for(const auto& file:files) {
            used.insert(key(file));
            if(project.headerSourceFiles().contains(file,Qt::CaseInsensitive))continue;
            addFile(file,section,target.second.isEmpty()?"Definition browsing; no source top selected":"Required by selected top");
        }
    };
    addContext("section/Sources",source,true);addContext("section/Simulation",simulation,false);
    const bool proven=project.sourceDiscoveryComplete && uncertain.isEmpty()
        && !source.second.isEmpty() && locate(source)>=0 && !simulation.second.isEmpty() && locate(simulation)>=0
        && report.unresolvedModules.isEmpty();
    for(const auto& file:project.headerSourceFiles())addFile(file,"section/Headers",used.contains(key(file))?"Included by selected top":"Workspace header");
    for(const auto& file:project.systemVerilogFiles) {
        if(cancelled && cancelled())return result;
        if(shown.contains(key(file)))continue;
        addFile(file,proven?"section/Unreferenced":"section/Sources",
            proven?"Not referenced by either selected top or its dependencies":"Reference status pending; workspace analysis or top selection is incomplete");
    }
    QCryptographicHash fingerprint(QCryptographicHash::Sha256);
    for(const auto& row:result->nodes){fingerprint.addData(row.id.toUtf8());fingerprint.addData(row.parentId.toUtf8());}
    result->structureFingerprint=fingerprint.result();
    return result;
}
