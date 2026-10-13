#pragma once
#include "globalcontrolservice.h"
#include "xipshdlcallbuilder.h"
#include "zeroslackexport.h"
#include <QObject>
#include <QPointer>
#include <functional>
#include <optional>

class TabManager;
class WorkspaceManager;
class MyCodeEditor;
class NativeContextView;
class QWidget;
namespace simdock { class Workbench; }

class ZEROSLACK_API XipsHdlCommandService final : public QObject {
    Q_OBJECT
public:
    XipsHdlCommandService(TabManager* tabs, WorkspaceManager* workspaces,
                          simdock::Workbench* buildInputs, QWidget* parent);
    ~XipsHdlCommandService() override;
    NativeContextView* view(QWidget* parent = nullptr);
    void prepare();
    QList<GlobalControlItem> items(const QString& filter, const GlobalControlQueryContext& context) const;
    void activate(const GlobalControlItem& item);
    bool canClose(QString* error) const;
    std::function<void()> showBrowser;
signals:
    void catalogChanged();
    void message(const QString& text);
private slots:
    void refreshCatalog();
    void useFinished(const QVariantMap& result);
private:
    void attachComponent();
    void loadReferences();
    QString adoptReceipt(const QVariantMap& receipt);
    void beginInsertion(const QVariantMap& receipt);
    void inspectCall(XipsHdlCallRequest request);
    void bindSignals(XipsHdlCallRequest request, const XipsHdlCallInspection& inspection);
    void completeCall(XipsHdlCallRequest request, const XipsHdlCallInspection& inspection);
    void cancel();
    bool insertionCurrent() const;
    void fail(const QString& reason);
    QPointer<TabManager> tabs;
    QPointer<WorkspaceManager> workspaces;
    QPointer<simdock::Workbench> buildInputs;
    QPointer<QWidget> owner;
    QPointer<NativeContextView> nativeView;
    QPointer<QWidget> component;
    QVariantMap catalog;
    QVariantList references;
    QString unavailable;
    mutable std::optional<GlobalControlQueryContext> siteContext;
    mutable bool procedural = false;
    struct Pending {
        GlobalControlItem item;
        QPointer<MyCodeEditor> editor;
        QString requestId, workspace, project;
        QHash<QString, QString> openContents;
        std::optional<QStringList> compilationInputs;
        QStringList includeDirectories;
        QHash<QString, QString> defines;
        std::shared_ptr<std::atomic_bool> cancelled;
    };
    std::optional<Pending> pending;
};
