#include "waveblockcontent.h"
#include "documentblockmodel.h"
#include "uicontrols.h"
#include "insightvisualstyle.h"
#include <QLibrary>
#include <QCoreApplication>
#include <QApplication>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVBoxLayout>
#include <QLabel>
#include <QTabWidget>
#include <QDynamicPropertyChangeEvent>
#include <QTimer>
#include <QUuid>
#include <QScopedValueRollback>

namespace {
struct Api {
    QLibrary ela,library;
    using CreateEditor=int(*)(const char*,size_t,const char*,QWidget*,QWidget**,char*,size_t);
    using Snapshot=int(*)(QWidget*,char*,size_t,size_t*,char*,size_t);
    using Update=int(*)(QWidget*,const char*,size_t,char*,size_t);
    using CreatePreview=int(*)(QWidget*,QWidget**,char*,size_t);
    CreateEditor create=nullptr; Snapshot snapshot=nullptr; Update update=nullptr,setPreview=nullptr; CreatePreview preview=nullptr;
    QString error;
    Api() {
        const auto root=QCoreApplication::applicationDirPath()+"/components/tickx/";
        const auto verify=[&](const QString& name,const QByteArray& hash) {
            QFile file(root+name);
            if (!file.open(QIODevice::ReadOnly) || QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex()!=hash) {
                error="Tickx 0.15.10 component is missing or changed: "+name; return false;
            }
            return true;
        };
        if (!verify("WaveWorkbenchEla.dll","61e96709023acb4710a1edbcef26b832e1a35c7079bb0d5414b5337d89723207")
            || !verify("wavewidgets.dll","92095197ea2e5512e6a7bb31d6df36c4a2194b84a340ad119fdccd2b4ab75d52")) return;
        ela.setFileName(root+"WaveWorkbenchEla.dll"); library.setFileName(root+"wavewidgets.dll");
        ela.setLoadHints(QLibrary::PreventUnloadHint); library.setLoadHints(QLibrary::PreventUnloadHint);
        if (!ela.load() || !library.load()) { error=ela.isLoaded() ? library.errorString() : ela.errorString(); return; }
        const auto abi=reinterpret_cast<int(*)()>(library.resolve("wavewidgets_abi_version"));
        if (!abi || abi()!=1) { error="Unsupported Tickx widget ABI."; return; }
        create=reinterpret_cast<CreateEditor>(library.resolve("wavewidgets_create_stimulus_editor_v1"));
        snapshot=reinterpret_cast<Snapshot>(library.resolve("wavewidgets_stimulus_project_v1"));
        update=reinterpret_cast<Update>(library.resolve("wavewidgets_update_stimulus_editor_v1"));
        preview=reinterpret_cast<CreatePreview>(library.resolve("wavewidgets_create_waveform_view_v1"));
        setPreview=reinterpret_cast<Update>(library.resolve("wavewidgets_set_waveform_preview_v1"));
        if (!create || !snapshot || !update || !preview || !setPreview) error="Tickx memory editor or preview contract is unavailable.";
    }
};
Api& api() { static Api value; return value; }
QByteArray normalized(const QByteArray& data) { return QJsonDocument::fromJson(data).toJson(QJsonDocument::Compact); }
bool isPreview(const QByteArray& data) { return QJsonDocument::fromJson(data).object().value("contract")=="wave-preview/v1"; }
QByteArray snapshotBytes(QWidget* editor,QString* error) {
    size_t size=0; char message[2048]{};
    if(api().snapshot(editor,nullptr,0,&size,message,sizeof(message))!=0 || !size || size>8*1024*1024) { if(error)*error=QString::fromUtf8(message); return {}; }
    QByteArray bytes(qsizetype(size),Qt::Uninitialized);
    if(api().snapshot(editor,bytes.data(),size,&size,message,sizeof(message))!=0) { if(error)*error=QString::fromUtf8(message); return {}; }
    bytes.chop(1); return normalized(bytes);
}
QByteArray projectPreview(const QByteArray& payload,QString* error) {
    const auto project=QJsonDocument::fromJson(payload).object();
    const auto scenario=project.value("scenarios").toArray().first().toObject();
    const auto duration=scenario.value("durationTick").toString().toLongLong();
    const auto tick=project.value("timebase").toObject().value("picosecondsPerTick").toString().toLongLong();
    if (duration<=0 || tick<=0 || duration>9007199254740991LL/tick) { *error="This project time range cannot be represented by the preview contract."; return {}; }
    QJsonArray lanes;
    for (const auto& value:scenario.value("lanes").toArray()) {
        const auto lane=value.toObject(); if (!lane.value("visible").toBool(true)) continue;
        QJsonArray segments;
        for (const auto& piece:lane.value("segments").toArray()) {
            const auto segment=piece.toObject();
            segments.append(QJsonObject{{"start",double(segment.value("startTick").toString().toLongLong()*tick)},
                {"end",double(segment.value("endTick").toString().toLongLong()*tick)},{"value",segment.value("value")}});
        }
        if (lane.value("kind")=="clock" && segments.isEmpty()) {
            QJsonObject clock;
            for (const auto& domain:project.value("clockDomains").toArray()) if (domain.toObject().value("id")==lane.value("clockDomainId")) clock=domain.toObject();
            const auto period=clock.value("periodTick").toString().toLongLong();
            const auto phase=clock.value("phaseTick").toString().toLongLong();
            const auto numerator=clock.value("dutyNumerator").toString().toLongLong(), denominator=clock.value("dutyDenominator").toString().toLongLong();
            if (period<=0 || denominator<=0 || numerator<=0 || numerator>=denominator || duration/period>20000) { *error="The clock exceeds the bounded preview range. The original project remains intact."; return {}; }
            const qint64 high=qMax<qint64>(1,qint64((static_cast<long double>(period)*numerator)/denominator));
            for (qint64 start=0;start<duration;) {
                auto offset=(start-phase)%period; if(offset<0) offset+=period;
                const bool active=offset<high;
                const auto end=start+qMin(duration-start,active ? high-offset : period-offset);
                segments.append(QJsonObject{{"start",double(start*tick)},{"end",double(end*tick)},{"value",active ? "1" : "0"}}); start=end;
            }
        }
        lanes.append(QJsonObject{{"id",lane.value("id")},{"name",lane.value("name")},{"width",lane.value("width")},
            {"kind",lane.value("kind")=="clock" ? "clock" : lane.value("width").toInt(1)>1 ? "bus" : "bit"},
            {"provenance","tickx-project"},{"segments",segments}});
    }
    return QJsonDocument(QJsonObject{{"contract","wave-preview/v1"},{"generation",1},{"mode","symbolic"},
        {"timebase",QJsonObject{{"unit","ps"},{"start",0},{"end",double(duration*tick)}}},{"lanes",lanes}}).toJson(QJsonDocument::Compact);
}
}
bool WaveBlockContent::validate(const QByteArray& data,QString* error) {
    auto& runtime=api(); if (!runtime.error.isEmpty()) { if(error)*error=runtime.error; return false; }
    if (data.isEmpty() || data.size()>8*1024*1024 || !QJsonDocument::fromJson(data).isObject()) { if(error)*error="Invalid or oversized Tickx JSON."; return false; }
    QWidget parent; QWidget* child=nullptr; char message[2048]{};
    const bool preview=isPreview(data);
    const int code=preview ? runtime.preview(&parent,&child,message,sizeof(message)) : runtime.create(data.constData(),size_t(data.size()),qVersion(),&parent,&child,message,sizeof(message));
    const bool ok=code==0 && child && (!preview || runtime.setPreview(child,data.constData(),size_t(data.size()),message,sizeof(message))==0);
    if (!ok && error) *error=QString::fromUtf8(message);
    return ok;
}
QString WaveBlockContent::initialProject() {
    const auto identity=QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"schemaVersion",1},{"projectId",identity},{"name","Wave"},
        {"timebase",QJsonObject{{"picosecondsPerTick","1000"}}},{"clockDomains",QJsonArray{}},
        {"scenarios",QJsonArray{QJsonObject{{"id","scenario"},{"name","Wave"},{"durationTick","100"},
            {"lanes",QJsonArray{QJsonObject{{"id","signal"},{"name","signal"},{"kind","bit"},{"width",1},
                {"segments",QJsonArray{QJsonObject{{"id","value"},{"startTick","0"},{"endTick","100"},{"value","0"}}}}}}},
            {"events",QJsonArray{}},{"relations",QJsonArray{}},{"markers",QJsonArray{}}}}},
        {"importedTraces",QJsonArray{}},{"linkedResources",QJsonArray{}},{"exportSettings",QJsonObject{}}}).toJson(QJsonDocument::Compact));
}
WaveBlockContent::WaveBlockContent(DocumentBlockModel* value,const QString& blockId,QWidget* parent)
    : QWidget(parent),model(value),id(blockId) {
    setObjectName("waveBlockContent"); auto* layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    tabs=UiControls::tabWidget(this); tabs->setObjectName("waveBlockTabs"); layout->addWidget(tabs,1);
    status=UiControls::label(this); status->setWordWrap(true); layout->addWidget(status); status->hide();
    connect(model,&DocumentBlockModel::contentsFlushRequested,this,[this](QString* error) { if (error->isEmpty()) flush(error); });
    connect(tabs,&QTabWidget::currentChanged,this,[this](int) { if (editor && tabs->currentWidget()==preview) { QString issue; flush(&issue); refreshPreview(accepted.toUtf8()); } });
}
void WaveBlockContent::present(const QString& payload,bool locked) {
    if (updating) return;
    QScopedValueRollback<bool> guard(updating,true); readOnly=locked;
    const auto bytes=normalized(payload.toUtf8()); auto& runtime=api();
    if (!runtime.error.isEmpty()) { status->setText(runtime.error); status->show(); return; }
    char error[2048]{};
    if (accepted.toUtf8()!=bytes) {
        QWidget* replacement=nullptr;
        if (!isPreview(bytes)) {
            if (runtime.create(bytes.constData(),size_t(bytes.size()),qVersion(),this,&replacement,error,sizeof(error))!=0) { status->setText(QString::fromUtf8(error)); status->show(); return; }
            if (replacement->property("wavewidgets.contract").toString()!="wave-workbench.stimulus-editor/v1") { delete replacement; status->setText("Unexpected Tickx editor contract."); status->show(); return; }
        }
        const bool restoreFocus=editor && (editor->hasFocus() || editor->isAncestorOf(QApplication::focusWidget()));
        const bool editingTab=!preview || tabs->currentWidget()==editor;
        // The public timing-update API creates an undo command. A host history
        // restore must instead establish a new native history baseline.
        delete editor; editor=replacement;
        if (editor) { tabs->insertTab(0,editor,"Edit"); editor->installEventFilter(this); }
        if (!preview) {
            if (runtime.preview(this,&preview,error,sizeof(error))!=0) { status->setText(QString::fromUtf8(error)); status->show(); return; }
            tabs->addTab(preview,"Preview"); previewGeneration=0;
        }
        accepted=QString::fromUtf8(bytes);
        tabs->setCurrentWidget(editingTab && editor ? editor : preview);
        if (editor) nativeBaseline=QString::fromUtf8(snapshotBytes(editor,nullptr));
        refreshPreview(bytes);
        if (restoreFocus) focusContent();
    }
    if (editor) {
        editor->setEnabled(!readOnly); tabs->setTabEnabled(tabs->indexOf(editor),!readOnly);
        if (readOnly) tabs->setCurrentWidget(preview);
    }
    if (preview) preview->setProperty("themeName",InsightVisualStyle::theme().textPrimary.lightness()>InsightVisualStyle::theme().panelBackground.lightness() ? "dark" : "light");
}
void WaveBlockContent::refreshPreview(const QByteArray& payload) {
    if (!preview) return;
    QString issue; auto bytes=isPreview(payload) ? payload : projectPreview(payload,&issue);
    if (!bytes.isEmpty()) {
        auto render=QJsonDocument::fromJson(bytes).object(); render["generation"]=double(++previewGeneration);
        bytes=QJsonDocument(render).toJson(QJsonDocument::Compact);
    }
    char error[2048]{};
    if (!bytes.isEmpty() && api().setPreview(preview,bytes.constData(),size_t(bytes.size()),error,sizeof(error))!=0) issue=QString::fromUtf8(error);
    status->setText(issue); status->setVisible(!issue.isEmpty());
}
bool WaveBlockContent::flush(QString* error) {
    if (!editor || updating || readOnly || !model) return true;
    QScopedValueRollback<bool> guard(updating,true);
    const auto bytes=snapshotBytes(editor,error); if(bytes.isEmpty()) return false;
    if (bytes!=nativeBaseline.toUtf8()) { nativeBaseline=accepted=QString::fromUtf8(bytes); model->editText(id,accepted); }
    return true;
}
bool WaveBlockContent::eventFilter(QObject* object,QEvent* event) {
    if (object==editor && event->type()==QEvent::DynamicPropertyChange
        && static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName()=="wavewidgets.stateRevision") edited();
    return false;
}
void WaveBlockContent::edited() {
    if (updating) return;
    QString error; if (!flush(&error)) { status->setText(error); status->show(); }
}
void WaveBlockContent::focusContent() {
    auto* view=tabs->currentWidget(); if (!view) return;
    auto* canvas=view==editor ? editor->findChild<QWidget*>("stimulusCanvas") : view;
    if (canvas) canvas->setFocus(Qt::OtherFocusReason);
}
