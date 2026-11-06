#pragma once
#include <QAction>
#include <QCryptographicHash>

namespace DefaultNativeObservation {
inline QJsonObject tree(QWidget* widget) {
    if(!widget)return {};
    QJsonArray children,actions;
    for(auto* child:widget->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))children.append(tree(child));
    for(auto* action:widget->actions())actions.append(QJsonObject{{"class",action->metaObject()->className()},
        {"name",action->objectName()},{"text",action->text()},{"enabled",action->isEnabled()}});
    return {{"class",widget->metaObject()->className()},{"name",widget->objectName()},{"children",children},{"actions",actions}};
}
template<class Harness> bool settled(Harness& h,double afterGeometry=0) {
    return QTest::qWaitFor([&]{
        const auto snapshot=h.snapshot(),c=snapshot["connector"].toObject();
        if(!h.rails()||!h.allResourcesComplete()||!c["valid"].toBool()||!c["paintedValid"].toBool())return false;
        return c["geometrySerial"].toDouble()>afterGeometry
            &&c["geometrySerial"]==c["paintedGeometrySerial"]&&c["requestSerial"]==c["paintedRequestSerial"]
            &&c["layoutSerial"]==c["paintedLayoutSerial"]&&c["contentRevision"]==c["paintedContentRevision"]
            &&snapshot["session"]==c["paintedSession"]
            &&c["contentRevision"].toDouble()==double(h.editor()->semanticDocumentRevision())
            &&snapshot["rails"].toObject()["appliedContentRevision"]==c["contentRevision"];
    },5000);
}
template<class Harness> QJsonObject sample(Harness& h) {
    auto value=h.snapshot();const auto session=value["session"].toString();
    auto connector=value["connector"].toObject();auto rails=value["rails"].toObject();
    QJsonObject relations{{"sessionNonempty",!session.isEmpty()},
        {"paintedSessionMatches",connector["paintedSession"]==value["session"]},
        {"geometryMatches",connector["geometrySerial"]==connector["paintedGeometrySerial"]},
        {"requestMatches",connector["requestSerial"]==connector["paintedRequestSerial"]},
        {"layoutMatches",connector["layoutSerial"]==connector["paintedLayoutSerial"]},
        {"contentMatches",connector["contentRevision"]==connector["paintedContentRevision"]},
        {"railsContentMatches",rails["appliedContentRevision"]==connector["contentRevision"]}};
    value["session"]="<active-session>";connector["paintedSession"]="<active-session>";
    for(const auto* key:{"geometrySerial","paintedGeometrySerial"})connector[key]="<current-geometry>";
    for(const auto* key:{"requestSerial","paintedRequestSerial"})connector[key]="<current-request>";
    for(const auto* key:{"layoutSerial","paintedLayoutSerial"})connector[key]="<current-layout>";
    connector["paintSerial"]="<completed-paint>";
    rails["layoutSerial"]="<current-rails-layout>";rails["reconciliationSerial"]="<current-reconciliation>";
    value["rails"]=rails;value["connector"]=connector;value["serialRelations"]=relations;
    const auto* editor=h.editor();const auto cursor=editor->textCursor();
    const auto origin=editor->viewport()->mapTo(h.rails(),QPoint());
    value["editor"]=QJsonObject{{"position",cursor.position()},{"anchor",cursor.anchor()},
        {"vertical",editor->verticalScrollBar()->value()},{"horizontal",editor->horizontalScrollBar()->value()},
        {"font",editor->font().toString()},{"dpr",editor->devicePixelRatioF()},{"wrap",int(editor->lineWrapMode())},
        {"viewport",QJsonArray{editor->viewport()->width(),editor->viewport()->height()}},
        {"origin",QJsonArray{origin.x(),origin.y()}},
        {"sourceSha256",QString::fromLatin1(QCryptographicHash::hash(editor->toPlainText().toUtf8(),QCryptographicHash::Sha256).toHex())}};
    value["tree"]=tree(h.window.template findChild<QWidget*>("linkedContextPage"));
    const auto* recovery=h.window.template findChild<QPushButton*>("linkedSaveCopy");
    value["recovery"]=QJsonObject{{"present",recovery!=nullptr},{"visible",recovery&&recovery->isVisible()},
        {"text",recovery?recovery->text():QString()}};
    return value;
}
template<class Harness> QJsonObject capture(Harness& h,QJsonArray* raw=nullptr) {
    if(!h.ready||!prepareFoldSource(h)||!h.store({anchorAt(h,"a","signal_000 <=",{"a0","a1"}),
        anchorAt(h,"b","signal_002 <=",{"b0","b1"}),anchorAt(h,"head","always_ff",{"head"}),
        anchorAt(h,"plain-a","logic clk",{"p0","p1"}),anchorAt(h,"plain-b","rst_n;",{"p2"}),
        anchorAt(h,"other","signal_003 =",{"other"})}))return {};
    auto* originalEditor=h.editor();auto* originalDocument=h.editor()->document();
    const auto originalView=motion(h.editor());
    if(!h.controller->setActive(true))return {};
    QJsonArray phases;double previousGeometry=0,previousLayout=0,previousPaint=0;
    const auto capturePhase=[&](const QString& phase){
        if(!settled(h,previousGeometry))return false;
        const auto actual=h.snapshot(),c=actual["connector"].toObject();
        auto state=sample(h);auto relations=state["serialRelations"].toObject();
        relations["geometryAdvanced"]=c["geometrySerial"].toDouble()>previousGeometry;
        relations["layoutMonotonic"]=c["layoutSerial"].toDouble()>=previousLayout;
        relations["paintAdvanced"]=c["paintSerial"].toDouble()>previousPaint;
        previousGeometry=c["geometrySerial"].toDouble();previousLayout=c["layoutSerial"].toDouble();previousPaint=c["paintSerial"].toDouble();
        state["serialRelations"]=relations;phases.append(QJsonObject{{"phase",phase},{"state",state}});
        if(raw)raw->append(QJsonObject{{"phase",phase},{"snapshot",actual}});
        return true;
    };
    if(!capturePhase("opened"))return {};
    auto* button=expandButton(h,"a");if(!button||!button->isVisible())return {};button->click();
    if(!capturePhase("group-expanded"))return {};
    QTextCursor cursor(h.editor()->document());const int at=h.text.indexOf("signal_000 <=");
    cursor.setPosition(at);cursor.setPosition(at+10,QTextCursor::KeepAnchor);h.editor()->setTextCursor(cursor);
    if(!capturePhase("selected"))return {};
    if(!h.editor()->toggleFoldAtLineForTest(3)||!capturePhase("collapsed"))return {};
    if(!h.editor()->toggleFoldAtLineForTest(3)||!capturePhase("reexpanded"))return {};
    auto* left=h.rails()->template findChild<QScrollArea*>("linkedNativeLeftRail");
    auto* right=h.rails()->template findChild<QScrollArea*>("linkedNativeRightRail");
    if(!left||!right)return {};
    wheelOn(left->viewport(),-120);
    if(!scrollFinished(left->verticalScrollBar())||!capturePhase("left-detached"))return {};
    wheelOn(right->viewport(),-120);
    if(!scrollFinished(right->verticalScrollBar())||!capturePhase("right-detached"))return {};
    const auto activeSession=h.snapshot().value("session");
    const bool closed=h.controller->setActive(false);
    const auto exit=h.snapshot();
    return {{"schema","zeroslack-default-native-frozen/v1"},{"platform",QGuiApplication::platformName()},
        {"window",QJsonArray{h.window.width(),h.window.height()}},{"phases",phases},{"closed",closed},
        {"exit",QJsonObject{{"active",h.controller->isActive()},{"railsGone",h.rails()==nullptr},
            {"resourcesEmpty",h.resources().isEmpty()},{"sessionCleared",exit["session"].toString().isEmpty()},
            {"priorSessionNonempty",!activeSession.toString().isEmpty()},{"editorPreserved",h.editor()==originalEditor},
            {"documentPreserved",h.editor()->document()==originalDocument},{"editorViewRestored",motion(h.editor())==originalView},
            {"originalPageRestored",h.stack->currentWidget()==h.page}}},
        {"coverage","Controller references/resources; actual Qt rails members, view geometry and widget/action tree; real editor sampling, fold and wheel events; matching connector geometry/request/layout/content/paint identities. Offscreen: no claim for DWM, OS hit testing or screenshot pixel equality."}};
}
}
