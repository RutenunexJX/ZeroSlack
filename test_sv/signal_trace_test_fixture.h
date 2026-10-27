#pragma once
#include "signaltracepanel.h"
#include "signaltracemodels.h"
#include "applicationthememanager.h"
#include "testuistyle.h"
#include <zeroslack/semantic/incrementalsemanticanalysisworker.h>
#include <zeroslack/semantic/semanticindexsnapshot.h>
#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

// Real production analysis; fixtures never depend on repository example trees.
struct SignalTraceTestFixture {
    QTemporaryDir directory;
    QString file,source;
    quint64 revision=0;
    SignalTraceContext context;
    bool build(const QString& text,const QMap<QString,QString>& companions={}){
        source=text;file=directory.filePath("top.sv");
        QHash<QString,QString> contents{{file,source}};
        for(auto it=companions.cbegin();it!=companions.cend();++it)contents.insert(directory.filePath(it.key()),it.value());
        for(auto it=contents.cbegin();it!=contents.cend();++it){QFile out(it.key());if(!out.open(QIODevice::WriteOnly) || out.write(it.value().toUtf8())<0)return false;}
        SemanticAnalysisRequest request;request.generation=++revision;request.computationRevision=revision;
        request.reason=SemanticAnalysisReason::WorkspaceOpen;request.impactHint=SemanticChangeImpact::WorkspaceConfig;
        request.project.workspaceRoot=directory.path();request.project.allFiles=contents.keys();request.project.systemVerilogFiles=contents.keys();request.project.topModule="top";
        request.changedFiles=contents.keys();request.sourceOverrides=contents;
        auto result=IncrementalSemanticAnalysisWorker::analyze(request,{}, {},[]{return false;});
        context={directory.path(),"trace-test",{}, {result.preparedSnapshot,revision}};
        return result.error.isEmpty() && bool(result.preparedSnapshot);
    }
    bool show(SignalTracePanel& panel,const QString& signal){
        panel.setContext(context);panel.resize(1100,650);panel.show();panel.showForSymbol(signal,file,"top");
        return QTest::qWaitFor([&]{return !panel.busy();},10000) && panel.currentObject().isValid();
    }
};

template<class Test> int runSignalTraceWidgetTest(int argc,char** argv){
    QStandardPaths::setTestModeEnabled(true);QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QTemporaryDir profile;if(!profile.isValid())return 2;
    QCoreApplication::setApplicationName("ZeroSlackTraceRegression");QCoreApplication::setOrganizationName("ZeroSlackTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profile.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,profile.path());
    if(!initializeUiStyleForTest())return 3;ApplicationThemeManager::instance().applyToApplication();
    Test test;return QTest::qExec(&test,argc,argv);
}
