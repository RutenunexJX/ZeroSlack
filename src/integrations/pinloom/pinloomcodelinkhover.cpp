#include "pinloomcodelinkhover.h"
#include "pinloompreviewimage.h"
#include "editorhoverpopup.h"
#include "mycodeeditor.h"
#include "uicontrols.h"
#include "editorblockpreview.h"
#include "documentreferenceservice.h"
#include <QMenu>

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QTextDocument>
#include <QVBoxLayout>

namespace {
QLabel* plainLabel(QWidget* parent, const QString& name)
{
    auto* label = UiControls::label(parent);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    return label;
}
}

PinloomCodeLinkHover::PinloomCodeLinkHover(PinloomHostClient* clientValue, QObject* parent)
    : QObject(parent), client(clientValue), loader(new PinloomPreviewImageLoader(this))
{
    dwellTimer.setSingleShot(true);
    dwellTimer.setInterval(300);
    leaveTimer.setSingleShot(true);
    leaveTimer.setInterval(220);
    connect(&dwellTimer, &QTimer::timeout, this, &PinloomCodeLinkHover::showCard);
    connect(&leaveTimer, &QTimer::timeout, this, [this] {
        if (!contains(QCursor::pos())) dismiss();
    });
    qApp->installEventFilter(this);
}

PinloomCodeLinkHover::~PinloomCodeLinkHover()
{
    qApp->removeEventFilter(this);
    dismiss();
}

void PinloomCodeLinkHover::setStore(PinloomCodeLinkStore* value)
{
    dismiss();
    store = value;
}

void PinloomCodeLinkHover::hover(MyCodeEditor* target, const QString& anchorId, const QRect& rect)
{
    if (!target || !target->isVisible() || !store || !client || anchorId.isEmpty()
        || QApplication::mouseButtons() != Qt::NoButton)
        return;
    if (editor == target && sourceAnchorId == anchorId && anchorRect == rect) {
        leaveTimer.stop();
        return;
    }
    dismiss();
    editor = target;
    sourceAnchorId = anchorId;
    anchorRect = rect;
    sessionConnections.append(connect(target, &QObject::destroyed, this, [this] { dismiss(); }));
    sessionConnections.append(connect(target, &MyCodeEditor::fileNameChanged, this, [this] { dismiss(); }));
    sessionConnections.append(connect(target->document(), &QTextDocument::contentsChanged, this, [this] { dismiss(); }));
    sessionConnections.append(connect(target->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { dismiss(); }));
    sessionConnections.append(connect(target->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { dismiss(); }));
    dwellTimer.start();
}

void PinloomCodeLinkHover::dismiss() { clearSession(true); }

void PinloomCodeLinkHover::clearSession(bool closePopup)
{
    ++generation;
    dwellTimer.stop();
    leaveTimer.stop();
    loader->cancel();
    if (client) client->cancelReadRequests(this);
    for (const auto& connection : std::as_const(sessionConnections)) disconnect(connection);
    sessionConnections.clear();
    auto oldPopup = popup;
    popup.clear();
    body.clear();
    image.clear();
    titleLabel.clear();
    detailsLabel.clear();
    statusLabel.clear();
    openButton.clear();
    editor.clear();
    sourceAnchorId.clear();
    targets.clear();
    identity = {};
    targetIndex = -1;
    opening = false;
    if (closePopup && oldPopup && oldPopup->contentModel().kind == PeekContentKind::PinloomPreview)
        oldPopup->closePopup();
}

bool PinloomCodeLinkHover::contains(const QPoint& position) const
{
    if (anchorRect.contains(position)) return true;
    if (popup && popup->isVisible()
        && QRect(popup->mapToGlobal(QPoint()), popup->size()).contains(position)) return true;
    auto* active = QApplication::activePopupWidget();
    return active && body && body->isAncestorOf(active);
}

bool PinloomCodeLinkHover::eventFilter(QObject* watched, QEvent* event)
{
    if (sourceAnchorId.isEmpty()) return false;
    auto* widget = qobject_cast<QWidget*>(watched);
    const bool editorSurface = editor && widget
        && (widget == editor || widget == editor->viewport()
            || widget->objectName() == QStringLiteral("editorLineNumberGutter")
            || widget == editor->verticalScrollBar() || widget == editor->horizontalScrollBar());
    switch (event->type()) {
    case QEvent::MouseMove:
        if (contains(static_cast<QMouseEvent*>(event)->globalPosition().toPoint())) leaveTimer.stop();
        else if (!leaveTimer.isActive()) leaveTimer.start();
        break;
    case QEvent::Leave:
        if ((editorSurface || widget == popup || widget == body) && !leaveTimer.isActive()) leaveTimer.start();
        break;
    case QEvent::MouseButtonPress:
        // Native QWindow events arrive before QWidget dispatch. Use the same
        // global geometry as the shared Peek container, not the receiver type.
        if (!popup || (!QRect(popup->mapToGlobal(QPoint()), popup->size())
                          .contains(static_cast<QMouseEvent*>(event)->globalPosition().toPoint())
                      && !(QApplication::activePopupWidget() && popup->isAncestorOf(QApplication::activePopupWidget()))))
            dismiss();
        break;
    case QEvent::KeyPress:
    case QEvent::Wheel:
        if (editorSurface) dismiss();
        break;
    case QEvent::Hide:
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::DevicePixelRatioChange:
        if (editor && widget && (widget == editor || widget->isAncestorOf(editor))) dismiss();
        break;
    case QEvent::ApplicationDeactivate:
        dismiss();
        break;
    case QEvent::WindowDeactivate:
        if (editor && widget == editor->window() && !QApplication::activePopupWidget()) dismiss();
        break;
    default: break;
    }
    return false;
}

