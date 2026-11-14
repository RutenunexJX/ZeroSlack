#include "editordiagnosticoverview.h"
#include "editorruntime.h"
#include "mycodeeditor.h"
#include "insightvisualstyle.h"
#include "uicontrols.h"
#include <QScrollBar>
#include <QStyleOptionSlider>
#include <QPainter>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QTimer>
#include <QMenu>
#include <QTextBlock>
#include <QTextLayout>
#include <algorithm>

EditorDiagnosticOverview::EditorDiagnosticOverview(MyCodeEditor* owner,MyCodeEditorState* value)
    :QWidget(owner->verticalScrollBar()),editor(owner),bar(owner->verticalScrollBar()),state(value),previousPolicy(owner->verticalScrollBarPolicy()) {
    setObjectName("editorDiagnosticOverview");setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);setFocusPolicy(Qt::NoFocus);
    bar->setMouseTracking(true);bar->installEventFilter(this);editor->installEventFilter(this);editor->viewport()->installEventFilter(this);
    connect(bar,&QScrollBar::rangeChanged,this,[this]{schedule();});
    connect(editor,&QPlainTextEdit::textChanged,this,[this]{schedule();});
    connect(editor,&QPlainTextEdit::updateRequest,this,[this]{schedule();});schedule();
}
EditorDiagnosticOverview::~EditorDiagnosticOverview(){
    if(editor && forcingBar)editor->setVerticalScrollBarPolicy(previousPolicy);
}
void EditorDiagnosticOverview::schedule(){
    if(pending)return;pending=true;QTimer::singleShot(0,this,[this]{pending=false;rebuild();});
}
QRect EditorDiagnosticOverview::track(bool slider) const {
    if(!bar)return {};
    QStyleOptionSlider option;option.initFrom(bar);option.rect=bar->rect();option.orientation=Qt::Vertical;
    option.minimum=bar->minimum();option.maximum=bar->maximum();option.pageStep=bar->pageStep();option.singleStep=bar->singleStep();
    option.sliderPosition=bar->sliderPosition();option.sliderValue=bar->value();option.upsideDown=bar->invertedAppearance();
    return bar->style()->subControlRect(QStyle::CC_ScrollBar,&option,slider?QStyle::SC_ScrollBarSlider:QStyle::SC_ScrollBarGroove,bar);
}
void EditorDiagnosticOverview::rebuild(){
    if(!editor || !bar)return;
    marks.clear();const bool visible=state->annotationDisplayOptions.enabled && !state->diagnosticSeverityByLine.isEmpty();
    if(visible && !forcingBar){previousPolicy=editor->verticalScrollBarPolicy();forcingBar=true;editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);}
    else if(!visible && forcingBar){forcingBar=false;editor->setVerticalScrollBarPolicy(previousPolicy);}
    setGeometry(bar->rect());setVisible(visible);raise();if(!visible){update();return;}
    if(bar->maximum()==bar->minimum())bar->setEnabled(true);
    state->projection.ensure(editor,state->folding.collapsedLineRanges());
    const auto groove=track();if(groove.isEmpty())return;
    const int markerWidth=qBound(2,groove.width()/3,6),markerHeight=qMax(3,qRound(editor->fontMetrics().height()/5.0));
    QMap<int,QList<int>> byPixel;
    for(auto it=state->diagnosticIndexesByLine.cbegin();it!=state->diagnosticIndexesByLine.cend();++it){
        for(int index:it.value()){
            if(index<0 || index>=state->diagnostics.size())continue;
            const auto& diagnostic=state->diagnostics[index];if(diagnostic.severity==SemanticDiagnostic::Info)continue;
            const auto fraction=state->projection.sourceScrollFraction(editor,it.key(),qMax(0,diagnostic.column-1));
            const int y=groove.top()+qRound(fraction*qMax(0,groove.height()-markerHeight));byPixel[y].append(index);
        }
    }
    for(auto it=byPixel.cbegin();it!=byPixel.cend();++it){
        bool error=false;
        for(int index:it.value())error|=state->diagnostics[index].severity==SemanticDiagnostic::Error;
        marks.append({QRect(groove.left(),it.key(),markerWidth,markerHeight),it.value(),error});
    }
    update();
}
void EditorDiagnosticOverview::paintEvent(QPaintEvent*){
    QPainter painter(this);const auto& theme=InsightVisualStyle::theme();
    for(const auto& mark:marks){
        painter.fillRect(mark.rect,mark.error?theme.syntax.errorUnderline:theme.warning);++state->hotPathMetrics.diagnosticOverviewCandidatesPainted;
    }
}
QList<int> EditorDiagnosticOverview::hit(const QPoint& point) const {
    QList<int> result;for(const auto& mark:marks)if(mark.rect.adjusted(-1,-3,2,3).contains(point))for(int index:mark.diagnostics)
        if(!result.contains(index) && index>=0 && index<state->diagnostics.size())result.append(index);
    std::sort(result.begin(),result.end(),[this](int a,int b){
        const auto& left=state->diagnostics[a];const auto& right=state->diagnostics[b];
        if(left.line!=right.line)return left.line<right.line;if(left.column!=right.column)return left.column<right.column;
        return left.message<right.message;
    });return result;
}
void EditorDiagnosticOverview::activate(int index){
    if(!editor || index<0 || index>=state->diagnostics.size())return;
    const auto diagnostic=state->diagnostics[index];const auto block=editor->document()->findBlockByNumber(qMax(0,diagnostic.line-1));
    if(!block.isValid())return;QTextCursor cursor(block);cursor.setPosition(block.position()+qBound(0,diagnostic.column-1,block.length()-1));
    editor->setTextCursor(cursor);editor->centerCursor();editor->setFocus(Qt::MouseFocusReason);
}
bool EditorDiagnosticOverview::eventFilter(QObject* object,QEvent* event){
    if(event->type()==QEvent::Resize || event->type()==QEvent::Show || event->type()==QEvent::FontChange
        || event->type()==QEvent::StyleChange || event->type()==QEvent::DevicePixelRatioChange)schedule();
    if(object!=bar || !isVisible())return false;
    if(event->type()==QEvent::ToolTip){
        auto* help=static_cast<QHelpEvent*>(event);const auto indexes=hit(help->pos());if(indexes.isEmpty())return false;
        QStringList text;for(int index:indexes){const auto& diagnostic=state->diagnostics[index];
            text<<QString("%1:%2 · %3").arg(diagnostic.line).arg(diagnostic.column).arg(diagnostic.message);if(text.size()==5)break;}
        if(indexes.size()>text.size())text<<tr("%1 more diagnostics; click to choose").arg(indexes.size()-text.size());
        QToolTip::showText(help->globalPos(),text.join('\n'),bar);return true;
    }
    if(event->type()==QEvent::MouseButtonRelease && consumeRelease){consumeRelease=false;return true;}
    if(event->type()!=QEvent::MouseButtonPress)return false;
    auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)return false;
    const auto point=mouse->position().toPoint();
    // Preserve the native slider's grab even when a diagnostic crosses it.
    if(bar->maximum()>bar->minimum() && track(true).contains(point))return false;
    const auto indexes=hit(point);if(indexes.isEmpty())return false;consumeRelease=true;
    if(indexes.size()==1){activate(indexes.first());return true;}
    auto* menu=UiControls::menu(editor);menu->setAttribute(Qt::WA_DeleteOnClose);
    const QPointer<QTextDocument> document=editor->document();const auto revision=editor->semanticDocumentRevision();
    for(int index:indexes){const auto diagnostic=state->diagnostics[index];
        auto* action=menu->addAction(QString("%1:%2 · %3").arg(diagnostic.line).arg(diagnostic.column).arg(diagnostic.message));
        connect(action,&QAction::triggered,this,[this,diagnostic,document,revision]{
            if(!editor || editor->document()!=document || editor->semanticDocumentRevision()!=revision)return;
            for(int current=0;current<state->diagnostics.size();++current){
                const auto& value=state->diagnostics[current];
                if(value.line==diagnostic.line && value.column==diagnostic.column && value.message==diagnostic.message && value.severity==diagnostic.severity){activate(current);break;}
            }
        });}
    menu->popup(mouse->globalPosition().toPoint());return true;
}
