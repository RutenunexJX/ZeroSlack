#include "associatedblockpage.h"
#include "shareddocument.h"
#include "documentblockmodel.h"
#include "documentblockstore.h"
#include "editorblockpreview.h"
#include "editorblockcontent.h"
#include "blocknotestyle.h"
#include "pinloompreviewimage.h"
#include "uicontrols.h"
#include "uitypography.h"
#include "roundedicons.h"
#include "insightvisualstyle.h"
#include <QPainter>
#include <QCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QMenu>
#include <QKeyEvent>
#include <QFileInfo>
#include <QUuid>
#include <QSignalBlocker>
#include <QApplication>
#include <QMouseEvent>
#include <QTimer>
#include "documentreferenceservice.h"

AssociatedBlockPage::AssociatedBlockPage(SharedDocument* owner, const QString& blockId, QWidget* parent, const QString& viewId, const EditorAssociatedBlock* prepared,EditorBlockContent* retained)
    : QWidget(parent), document(owner), id(blockId), view(viewId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : viewId) {
    setObjectName("associatedBlockPage"); setProperty("editorViewId", view);
    setAttribute(Qt::WA_StyledBackground);
    setProperty("blockId", id); setMouseTracking(true);
    setMinimumSize(120, 80); setFocusPolicy(Qt::StrongFocus);
    owner->retainConsumer(this);
    const auto* initial=model()->find(id); if (!initial) initial=prepared;
    client = new PinloomHostClient(this);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(32, 36, 16, 12); layout->setSpacing(8);
    auto* row = new QHBoxLayout; row->setSpacing(2);
    origin = UiControls::label(this); origin->setMinimumWidth(0); origin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    origin->setTextFormat(Qt::PlainText); UiTypography::apply(origin, UiTypography::Role::Metadata); row->addWidget(origin, 1);
    tools=new QWidget(this); tools->setObjectName("associatedBlockTools");
    auto* toolRow=new QHBoxLayout(tools); toolRow->setContentsMargins(2,2,2,2); toolRow->setSpacing(2);
    const auto tool = [&](RoundedIcons::Kind icon, const QString& label) {
        auto* button = UiControls::toolButton(tools); button->setIcon(RoundedIcons::icon(icon)); button->setFixedSize(28,28);
        button->setIconSize({16,16}); button->setAutoRaise(true); button->setToolTip(label); button->setAccessibleName(label); toolRow->addWidget(button); return button;
    };
    auto* source = tool(RoundedIcons::SourceFile, "Open owning code document");
    connect(source, &QToolButton::clicked, this, &AssociatedBlockPage::sourceRequested);
    grip = UiControls::toolButton(this);grip->setIcon(RoundedIcons::icon(RoundedIcons::Grip));grip->setIconSize({16,16});grip->setFixedSize(26,26);
    grip->setToolTip("Select or drag block");grip->setAccessibleName("Select or drag block");
    connect(grip,&QToolButton::clicked,this,[this]{setProperty("blockSelected",true);setFocus();update();});
    edit=UiControls::toolButton(this);edit->setObjectName("blockEditToggle");edit->setCheckable(true);
    edit->setIcon(RoundedIcons::icon(RoundedIcons::BlockEdit));edit->setIconSize({16,16});edit->setFixedSize(26,26);
    edit->setToolTip("Edit block");edit->setAccessibleName("Edit block");
    connect(edit,&QToolButton::clicked,this,[this](bool editing){if(content)content->setEditing(editing);edit->setChecked(content && content->isEditing());});
    edit->hide();
    grip->setObjectName("associatedBlockDragHandle"); grip->setCursor(Qt::OpenHandCursor);
    auto* more = tool(RoundedIcons::More, "Block actions"); auto* menu = UiControls::menu(more);
    more->setMenu(menu); more->setPopupMode(QToolButton::InstantPopup);
    connect(menu->addAction("Copy block"),&QAction::triggered,this,[this] { emit clipboardRequested(false); });
    connect(menu->addAction("Cut block"),&QAction::triggered,this,[this] { emit clipboardRequested(true); });
    EditorBlockContent::addAppearanceMenu(menu,model(),id,this,[this] { return !document || document->readOnly(); });
    DocumentReferenceService::addMenu(menu,model(),id,this,[this] { return !document || document->readOnly(); });
    if (initial && (initial->type=="image" || initial->type=="reference")) {
        auto* original = menu->addAction("Open original");
        if (initial->type=="reference") EditorBlockPreview::bindOriginalAction(original,menu,model(),client,id);
        connect(original, &QAction::triggered, this, &AssociatedBlockPage::openOriginal);
    }
    remove = tool(RoundedIcons::Delete, "Delete block (undoable)");
    connect(remove, &QToolButton::clicked, this, [this] { if (document && !document->readOnly() && finishEditing()) model()->remove(id); });
    layout->addLayout(row);
    content = retained ? retained : new EditorBlockContent(model(), client, *initial, this);
    if(retained)retained->setParent(this);
    content->openOriginal = [this] { openOriginal(); };
    text = content->textEditor(); layout->addWidget(content, 1);
    description = UiControls::label(this); description->setWordWrap(true); description->setTextFormat(Qt::PlainText);
    description->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(description);
    tools->hide(); qApp->installEventFilter(this);
    connect(model(), &DocumentBlockModel::changed, this, &AssociatedBlockPage::refresh);
    connect(model(), &DocumentBlockModel::presentationChanged, this, &AssociatedBlockPage::refresh);
    connect(model(), &DocumentBlockModel::errorChanged, this, [this](const QString& issue) {
        if (!issue.isEmpty()) { description->setText(issue); description->show(); }
    });
    connect(owner, &SharedDocument::statusChanged, this, &AssociatedBlockPage::refresh);
    connect(owner, &SharedDocument::identityChanged, this, &AssociatedBlockPage::refresh);
    connect(&BlockNoteStyle::instance(), &BlockNoteStyle::changed, this, &AssociatedBlockPage::refresh);
    refresh();
}
AssociatedBlockPage::~AssociatedBlockPage() { qApp->removeEventFilter(this); finishEditing(); if (document) document->releaseConsumer(this); }
SharedDocument* AssociatedBlockPage::ownerDocument() const { return document; }
DocumentBlockModel* AssociatedBlockPage::model() const { return document ? DocumentBlockModel::get(document->textDocument()) : nullptr; }
QString AssociatedBlockPage::title() const {
    const auto presented=model()?model()->presentation(id):EditorAssociatedBlock{};
    const auto* block=presented.id.isEmpty() || presented.removedReference()?nullptr:&presented;
    if (!block) return "Removed block";
    if (!block->caption.trimmed().isEmpty()) return block->caption;
    if (block->type == "text") return block->text.section('\n',0,0).trimmed().left(80).isEmpty() ? "Note" : block->text.section('\n',0,0).trimmed().left(80);
    return block->type=="image" ? "Image" : block->type=="wave" ? "Wave" : block->type=="code" ? "Code" : block->type=="attachment" ? "Attachment" : "Reference";
}
bool AssociatedBlockPage::finishEditing() {
    if(content && !content->setEditing(false))return false;
    if(model())model()->endTextSession();if(edit)edit->setChecked(false);return true;
}
EditorBlockContent* AssociatedBlockPage::takeContent() {
    auto* value=content;if(!value)return nullptr;
    layout()->removeWidget(value);content=nullptr;text=nullptr;value->openOriginal={};return value;
}
void AssociatedBlockPage::adoptContent(EditorBlockContent* value) {
    if(!value)return;delete content;content=value;text=content->textEditor();
    content->setParent(this);static_cast<QVBoxLayout*>(layout())->insertWidget(1,content,1);
    content->openOriginal=[this]{openOriginal();};refresh();
}
void AssociatedBlockPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if(edit){edit->move(32,4);edit->raise();}
    if(grip){grip->move(2,40);grip->raise();}
    if (tools) { tools->adjustSize(); tools->move(qMax(4,width()-tools->width()-4),4); tools->raise(); }
}
void AssociatedBlockPage::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    if(property("blockSelected").toBool()) {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(InsightVisualStyle::theme().accent,1));painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1),5,5);
    }
}
void AssociatedBlockPage::refreshControls() {
    EditorBlockContent::refreshTools(this,tools);
    const auto* focus=QApplication::focusWidget();
    grip->setVisible(rect().contains(mapFromGlobal(QCursor::pos())) || focus==this
        || (focus && isAncestorOf(focus)) || property("blockSelected").toBool());
}
void AssociatedBlockPage::replay(bool redo) {
    if (!document || document->readOnly()) return;
    if(!finishEditing())return;
    if (!model()->undoAssociation(redo)) {
        model()->beginTextReplay();
        if (redo) document->textDocument()->redo(); else document->textDocument()->undo();
        model()->endTextReplay();
    }
}
bool AssociatedBlockPage::eventFilter(QObject* object, QEvent* event) {
    auto* widget=qobject_cast<QWidget*>(object);
    if (!content || !widget || (widget!=this && !isAncestorOf(widget))) return false;
    if (event->type()==QEvent::MouseMove || event->type()==QEvent::Enter || event->type()==QEvent::Leave
        || event->type()==QEvent::FocusIn || event->type()==QEvent::FocusOut)
        QTimer::singleShot(0,this,[this] { refreshControls(); });
    if(event->type()==QEvent::FocusIn && widget!=this && widget!=grip) {setProperty("blockSelected",false);update();}
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (event->type()==QEvent::KeyPress) {
            if (!content->isEditing() && (key->key()==Qt::Key_Tab || key->key()==Qt::Key_Backtab)) {
                tools->show(); edit->show(); auto controls=tools->findChildren<QToolButton*>();controls.prepend(edit);controls.prepend(grip);
                if (!controls.isEmpty()) {
                    int index=controls.indexOf(qobject_cast<QToolButton*>(widget));
                    const bool back=key->key()==Qt::Key_Backtab || key->modifiers().testFlag(Qt::ShiftModifier);
                    controls[(index+(back ? -1 : 1)+controls.size())%controls.size()]->setFocus();
                }
                return true;
            }
        }
        const bool undo=key->matches(QKeySequence::Undo), redo=key->matches(QKeySequence::Redo);
        if ((undo || redo) && content->nativeWaveEditing()) return false;
        const bool save=key->matches(QKeySequence::Save), saveAs=key->matches(QKeySequence::SaveAs);
        if (undo || redo || save || saveAs) {
            key->accept();
            if (event->type() == QEvent::KeyPress) { if (undo || redo) replay(redo); else emit saveRequested(saveAs); }
            return true;
        }
    }
    return QWidget::eventFilter(object,event);
}
void AssociatedBlockPage::refresh() {
    if (!document || updating) return;
    updating=true;
    const auto presented=model()->presentation(id);
    const auto* block=presented.id.isEmpty() || presented.removedReference()?nullptr:&presented;
    origin->setText(document->fileName().isEmpty() ? "Untitled code · Block" : QFileInfo(document->fileName()).fileName()+" · Block");
    origin->setToolTip(document->fileName());
    grip->setEnabled(block && !document->readOnly()); remove->setEnabled(block && !document->readOnly());
    edit->setEnabled(block && !document->readOnly());edit->setChecked(content && content->isEditing());
    if(!content){updating=false;return;}
    content->setVisible(block != nullptr);
    if (!block) {
        description->show(); description->setText("This block was removed. Undo restores it.");
    } else {
        const auto appearance=BlockNoteStyle::instance().appearance(block->appearance);
        setStyleSheet(QString("QWidget#associatedBlockPage {background:%1; border:1px solid %2;}")
            .arg(appearance.background.name(QColor::HexArgb),appearance.border.name(QColor::HexArgb)));
        description->setVisible(block->compactReference());
        if(block->compactReference())description->setText((block->caption.isEmpty()?block->uri:block->caption)+"\n"+block->referenceStatus);
        auto inPage=*block;inPage.autoHeight=false;
        content->present(inPage, document->readOnly(),block->compactReference());
    }
    updating=false;refreshControls();emit statusChanged();
}
void AssociatedBlockPage::openOriginal() {
    const auto* block=model() ? model()->find(id) : nullptr; if (!block) return;
    if (block->type=="image" && content) {
        auto* image=content->imagePreview();
        if (image && !image->sourcePixmap().isNull()) openPinloomImagePreview(this,image->sourcePixmap(),"Image",block->caption);
        else { description->setText("The image is unavailable or still loading."); description->show(); }
        return;
    }
    const QPointer<AssociatedBlockPage> self(this);
    EditorBlockPreview::openReference(model(),client,id,this,[self](bool ok,const QString& error) {
        if (self && self->model() && self->model()->find(self->id) && !ok) { self->description->setText(error); self->description->show(); }
    });
}
