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

AssociatedBlockPage::AssociatedBlockPage(SharedDocument* owner, const QString& blockId, QWidget* parent, const QString& viewId)
    : QWidget(parent), document(owner), id(blockId), view(viewId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : viewId) {
    setObjectName("associatedBlockPage"); setProperty("editorViewId", view);
    setMinimumSize(120, 80); setFocusPolicy(Qt::StrongFocus);
    owner->retainConsumer(this);
    client = new PinloomHostClient(this);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(16, 12, 16, 12); layout->setSpacing(8);
    auto* row = new QHBoxLayout; row->setSpacing(2);
    origin = UiControls::label(this); origin->setMinimumWidth(0); origin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    origin->setTextFormat(Qt::PlainText); UiTypography::apply(origin, UiTypography::Role::Metadata); row->addWidget(origin, 1);
    const auto tool = [&](RoundedIcons::Kind icon, const QString& label) {
        auto* button = UiControls::toolButton(this); button->setIcon(RoundedIcons::icon(icon)); button->setFixedSize(28,28);
        button->setIconSize({16,16}); button->setAutoRaise(true); button->setToolTip(label); button->setAccessibleName(label); row->addWidget(button); return button;
    };
    auto* source = tool(RoundedIcons::File, "Open owning code document");
    connect(source, &QToolButton::clicked, this, &AssociatedBlockPage::sourceRequested);
    grip = tool(RoundedIcons::Grip, "Drag to a tab group or the owner's line numbers");
    grip->setObjectName("associatedBlockDragHandle"); grip->setCursor(Qt::OpenHandCursor);
    auto* more = tool(RoundedIcons::More, "Block actions"); auto* menu = UiControls::menu(more);
    more->setMenu(menu); more->setPopupMode(QToolButton::InstantPopup);
    EditorBlockContent::addAppearanceMenu(menu,model(),id,this,[this] { return !document || document->readOnly(); });
    if (model()->find(id)->type!="text") {
        auto* original = menu->addAction("Open original");
        if (model()->find(id)->type=="reference") EditorBlockPreview::bindOriginalAction(original,menu,model(),client,id);
        connect(original, &QAction::triggered, this, &AssociatedBlockPage::openOriginal);
    }
    remove = tool(RoundedIcons::Close, "Delete block (undoable)");
    connect(remove, &QToolButton::clicked, this, [this] { if (document && !document->readOnly()) { finishEditing(); model()->remove(id); } });
    layout->addLayout(row);
    content = new EditorBlockContent(model(), client, *model()->find(id), this);
    content->openOriginal = [this] { openOriginal(); };
    text = content->textEditor(); layout->addWidget(content, 1); setFocusProxy(content);
    description = UiControls::label(this); description->setWordWrap(true); description->setTextFormat(Qt::PlainText);
    description->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(description);
    for (auto* widget : findChildren<QWidget*>()) widget->installEventFilter(this);
    installEventFilter(this);
    connect(model(), &DocumentBlockModel::changed, this, &AssociatedBlockPage::refresh);
    connect(owner, &SharedDocument::statusChanged, this, &AssociatedBlockPage::refresh);
    connect(owner, &SharedDocument::identityChanged, this, &AssociatedBlockPage::refresh);
    connect(&BlockNoteStyle::instance(), &BlockNoteStyle::changed, this, &AssociatedBlockPage::refresh);
    refresh();
}
AssociatedBlockPage::~AssociatedBlockPage() { finishEditing(); if (document) document->releaseConsumer(this); }
SharedDocument* AssociatedBlockPage::ownerDocument() const { return document; }
DocumentBlockModel* AssociatedBlockPage::model() const { return document ? DocumentBlockModel::get(document->textDocument()) : nullptr; }
QString AssociatedBlockPage::title() const {
    const auto* block = model() ? model()->find(id) : nullptr;
    if (!block) return "Removed block";
    if (!block->caption.trimmed().isEmpty()) return block->caption;
    if (block->type == "text") return block->text.section('\n',0,0).trimmed().left(80).isEmpty() ? "Note" : block->text.section('\n',0,0).trimmed().left(80);
    return block->type == "image" ? "Image" : "Reference";
}
void AssociatedBlockPage::finishEditing() { if (model()) model()->endTextSession(); }
void AssociatedBlockPage::replay(bool redo) {
    if (!document || document->readOnly()) return;
    finishEditing();
    if (!model()->undoAssociation(redo)) {
        model()->beginTextReplay();
        if (redo) document->textDocument()->redo(); else document->textDocument()->undo();
        model()->endTextReplay();
    }
}
bool AssociatedBlockPage::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::FocusOut && object == text) finishEditing();
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        const bool undo=key->matches(QKeySequence::Undo), redo=key->matches(QKeySequence::Redo);
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
    const auto* block=model()->find(id);
    origin->setText(document->fileName().isEmpty() ? "Untitled code · Block" : QFileInfo(document->fileName()).fileName()+" · Block");
    origin->setToolTip(document->fileName());
    grip->setEnabled(block && !document->readOnly()); remove->setEnabled(block && !document->readOnly());
    content->setVisible(block != nullptr);
    if (!block) {
        description->show(); description->setText("This block was removed. Undo restores it.");
    } else {
        const auto appearance=BlockNoteStyle::instance().appearance(block->appearance);
        setStyleSheet(QString("QWidget#associatedBlockPage {background:%1; border:1px solid %2;}")
            .arg(appearance.background.name(QColor::HexArgb),appearance.border.name(QColor::HexArgb)));
        description->hide(); content->present(*block, document->readOnly());
    }
    updating=false; emit statusChanged();
}
void AssociatedBlockPage::openOriginal() {
    const auto* block=model() ? model()->find(id) : nullptr; if (!block) return;
    if (block->type=="image") {
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
