#include "mycodeeditor.h"
#include "documentblockmodel.h"
#include "documentreferenceservice.h"
#include "shareddocument.h"
#include "formatterservice.h"
#include "documentblockstore.h"
#include "tabmanager.h"
#include "tsdocument.h"
#include "pinloompreviewimage.h"
#include "blocknotestyle.h"
#include "editorblockcontent.h"
#include "editorgutter.h"
#include "waveblockcontent.h"
#include "insightvisualstyle.h"
#include <QLibrary>
#include <QPlainTextDocumentLayout>
#include <QtTest>
#include <QtWidgets>

namespace {
class WheelDeliveryObserver final : public QObject {
public:
    QWidget* viewport = nullptr;
    QList<QVariantList> deliveries;
    bool eventFilter(QObject* receiver,QEvent* event) override {
        if(receiver==viewport && event->type()==QEvent::Wheel){
            const auto* wheel=static_cast<QWheelEvent*>(event);
            deliveries.append({wheel->pixelDelta(),wheel->angleDelta(),int(wheel->buttons()),
                int(wheel->modifiers()),int(wheel->phase()),wheel->inverted(),int(wheel->source()),
                QVariant::fromValue(qulonglong(wheel->timestamp())),wheel->globalPosition(),wheel->position()});
        }
        return false;
    }
};
}

class EditorBlockTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    std::unique_ptr<MyCodeEditor> editor;
    DocumentBlockModel* model() { return editor->blockModel(); }
    QString insert(int position = 0) {
        QTextCursor cursor(editor->document()); cursor.setPosition(position);
        EditorAssociatedBlock fixed; fixed.type = "text"; fixed.text = "note";
        return model()->insert(fixed, cursor);
    }
    void settleGutter() {
        const auto size=editor->size();editor->resize(size.width()+1,size.height());editor->resize(size);
        QCoreApplication::processEvents();editor->viewport()->repaint();
    }
    QMap<QString,QRect> markerHits() {
        auto* gutter=editor->findChild<QWidget*>("editorLineNumberGutter");
        QMap<QString,QRect> result;
        for(int y=0;y<gutter->height();++y){
            const QPoint point(gutter->width()/2,y);
            const auto id=EditorGutter::associatedBlockAt(editor.get(),point);
            if(!id.isEmpty())result[id]=result.contains(id)?result[id].united(QRect(point,QSize(1,1))):QRect(point,QSize(1,1));
        }
        return result;
    }
