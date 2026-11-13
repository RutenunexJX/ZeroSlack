#include "editorblockcontent.h"
#include "editorblockpreview.h"
#include "pinloompreviewimage.h"
#include "blocknotestyle.h"
#include "waveblockcontent.h"
#include "documentblockstore.h"
#include "uicontrols.h"
#include "uitypography.h"
#include <QTextEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QApplication>
#include <QCursor>
#include <QFileDialog>
#include <QSaveFile>

void EditorBlockContent::refreshTools(QWidget* frame, QWidget* tools) {
    if (!frame || !tools) return;
    const auto point=frame->mapFromGlobal(QCursor::pos());
    const auto* focus=QApplication::focusWidget();
    tools->setVisible((frame->rect().adjusted(-4,-4,4,4).contains(point) && point.y()<46
        && point.x()>frame->width()-tools->sizeHint().width()-28)
        || (focus && (focus==tools || tools->isAncestorOf(focus)))
        || (QApplication::activePopupWidget() && frame->isAncestorOf(QApplication::activePopupWidget())));
    tools->raise();
}

void EditorBlockContent::addAppearanceMenu(QMenu* menu, DocumentBlockModel* value, const QString& id,
    QWidget* host, std::function<bool()> readOnly) {
    const QPointer<DocumentBlockModel> model(value); const QPointer<QWidget> guard(host);
    auto* exportAttachment=menu->addAction("Export attachment…"); exportAttachment->setVisible(false);
    connect(menu,&QMenu::aboutToShow,host,[model,id,exportAttachment] {
        const auto* block=model ? model->find(id) : nullptr; exportAttachment->setVisible(block && block->type=="attachment");
    });
    connect(exportAttachment,&QAction::triggered,host,[model,id,guard] {
        const auto* block=model ? model->find(id) : nullptr; if(!block || !guard) return;
        const auto asset=block->asset;
        const auto path=QFileDialog::getSaveFileName(guard,"Export attachment",block->caption);
        if(path.isEmpty() || !model || !model->find(id) || model->find(id)->asset!=asset) return;
        QString issue; const auto bytes=DocumentBlockStore::get(model)->assetBytes(asset,&issue);
        if(!issue.isEmpty()) { model->setError(issue); return; }
        QSaveFile file(path); if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) model->setError(file.errorString());
    });
    auto* appearance=menu->addMenu("Appearance");
    const auto change=[model,id,guard,readOnly](const QString& key, const QString& value) {
        if (!guard || !model || readOnly()) return;
        if (const auto* block=model->find(id)) {
            auto next=*block;
            if (key.isEmpty() || !BlockNoteStyle::validPreset(next.appearance)) next.appearance=BlockNoteStyle::defaults(next.type);
            if (!key.isEmpty()) next.appearance.insert(key,value);
            if (key=="preset") {
                const auto defaults=BlockNoteStyle::defaults(value=="Paper" ? "text" : value=="Plain" ? "image" : value=="Technical" ? "code" : "reference");
                next.appearance=defaults;
            }
            model->endTextSession(); model->update(next);
        }
    };
    const auto add=[&](QMenu* parent,const QStringList& names,const QString& key) {
        for (const auto& name:names) {
            auto values=BlockNoteStyle::defaults("text");
            if (const auto* block=model->find(id); block && BlockNoteStyle::validPreset(block->appearance)) values=block->appearance;
            if(key=="preset") values=BlockNoteStyle::defaults(name=="Paper" ? "text" : name=="Plain" ? "image" : name=="Technical" ? "code" : "reference");
            else values[key]=name;
            const auto style=BlockNoteStyle::instance().appearance(values);
            QPixmap swatch(40,24); swatch.fill(Qt::transparent);
            QPainter painter(&swatch); painter.setPen(style.border.alpha() ? style.border : style.text); painter.setBrush(style.background);
            painter.drawRoundedRect(QRectF(1,1,38,22),3,3); painter.setPen(style.text); painter.setFont(style.font); painter.drawText(swatch.rect(),Qt::AlignCenter,"Aa"); painter.end();
            auto* action=parent->addAction(QIcon(swatch),name); action->setCheckable(true);
            if (const auto* block=model->find(id)) action->setChecked(block->appearance.value(key)==name);
            connect(parent,&QMenu::aboutToShow,host,[model,id,action,key,name,readOnly] {
                const auto* block=model ? model->find(id) : nullptr;
                action->setChecked(block && block->appearance.value(key)==name);
                action->setEnabled(block && !readOnly());
            });
            connect(action,&QAction::triggered,host,[change,key,name] { change(key,name); });
        }
    };
    add(appearance,BlockNoteStyle::presets(),"preset");
    add(appearance->addMenu("Font"),BlockNoteStyle::fonts(),"fontSet");
    add(appearance->addMenu("Tone"),BlockNoteStyle::tones(),"tone");
    appearance->addSeparator();
    connect(appearance->addAction("Reset to type default"),&QAction::triggered,host,[change] { change({},{}); });
}