void PinloomCodeLinkHover::showCard()
{
    if (!editor || !store || !contains(QCursor::pos())) { dismiss(); return; }
    const auto binding = store->anchorByIdForDocument(sourceAnchorId, editor->documentFileName(),
                                                      editor->cachedDocumentText(), editor->syntaxDocument());
    targets = binding.anchor.links;
    targets.removeIf([&](const auto& link){return DocumentReferenceService::hasLegacy(editor->blockModel(),binding.anchor.id,link.id);});
    if (targets.isEmpty()) { dismiss(); return; }
    popup = editor->beginPinloomLinkPreview();
    if (!popup) { dismiss(); return; }

    body = new QWidget;
    body->setObjectName(QStringLiteral("pinloomHoverCard"));
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(6);
    if (targets.size() > 1) {
        auto* choices = UiControls::comboBox(body);
        choices->setObjectName(QStringLiteral("pinloomHoverTargets"));
        choices->setFocusPolicy(Qt::NoFocus);
        choices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        choices->setMinimumContentsLength(12);
        for (const auto& target : std::as_const(targets)) choices->addItem(target.title);
        connect(choices, &QComboBox::currentIndexChanged, this, &PinloomCodeLinkHover::resolveTarget);
        layout->addWidget(choices);
    }
    titleLabel = plainLabel(body, QStringLiteral("pinloomHoverTitle"));
    QFont bold = titleLabel->font(); bold.setBold(true); titleLabel->setFont(bold);
    layout->addWidget(titleLabel);
    detailsLabel = plainLabel(body, QStringLiteral("pinloomHoverDetails"));
    layout->addWidget(detailsLabel);
    image = new PinloomPreviewImageLabel(body);
    image->setObjectName(QStringLiteral("pinloomHoverImage"));
    // Short editor panes keep the card actions visible by shrinking the preview.
    image->setMinimumHeight(image->fontMetrics().height() * 2);
    image->setMaximumHeight(180);
    layout->addWidget(image, 1);
    image->activated = [this] {
        if (!image || image->sourcePixmap().isNull()) return;
        const auto pixmap = image->sourcePixmap();
        const auto title = titleLabel->text();
        const auto alt = image->accessibleName();
        const QPointer<MyCodeEditor> owner(editor);
        QTimer::singleShot(0, this, [this, owner, pixmap, title, alt] {
            dismiss();
            if (owner) openPinloomImagePreview(owner, pixmap, title, alt);
        });
    };
    statusLabel = plainLabel(body, QStringLiteral("pinloomHoverStatus"));
    layout->addWidget(statusLabel);
    auto* actions = new QVBoxLayout;
    openButton = UiControls::pushButton(QStringLiteral("Open original"), body);
    openButton->setObjectName(QStringLiteral("pinloomHoverOpen"));
    openButton->setFocusPolicy(Qt::NoFocus);
    connect(openButton, &QPushButton::clicked, this, [this] {
        if (!client || !identity.isValid() || opening) return;
        opening = true;
        openButton->setEnabled(false);
        statusLabel->setText(QStringLiteral("Opening through Pinloom..."));
        const auto request = generation;
        const QPointer<PinloomCodeLinkHover> self(this);
        const QPointer<MyCodeEditor> owner(editor);
        EditorBlockPreview::openInspectedTarget(originalTarget,client,this,
            [self,request]{return self && self->body && request==self->generation;},
            [self, owner, request](bool opened, const QString& detail) {
            const QString message = detail.isEmpty()
                ? (opened ? QStringLiteral("Original location opened.") : QStringLiteral("Original location could not be opened.")) : detail;
            if (self && request == self->generation && self->body) {
                self->opening = false;
                self->openButton->setEnabled(true);
                self->statusLabel->setText(message);
            }
            if (owner) emit owner->editorStatusMessageRequested(message);
        });
    });
    actions->addWidget(openButton);
    auto* secondaryActions = new QHBoxLayout;
    auto* retry = UiControls::pushButton(QStringLiteral("Retry"), body);
    retry->setObjectName(QStringLiteral("pinloomHoverRetry"));
    retry->setFocusPolicy(Qt::NoFocus);
    connect(retry, &QPushButton::clicked, this, [this] { resolveTarget(targetIndex); });
    secondaryActions->addWidget(retry);
    auto* links = UiControls::pushButton(QStringLiteral("Open links"), body);
    links->setFocusPolicy(Qt::NoFocus);
    connect(links, &QPushButton::clicked, this, [this] {
        const auto owner = editor;
        const auto anchor = sourceAnchorId;
        QTimer::singleShot(0, this, [this, owner, anchor] {
            dismiss();
            if (owner) emit owner->pinloomCodeLinkActivated(anchor);
        });
    });
    secondaryActions->addWidget(links);
    auto* display=UiControls::pushButton(QStringLiteral("Reference"),body);
    display->setObjectName("pinloomReferenceActions");
    auto* menu=UiControls::menu(display);display->setMenu(menu);
    for(const auto& choice:QList<QPair<QString,QString>>{{"Compact link","compact"},{"Embedded preview","embedded"},{"Remove local reference","removed"}}) {
        auto* action=menu->addAction(choice.first);action->setEnabled(!editor->documentReadOnly());
        connect(action,&QAction::triggered,this,[this,choice] {
            if(!editor || editor->documentReadOnly() || !store || targetIndex<0 || targetIndex>=targets.size())return;
            const QPointer<MyCodeEditor> owner=editor;
            const auto anchor=store->anchorByIdForDocument(sourceAnchorId,owner->documentFileName(),owner->cachedDocumentText(),owner->syntaxDocument());
            const auto linkId=targets[targetIndex].id;
            const auto sourceRevision=owner->semanticDocumentRevision();
            const auto storageRevision=store->storageRevision(); const QPointer<QTextDocument> sourceDocument=owner->document();
            QTimer::singleShot(0,this,[this,owner,anchor,linkId,choice,sourceRevision,storageRevision,sourceDocument] {
                dismiss();if(!owner || owner->documentReadOnly())return;
                if(!sourceDocument || owner->document()!=sourceDocument || owner->semanticDocumentRevision()!=sourceRevision
                    || !store || store->storageRevision()!=storageRevision) {
                    emit owner->editorStatusMessageRequested("The source binding changed. Open the reference menu again.");return;
                }
                QString error;const auto id=DocumentReferenceService::convertLegacy(owner->blockModel(),anchor,linkId,choice.second,&error);
                if(id.isEmpty())emit owner->editorStatusMessageRequested(error);
                owner->setFocus(Qt::OtherFocusReason);
            });
        });
    }
    secondaryActions->addWidget(display);
    actions->addLayout(secondaryActions);
    layout->addLayout(actions);
    popup->showEmbeddedContent(body, QStringLiteral("Pinloom"), anchorRect, QApplication::font());
    sessionConnections.append(connect(body, &QObject::destroyed, this, [this] { clearSession(false); }));
    resolveTarget(0);
}

