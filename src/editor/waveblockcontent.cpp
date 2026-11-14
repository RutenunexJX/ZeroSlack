#include "waveblockcontent.h"
#include "documentblockmodel.h"
#include "uicontrols.h"
#include "insightvisualstyle.h"
#include <QLibrary>
#include <QCoreApplication>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QSettings>
#include <QTextDocument>
#include <QScopedValueRollback>
#include <utility>
namespace {
struct Api {
    QLibrary ela,library;
    using Create=int(*)(const char*,size_t,const char*,QWidget*,QWidget**,char*,size_t);
    using Snapshot=int(*)(QWidget*,char*,size_t,size_t*,char*,size_t);
    using Update=int(*)(QWidget*,const char*,size_t,char*,size_t);
    using Action=int(*)(QWidget*,const char*,char*,size_t);
    using Callback=void(*)(void*,const char*,size_t);
    using Observe=int(*)(QWidget*,Callback,void*,char*,size_t);
    using Preview=int(*)(QWidget*,QWidget**,char*,size_t);
    Create create=nullptr; Snapshot project=nullptr,state=nullptr;
    Update configure=nullptr,sync=nullptr,setPreview=nullptr;
    Action action=nullptr; Observe observe=nullptr; Preview preview=nullptr;
    QString error;
    Api() {
        const auto root=QCoreApplication::applicationDirPath()+"/components/tickx/";
        const auto verify=[&](const QString& name,const QByteArray& hash) {
            QFile file(root+name);
            if(!file.open(QIODevice::ReadOnly) || QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex()!=hash) {
                error="The frozen Tickx component is missing or changed: "+name;return false;
            }
            return true;
        };
        if(!verify("wavewidgets.dll","e89d2845a7c132380a1dc9a5aa23c6ef303c8d283ec4c697da613ef096031057")
            || !verify("WaveWorkbenchEla.dll","61e96709023acb4710a1edbcef26b832e1a35c7079bb0d5414b5337d89723207")
            || !verify("LICENSE","a310654192acd627fd9ffae6d3692e3ec66782f298b3f9b7f6c61c4c61e1fd7e")
            || !verify("NOTICE","36729df452801962a892fd8ad48b83763531a26b81134a684ae0375abd326fe6")
            || !verify("THIRD-PARTY-NOTICES.md","618eaf788fca27b5e25a5b5af1b247c50edac5073afa9da418c8f5033e9e95fc")
            || !verify("build-info.json","54f8a8149666c5ab093c01d085bfd6dcdbc29f4923cb3a99e26a5f0218feaa88"))return;
        ela.setFileName(root+"WaveWorkbenchEla.dll");library.setFileName(root+"wavewidgets.dll");
        ela.setLoadHints(QLibrary::PreventUnloadHint);library.setLoadHints(QLibrary::PreventUnloadHint);
        if(!ela.load() || !library.load()){error=ela.isLoaded()?library.errorString():ela.errorString();return;}
        const auto abi=reinterpret_cast<int(*)()>(library.resolve("wavewidgets_abi_version"));
        if(!abi || abi()!=1){error="Unsupported Tickx widget ABI.";return;}
        create=reinterpret_cast<Create>(library.resolve("wavewidgets_create_wave_editor_v1"));
        project=reinterpret_cast<Snapshot>(library.resolve("wavewidgets_wave_editor_project_v1"));
        state=reinterpret_cast<Snapshot>(library.resolve("wavewidgets_wave_editor_state_v1"));
        configure=reinterpret_cast<Update>(library.resolve("wavewidgets_configure_wave_editor_v1"));
        sync=reinterpret_cast<Update>(library.resolve("wavewidgets_sync_wave_editor_v1"));
        action=reinterpret_cast<Action>(library.resolve("wavewidgets_wave_editor_action_v1"));
        observe=reinterpret_cast<Observe>(library.resolve("wavewidgets_set_wave_editor_callback_v1"));
        preview=reinterpret_cast<Preview>(library.resolve("wavewidgets_create_waveform_view_v1"));
        setPreview=reinterpret_cast<Update>(library.resolve("wavewidgets_set_waveform_preview_v1"));
        if(!create || !project || !state || !configure || !sync || !action || !observe || !preview || !setPreview)
            error="The complete Tickx wave-editor/v1 contract is unavailable.";
    }
};
Api& api(){static Api value;return value;}
QByteArray normalized(const QByteArray& data){return QJsonDocument::fromJson(data).toJson(QJsonDocument::Compact);}
bool isPreview(const QByteArray& data){return QJsonDocument::fromJson(data).object().value("contract")=="wave-preview/v1";}
QByteArray snapshot(QWidget* widget,Api::Snapshot read,QString* error=nullptr) {
    size_t size=0;char message[2048]{};
    if(read(widget,nullptr,0,&size,message,sizeof(message))!=0 || size<2 || size>8*1024*1024) {
        if(error)*error=*message?QString::fromUtf8(message):QStringLiteral("Invalid Tickx snapshot size.");return {};
    }
    QByteArray bytes(qsizetype(size),Qt::Uninitialized);
    if(read(widget,bytes.data(),size,&size,message,sizeof(message))!=0){if(error)*error=QString::fromUtf8(message);return {};}
    bytes.chop(1);return normalized(bytes);
}
}
bool WaveBlockContent::validate(const QByteArray& data,QString* error) {
    auto& runtime=api();if(!runtime.error.isEmpty()){if(error)*error=runtime.error;return false;}
    if(data.isEmpty() || data.size()>8*1024*1024 || !QJsonDocument::fromJson(data).isObject()) {
        if(error)*error="Invalid or oversized Tickx JSON.";return false;
    }
    QWidget parent;QWidget* child=nullptr;char message[2048]{};const bool legacy=isPreview(data);
    const int code=legacy?runtime.preview(&parent,&child,message,sizeof(message))
        :runtime.create(data.constData(),size_t(data.size()),qVersion(),&parent,&child,message,sizeof(message));
    const bool ok=code==0 && child && (legacy ? runtime.setPreview(child,data.constData(),size_t(data.size()),message,sizeof(message))==0
        : child->property("wavewidgets.capabilities").toStringList().contains("wave-style/v1"));
    if(!ok && error)*error=QString::fromUtf8(message);return ok;
}
QString WaveBlockContent::initialProject() {
    if(!api().error.isEmpty())return {};
    QWidget parent;QWidget* child=nullptr;char message[2048]{};
    if(api().create(nullptr,0,qVersion(),&parent,&child,message,sizeof(message))!=0 || !child
        || !child->property("wavewidgets.capabilities").toStringList().contains("wave-style/v1"))return {};
    return QString::fromUtf8(snapshot(child,api().project));
}
WaveBlockContent::WaveBlockContent(DocumentBlockModel* value,const QString& blockId,QWidget* parent)
    :QWidget(parent),model(value),id(blockId) {
    setObjectName("waveBlockContent");auto* row=new QVBoxLayout(this);row->setContentsMargins(0,0,0,0);row->setSpacing(2);
    status=UiControls::label(this);status->setWordWrap(true);row->addWidget(status);status->hide();
    savedView=QJsonDocument::fromJson(QSettings().value(viewKey()).toByteArray()).object();
    bindModel();
}
void WaveBlockContent::rebind(DocumentBlockModel* value){
    if(!value || value==model || !value->find(id))return;
    saveView();if(model)disconnect(model,nullptr,this,nullptr);
    model=value;bindModel();updateState();updateHistory();saveView();
}
void WaveBlockContent::bindModel(){
    connect(model,&DocumentBlockModel::contentsFlushRequested,this,[this](QString* error){if(error->isEmpty())flush(error);});
    connect(model,&DocumentBlockModel::changed,this,[this]{
        if(!publishing && model)if(const auto* block=model->find(id))present(block->text,readOnly,editing);
    });
    connect(model,&DocumentBlockModel::historyChanged,this,[this]{if(!updating && !publishing)updateHistory();});
}
WaveBlockContent::~WaveBlockContent(){
    if(editor){char message[256]{};api().observe(editor,nullptr,nullptr,message,sizeof(message));}
    saveView();
}
QString WaveBlockContent::viewKey() const{return "WaveBlockViews/"+(model?model->documentId():QString())+"/"+id;}
void WaveBlockContent::saveView(){if(model && !savedView.isEmpty())QSettings().setValue(viewKey(),QJsonDocument(savedView).toJson(QJsonDocument::Compact));}
bool WaveBlockContent::fail(const QString& message,QString* error){
    const auto text=message.isEmpty()?QStringLiteral("Tickx could not apply this operation."):message;
    if(error)*error=text;status->setText(text);status->show();return false;
}
bool WaveBlockContent::present(const QString& payload,bool locked,bool editMode) {
    if(updating || publishing)return true;
    QScopedValueRollback<bool> guard(updating,true);auto& runtime=api();
    if(!runtime.error.isEmpty())return fail(runtime.error);
    const auto bytes=normalized(payload.toUtf8());char message[2048]{};
    if(isPreview(bytes)) {
        if(editMode)return fail("Legacy Wave previews are read-only.");
        if(editor)return fail("A project cannot be replaced by a legacy preview in place.");
        if(!preview){if(runtime.preview(this,&preview,message,sizeof(message))!=0)return fail(QString::fromUtf8(message));
            static_cast<QVBoxLayout*>(layout())->insertWidget(0,preview,1);}
        if(accepted.toUtf8()!=bytes && runtime.setPreview(preview,bytes.constData(),size_t(bytes.size()),message,sizeof(message))!=0)
            return fail(QString::fromUtf8(message));
        accepted=QString::fromUtf8(bytes);readOnly=locked;editing=false;status->hide();return true;
    }
    if(preview)return fail("Legacy preview data is read-only.");
    const bool created=!editor;
    if(created){
        if(runtime.create(bytes.constData(),size_t(bytes.size()),qVersion(),this,&editor,message,sizeof(message))!=0 || !editor)
            return fail(QString::fromUtf8(message));
        if(editor->property("wavewidgets.contract").toString()!="wave-workbench.wave-editor/v1"
            || !editor->property("wavewidgets.capabilities").toStringList().contains("wave-style/v1")){
            delete editor;editor=nullptr;return fail("The required Tickx editor and wave-style/v1 capability are unavailable.");}
        static_cast<QVBoxLayout*>(layout())->insertWidget(0,editor,1);
    } else if(accepted.toUtf8()!=bytes && runtime.sync(editor,bytes.constData(),size_t(bytes.size()),message,sizeof(message))!=0)
        return fail(QString::fromUtf8(message));
    const auto& theme=InsightVisualStyle::theme();
    QJsonObject options{{"mode",editMode?"edit":"preview"},{"readOnly",locked},{"historyOwner","host"},
        {"canUndo",model && model->canUndo()},{"canRedo",model && model->canRedo()},
        {"theme",theme.textPrimary.lightness()>theme.panelBackground.lightness()?"dark":"light"}};
    if(created && !savedView.isEmpty())options["view"]=savedView;
    const auto config=QJsonDocument(options).toJson(QJsonDocument::Compact);
    if(runtime.configure(editor,config.constData(),size_t(config.size()),message,sizeof(message))!=0){if(created){delete editor;editor=nullptr;}return fail(QString::fromUtf8(message));}
    if(created && runtime.observe(editor,&WaveBlockContent::callback,this,message,sizeof(message))!=0){delete editor;editor=nullptr;return fail(QString::fromUtf8(message));}
    accepted=QString::fromUtf8(bytes);readOnly=locked;editing=editMode;status->hide();updateState();return true;
}
void WaveBlockContent::callback(void* context,const char* json,size_t size) {
    auto* self=static_cast<WaveBlockContent*>(context);
    const auto event=QJsonDocument::fromJson(QByteArray(json,qsizetype(size))).object().value("event").toString();
    if(event=="changed") {
        QString issue;const auto bytes=snapshot(self->editor,api().project,&issue);
        if(bytes.isEmpty()){QTimer::singleShot(0,self,[self,issue]{self->fail(issue);});return;}
        self->pendingProjects.append(bytes);
        if(!self->scheduled){self->scheduled=true;QTimer::singleShot(0,self,[self]{self->scheduled=false;self->publishPending();});}
    } else if(event=="undoRequested" || event=="redoRequested") {
        QTimer::singleShot(0,self,[self,redo=event=="redoRequested"]{self->replay(redo);});
    } else if(event=="viewChanged" || event=="preferredHeightChanged") {
        QTimer::singleShot(0,self,[self]{self->updateState();});
    }
}
void WaveBlockContent::publishProject(const QByteArray& bytes) {
    if(!model || !model->find(id) || bytes.isEmpty())return;
    accepted=QString::fromUtf8(bytes);
    if(normalized(model->find(id)->text.toUtf8())==bytes)return;
    QScopedValueRollback<bool> guard(publishing,true);
    model->endTextSession();model->editText(id,accepted);model->endTextSession();
}
void WaveBlockContent::publishPending(){
    const auto values=std::exchange(pendingProjects,{});
    for(const auto& bytes:values)publishProject(bytes);
    updateState();updateHistory();
}
bool WaveBlockContent::flush(QString* error) {
    if(!editor || updating || publishing || !model)return true;
    char message[2048]{};
    if(api().action(editor,"commit",message,sizeof(message))!=0)return fail(QString::fromUtf8(message),error);
    const auto bytes=snapshot(editor,api().project,error);if(bytes.isEmpty())return fail(error?*error:QString(),error);
    publishPending();publishProject(bytes);updateState();updateHistory();saveView();status->hide();return true;
}
void WaveBlockContent::updateState(){
    if(!editor)return;
    const auto state=QJsonDocument::fromJson(snapshot(editor,api().state)).object();
    if(state.isEmpty())return;savedView=state.value("view").toObject();
    if(model)model->setPresentationHeight(id,qBound(160,state.value("preferredHeight").toInt(240),540));
}
void WaveBlockContent::updateHistory(){
    if(!editor || !model || updating)return;
    const auto config=QJsonDocument(QJsonObject{{"canUndo",model->canUndo()},{"canRedo",model->canRedo()}}).toJson(QJsonDocument::Compact);
    char message[2048]{};if(api().configure(editor,config.constData(),size_t(config.size()),message,sizeof(message))!=0)fail(QString::fromUtf8(message));
}
void WaveBlockContent::replay(bool redo){
    QString issue;if(readOnly || !model || !flush(&issue))return;
    if(!model->undoAssociation(redo))if(auto* document=qobject_cast<QTextDocument*>(model->parent())){
        model->beginTextReplay();if(redo)document->redo();else document->undo();model->endTextReplay();
    }
    if(const auto* block=model->find(id))present(block->text,readOnly,editing);
    updateHistory();
}
void WaveBlockContent::focusContent(){
    auto* canvas=editor?editor->findChild<QWidget*>("waveEditorCanvas"):preview;
    if(canvas)canvas->setFocus(Qt::OtherFocusReason);
}
