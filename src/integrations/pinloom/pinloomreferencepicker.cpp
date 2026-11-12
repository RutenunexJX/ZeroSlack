#include "pinloomreferencepicker.h"
#include "pinloomcontextview.h"
#include "pinloompreviewimage.h"
#include "uicontrols.h"
#include <QTabWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QFileInfo>

PinloomReferencePicker::PinloomReferencePicker(PinloomHostClient* value, const QString& initialUri, QWidget* parent)
    : UiDialog(parent), client(value)
{
    setObjectName(QStringLiteral("pinloomReferencePicker")); setWindowTitle(tr("Insert reference"));
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(16,40,16,16);
    tabs = UiControls::tabWidget(this); tabs->setObjectName(QStringLiteral("referencePickerMode"));
    browser = new PinloomContextView(client, tabs); tabs->addTab(browser,tr("Find in Pinloom"));
    auto* linkPage = new QWidget(tabs); auto* linkLayout = new QVBoxLayout(linkPage);
    link = UiControls::lineEdit(linkPage); link->setObjectName(QStringLiteral("referencePickerLink"));
    link->setPlaceholderText(tr("pinloom://… · file:///… · https://…"));
    linkLayout->addWidget(UiControls::label(tr("Existing object or link"),linkPage)); linkLayout->addWidget(link); linkLayout->addStretch();
    tabs->addTab(linkPage,tr("Paste link")); layout->addWidget(tabs,1);
    capability = UiControls::label(this); capability->setObjectName(QStringLiteral("referencePickerCapability"));
    capability->setWordWrap(true); layout->addWidget(capability);
    auto* actions = new QHBoxLayout; actions->addStretch();
    auto* cancel = UiControls::pushButton(tr("Cancel"),this); cancel->setObjectName(QStringLiteral("referencePickerCancel")); actions->addWidget(cancel);
    insert = UiControls::pushButton(tr("Insert reference"),this); insert->setObjectName(QStringLiteral("referencePickerInsert")); actions->addWidget(insert);
    insert->setAutoDefault(false); insert->setEnabled(false); cancel->setDefault(true);
    layout->addLayout(actions); resize(620,520);
    loader = new PinloomPreviewImageLoader(this);
    connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
    connect(insert,&QPushButton::clicked,this,[this] { updateSelection(); if(insert->isEnabled()) accept(); });
    connect(browser,&PinloomContextView::previewStateChanged,this,&PinloomReferencePicker::updateSelection);
    connect(browser,&PinloomContextView::currentEntryChanged,this,&PinloomReferencePicker::updateSelection);
    connect(link,&QLineEdit::textChanged,this,&PinloomReferencePicker::updateLink);
    connect(tabs,&QTabWidget::currentChanged,this,&PinloomReferencePicker::updateSelection);
    browser->restoreState({});
    if (!initialUri.isEmpty()) { tabs->setCurrentIndex(1); link->setText(initialUri); }
    updateSelection();
}
PinloomReferencePicker::~PinloomReferencePicker() { if(client) client->cancelReadRequests(this); }

void PinloomReferencePicker::updateSelection()
{
    selected.clear(); caption.clear();
    if(tabs->currentIndex()==1) {
        capability->setText(linkStatus.isEmpty() ? tr("Paste a link to check its preview support.") : linkStatus);
        insert->setEnabled(linkReady);
        if(linkReady) selected=link->text().trimmed();
        return;
    }
    const auto entry=browser->currentEntry(); const auto state=browser->previewState();
    capability->setText(browser->previewReason().isEmpty() ? tr("Choose a reference. Its preview support appears here.") : browser->previewReason());
    const bool ready=entry.isValid() && !entry.deleted && (state=="preview" || state=="link-only");
    insert->setEnabled(ready);
    if(ready) { selected=entry.identity.toUri().toString(); caption=entry.title; }
}

void PinloomReferencePicker::updateLink()
{
    const auto request=++generation; loader->cancel(); if(client) client->cancelReadRequests(this);
    linkReady=false; const QUrl url(link->text().trimmed(),QUrl::StrictMode);
    if(!url.isValid() || url.isRelative() || url.isEmpty()) {
        linkStatus=tr("Enter a valid object URI, local file link, or web link."); updateSelection(); return;
    }
    const auto identity=PinloomHostIdentity::fromUri(url);
    if(!identity.isValid()) {
        linkReady=url.isLocalFile() || url.scheme()=="https" || url.scheme()=="http";
        linkStatus=linkReady ? (url.isLocalFile() && !QFileInfo::exists(url.toLocalFile())
            ? tr("Missing local file · Link only; the path is retained.")
            : tr("Link only · Use Open original to view this target.")) : tr("This link type is not supported.");
        updateSelection(); return;
    }
    linkStatus=tr("Checking Pinloom preview…"); updateSelection();
    if(!client) { linkStatus=tr("Pinloom is unavailable. Choose Cancel to keep the document unchanged."); updateSelection(); return; }
    const QPointer<PinloomReferencePicker> self(this);
    client->resolve(identity,[self,request](const PinloomHostDocument& document,const QString& error) {
        if(!self || request!=self->generation) return;
        if(!error.isEmpty() || !document.isValid() || document.entry.deleted) {
            self->linkStatus=error.isEmpty() ? tr("The referenced object is missing or was deleted.") : tr("Pinloom unavailable · %1").arg(error);
            self->updateSelection(); return;
        }
        const bool text=!document.content.trimmed().isEmpty();
        self->linkReady=true;
        self->linkStatus=text ? tr("Text preview available") : tr("Link only · Open the original in Pinloom");
        if(document.preview.isReady() && document.preview.isImage()) {
            self->linkReady=false;
            self->loader->load(document.preview,[self,request,text](const PinloomPreviewImageResult& result) {
                if(!self || request!=self->generation) return;
                self->linkReady=true;
                self->linkStatus=!result.image.isNull() ? tr("Image preview available") : text ? tr("Text preview available · Image unavailable") : tr("Link only · %1").arg(result.error);
                self->updateSelection();
            });
        }
        self->updateSelection();
    },this);
}