void PinloomCodeLinkHover::showFailure(const QString& error)
{
    if (!body) return;
    image->clearSourcePixmap();
    image->hide();
    statusLabel->setText(error.isEmpty() ? QStringLiteral("Preview unavailable. Retry or open the linked entry.") : error);
}

void PinloomCodeLinkHover::resolveTarget(int index)
{
    if (!body || index < 0 || index >= targets.size()) return;
    const auto request = ++generation;
    loader->cancel();
    if (client) client->cancelReadRequests(this);
    targetIndex = index;
    opening = false;
    originalTarget = {};
    const auto& record = targets.at(index);
    identity = PinloomHostIdentity::fromVariantMap(record.identity);
    if (!identity.isValid()) identity = PinloomHostIdentity::fromUri(record.uri);
    image->clearSourcePixmap();
    image->hide();
    titleLabel->setText(record.title);
    detailsLabel->clear();
    statusLabel->setText(QStringLiteral("Loading preview..."));
    openButton->setEnabled(false);
    if (!client || !identity.isValid()) { showFailure(QStringLiteral("Pinloom target is unavailable. Use Open links.")); return; }
    const QPointer<PinloomCodeLinkHover> self(this);
    const auto current=[self,request] {return self && self->body && request==self->generation;};
    const auto uri=record.uri.toString();
    EditorBlockPreview::requestTarget(uri,client,this,current,[self,current](const EditorBlockPreviewResult& result) {
        if(!current())return;
        self->originalTarget=result.original;
        self->openButton->setEnabled(result.original.available());
        self->openButton->setToolTip(result.original.reason);
        if(!result.title.isEmpty())self->titleLabel->setText(result.title);
        self->detailsLabel->setText(result.location.isEmpty() ? result.text.left(2048) : result.location);
        self->statusLabel->setText(result.status);
        if(result.image.isNull())self->image->clearSourcePixmap();
        else self->image->setSourcePixmap(QPixmap::fromImage(result.image),self->titleLabel->text());
        self->image->setVisible(!result.image.isNull());
    });
}