EditorBlockContent::EditorBlockContent(DocumentBlockModel* value, PinloomHostClient* client,
    const EditorAssociatedBlock& block, QWidget* parent) : QWidget(parent),model(value),id(block.id) {
    setObjectName("editorBlockContent");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(4);
    if (block.type=="wave") {
        wave=new WaveBlockContent(model,id,this); layout->addWidget(wave,1);
    } else if (block.type=="text" || block.type=="code") {
        text=new QTextEdit(this); text->setObjectName("editorBlockText"); text->setFrameShape(QFrame::NoFrame);
        text->setAcceptRichText(false); text->setUndoRedoEnabled(false); text->setPlaceholderText("Write a note…");
        text->setMouseTracking(true); layout->addWidget(text,1); setFocusProxy(text);
        connect(text,&QTextEdit::textChanged,this,[this] { if(!updating && model && !text->isReadOnly()) model->editText(id,text->toPlainText()); });
    } else {
        preview=new EditorBlockPreview(model,client,this);
        imageHost=new QWidget(this); auto* imageRow=new QHBoxLayout(imageHost); imageRow->setContentsMargins(0,0,0,0);
        image=new PinloomPreviewImageLabel(imageHost); image->activated=[this] { if(openOriginal) openOriginal(); };
        imageRow->addStretch(); imageRow->addWidget(image,1); imageRow->addStretch(); layout->addWidget(imageHost,1);
        description=UiControls::label(this); description->setWordWrap(true); description->setTextFormat(Qt::PlainText);
        description->setTextInteractionFlags(Qt::TextSelectableByMouse); description->setAlignment(Qt::AlignLeft|Qt::AlignTop); layout->addWidget(description);
        UiTypography::apply(description,UiTypography::Role::Metadata);
        referenceText=new QTextEdit(this); referenceText->setObjectName("editorBlockReferenceText");
        referenceText->setReadOnly(true); referenceText->setAcceptRichText(false); referenceText->setFrameShape(QFrame::NoFrame);
        referenceText->setMinimumSize(0,0); layout->addWidget(referenceText,1); referenceText->hide();
    }
}
void EditorBlockContent::setEditing(bool value) {
    editing=value; setAttribute(Qt::WA_TransparentForMouseEvents,!value);
    if (!model) return;
    if (value) {
        model->beginTextSession(id);
        if (wave) wave->focusContent();
        else if (text) text->setFocus(Qt::OtherFocusReason);
        else if (referenceText && referenceText->isVisible()) referenceText->setFocus(Qt::OtherFocusReason);
    } else { if(wave) { QString issue; wave->flush(&issue); if(!issue.isEmpty()) model->setError(issue); } model->endTextSession(); }
}
void EditorBlockContent::present(const EditorAssociatedBlock& block, bool readOnly, bool compact) {
    if (!model || id!=block.id) return;
    setVisible(!compact); updating=true;
    if (wave) {
        wave->present(block.text,readOnly);
    } else if (text) {
        text->setReadOnly(readOnly);
        if (text->toPlainText()!=block.text) text->setPlainText(block.text);
        if (revision!=block.revision || styleRevision!=BlockNoteStyle::instance().revision()) {
            const auto appearance=BlockNoteStyle::instance().appearance(block.appearance);
            text->setStyleSheet(QString("QTextEdit {background:transparent;color:%1;border:0;}").arg(appearance.text.name()));
            BlockNoteStyle::format(text->document(),appearance);
        }
    } else {
        // Auto-height links scroll with the code view. A transient inner bar
        // would narrow the document and keep it in an over-wrapped layout.
        referenceText->setVerticalScrollBarPolicy(block.autoHeight && !EditorBlockPreview::linkOnlyText(block).isEmpty()
            ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
        if (revision!=block.revision || styleRevision!=BlockNoteStyle::instance().revision()) {
            const auto appearance=BlockNoteStyle::instance().appearance(block.appearance);
            referenceText->setStyleSheet(QString("QTextEdit {background:transparent;color:%1;border:0;}").arg(appearance.text.name()));
            BlockNoteStyle::format(referenceText->document(),appearance);
        }
        image->setMaximumWidth(block.width);
        const auto key=model->documentId()+'\n'+block.asset+'\n'+block.uri+'\n'+block.caption;
        if(compact) { preview->cancel(); resourceKey.clear(); image->clearSourcePixmap(); imageHost->hide(); }
        else if(resourceKey!=key) {
            resourceKey=key; const QPointer<EditorBlockContent> self(this);
            preview->request(id,[self,key](const EditorBlockPreviewResult& result) {
                if(!self || self->resourceKey!=key || !self->model) return;
                const auto* current=self->model->find(self->id); if(!current) return;
                QStringList content;
                if(!current->caption.isEmpty() && EditorBlockPreview::linkOnlyText(*current).isEmpty()) content<<current->caption;
                if(!result.status.isEmpty()) content<<result.status;
                self->description->setText(content.join('\n')); self->description->setVisible(!content.isEmpty());
                self->referenceText->setPlainText(result.text); self->referenceText->setVisible(!result.text.isEmpty());
                BlockNoteStyle::format(self->referenceText->document(),BlockNoteStyle::instance().appearance(current->appearance));
                if(result.image.isNull()) self->image->clearSourcePixmap();
                else self->image->setSourcePixmap(QPixmap::fromImage(result.image),current->caption);
                self->imageHost->setVisible(!result.image.isNull());
            });
        }
    }
    revision=block.revision; styleRevision=BlockNoteStyle::instance().revision(); updating=false;
}