private slots:
    void previewWheelMatchesSource_data() {
        QTest::addColumn<QString>("type");QTest::addColumn<bool>("blank");
        for(const auto& type:{"text","code","reference","image","wave"})
            for(bool blank:{false,true})QTest::newRow(qPrintable(QString(type)+(blank?"-blank":"-content")))<<QString(type)<<blank;
    }
    void previewWheelMatchesSource() {
        QFETCH(QString,type);QFETCH(bool,blank);
        editor->resize(950,720);editor->setLineWrapMode(QPlainTextEdit::NoWrap);
        const auto source=(QString("logic ")+QString(180,'x')+";\n").repeated(160);
        editor->setPlainText(source);
        EditorAssociatedBlock block;block.type=type;block.autoHeight=false;block.height=260;
        block.text=type=="wave"?WaveBlockContent::initialProject():QString("Block preview content\n").repeated(80);
        if(type=="reference")block.uri="https://example.invalid/"+QString("reference/").repeated(70);
        QTemporaryDir assets;QVERIFY(assets.isValid());
        if(type=="image"){
            QImage image(300,120,QImage::Format_RGB32);image.fill(Qt::blue);
            const auto file=assets.filePath("image.png");QVERIFY(image.save(file));QString error;
            block.asset=DocumentBlockStore::get(model())->importImage(file,&error);QVERIFY2(!block.asset.isEmpty(),qPrintable(error));
        }
        const auto id=model()->insert(block,QTextCursor(editor->document()->findBlockByNumber(20)));QVERIFY(!id.isEmpty());
        QWidget* shell=nullptr;
        const auto locate=[&]{for(auto* frame:editor->findChildren<QWidget*>("editorBlockShell"))if(frame->property("blockId")==id)shell=frame;return shell;};
        editor->goToLineNumber(21);QTRY_VERIFY(locate());
        auto* bar=editor->verticalScrollBar();
        const auto top=editor->blockGeometry(20).top;bar->setValue(bar->value()+qRound(top)-100);
        QTRY_VERIFY(shell->isVisible() && shell->y()>0);QCoreApplication::processEvents();
        auto* content=dynamic_cast<EditorBlockContent*>(shell->findChild<QWidget*>("editorBlockContent"));QVERIFY(content && !content->isEditing());
        QWidget* area=content;
        if(type=="text" || type=="code")area=content->textEditor()->viewport();
        if(type=="reference"){
            auto* text=content->findChild<QTextEdit*>("editorBlockReferenceText");QVERIFY(text);
            QTRY_VERIFY(text->isVisible() && !text->toPlainText().isEmpty());area=text->viewport();
        }
        if(type=="image"){QTRY_VERIFY(!content->imagePreview()->sourcePixmap().isNull());area=content->imagePreview();}
        if(type=="wave"){
            auto* native=content->findChild<QWidget*>("WaveEditor");QVERIFY(native);
            area=native->findChild<QWidget*>("waveEditorCanvas");QVERIFY(area && area->isVisible());
        }
        const QPoint global=blank?shell->mapToGlobal(QPoint(shell->width()-3,shell->height()/2))
            :area->mapToGlobal(area->rect().center());
        auto* hit=editor->viewport()->childAt(editor->viewport()->mapFromGlobal(global));QVERIFY(hit);
        QVERIFY(shell==hit || shell->isAncestorOf(hit));
        if(!blank)QVERIFY(hit==area || area->isAncestorOf(hit));
        const auto sourceState=editor->textCursor();const auto selected=shell->property("blockSelected");
        model()->markSaved();editor->document()->setModified(false);const auto stored=model()->serialize();
        const int start=bar->value(),horizontal=editor->horizontalScrollBar()->maximum()/2;
        const int anchorOffset=shell->y()-qRound(editor->blockGeometry(20).top);
        const auto innerBars=content->findChildren<QScrollBar*>();QList<int> innerValues;for(auto* inner:innerBars)innerValues<<inner->value();
        WheelDeliveryObserver observer;observer.viewport=editor->viewport();qApp->installEventFilter(&observer);
        QSignalSpy zoom(editor.get(),&MyCodeEditor::fontZoomRequested);
        struct Input{QPoint pixel,angle;Qt::KeyboardModifiers modifiers;Qt::ScrollPhase phase;bool inverted;};
        const QList<Input> inputs={{{},{0,-120},Qt::NoModifier,Qt::NoScrollPhase,false},
            {{},{0,120},Qt::NoModifier,Qt::NoScrollPhase,false},{{0,-1},{},Qt::NoModifier,Qt::ScrollUpdate,false},
            {{0,-7},{0,-120},Qt::NoModifier,Qt::ScrollUpdate,true},{{0,3},{},Qt::NoModifier,Qt::ScrollMomentum,false},
            {{},{},Qt::NoModifier,Qt::ScrollBegin,false},{{},{},Qt::NoModifier,Qt::ScrollEnd,false},
            {{},{0,-120},Qt::ControlModifier,Qt::NoScrollPhase,false},{{0,-2},{},Qt::ControlModifier,Qt::ScrollUpdate,false},
            {{},{0,-120},Qt::ShiftModifier,Qt::NoScrollPhase,false},{{-5,0},{},Qt::ShiftModifier,Qt::ScrollUpdate,false},
            {{},{-120,0},Qt::NoModifier,Qt::NoScrollPhase,false},
            {{},{0,120},Qt::ControlModifier|Qt::ShiftModifier,Qt::NoScrollPhase,false}};
        for(const auto& input:inputs){
            const auto dispatch=[&](QWidget* receiver){
                QWheelEvent wheel(receiver->mapFromGlobal(QPointF(global)),global,input.pixel,input.angle,
                    Qt::NoButton,input.modifiers,input.phase,input.inverted,Qt::MouseEventSynthesizedBySystem);
                wheel.setTimestamp(123456);QApplication::sendEvent(receiver,&wheel);return wheel.isAccepted();
            };
            bar->setValue(start);editor->horizontalScrollBar()->setValue(horizontal);observer.deliveries.clear();zoom.clear();
            QVERIFY(dispatch(editor->viewport()));QCOMPARE(observer.deliveries.size(),1);
            const auto expected=observer.deliveries.first();const int expectedY=bar->value(),expectedX=editor->horizontalScrollBar()->value(),expectedZoom=zoom.size();
            bar->setValue(start);editor->horizontalScrollBar()->setValue(horizontal);QCoreApplication::processEvents();observer.deliveries.clear();zoom.clear();
            QVERIFY(dispatch(hit));QCOMPARE(observer.deliveries.size(),1);QCOMPARE(observer.deliveries.first(),expected);
            QCOMPARE(bar->value(),expectedY);QCOMPARE(editor->horizontalScrollBar()->value(),expectedX);QCOMPARE(zoom.size(),expectedZoom);
            QCoreApplication::processEvents();QCOMPARE(shell->y()-qRound(editor->blockGeometry(20).top),anchorOffset);
            for(int i=0;i<innerBars.size();++i)QCOMPARE(innerBars[i]->value(),innerValues[i]);
            QCOMPARE(content->isEditing(),false);QCOMPARE(shell->property("blockSelected"),selected);
        }
        // At either boundary the routed event must still be consumed exactly once.
        for(bool bottom:{false,true}){
            bar->setValue(bottom?bar->maximum():bar->minimum());const int boundary=bar->value();observer.deliveries.clear();
            QWheelEvent wheel(hit->mapFromGlobal(global),global,{},QPoint(0,bottom?-120:120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(hit,&wheel);QVERIFY(wheel.isAccepted());QCOMPARE(observer.deliveries.size(),1);QCOMPARE(bar->value(),boundary);
        }
        QCOMPARE(model()->serialize(),stored);QVERIFY(!model()->dirty());QVERIFY(!editor->document()->isModified());QCOMPARE(editor->toPlainText(),source);
        QCOMPARE(editor->textCursor().position(),sourceState.position());QCOMPARE(editor->textCursor().anchor(),sourceState.anchor());
        qInfo().noquote()<<"PREVIEW_WHEEL"<<type<<"blank"<<blank<<"receiver"<<hit->metaObject()->className()<<hit->objectName()<<"DPR"<<editor->devicePixelRatioF()<<"matchedInputs"<<inputs.size();
    }
    void projectedPixelWheelKeepsFineDeltas() {
        editor->setPlainText(QString("logic item;\n").repeated(200));insert();settleGutter();
        auto* bar=editor->verticalScrollBar();bar->setValue(300);const int start=bar->value();
        for(int delta:{-1,-2,-3,1,2,3}){
            const int before=bar->value();QWheelEvent wheel(QPointF(20,20),editor->viewport()->mapToGlobal(QPoint(20,20)),
                QPoint(0,delta),QPoint(0,delta<0?-120:120),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
            QApplication::sendEvent(editor->viewport(),&wheel);QCOMPARE(bar->value()-before,-delta);
        }
        QCOMPARE(bar->value(),start);
    }
    void foldedPixelWheelKeepsRowUnits_data() {
        QTest::addColumn<QString>("inactive");
        for(const auto* kind:{"no-block","all-detached","removed-reference"})QTest::newRow(kind)<<QString(kind);
    }
    void foldedPixelWheelKeepsRowUnits() {
        QFETCH(QString,inactive);
        const auto source=QString("module m;\ninitial begin\n  a = 1;\n  b = 2;\nend\nendmodule\n")+QString("// tail\n").repeated(200);
        editor->setPlainText(source);editor->document()->setModified(false);
        if(inactive!="no-block"){
            EditorAssociatedBlock block;block.type=inactive=="all-detached"?"text":"reference";block.text="Retained association";
            block.detached=inactive=="all-detached";
            if(block.type=="reference"){
                block.uri="https://example.invalid/wheel-units";
                block.reference={{"version",1},{"sourceKind","legacy"},{"display","embedded"},
                    {"anchorId","wheel-anchor"},{"linkId","wheel-link"},{"targetHash",QString(64,'a')}};
            }
            const auto id=model()->insert(block,QTextCursor(editor->document()->findBlockByNumber(20)));QVERIFY(!id.isEmpty());
            if(block.type=="reference"){QVERIFY(model()->remove(id));QVERIFY(model()->find(id)->removedReference());}
            QCOMPARE(model()->items().size(),1);
        }
        QTRY_VERIFY(!editor->syntaxDocument()->hasPendingEdits());QVERIFY(editor->toggleFoldAtLineForTest(1));settleGutter();
        QVERIFY(editor->viewProjectionActive());QVERIFY(!editor->sourceLineVisible(2));
        auto* bar=editor->verticalScrollBar();QCOMPARE(bar->singleStep(),1);QVERIFY(bar->maximum()>80);
        model()->markSaved();const auto stored=model()->serialize();
        struct Input{int pixel,angle,rows;};
        const QList<Input> inputs={{-7,0,3},{7,0,-3},{-1,0,3},{-1,0,3},{1,0,-3},
            {-7,-120,3},{7,120,-3},{-1,-240,6},{1,240,-6},{0,-120,3},{0,120,-3}};
        for(const auto& input:inputs){
            bar->setValue(40);const auto top=editor->blockGeometry(40).top;const auto rowHeight=editor->blockGeometry(40).height;
            QWheelEvent wheel(QPointF(20,20),editor->viewport()->mapToGlobal(QPoint(20,20)),QPoint(0,input.pixel),QPoint(0,input.angle),
                Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
            QApplication::sendEvent(editor->viewport(),&wheel);
            qInfo()<<"FOLDED_WHEEL_UNITS"<<inactive<<"pixel"<<input.pixel<<"angle"<<input.angle<<"actualRows"<<bar->value()-40<<"expectedRows"<<input.rows;
            QVERIFY(wheel.isAccepted());QCOMPARE(bar->value()-40,input.rows);
            QVERIFY(qAbs(editor->blockGeometry(40).top-top+input.rows*rowHeight)<1.0);
        }
        QCOMPARE(model()->serialize(),stored);QVERIFY(!model()->dirty());QVERIFY(!editor->document()->isModified());QCOMPARE(editor->toPlainText(),source);
    }
    void pixelWheelFollowsProjectionTransitions_data() {
        QTest::addColumn<QString>("change");
        for(const auto* kind:{"remove-undo","detach-reattach","reference-remove-undo"})QTest::newRow(kind)<<QString(kind);
    }
    void pixelWheelFollowsProjectionTransitions() {
        QFETCH(QString,change);
        const auto source=QString("module m;\ninitial begin\n  a = 1;\n  b = 2;\nend\nendmodule\n")+QString("// tail\n").repeated(200);
        editor->setPlainText(source);editor->document()->setModified(false);
        QTRY_VERIFY(!editor->syntaxDocument()->hasPendingEdits());QVERIFY(editor->toggleFoldAtLineForTest(1));settleGutter();
        const QTextCursor anchor(editor->document()->findBlockByNumber(20));
        EditorAssociatedBlock block;block.type=change=="reference-remove-undo"?"reference":"text";block.text="Wheel units";
        if(block.type=="reference"){
            block.uri="https://example.invalid/wheel-units";
            block.reference={{"version",1},{"sourceKind","legacy"},{"display","embedded"},
                {"anchorId","wheel-anchor"},{"linkId","wheel-link"},{"targetHash",QString(64,'a')}};
        }
        const auto id=model()->insert(block,anchor);QVERIFY(!id.isEmpty());auto* bar=editor->verticalScrollBar();
        for(int phase=0;phase<5;++phase){
            const bool pixelUnits=phase%2==0;
            if(phase>0){
                if(change=="detach-reattach"){
                    if(pixelUnits)QVERIFY(model()->reattach(id,anchor));
                    else{auto detached=*model()->find(id);detached.detached=true;QVERIFY(model()->update(detached));}
                }else if(pixelUnits)QVERIFY(model()->undoAssociation(false));
                else QVERIFY(model()->remove(id));
            }
            settleGutter();QVERIFY(editor->viewProjectionActive());QVERIFY(!editor->sourceLineVisible(2));
            if(pixelUnits)QVERIFY(bar->singleStep()>1);else QCOMPARE(bar->singleStep(),1);
            bar->setValue(pixelUnits?300:40);const int before=bar->value();model()->markSaved();const auto stored=model()->serialize();
            const int delta=pixelUnits?-1:-7;const int expected=pixelUnits?1:3;
            QWheelEvent wheel(QPointF(20,20),editor->viewport()->mapToGlobal(QPoint(20,20)),QPoint(0,delta),{},
                Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
            QApplication::sendEvent(editor->viewport(),&wheel);
            qInfo()<<"WHEEL_PROJECTION_TRANSITION"<<change<<"phase"<<phase<<"pixelUnits"<<pixelUnits<<"actual"<<bar->value()-before<<"expected"<<expected;
            QVERIFY(wheel.isAccepted());QCOMPARE(bar->value()-before,expected);QCOMPARE(model()->serialize(),stored);QVERIFY(!model()->dirty());
        }
        QCOMPARE(editor->toPlainText(),source);QVERIFY(!editor->document()->isModified());
    }
    void blocksKeepGutterWidthAndSourceColumn() {
        auto* gutter=editor->findChild<QWidget*>("editorLineNumberGutter");QVERIFY(gutter);
        settleGutter();const int width=gutter->width(),viewportX=editor->viewport()->x();
        const auto source=editor->toPlainText();
        const auto codeX=[&]{return editor->viewport()->x()+editor->cursorRect(QTextCursor(editor->document())).x();};
        const int beforeX=codeX();
        const auto first=insert(),second=insert(editor->document()->findBlockByNumber(1).position());
        for(bool visible:{true,false,true}){
            editor->setAssociatedBlocksVisible(visible);
            for(bool collapsed:{false,true,false}){
                for(const auto& id:{first,second}){auto block=*model()->find(id);block.collapsed=collapsed;QVERIFY(model()->update(block));}
                settleGutter();
                QCOMPARE(EditorGutter::totalWidth(editor.get()),width);QCOMPARE(gutter->width(),width);
                QCOMPARE(editor->viewport()->x(),viewportX);QCOMPARE(codeX(),beforeX);QCOMPARE(editor->toPlainText(),source);
            }
        }
        QVERIFY(model()->remove(first));QVERIFY(model()->remove(second));settleGutter();
        QCOMPARE(gutter->width(),width);QCOMPARE(codeX(),beforeX);
        editor->setPlainText(QString("line\n").repeated(998));settleGutter();const int threeDigits=gutter->width();
        editor->moveCursor(QTextCursor::End);editor->insertPlainText("\nline");settleGutter();
        QVERIFY(gutter->width()>threeDigits);QCOMPARE(editor->viewport()->x()-gutter->width(),viewportX-width);
    }
    void inlineMarkersAndHandlesAvoidSourceRows() {
        editor->resize(800,800);
        const auto first=insert(),second=insert(),third=insert(editor->document()->findBlockByNumber(1).position());
        const QStringList ids{first,second,third};
        const auto source=editor->toPlainText();
        const auto stored=model()->serialize();
        auto* gutter=editor->findChild<QWidget*>("editorLineNumberGutter");QVERIFY(gutter);
        for(bool visible:{true,false,true}){
            editor->setAssociatedBlocksVisible(visible);settleGutter();
            const auto hits=markerHits();QCOMPARE(hits.size(),ids.size());
            for(const auto& id:ids){
                QVERIFY(hits[id].height()>=editor->fontMetrics().height());
                for(int row=0;row<editor->blockCount();++row){
                    const auto g=editor->blockGeometry(row);
                    QVERIFY(!hits[id].intersects(QRect(0,qRound(g.top),gutter->width(),qRound(g.height))));
                }
                for(const auto& other:ids)if(id!=other)QVERIFY(!hits[id].intersects(hits[other]));
                if(visible){
                    QWidget* shell=nullptr;QWidget* grip=nullptr;
                    for(auto* candidate:editor->findChildren<QWidget*>("editorBlockShell"))if(candidate->property("blockId")==id)shell=candidate;
                    for(auto* candidate:editor->findChildren<QWidget*>("editorBlockDragHandle"))if(candidate->property("blockId")==id)grip=candidate;
                    QVERIFY(shell && grip);
                    const auto marker=EditorGutter::blockMarkerRect(editor.get(),shell->geometry());
                    QVERIFY(marker.contains(grip->geometry()));QVERIFY(gutter->rect().contains(marker));
                    QCOMPARE(EditorGutter::associatedBlockAt(editor.get(),grip->geometry().center()),id);
                    QTest::mouseClick(gutter,Qt::LeftButton,Qt::NoModifier,hits[id].center());
                    QVERIFY(shell->property("blockSelected").toBool());
                }else{
                    for(auto* frame:editor->findChildren<QWidget*>("editorBlockShell"))QVERIFY(!frame->isVisible());
                    for(auto* grip:editor->findChildren<QWidget*>("editorBlockDragHandle"))QVERIFY(!grip->isVisible());
                    QSignalSpy copy(editor.get(),&MyCodeEditor::editorBlockClipboardRequested);
                    QTest::mouseClick(gutter,Qt::LeftButton,Qt::NoModifier,hits[id].center());
                    auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());QVERIFY(menu);
                    QCOMPARE(menu->objectName(),QString("collapsedBlockMenu"));
                    QAction* action=nullptr;for(auto* item:menu->actions())if(item->text()=="Copy")action=item;
                    QVERIFY(action);action->trigger();menu->close();
                    QCOMPARE(copy.count(),1);QCOMPARE(copy.first().first().toString(),id);
                }
            }
            const auto row=editor->blockGeometry(0);
            QVERIFY(EditorGutter::associatedBlockAt(editor.get(),QPoint(gutter->width()/2,qRound(row.top+row.height/2))).isEmpty());
            QCOMPARE(model()->serialize(),stored);QCOMPARE(editor->toPlainText(),source);
        }
    }
    void compactMarkersFollowFoldedAndScrolledProjection() {
        editor->resize(650,700);
        const QString source="module m;\ninitial begin\n  a = 1;\n  b = 2;\nend\nendmodule\n"+QString("// tail\n").repeated(80);
        editor->setPlainText(source);
        const auto first=insert(source.indexOf("a =")),second=insert(source.indexOf("a =")),third=insert(source.indexOf("b ="));
        editor->setAssociatedBlocksVisible(false);settleGutter();
        QTRY_VERIFY(!editor->syntaxDocument()->hasPendingEdits());QVERIFY(editor->toggleFoldAtLineForTest(1));settleGutter();
        const auto before=markerHits();QCOMPARE(before.size(),3);
        const auto anchor=editor->blockGeometry(1),next=editor->blockGeometry(5);
        for(const auto& hit:before){QVERIFY(hit.top()>=anchor.top+anchor.height);QVERIFY(hit.bottom()<next.top);}
        auto* scroll=editor->verticalScrollBar();const int previous=scroll->value();
        scroll->setValue(previous+editor->fontMetrics().height());QCoreApplication::processEvents();
        const auto after=markerHits();QCOMPARE(after.size(),3);
        for(const auto& id:{first,second,third})QCOMPARE(after[id].top()-before[id].top(),previous-scroll->value());
        QVERIFY(editor->toggleFoldAtLineForTest(1));editor->setAssociatedBlocksVisible(true);settleGutter();
        QCOMPARE(editor->toPlainText(),source);
    }
    void associatedBlocksFollowDocumentScroll_data() {
        QTest::addColumn<QString>("input");
        for(const auto& input:{"wheel","thumb","page"})QTest::newRow(input)<<QString::fromLatin1(input);
    }
    void associatedBlocksFollowDocumentScroll() {
        QFETCH(QString,input);
        QStringList lines;for(int i=0;i<250;++i)lines<<QString("logic scroll_anchor_%1;").arg(i);
        const auto source=lines.join('\n');editor->setPlainText(source);editor->resize(900,740);
        QStringList ids;QList<QTextCursor> anchors;
        for(int line:{2,7}){
            QTextCursor anchor(editor->document()->findBlockByNumber(line));anchors<<anchor;
            EditorAssociatedBlock block;block.type=line==2?"text":"wave";
            block.text=line==2?"Unsaved block input":WaveBlockContent::initialProject();
            block.autoHeight=false;block.height=150;ids<<model()->insert(block,anchor);
            QVERIFY(!ids.last().isEmpty());
        }
        auto* bar=editor->verticalScrollBar();QTRY_VERIFY(bar->maximum()>editor->height());
        bar->setValue(0);
        const auto shellFor=[&](const QString& id)->QWidget* {
            for(auto* shell:editor->findChildren<QWidget*>("editorBlockShell"))
                if(shell->property("blockId").toString()==id)return shell;
            return nullptr;
        };
        QList<QWidget*> shells,grips;QList<int> offsets,gripOffsets;
        for(int i=0;i<ids.size();++i){
            QWidget* shell=nullptr;QTRY_VERIFY((shell=shellFor(ids[i])) && shell->isVisible());shells<<shell;
            QWidget* grip=nullptr;for(auto* candidate:editor->findChildren<QWidget*>("editorBlockDragHandle"))
                if(candidate->property("blockId").toString()==ids[i])grip=candidate;
            QVERIFY(grip);grips<<grip;
            offsets<<shell->y()-editor->cursorRect(anchors[i]).y();
            gripOffsets<<grip->mapTo(editor.get(),QPoint()).y()-shell->mapTo(editor.get(),QPoint()).y();
        }
        auto* text=shells[0]->findChild<QTextEdit*>("editorBlockText");QVERIFY(text);
        editor->selectAssociatedBlock(ids[0],true);QTest::keyClicks(text," pending");
        auto selection=text->textCursor();selection.movePosition(QTextCursor::PreviousWord,QTextCursor::KeepAnchor);text->setTextCursor(selection);
        const auto textState=text->toPlainText();const QPointer<QWidget> wave=shells[1]->findChild<QWidget*>("WaveEditor");QVERIFY(wave);
        const auto before=model()->serialize();const auto beforeAnchor=editor->cursorRect(anchors[0]).y();
        const auto capture=[&](const QString& phase){
            QJsonArray items;
            for(int i=0;i<ids.size();++i)items.append(QJsonObject{{"id",ids[i]},
                {"anchorY",editor->cursorRect(anchors[i]).y()},{"shellY",shells[i]->y()},
                {"gripY",grips[i]->mapTo(editor.get(),QPoint()).y()}});
            const QJsonObject sample{{"input",input},{"phase",phase},{"scroll",bar->value()},
                {"dpr",editor->devicePixelRatioF()},{"blocks",items}};
            qInfo().noquote()<<QJsonDocument(sample).toJson(QJsonDocument::Compact);
        };
        capture("before");
        if(input=="wheel"){
            const QPoint point(editor->viewport()->width()-25,80);
            QWheelEvent wheel(point,editor->viewport()->mapToGlobal(point),{},QPoint(0,-120),
                Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(editor->viewport(),&wheel);
        }else if(input=="thumb"){
            QStyleOptionSlider option;option.initFrom(bar);option.rect=bar->rect();option.orientation=Qt::Vertical;
            option.minimum=bar->minimum();option.maximum=bar->maximum();option.pageStep=bar->pageStep();
            option.sliderPosition=bar->sliderPosition();option.sliderValue=bar->value();
            const auto start=bar->style()->subControlRect(QStyle::CC_ScrollBar,&option,QStyle::SC_ScrollBarSlider,bar).center();
            const auto end=start+QPoint(0,18);QTest::mousePress(bar,Qt::LeftButton,Qt::NoModifier,start);
            QMouseEvent move(QEvent::MouseMove,end,bar->mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(bar,&move);QTest::mouseRelease(bar,Qt::LeftButton,Qt::NoModifier,end);
        }else QTest::keyClick(bar,Qt::Key_PageDown);
        QTRY_VERIFY(bar->value()>0);QTest::qWait(200);capture("after");
        QVERIFY(editor->cursorRect(anchors[0]).y()<beforeAnchor);
        for(int i=0;i<ids.size();++i){
            QTRY_COMPARE(shells[i]->y()-editor->cursorRect(anchors[i]).y(),offsets[i]);
            QCOMPARE(grips[i]->mapTo(editor.get(),QPoint()).y()-shells[i]->mapTo(editor.get(),QPoint()).y(),gripOffsets[i]);
        }
        bar->setValue(bar->maximum());QTest::qWait(30);
        for(auto* shell:shells)QVERIFY(!shell->isVisible() || !shell->geometry().intersects(editor->viewport()->rect()));
        if(input=="wheel")QVERIFY(text->hasFocus());
        bar->setValue(0);
        for(int i=0;i<ids.size();++i){QTRY_VERIFY(shells[i]->isVisible());QTRY_COMPARE(shells[i]->y()-editor->cursorRect(anchors[i]).y(),offsets[i]);}
        QCOMPARE(text->toPlainText(),textState);QCOMPARE(shells[1]->findChild<QWidget*>("WaveEditor"),wave.data());
        QCOMPARE(text->textCursor().position(),selection.position());QCOMPARE(text->textCursor().anchor(),selection.anchor());
        QCOMPARE(model()->serialize(),before);QCOMPARE(editor->toPlainText(),source);capture("returned");
    }
    void referenceDisplayKeepsOneIdentityAndEmbeddedMetrics() {
        EditorAssociatedBlock reference;reference.type="reference";reference.uri="https://example.invalid/reference";
        reference.caption="Specification";reference.height=230;reference.width=650;
        const auto id=model()->insert(reference,editor->textCursor());QVERIFY(!id.isEmpty());
        const auto original=*model()->find(id);const auto text=editor->toPlainText();
        editor->selectAssociatedBlock(id);QFrame* shell=nullptr;
        QTRY_VERIFY((shell=editor->findChild<QFrame*>("editorBlockShell")) && shell->isVisible());
        const QPointer<QWidget> content=shell->findChild<QWidget*>("editorBlockContent");QVERIFY(content);
        const auto embedded=shell->height();QVERIFY(embedded>36);
        QVERIFY(DocumentReferenceService::setDisplay(model(),id,"compact"));QTRY_COMPARE(shell->height(),36);
        QCOMPARE(model()->items().size(),1);QCOMPARE(model()->find(id)->uri,original.uri);
        QCOMPARE(model()->find(id)->height,230);QCOMPARE(model()->find(id)->width,650);
        QCOMPARE(model()->find(id)->appearance,original.appearance);QVERIFY(content);
        auto* label=shell->findChild<QLabel*>(QString(),Qt::FindDirectChildrenOnly);QVERIFY(label);
        QVERIFY(label->isVisible());QVERIFY(label->height()>0);QVERIFY(label->text().contains("Specification"));
        QVERIFY(DocumentReferenceService::setDisplay(model(),id,"embedded"));QTRY_COMPARE(shell->height(),embedded);
        QCOMPARE(shell->findChild<QWidget*>("editorBlockContent"),content.data());
        QVERIFY(model()->undoAssociation(false));QTRY_COMPARE(shell->height(),36);
        QVERIFY(model()->undoAssociation(true));QTRY_COMPARE(shell->height(),embedded);
        QCOMPARE(editor->toPlainText(),text);
        auto saved=model()->serialize();QTextDocument restoredDocument;restoredDocument.setPlainText(text);
        auto* restored=DocumentBlockModel::get(&restoredDocument);QString error;QVERIFY(restored->restore(saved,&error));
        QCOMPARE(restored->find(id)->reference,model()->find(id)->reference);QCOMPARE(restored->find(id)->appearance,original.appearance);
        auto rows=saved["blocks"].toArray();auto row=rows[0].toObject();row["reference"]=QJsonObject{{"version",2},{"display","compact"}};
        rows[0]=row;saved["blocks"]=rows;const auto before=restored->serialize();QVERIFY(!restored->restore(saved,&error));QCOMPARE(restored->serialize(),before);
    }
    void initTestCase() {
        QVERIFY(settings.isValid());QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
        QCoreApplication::setOrganizationName("ZeroSlackTests");QCoreApplication::setApplicationName("BlockWave");
    }
    void scrollbarDiagnosticsUseRealLocationsAndPreserveNativeScroll() {
        QStringList lines{"module scroll_test;"};for(int i=1;i<80;++i)lines<<QString("  logic signal_%1;").arg(i);lines<<"endmodule";
        editor->setPlainText(lines.join('\n'));editor->resize(520,280);editor->show();
        auto* bar=editor->verticalScrollBar();QVERIFY(bar);QTRY_VERIFY(bar->maximum()>0);
        SemanticDiagnostic first;first.line=50;first.column=5;first.message="First issue";first.severity=SemanticDiagnostic::Error;
        editor->setDiagnosticHighlights({first});
        const auto mark=[&] {
            const auto image=bar->grab().toImage();const auto color=InsightVisualStyle::theme().syntax.errorUnderline.rgb();
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
                if(image.pixelColor(x,y).rgb()==color)return QPoint(qRound(x/bar->devicePixelRatioF()),qRound(y/bar->devicePixelRatioF()));
            return QPoint(-1,-1);
        };
        QTRY_VERIFY(mark().x()>=0);auto* overview=bar->findChild<QWidget*>("editorDiagnosticOverview");QVERIFY(overview && overview->isVisible());
        QCOMPARE(overview->parentWidget(),bar);QVERIFY(overview->testAttribute(Qt::WA_TransparentForMouseEvents));
        QTest::mouseClick(bar,Qt::LeftButton,Qt::NoModifier,mark());QCOMPARE(editor->textCursor().blockNumber(),49);QCOMPARE(editor->textCursor().positionInBlock(),4);
        SemanticDiagnostic second=first;second.column=9;second.message="Second issue";editor->setDiagnosticHighlights({first,second});bar->setValue(0);QCoreApplication::processEvents();
        QTest::mouseClick(bar,Qt::LeftButton,Qt::NoModifier,mark());
        QTRY_VERIFY(qobject_cast<QMenu*>(QApplication::activePopupWidget()));auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
        QCOMPARE(menu->actions().size(),2);QVERIFY(menu->actions()[1]->text().contains("50:9"));menu->actions()[1]->trigger();menu->close();
        QCOMPARE(editor->textCursor().blockNumber(),49);QCOMPARE(editor->textCursor().positionInBlock(),8);
        bar->setValue(0);QTest::keyClick(bar,Qt::Key_PageDown);QVERIFY(bar->value()>0);bar->setValue(0);
        QStyleOptionSlider option;option.initFrom(bar);option.rect=bar->rect();option.orientation=Qt::Vertical;
        option.minimum=bar->minimum();option.maximum=bar->maximum();option.pageStep=bar->pageStep();option.sliderPosition=0;option.sliderValue=0;
        const auto slider=bar->style()->subControlRect(QStyle::CC_ScrollBar,&option,QStyle::SC_ScrollBarSlider,bar);
        const auto start=slider.center();QTest::mousePress(bar,Qt::LeftButton,Qt::NoModifier,start);
        QMouseEvent move(QEvent::MouseMove,start+QPoint(0,30),bar->mapToGlobal(start+QPoint(0,30)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(bar,&move);QTest::mouseRelease(bar,Qt::LeftButton,Qt::NoModifier,start+QPoint(0,30));QVERIFY(bar->value()>0);
        editor->setDiagnosticHighlights({});QTRY_VERIFY(!overview->isVisible());
        editor->setPlainText("module m;\ninitial begin\n  a = 1;\n  b = 2;\nend\nendmodule\n");
        insert(editor->toPlainText().indexOf("a ="));QTRY_VERIFY(!editor->syntaxDocument()->hasPendingEdits());QVERIFY(editor->toggleFoldAtLineForTest(1));
        first.line=3;first.column=3;editor->setDiagnosticHighlights({first});QCoreApplication::processEvents();
        QTRY_VERIFY(mark().x()>=0);QTest::mouseClick(bar,Qt::LeftButton,Qt::NoModifier,mark());
        QCOMPARE(editor->textCursor().blockNumber(),2);QVERIFY(editor->foldLineVisibleForTest(2));QVERIFY(!model()->items().isEmpty());
        auto options=editor->annotationDisplayOptions();options.enabled=false;editor->setAnnotationDisplayOptions(options);QTRY_VERIFY(!overview->isVisible());
    }
    void namedPresetsPreserveLegacyAndRoundTrip() {
        for (const auto& type:QStringList{"text","image","reference","attachment","code","wave"}) {
            const auto id=model()->insert(type,QTextCursor(editor->document()),type=="wave" ? WaveBlockContent::initialProject() : "sample");
            QVERIFY(!id.isEmpty()); QVERIFY(BlockNoteStyle::validPreset(model()->find(id)->appearance));
            const auto expected=type=="text" ? "Paper" : type=="image" ? "Plain" : type=="code" || type=="wave" ? "Technical" : "Outline";
            QCOMPARE(model()->find(id)->appearance.value("preset").toString(),QString(expected));
        }
        auto value=model()->items().first(); value.appearance={{"style","Outlined"},{"background","#fff4cc"}}; QVERIFY(model()->update(value));
        auto saved=model()->serialize(); QTextDocument copy; QString issue; auto* restored=DocumentBlockModel::get(&copy);
        QVERIFY2(restored->restore(saved,&issue),qPrintable(issue)); QCOMPARE(restored->items().first().appearance,value.appearance);
        auto bad=saved; auto items=bad["blocks"].toArray(); auto row=items[1].toObject(); auto appearance=row["appearance"].toObject();
        appearance["presetVersion"]="future"; row["appearance"]=appearance; items[1]=row; bad["blocks"]=items;
        const auto before=restored->serialize(); QVERIFY(!restored->restore(bad,&issue)); QCOMPARE(restored->serialize(),before);
    }
    void tickxMemoryEditorPreviewAndIndependentCopy() {
        QString issue; const auto payload=WaveBlockContent::initialProject(); QVERIFY2(WaveBlockContent::validate(payload.toUtf8(),&issue),qPrintable(issue));
        EditorAssociatedBlock block;block.type="wave";block.text=payload;block.autoHeight=true;
        const auto id=model()->insert(block,QTextCursor(editor->document()));
        WaveBlockContent view(model(),id,nullptr);view.resize(900,440);QVERIFY(view.present(payload,false));view.show();
        auto* native=view.findChild<QWidget*>("WaveEditor");QVERIFY(native);
        auto* canvas=native->findChild<QWidget*>("waveEditorCanvas");QVERIFY(canvas);
        QCOMPARE(native->property("wavewidgets.contract").toString(),QString("wave-workbench.wave-editor/v1"));
        QVERIFY(!view.findChild<QTabWidget*>("waveBlockTabs"));
        QLibrary library(QCoreApplication::applicationDirPath()+"/components/tickx/wavewidgets.dll");QVERIFY(library.load());
        using Read=int(*)(QWidget*,char*,size_t,size_t*,char*,size_t);
        using Update=int(*)(QWidget*,const char*,size_t,char*,size_t);
        using Action=int(*)(QWidget*,const char*,char*,size_t);
        const auto read=reinterpret_cast<Read>(library.resolve("wavewidgets_wave_editor_state_v1"));
        const auto configure=reinterpret_cast<Update>(library.resolve("wavewidgets_configure_wave_editor_v1"));
        const auto action=reinterpret_cast<Action>(library.resolve("wavewidgets_wave_editor_action_v1"));QVERIFY(read && configure && action);
        char message[2048]{};
        const auto state=[&]{size_t size=0;if(read(native,nullptr,0,&size,message,sizeof message)!=0)return QJsonObject{};
            QByteArray data(qsizetype(size),Qt::Uninitialized);if(read(native,data.data(),size,&size,message,sizeof message)!=0)return QJsonObject{};
            data.chop(1);return QJsonDocument::fromJson(data).object();};
        const auto lanes=[&]{return QJsonDocument::fromJson(model()->find(id)->text.toUtf8()).object()["scenarios"].toArray()[0].toObject()["lanes"].toArray();};
        QCOMPARE(lanes().size(),0);QCOMPARE(state()["mode"].toString(),QString("preview"));
        QVERIFY(action(native,"add-bit",message,sizeof message)!=0);
        QVERIFY(view.present(payload,false,true));
        const auto add=[&](const char* kind,const QString& name,const QString& parameter){
            QCOMPARE(action(native,kind,message,sizeof message),0);
            auto* nameEdit=native->findChild<QLineEdit*>("QuickLaneNameEdit");QVERIFY(nameEdit && nameEdit->isVisible());
            nameEdit->setFocus();nameEdit->selectAll();QTest::keyClicks(nameEdit,name);
            auto* field=native->findChild<QLineEdit*>("QuickLaneParameterEdit");
            if(!parameter.isEmpty()){QVERIFY(field && field->isVisible());field->setFocus();field->selectAll();QTest::keyClicks(field,parameter);}
            QTest::keyClick(parameter.isEmpty()?nameEdit:field,Qt::Key_Return);QTest::qWait(20);
        };
        add("add-clock","clock","10 ns");add("add-bit","enable",{});add("add-bus","data","8");
        QVERIFY2(view.flush(&issue),qPrintable(issue));QCOMPARE(lanes().size(),3);QCOMPARE(lanes()[2].toObject()["width"].toInt(),8);
        const auto edited=model()->find(id)->text;
        const auto config=QJsonDocument(QJsonObject{{"view",QJsonObject{{"cursorTick","7000"},{"startTick","1000"},{"endTick","40000"}}}}).toJson(QJsonDocument::Compact);
        QCOMPARE(configure(native,config.constData(),size_t(config.size()),message,sizeof message),0);const auto beforeView=state().value("view");
        QVERIFY(view.present(edited,false,false));QCOMPARE(view.findChild<QWidget*>("WaveEditor"),native);
        QCOMPARE(native->findChild<QWidget*>("waveEditorCanvas"),canvas);QCOMPARE(state().value("view"),beforeView);QVERIFY(native->isEnabled());
        QVERIFY(view.present(edited,false,true));QCOMPARE(state().value("view"),beforeView);
        QCOMPARE(action(native,"undo",message,sizeof message),0);QTRY_COMPARE(lanes().size(),2);
        QCOMPARE(action(native,"redo",message,sizeof message),0);QTRY_COMPARE(lanes().size(),3);
        QCOMPARE(view.findChild<QWidget*>("WaveEditor"),native);
        QCOMPARE(action(native,"add-bus",message,sizeof message),0);
        auto* width=native->findChild<QLineEdit*>("QuickLaneParameterEdit");QVERIFY(width);width->setFocus();width->selectAll();QTest::keyClicks(width,"0");
        QVERIFY(!view.flush(&issue));QVERIFY(!view.present(model()->find(id)->text,false,false));QCOMPARE(state()["mode"].toString(),QString("edit"));
        QCOMPARE(lanes().size(),3);width->selectAll();QTest::keyClicks(width,"16");QVERIFY2(view.flush(&issue),qPrintable(issue));QCOMPARE(lanes().size(),4);
        WaveBlockContent second(model(),id,nullptr);QVERIFY(second.present(model()->find(id)->text,false));
        auto* peer=second.findChild<QWidget*>("WaveEditor");QVERIFY(peer && peer!=native);
        const auto copy=model()->transferTo(model(),id,QTextCursor(editor->document()),true);QVERIFY(!copy.isEmpty() && copy!=id);
        QVERIFY(model()->undoAssociation(false));QVERIFY(!model()->find(copy));
        QVERIFY(model()->undoAssociation(false));QCOMPARE(lanes().size(),3);
        QCOMPARE(second.findChild<QWidget*>("WaveEditor"),peer);QCOMPARE(view.findChild<QWidget*>("WaveEditor"),native);
        QVERIFY(model()->undoAssociation(true));QCOMPARE(lanes().size(),4);
        QWidget reparented;view.setParent(&reparented);QCOMPARE(view.findChild<QWidget*>("WaveEditor"),native);view.setParent(nullptr);
        QVERIFY(view.present(model()->find(id)->text,true));QVERIFY(native->isEnabled());QVERIFY(action(native,"add-bit",message,sizeof message)!=0);
        QVERIFY(view.flush(&issue));
        const auto saved=model()->serialize();QTextDocument restoredDocument;auto* restored=DocumentBlockModel::get(&restoredDocument);
        QVERIFY(restored->restore(saved,&issue));WaveBlockContent reopened(restored,id,nullptr);QVERIFY(reopened.present(restored->find(id)->text,false));
        QCOMPARE(restored->find(id)->text,model()->find(id)->text);QVERIFY(reopened.findChild<QWidget*>("WaveEditor"));
    }
    void tickxStyleUsesHostHistoryAndPreservesViews() {
        const auto payload=WaveBlockContent::initialProject();QVERIFY(!payload.isEmpty());
        const auto id=model()->insert("wave",QTextCursor(editor->document()),payload);
        WaveBlockContent first(model(),id,nullptr),second(model(),id,nullptr);
        first.resize(900,440);first.show();QVERIFY(first.present(payload,false,true));
        QVERIFY(second.present(payload,false));
        auto* native=first.findChild<QWidget*>("WaveEditor");QVERIFY(native);
        QVERIFY(native->property("wavewidgets.capabilities").toStringList().contains("wave-style/v1"));
        auto* entry=native->findChild<QAction*>("waveEditorStyle");QVERIFY(entry&&entry->isEnabled()&&entry->shortcut().isEmpty());
        QLibrary library(QCoreApplication::applicationDirPath()+"/components/tickx/wavewidgets.dll");QVERIFY(library.load());
        using Action=int(*)(QWidget*,const char*,char*,size_t);
        using Read=int(*)(QWidget*,char*,size_t,size_t*,char*,size_t);
        const auto action=reinterpret_cast<Action>(library.resolve("wavewidgets_wave_editor_action_v1"));
        const auto read=reinterpret_cast<Read>(library.resolve("wavewidgets_wave_editor_state_v1"));QVERIFY(action&&read);
        char message[2048]{};
        const auto state=[&](QWidget* widget){size_t size=0;if(read(widget,nullptr,0,&size,message,sizeof message)!=0)return QJsonObject{};
            QByteArray data(qsizetype(size),Qt::Uninitialized);if(read(widget,data.data(),size,&size,message,sizeof message)!=0)return QJsonObject{};
            data.chop(1);return QJsonDocument::fromJson(data).object();};
        const auto choose=[&](const QString& preset,bool accept){
            QCOMPARE(action(native,"wave-style",message,sizeof message),0);
            QTRY_VERIFY(native->findChild<QDialog*>("SignalStyleDefaultsDialog"));
            auto* dialog=native->findChild<QDialog*>("SignalStyleDefaultsDialog");QVERIFY(dialog->isVisible());
            auto* combo=dialog->findChild<QComboBox*>("SignalStylePreset");QVERIFY(combo&&combo->findData(preset)>=0);
            QTest::mouseClick(combo,Qt::LeftButton);QTRY_VERIFY(combo->view()->isVisible());
            QTest::keyClick(combo->view(),Qt::Key_Home);
            for(int i=0;i<combo->findData(preset);++i)QTest::keyClick(combo->view(),Qt::Key_Down);
            QTest::keyClick(combo->view(),Qt::Key_Return);QCOMPARE(combo->currentData().toString(),preset);
            QString issue;QVERIFY(!first.flush(&issue));QVERIFY(!issue.isEmpty());
            auto* buttons=dialog->findChild<QDialogButtonBox*>();QVERIFY(buttons);
            QTest::mouseClick(buttons->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel),Qt::LeftButton);
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QCoreApplication::processEvents();
        };
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        QTRY_VERIFY(state(native).value("view").toObject().value("endTick").toString().toLongLong()>0);
        model()->markSaved();const auto generation=model()->revision();const auto beforeView=state(native).value("view");
        choose("blueprint",false);QCOMPARE(model()->find(id)->text,payload);QCOMPARE(model()->revision(),generation);QVERIFY(!model()->dirty());
        QSignalSpy changes(model(),&DocumentBlockModel::changed);
        choose("blueprint",true);QTRY_COMPARE(changes.size(),1);QVERIFY(model()->dirty());
        const auto blueprint=model()->find(id)->text;QVERIFY(blueprint!=payload);
        auto project=QJsonDocument::fromJson(blueprint.toUtf8()).object();
        QCOMPARE(project["waveWorkbench.signalAppearance"].toObject()["preset"].toString(),QString("blueprint"));
        QCOMPARE(state(native)["view"],beforeView);
        QVERIFY(first.present(blueprint,false));QCOMPARE(state(native)["view"],beforeView);QVERIFY(!entry->isEnabled());
        QVERIFY(action(native,"wave-style",message,sizeof message)!=0);QVERIFY(first.present(blueprint,false,true));
        QVERIFY(model()->undoAssociation(false));QCOMPARE(model()->find(id)->text,payload);
        QVERIFY(model()->undoAssociation(true));QCOMPARE(model()->find(id)->text,blueprint);
        const auto repeatGeneration=model()->revision();choose("blueprint",true);QCOMPARE(model()->revision(),repeatGeneration);
        choose("paper",true);QTRY_VERIFY(model()->find(id)->text!=blueprint);const auto paper=model()->find(id)->text;
        QCOMPARE(state(native)["view"],beforeView);
        QVERIFY(second.present(paper,false));QVERIFY(first.findChild<QWidget*>("WaveEditor")==native);
        const auto copy=model()->transferTo(model(),id,QTextCursor(editor->document()),true);QVERIFY(!copy.isEmpty()&&copy!=id);
        QCOMPARE(model()->find(copy)->text,paper);choose("blueprint",true);QTRY_COMPARE(model()->find(id)->text,blueprint);QCOMPARE(model()->find(copy)->text,paper);
        QTextDocument restoredDocument;restoredDocument.setPlainText(editor->toPlainText());auto* restored=DocumentBlockModel::get(&restoredDocument);
        QString issue;QVERIFY2(restored->restore(model()->serialize(),&issue),qPrintable(issue));
        WaveBlockContent reopened(restored,id,nullptr);QVERIFY(reopened.present(restored->find(id)->text,false));QCOMPARE(restored->find(id)->text,blueprint);
        QVERIFY(first.present(blueprint,true,true));QVERIFY(!entry->isEnabled());QVERIFY(action(native,"wave-style",message,sizeof message)!=0);
        QVERIFY2(first.flush(&issue),qPrintable(issue));QCOMPARE(model()->find(id)->text,blueprint);
    }
    void init() {
        editor = std::make_unique<MyCodeEditor>(); editor->resize(800, 500);
        editor->setPlainText("alpha\nbeta\ngamma\n"); editor->document()->setModified(false);
        editor->show(); editor->setFocus(); QCoreApplication::processEvents();
        const auto expected=qEnvironmentVariable("ZEROSLACK_EXPECTED_DPR");
        if (!expected.isEmpty()) QCOMPARE(editor->devicePixelRatioF(),expected.toDouble());
    }
    void cleanup() { editor.reset(); BlockNoteStyle::instance().apply({}); }
    void independentSourceAndSharedAuthority() {
        const auto source = editor->toPlainText(); const int count = editor->blockCount();
        const auto id = insert(3); QVERIFY(!id.isEmpty()); QVERIFY(model()->dirty());
        QVERIFY(!editor->document()->isModified()); QCOMPARE(editor->toPlainText(), source); QCOMPARE(editor->blockCount(), count);
        auto* shared = editor->document(); MyCodeEditor other; other.attachSharedDocument(shared);
        QCOMPARE(other.blockModel(), model()); model()->editText(id, "other view"); QCOMPARE(other.blockModel()->find(id)->text, "other view");
        auto* detached = new QTextDocument(&other); detached->setDocumentLayout(new QPlainTextDocumentLayout(detached)); other.attachSharedDocument(detached);
    }
    void mixedUndoRedo() {
        QTextCursor cursor(editor->document()); cursor.setPosition(2); editor->setTextCursor(cursor);
        QTest::keyClicks(editor.get(), "1"); const auto withFirst = editor->toPlainText();
        const auto id = insert(2);
        QTest::keyClicks(editor.get(), "2"); const auto withSecond = editor->toPlainText();
        model()->beginTextSession(id); model()->editText(id, "edited"); model()->editText(id, "edited note"); model()->endTextSession();
        QVERIFY(model()->remove(id));
        editor->undo(); QVERIFY(model()->find(id)); QCOMPARE(model()->find(id)->text, "edited note");
        editor->undo(); QCOMPARE(model()->find(id)->text, "note");
        editor->undo(); QCOMPARE(editor->toPlainText(), withFirst); QVERIFY(model()->find(id));
        editor->undo(); QVERIFY(!model()->find(id)); QCOMPARE(editor->toPlainText(), withFirst);
        editor->undo(); QCOMPARE(editor->toPlainText(), "alpha\nbeta\ngamma\n");
        editor->redo(); QCOMPARE(editor->toPlainText(), withFirst);
        editor->redo(); QVERIFY(model()->find(id));
        editor->redo(); QCOMPARE(editor->toPlainText(), withSecond);
        editor->redo(); QCOMPARE(model()->find(id)->text, "edited note");
        editor->redo(); QVERIFY(!model()->find(id));
    }
    void missingAnchorRetainsContentAndUndo() {
        const auto id = insert(1);
        QTextCursor cursor(editor->document()); cursor.setPosition(0); cursor.setPosition(6, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor); QTest::keyClick(editor.get(), Qt::Key_Delete);
        QVERIFY(model()->find(id)->missing); QCOMPARE(model()->find(id)->text, "note");
        editor->undo(); QVERIFY(!model()->find(id)->missing); QCOMPARE(editor->toPlainText(), "alpha\nbeta\ngamma\n");
        cursor = editor->textCursor(); cursor.setPosition(8); QVERIFY(model()->reattach(id, cursor));
        QVERIFY(model()->find(id)->start >= 6);
        editor->undo(); QCOMPARE(model()->find(id)->start, 0);
    }
    void shellFocusAndDelete() {
        const auto id = insert(3); editor->selectAssociatedBlock(id, true);
        auto* text = editor->findChild<QTextEdit*>("editorBlockText"); QVERIFY(text); QVERIFY(text->hasFocus());
        QTest::keyClicks(text, "more"); QVERIFY(model()->find(id)->text.contains("more"));
        const auto before=text->toPlainText();QTest::keyClick(text, Qt::Key_Escape);
        QVERIFY(!text->isReadOnly());QCOMPARE(text->toPlainText(),before);
        auto* shell = editor->findChild<QFrame*>("editorBlockShell"); QVERIFY(shell);
        auto* edit=shell->findChild<QToolButton*>("blockEditToggle");QVERIFY(edit && edit->isChecked());
        edit->click();QVERIFY(text->isReadOnly());QVERIFY(!edit->isChecked());
        editor->selectAssociatedBlock(id);QVERIFY(shell->hasFocus());
        QTest::keyClick(shell,Qt::Key_Return);QVERIFY(text->isReadOnly());
        QTest::keyClick(shell, Qt::Key_Delete); QVERIFY(!model()->find(id));
        QTest::keyClick(editor.get(), Qt::Key_Z, Qt::ControlModifier); QVERIFY(model()->find(id));
    }
    void geometryAndVirtualization_data() {
        QTest::addColumn<int>("blocks"); QTest::newRow("none") << 0; QTest::newRow("ten") << 10; QTest::newRow("hundred") << 100;
    }
    void evictedTextLayoutIsRestored() {
        editor->setPlainText("first_line\nrendered_second_line\nlast_line\n");
        insert(editor->document()->findBlockByNumber(2).position());
        QCoreApplication::processEvents(); editor->viewport()->repaint();
        const auto block = editor->document()->findBlockByNumber(1);
        auto* layout = block.layout(); QVERIFY(layout && layout->lineCount() > 0);
        const auto revision = block.revision();
        const auto before = editor->viewport()->grab().toImage();
        const auto geometry = editor->blockGeometry(1);
        const auto scale = before.devicePixelRatio();
        const QRect row(0, qCeil(geometry.top * scale), before.width(), qFloor(geometry.height * scale));
        // Qt may evict shaped lines without changing the document's text revision.
        layout->clearLayout(); QCOMPARE(block.revision(), revision); QCOMPARE(layout->lineCount(), 0);
        editor->viewport()->repaint();
        QVERIFY2(layout->lineCount() > 0, "Cached row height must not suppress rebuilding evicted text layout");
        const auto after = editor->viewport()->grab().toImage();
        QCOMPARE(after.copy(row), before.copy(row));
    }
    void gutterAndImageUseProjectedSpace() {
        auto palette=editor->palette();
        palette.setColor(QPalette::AlternateBase,QColor(219,228,234));
        editor->setPalette(palette);
        insert(); QCoreApplication::processEvents(); editor->repaint();
        auto* gutter=editor->findChild<QWidget*>("editorLineNumberGutter"); QVERIFY(gutter);
        const auto capture=gutter->grab().toImage(); const auto scale=capture.devicePixelRatio();
        const int from=qCeil((editor->blockGeometry(0).top+editor->blockGeometry(0).height+3)*scale);
        const int to=qFloor((editor->blockGeometry(1).top-3)*scale);
        QVERIFY(to>from+50);
        const auto background=editor->palette().color(QPalette::AlternateBase).rgb();
        bool badgeVisible=false;
        for(int y=from;y<qMin(to,from+qCeil(30*scale));++y) for(int x=qCeil(3*scale);x<qFloor((gutter->width()-20)*scale);++x)
            badgeVisible|=capture.pixel(x,y)!=background;
        QVERIFY2(badgeVisible,"The association gutter must display its type and ordinal");
        for(int y=from+qCeil(60*scale);y<to;++y) for(int x=qCeil(18*scale);x<qFloor((gutter->width()-20)*scale);++x) {
            if (capture.pixel(x,y)!=background) {
                qWarning()<<"gutter pixel"<<QPoint(x,y)<<Qt::hex<<capture.pixel(x,y)<<background
                    <<"gap"<<from<<to<<"gutter"<<gutter->geometry()<<"viewport"<<editor->viewport()->geometry();
                const auto artifacts=qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
                if (!artifacts.isEmpty()) capture.save(artifacts+"/gutter-gap.png");
            }
            QVERIFY2(capture.pixel(x,y)==background,"Line numbers must not appear in association space");
        }
        QTemporaryDir root; QVERIFY(root.isValid()); QImage original(240,120,QImage::Format_RGB32); original.fill(Qt::blue);
        const auto path=root.filePath("preview.png"); QVERIFY(original.save(path)); QString error;
        EditorAssociatedBlock block; block.type="image"; block.height=150; block.width=360;
        block.asset=DocumentBlockStore::get(model())->importImage(path,&error); QVERIFY2(!block.asset.isEmpty(),qPrintable(error));
        const auto id=model()->insert(block,QTextCursor(editor->document())); QVERIFY(!id.isEmpty());
        PinloomPreviewImageLabel* preview=nullptr;
        const auto imageReady=[&] {
            for(auto* label:editor->findChildren<QLabel*>())
                if(auto* image=dynamic_cast<PinloomPreviewImageLabel*>(label)) preview=image;
            return preview && !preview->sourcePixmap().isNull();
        };
        QTRY_VERIFY(imageReady());
        QCoreApplication::processEvents();
        QVERIFY(preview->width()>=200); QVERIFY(preview->height()>=80);
        QVERIFY(preview->pixmap().width()>=100);
    }
    void geometryAndVirtualization() {
        QFETCH(int, blocks);
        QString source; source.reserve(320000); for (int i = 0; i < 20000; ++i) source += "wire value_" + QString::number(i) + ";\n";
        QElapsedTimer timer; timer.start(); editor->setPlainText(source); const auto opening = timer.nsecsElapsed();
        const auto before = editor->viewProjectionMetricsForTest();
        for (int i = 0; i < blocks; ++i) insert(editor->document()->findBlockByNumber(i * 150).position());
        editor->viewport()->update(); QTest::qWait(10);
        const auto metrics = editor->viewProjectionMetricsForTest();
        QCOMPARE(metrics.sourceRowsVisitedDuringRebuild, before.sourceRowsVisitedDuringRebuild);
        QVERIFY(metrics.sourceRowsMeasured < 250); QVERIFY(editor->associatedBlockViewCount() < 10);
        QCOMPARE(editor->toPlainText(), source);
        if (blocks) {
            QVERIFY(editor->blockGeometry(1).top - editor->blockGeometry(0).top > 150);
            const auto block = editor->document()->findBlockByNumber(1); QTextCursor cursor(block);
            const auto rect = editor->cursorRect(cursor);
            QCOMPARE(editor->cursorForPosition(rect.center()).blockNumber(), 1);
        }
        timer.restart(); QTest::keyClicks(editor.get(), "x"); const auto input = timer.nsecsElapsed();
        timer.restart(); editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->maximum() / 2); QTest::qWait(10); const auto scroll = timer.nsecsElapsed();
        qInfo().noquote() << QString("PERF blocks=%1 open_ns=%2 input_ns=%3 scroll_ns=%4 measured_rows=%5 views=%6")
            .arg(blocks).arg(opening).arg(input).arg(scroll).arg(editor->viewProjectionMetricsForTest().sourceRowsMeasured).arg(editor->associatedBlockViewCount());
    }
    void modelPersistenceValidation() {
        const auto id = insert(2); const auto json = model()->serialize();
        MyCodeEditor other; other.setPlainText(editor->toPlainText()); QString error;
        QVERIFY(other.blockModel()->restore(json, &error)); QCOMPARE(other.blockModel()->find(id)->text, "note");
        auto future = json; future["version"] = 9; QVERIFY(!other.blockModel()->restore(future, &error));
        QCOMPARE(other.blockModel()->find(id)->text, "note");
    }
    void formattingMapsAnchorsAndUndo() {
        const QString source="module m;\nlogic a;\nalways_comb begin\na=1;\nend\nendmodule\n";
        editor->setPlainText(source); const auto id=insert(source.indexOf("a=1"));
        const auto before=*model()->find(id); const auto report=editor->formatDocument(); QVERIFY2(report.accepted(),qPrintable(report.diagnostic)); QVERIFY(report.changed);
        const auto formatted=*model()->find(id); QVERIFY(!formatted.missing);
        QCOMPARE(editor->document()->findBlock(formatted.start).text().trimmed(),QString("a=1;"));
        editor->undo(); QCOMPARE(editor->toPlainText(),source); QVERIFY(!model()->find(id)->missing); QCOMPARE(model()->find(id)->start,before.start);
        editor->redo(); QCOMPARE(editor->toPlainText(),report.formattedText); QVERIFY(!model()->find(id)->missing); QCOMPARE(model()->find(id)->start,formatted.start);
    }
    void emptyEndsAndOrderedBlocks() {
        editor->setPlainText({}); const auto empty=insert(); QCoreApplication::processEvents();
        QCOMPARE(editor->blockCount(),1); QCOMPARE(editor->toPlainText(),QString()); QVERIFY(model()->find(empty));
        editor->undo(); QVERIFY(model()->items().isEmpty());
        editor->setPlainText("first\nlast"); const auto first=insert(2); const auto second=insert(3); const auto last=insert(10);
        QCOMPARE(model()->find(first)->end,5); QCOMPARE(model()->find(last)->end,10);
        QVERIFY(model()->find(second)->order>model()->find(first)->order); QVERIFY(model()->reorder(second,-1));
        QVERIFY(model()->find(second)->order<model()->find(first)->order);
        auto value=*model()->find(first); value.collapsed=true; QVERIFY(model()->update(value));
        editor->undo(); QVERIFY(!model()->find(first)->collapsed); editor->redo(); QVERIFY(model()->find(first)->collapsed);
        QCOMPARE(editor->toPlainText(),QString("first\nlast"));
    }
    void wrappedGeometryAndSelectionReturn() {
        QString source="module m;\n"+QString("long_identifier + ").repeated(24)+"0;\nendmodule\n";
        editor->setPlainText(source); editor->setLineWrapMode(QPlainTextEdit::WidgetWidth); editor->resize(360,640);
        QTextCursor selection(editor->document()); selection.setPosition(12); selection.setPosition(50,QTextCursor::KeepAnchor); editor->setTextCursor(selection);
        const auto id=model()->insert("text",selection,"selected range"); editor->selectAssociatedBlock(id,true);
        auto* text=editor->findChild<QTextEdit*>("editorBlockText"); QVERIFY(text);
        auto* shell=editor->findChild<QFrame*>("editorBlockShell");QVERIFY(shell);
        auto* toggle=shell->findChild<QToolButton*>("blockEditToggle");QVERIFY(toggle);toggle->click();
        editor->setFocus();QCOMPARE(editor->textCursor(),selection);
        for (int width : {360,900,360}) {
            editor->resize(width,640); QCoreApplication::processEvents(); editor->viewport()->repaint();
            QTextCursor cursor(editor->document()); cursor.setPosition(45); editor->setTextCursor(cursor); editor->ensureCursorVisible();
            const auto rect=editor->cursorRect(cursor); QCOMPARE(editor->cursorForPosition(rect.center()).position(),cursor.position());
            QVERIFY(editor->blockGeometry(2).top>editor->blockGeometry(1).top+100);
        }
        QCOMPARE(editor->toPlainText(),source);
    }
    void persistedPerformance_data() { geometryAndVirtualization_data(); }
    void automaticNotesAreViewMetrics() {
        const auto code = editor->toPlainText(); const auto codeFont = editor->font();
        const auto id = model()->insert("text", QTextCursor(editor->document()), QString("A readable note with context. ").repeated(12));
        QVERIFY(model()->find(id)->autoHeight); model()->markSaved();
        editor->selectAssociatedBlock(id, true); QCoreApplication::processEvents();
        auto* shell = editor->findChild<QFrame*>("editorBlockShell"); QVERIFY(shell);
        const auto original = model()->serialize(); const auto generation = model()->revision();
        const int wideHeight = shell->height();
        editor->resize(340, 700); QCoreApplication::processEvents();
        QVERIFY(shell->height() > wideHeight);
        const int narrowHeight = shell->height();
        BlockNoteStyle::instance().apply({{"blockNotes.sizePx", 22}, {"blockNotes.lineHeight", 2.2}});
        QCoreApplication::processEvents(); QVERIFY(shell->height() > narrowHeight);
        QCOMPARE(model()->serialize(), original); QCOMPARE(model()->revision(), generation); QVERIFY(!model()->dirty());
        QCOMPARE(editor->font(), codeFont); QCOMPARE(editor->toPlainText(), code); QVERIFY(!editor->document()->isModified());
        auto* text = shell->findChild<QTextEdit*>("editorBlockText"); QVERIFY(text);
        QCOMPARE(text->document()->defaultFont().pixelSize(), 22);
        QVERIFY(text->verticalScrollBar()->maximum() <= 1);
        auto overridden = *model()->find(id); overridden.appearance = {{"style", "Outlined"}, {"background", "#fff4cc"}};
        QVERIFY(model()->update(overridden)); QVERIFY(model()->dirty());
        editor->undo(); QCOMPARE(model()->serialize(), original); QVERIFY(!model()->dirty());
    }
    void legacyHeightAndAppearanceValidation() {
        const auto id = insert(); auto legacy = model()->serialize();
        auto entries = legacy["blocks"].toArray(); auto item = entries[0].toObject();
        item.remove("heightMode"); item.remove("appearance"); item.remove("detached"); entries[0] = item; legacy["blocks"] = entries;
        QString error; QVERIFY(model()->restore(legacy, &error)); QVERIFY(!model()->find(id)->autoHeight);
        QCOMPARE(model()->find(id)->height, 160);
        editor->selectAssociatedBlock(id); QCoreApplication::processEvents();
        auto* shell = editor->findChild<QFrame*>("editorBlockShell"); QVERIFY(shell); QCOMPARE(shell->height(),196);
        BlockNoteStyle::instance().apply({{"blockNotes.sizePx", 32}}); QCoreApplication::processEvents(); QCOMPARE(shell->height(),196);
        item["appearance"] = QJsonObject{{"background", "invalid-color"}}; entries[0] = item; legacy["blocks"] = entries;
        QVERIFY(!model()->restore(legacy, &error)); QVERIFY(model()->find(id)); QCOMPARE(model()->find(id)->height,160);
    }
    void linkOnlyReferencesUseTextGeometryWithoutPreviewErrors() {
        EditorAssociatedBlock link; link.type="reference"; link.autoHeight=true;
        link.uri="https://example.invalid/reference/"+QString("long-path/").repeated(12);
        const auto source=editor->toPlainText();
        const auto id=model()->insert(link,editor->textCursor()); model()->markSaved();
        editor->selectAssociatedBlock(id); QCoreApplication::processEvents();
        auto* shell=editor->findChild<QFrame*>("editorBlockShell"); QVERIFY(shell);
        auto* text=shell->findChild<QTextEdit*>("editorBlockReferenceText"); QVERIFY(text);
        QTRY_VERIFY(text->isVisible()); QVERIFY(text->toPlainText().contains(link.uri));
        QVERIFY(text->toPlainText().contains("Link only")); QVERIFY(text->isReadOnly());
        const auto saved=model()->serialize(); const auto revision=model()->revision();
        const int wide=shell->height();
        editor->resize(340,700); QCoreApplication::processEvents(); QVERIFY(shell->height()>wide);
        QVERIFY(text->verticalScrollBar()->maximum()<=1);
        const int narrow=shell->height();
        BlockNoteStyle::instance().apply({{"blockNotes.sizePx",22},{"blockNotes.lineHeight",2.0}});
        QCoreApplication::processEvents(); QVERIFY(shell->height()>narrow);
        qInfo()<<"LINK_GEOMETRY"<<editor->devicePixelRatioF()<<shell->size()<<text->size()<<text->viewport()->size()
               <<text->document()->size()<<text->verticalScrollBar()->maximum()<<text->document()->defaultFont();
        QVERIFY(text->verticalScrollBar()->maximum()<=1);
        QVERIFY(text->document()->size().height()<=text->viewport()->height()+1);
        QCOMPARE(model()->serialize(),saved); QCOMPARE(model()->revision(),revision); QVERIFY(!model()->dirty());
        QCOMPARE(editor->toPlainText(),source); QVERIFY(!editor->document()->isModified());
    }
    void foldedAnchorsKeepKeyboardOrder() {
        const QString source="module m;\ninitial begin\n    a = 1;\n    b = 2;\nend\nendmodule\n";
        editor->setPlainText(source); const auto first=insert(source.indexOf("a =")); const auto second=insert(source.indexOf("b ="));
        QTRY_VERIFY(!editor->syntaxDocument()->hasPendingEdits());
        QVERIFY(editor->toggleFoldAtLineForTest(1)); QCoreApplication::processEvents();
        QVERIFY(!editor->foldLineVisibleForTest(2)); QVERIFY(!model()->find(first)->missing);
        QTextCursor cursor(editor->document()); cursor.setPosition(source.indexOf("initial")); editor->setTextCursor(cursor); editor->setFocus();
        QTest::keyClick(editor.get(),Qt::Key_Down); auto* shell=qobject_cast<QFrame*>(QApplication::focusWidget()); QVERIFY(shell); QCOMPARE(shell->property("blockId").toString(),first);
        QTest::keyClick(shell,Qt::Key_Down); shell=qobject_cast<QFrame*>(QApplication::focusWidget()); QVERIFY(shell); QCOMPARE(shell->property("blockId").toString(),second);
        QTest::keyClick(shell,Qt::Key_Down); QVERIFY(editor->hasFocus()); QVERIFY(editor->textCursor().blockNumber()>=4);
        QTest::keyClick(editor.get(),Qt::Key_Up); shell=qobject_cast<QFrame*>(QApplication::focusWidget()); QVERIFY(shell); QCOMPARE(shell->property("blockId").toString(),second);
        QVERIFY(editor->toggleFoldAtLineForTest(1)); QCOMPARE(editor->toPlainText(),source);
    }
    void persistedPerformance() {
        QFETCH(int,blocks); QTemporaryDir root; QVERIFY(root.isValid()); const auto path=root.filePath("large.sv");
        QString source="module large;\n"; for(int i=0;i<20000;++i) source+="wire value_"+QString::number(i)+";\n"; source+="endmodule\n";
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(source.toUtf8()),source.toUtf8().size()); file.close();
        editor->hide(); editor->setPlainText(source); auto* store=DocumentBlockStore::get(model()); QString error; QVERIFY(store->bind(path,root.path(),&error));
        for(int i=0;i<blocks;++i) insert(editor->document()->findBlockByNumber(i*150).position());
        const auto hash=DocumentBlockStore::fileRevision(path); if(blocks) QVERIFY2(store->save(path,root.path(),hash,hash,false,{}, {},&error),qPrintable(error));
        QTabWidget tabs; tabs.resize(800,500); tabs.show(); TabManager manager(&tabs); manager.setWorkspaceScope({root.path()},root.path());
        QElapsedTimer timer; timer.start(); QVERIFY(manager.openFileInTab(path)); QCoreApplication::processEvents();
        auto* page=manager.getCurrentEditor(); QVERIFY(page); page->viewport()->repaint(); const auto opening=timer.nsecsElapsed();
        const auto startMetrics=page->viewProjectionMetricsForTest();
        QCOMPARE(page->blockModel()->items().size(),blocks); page->setFocus();
        timer.restart(); QTest::keyClicks(page,"x"); QCoreApplication::processEvents(); page->viewport()->repaint(); const auto input=timer.nsecsElapsed();
        timer.restart(); page->verticalScrollBar()->setValue(page->verticalScrollBar()->maximum()/2); QCoreApplication::processEvents(); page->viewport()->repaint(); const auto scroll=timer.nsecsElapsed();
        const auto metrics=page->viewProjectionMetricsForTest(); QCOMPARE(metrics.sourceRowsVisitedDuringRebuild,quint64(0)); QVERIFY(metrics.sourceRowsMeasured<250); QVERIFY(page->associatedBlockViewCount()<10);
        qInfo().noquote()<<QString("PERSISTED_PERF blocks=%1 open_ns=%2 input_ns=%3 scroll_ns=%4 rebuilds=%5 measured_rows=%6 views=%7 first_measured=%8")
            .arg(blocks).arg(opening).arg(input).arg(scroll).arg(metrics.rebuilds).arg(metrics.sourceRowsMeasured).arg(page->associatedBlockViewCount()).arg(startMetrics.sourceRowsMeasured);
        manager.unsavedDocumentManagerForTesting()->setDecisionProvider([](const auto&,QWidget*){return UnsavedDocumentBatchDecision::DiscardAll;}); QVERIFY(manager.closeAllTabs());
    }
};
QTEST_MAIN(EditorBlockTest)
#include "editor_block_test.moc"
