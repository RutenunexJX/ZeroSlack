#pragma once
#include <QAction>
#include <QCryptographicHash>

namespace DefaultWebObservation {
inline QJsonObject tree(QWidget* widget) {
    if(!widget)return {};
    QJsonArray children,actions;
    for(auto* child:widget->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))children.append(tree(child));
    for(auto* action:widget->actions())actions.append(QJsonObject{{"class",action->metaObject()->className()},
        {"name",action->objectName()},{"text",action->text()},{"enabled",action->isEnabled()}});
    return {{"class",widget->metaObject()->className()},{"name",widget->objectName()},{"children",children},{"actions",actions}};
}
template<class Harness> QJsonObject sample(Harness& h) {
    auto value=h.snapshot();value["session"]="<session>";
    const auto* editor=h.editor();const auto cursor=editor->textCursor();
    auto* canvas=h.window.template findChild<QWidget*>("linkedContextCanvas");
    const auto origin=canvas?editor->viewport()->mapTo(canvas,QPoint()):QPoint();
    value["editor"]=QJsonObject{{"position",cursor.position()},{"anchor",cursor.anchor()},
        {"vertical",editor->verticalScrollBar()->value()},{"horizontal",editor->horizontalScrollBar()->value()},
        {"font",editor->font().toString()},{"dpr",editor->devicePixelRatioF()},{"wrap",int(editor->lineWrapMode())},
        {"viewport",QJsonArray{editor->viewport()->width(),editor->viewport()->height()}},
        {"origin",QJsonArray{origin.x(),origin.y()}},
        {"sourceSha256",QString::fromLatin1(QCryptographicHash::hash(editor->toPlainText().toUtf8(),QCryptographicHash::Sha256).toHex())}};
    value["tree"]=tree(h.window.template findChild<QWidget*>("linkedContextPage"));
    return value;
}
template<class Harness> QJsonObject capture(Harness& h) {
    if(!h.ready||!h.store({h.anchor("first",2,{"alpha"}),h.anchor("middle",20,{"beta"}),h.anchor("last",70,{"gamma"})})||!h.controller->setActive(true))return {};
    if(!QTest::qWaitFor([&]{return h.editor()->viewport()->width()>0&&h.window.template findChild<QWidget*>("linkedContextPage")->isVisible();},5000))return {};
    QCoreApplication::processEvents();
    QJsonArray phases;phases.append(QJsonObject{{"phase","opened"},{"state",sample(h)}});
    QTextCursor cursor(h.editor()->document());const int at=h.text.indexOf("signal_020");
    cursor.setPosition(at);cursor.setPosition(at+10,QTextCursor::KeepAnchor);h.editor()->setTextCursor(cursor);
    h.editor()->verticalScrollBar()->setValue(20);QCoreApplication::processEvents();
    phases.append(QJsonObject{{"phase","selected-scrolled"},{"state",sample(h)}});
    const bool closed=h.controller->setActive(false);
    return {{"schema","zeroslack-default-web-frozen/v1"},{"platform",QGuiApplication::platformName()},
        {"window",QJsonArray{h.window.width(),h.window.height()}},{"phases",phases},{"closed",closed},
        {"coverage","Actual controller geometry/references, original editor state and complete Qt widget/action kinds and hierarchy. Offscreen: no claim for WebView2 rendering, mask, browser message consumption, DPR negotiation or Overlay painting."}};
}
}
