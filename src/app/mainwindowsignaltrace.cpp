#include "mainwindow.h"
#include "signaltracepanel.h"
#include "mycodeeditor.h"
#include "tabmanager.h"
#include "shareddocument.h"
#include "navigationwidget.h"
#include "editorfileidentity.h"
#include "semanticindex.h"
#include <QFile>
#include <QTextBlock>

bool MainWindow::traceEvidenceMatches(const SignalTraceEvidence& evidence)
{
    if(evidence.sourceHash.isEmpty() || evidence.range.fileName.isEmpty())return false;
    if(tabManager)for(auto* editor:tabManager->openEditors()){
        const auto document=tabManager->getDocumentForEditor(editor);
        if(!EditorFileIdentity::same(document.fileName,evidence.range.fileName))continue;
        const auto* shared=tabManager->sharedDocumentForEditor(editor);
        const quint64 revision=shared ? shared->textRevision() : quint64(qMax(0,document.textVersion));
        auto& cached=traceSourceHashes[document.fileName];
        if(cached.documentId!=document.documentId || cached.revision!=revision || cached.hash.isEmpty())cached={document.documentId,revision,SemanticSignalRoleEvidence::sourceHash(editor->cachedDocumentText())};
        return cached.hash==evidence.sourceHash;
    }
    QFile file(evidence.range.fileName);
    return file.open(QIODevice::ReadOnly) && SemanticSignalRoleEvidence::sourceHash(QString::fromUtf8(file.readAll()))==evidence.sourceHash;
}

void MainWindow::configureSignalTracePanel(SignalTracePanel* panel)
{
    if(!panel || panel->property("traceHostConfigured").toBool())return;
    panel->setProperty("traceHostConfigured",true);
    tracePanels.append(panel);
    const QPointer<MainWindow> host(this);
    const QString owner=QString::number(quintptr(panel),16);
    panel->setNavigationHandler([this,host](const SignalTraceEvidence& evidence){
        return host && traceEvidenceMatches(evidence) && revealSuiteSource(evidence.range.fileName,evidence.range.line,evidence.range.column);
    });
    panel->setMarkerHandler([this,host,owner](const QList<SignalTraceEvidence>& references){
        if(!host || !tabManager)return;
        for(auto* editor:tabManager->openEditors()){
            QList<EditorAnnotation> marks;
            const auto document=tabManager->getDocumentForEditor(editor);
            for(const auto& reference:references){
                if(!EditorFileIdentity::same(document.fileName,reference.range.fileName) || !traceEvidenceMatches(reference))continue;
                const auto block=editor->document()->findBlockByNumber(reference.range.line-1);
                if(!block.isValid())continue;
                EditorAnnotation mark;mark.kind=EditorAnnotationKind::SignalTrace;mark.placement=EditorAnnotationPlacement::Overlay;
                mark.range.firstLine=reference.range.line-1;mark.range.lastLine=qMax(reference.range.line,reference.range.endLine)-1;
                mark.range.startPosition=block.position()+qBound(0,reference.range.column-1,block.length()-1);
                const auto end=editor->document()->findBlockByNumber(mark.range.lastLine);
                mark.range.endPosition=end.isValid() ? end.position()+qBound(0,reference.range.endColumn-1,end.length()-1) : mark.range.startPosition+1;
                mark.range.endPosition=qMax(mark.range.startPosition+1,mark.range.endPosition);
                mark.priority=AnnotationLayer::defaultPriority(mark.kind);mark.semanticKey=reference.id;
                mark.text=SignalTraceService::roleName(reference.role);mark.detail=reference.text;marks.append(mark);
            }
            editor->setSignalTraceAnnotations(owner,marks);
        }
    });
    panel->setSelectionHandler([this,host,panel](const SignalTraceObject& object){
        if(!host)return;
        for(const auto& other:tracePanels)if(other && other!=panel && other->isVisible())other->syncSelection(object);
        if(auto* navigation=findChild<NavigationWidget*>())navigation->selectDesignInstance(object.instancePath);
    });
    connect(panel,&QObject::destroyed,this,[this,owner]{if(tabManager)for(auto* editor:tabManager->openEditors())editor->setSignalTraceAnnotations(owner,{});});
}

void MainWindow::updateSignalTraceContext()
{
    const auto source=activeLiveInsightToolContext(false);
    SignalTraceContext context{source.workspaceId,source.projectId,source.instancePath,SemanticIndex::getInstance()->snapshotToken()};
    QSet<QString> opened;
    if(tabManager)for(auto* editor:tabManager->openEditors()){
        const auto document=tabManager->getDocumentForEditor(editor);
        if(document.fileName.isEmpty())continue;opened.insert(document.fileName);
        const auto* shared=tabManager->sharedDocumentForEditor(editor);
        const quint64 revision=shared ? shared->textRevision() : quint64(qMax(0,document.textVersion));
        auto& cached=traceSourceHashes[document.fileName];
        if(cached.documentId!=document.documentId || cached.revision!=revision || cached.hash.isEmpty())cached={document.documentId,revision,SemanticSignalRoleEvidence::sourceHash(editor->cachedDocumentText())};
        context.currentSourceHashes[document.fileName]=cached.hash;
    }
    for(auto it=traceSourceHashes.begin();it!=traceSourceHashes.end();)if(!opened.contains(it.key()))it=traceSourceHashes.erase(it);else ++it;
    LiveInsightRequestKey key;key.kind=LiveInsightKind::Kernel;key.workspaceId=source.workspaceId;key.documentId=source.documentId;
    key.documentRevision=source.documentRevision;key.semanticRevision=context.token.revision;key.contextKey=source.projectId;
    if(hotspotPanel)configureSignalTracePanel(hotspotPanel);
    for(auto it=tracePanels.begin();it!=tracePanels.end();)if(!*it)it=tracePanels.erase(it);else{(*it)->setContext(context,key);++it;}
    if(auto* navigation=findChild<NavigationWidget*>();navigation && !navigation->property("traceSelectionConnected").toBool()){
        navigation->setProperty("traceSelectionConnected",true);
        connect(navigation,&NavigationWidget::designInstanceSelected,this,[this](const QString& instance){
            for(const auto& panel:tracePanels)if(panel && panel->isVisible())panel->syncInstanceSelection(instance);
        });
    }
    const auto selectionKey=source.workspaceId+'\n'+source.projectId+'\n'+source.fileName+'\n'+source.moduleName+'\n'
        +source.instancePath+'\n'+source.signalName+'\n'+QString::number(context.token.revision);
    if(selectionKey!=traceSelectionKey){
        traceSelectionKey=selectionKey;
        const auto resolved=SignalTraceService(context).resolve({},source.signalName,source.fileName,source.moduleName);
        if(resolved.status==SignalTraceStatus::Ready)
            for(const auto& panel:tracePanels)if(panel && panel->isVisible())panel->syncSelection(resolved.object);
    }
}

