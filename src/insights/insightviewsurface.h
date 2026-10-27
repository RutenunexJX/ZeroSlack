#pragma once
#include "rtlinsightviewplugins.h"
#include "rtlinsightspanelcoordinator.h"
#include "signaltracepanel.h"
#include "compactlayout.h"
#include <QDockWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <memory>

// A host adapter; identity and semantic snapshots are supplied by the caller.
class InsightViewSurface final {
public:
    InsightViewSurface(InsightWorkbenchViewKind kind,QWidget* parent):kindValue(kind) {
        if(kind==InsightWorkbenchViewKind::Kernel || kind==InsightWorkbenchViewKind::Hotspot){
            dockValue=new QDockWidget(parent);
            traceValue=new SignalTracePanel(dockValue);
            traceValue->setHeatmapMode(kind==InsightWorkbenchViewKind::Hotspot);
            dockValue->setWidget(traceValue);
        }else if(kind==InsightWorkbenchViewKind::Block){
            dockValue=new QDockWidget(parent);
            designButton=new QPushButton(QStringLiteral("Open module in Design"),dockValue);
            designButton->setEnabled(false);
            QObject::connect(designButton,&QPushButton::clicked,designButton,[this]{
                if(designHandler)designHandler(context.fileName,context.moduleName);
            });
            dockValue->setWidget(designButton);
        }else{
            rtlValue=std::make_unique<RtlInsightsPanelCoordinator>(parent);
            dockValue=rtlValue->dock();
            for(const char* name:{"rtlSignalUsageHotspotButton","rtlFsmGraphButton","rtlModuleBlockDiagramButton","rtlInsightsTitle","rtlInsightsPinButton"})
                if(auto* control=dockValue->findChild<QWidget*>(QString::fromLatin1(name)))control->hide();
            if(auto* graph=dockValue->findChild<QWidget*>(QStringLiteral("rtlInsightsGraphPanel")))
                CompactFlowLayout::replaceRows(qobject_cast<QVBoxLayout*>(graph->layout()));
            rtlValue->stackForTest()->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
        }
        dockValue->setObjectName(QStringLiteral("insightWorkbenchSurface.%1").arg(static_cast<int>(kind)));
        dockValue->setMinimumSize(0,0);
        dockValue->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
        dockValue->setFeatures(QDockWidget::NoDockWidgetFeatures);
        auto* title=new QWidget(dockValue);title->setFixedHeight(0);dockValue->setTitleBarWidget(title);
        dockValue->show();
    }
    ~InsightViewSurface(){rtlValue.reset();delete dockValue;}
    QDockWidget* widget()const{return dockValue;}
    RtlInsightsPanelCoordinator* rtl()const{return rtlValue.get();}
    SignalTracePanel* trace()const{return traceValue;}
    void fit(){if(traceValue)traceValue->focusFit();else if(rtlValue)rtlValue->focusFit();}
    void setNavigationHandler(std::function<bool(const QString&,int,int)> handler){
        if(traceValue)traceValue->setNavigationHandler([handler=std::move(handler)](const SignalTraceEvidence& evidence){
            return handler && handler(evidence.range.fileName,evidence.range.line,evidence.range.column);
        });
        else if(rtlValue)rtlValue->setNavigationHandler(std::move(handler));
    }
    void setStatusHandler(std::function<void(const QString&,int)> handler){
        if(traceValue)traceValue->setStatusMessageHandler(std::move(handler));
        else if(rtlValue)rtlValue->setStatusMessageHandler(std::move(handler));
    }
    void setDesignHandler(std::function<void(const QString&,const QString&)> handler){
        designHandler=std::move(handler);
        if(designButton)designButton->setEnabled(bool(designHandler));
    }
    void setContext(const InsightViewContext& c){
        const bool sameTarget=context.workspaceId==c.workspaceId && context.projectId==c.projectId
            && context.fileName==c.fileName && context.moduleName==c.moduleName
            && context.signalName==c.signalName && context.signalAccessPath==c.signalAccessPath;
        context=c;
        if(traceValue){
            SignalTraceContext traceContext{c.workspaceId,c.projectId,c.instancePath,c.semantic};
            LiveInsightRequestKey key;key.kind=kindValue==InsightWorkbenchViewKind::Hotspot?LiveInsightKind::Hotspot:LiveInsightKind::Kernel;
            key.workspaceId=c.workspaceId;key.documentId=c.documentId;key.documentRevision=c.documentRevision;
            key.semanticRevision=c.semantic.revision;key.contextKey=c.projectId;
            traceValue->setContext(traceContext,key);
            if(!sameTarget)traceValue->showForSymbol(c.signalName,c.fileName,c.moduleName,c.signalAccessPath);
        }else if(rtlValue){
            rtlValue->setGraphContext(c,c.semantic);
            if(c.signalName.isEmpty()){rtlValue->updateModuleContext(c.fileName,c.moduleName,{});rtlValue->showFsmGraph();}
            else rtlValue->showStateTransitionGraphForSignal(c.fileName,c.moduleName,c.signalName);
        }
    }
    GraphExportResult exportGraph(const QString& path,const GraphExportOptions& options)const{
        if(traceValue)return traceValue->exportGraph(path,options);
        if(rtlValue)return rtlValue->exportCurrentGraph(path,options);
        GraphExportResult result;result.outputPath=path;result.failure=GraphExportFailure::InvalidSource;
        result.failureReason=QStringLiteral("Module hierarchy is available in Design; the retired diagram has no graph export.");return result;
    }
private:
    InsightWorkbenchViewKind kindValue;
    InsightViewContext context;
    QDockWidget* dockValue=nullptr;
    SignalTracePanel* traceValue=nullptr;
    QPushButton* designButton=nullptr;
    std::unique_ptr<RtlInsightsPanelCoordinator> rtlValue;
    std::function<void(const QString&,const QString&)> designHandler;
};
