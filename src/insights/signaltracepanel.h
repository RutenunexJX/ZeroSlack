#pragma once
#include "zeroslackexport.h"
#include "liveinsighttypes.h"
#include "graphexportservice.h"
#include <zeroslack/semantic/signalfeedbackservice.h>
#include <QWidget>
#include <memory>

class SignalTraceHeatModel;
class QTreeWidget;
class QTableView;

class ZEROSLACK_API SignalTracePanel final : public QWidget {
    Q_OBJECT
public:
    explicit SignalTracePanel(QWidget* parent=nullptr);
    ~SignalTracePanel() override;
    void setContext(SignalTraceContext context,LiveInsightRequestKey key={});
    void showForSymbol(const QString& name,const QString& file,const QString& module,const QString& access={});
    void showObject(const SignalTraceObject& object,bool recordHistory=true);
    void clearTarget();
    void refresh();
    void cancel();
    void setHeatmapMode(bool enabled);
    void setNavigationHandler(std::function<bool(const SignalTraceEvidence&)> handler);
    void setStatusMessageHandler(std::function<void(const QString&,int)> handler);
    void setMarkerHandler(std::function<void(const QList<SignalTraceEvidence>&)> handler);
    void setSelectionHandler(std::function<void(const SignalTraceObject&)> handler);
    void syncSelection(const SignalTraceObject& object);
    void syncInstanceSelection(const QString& instancePath);
    void setMainAreaHandler(std::function<void(bool)> handler);
    void setInMainArea(bool expanded);
    bool isInMainArea() const;
    void setFocusSearchText(const QString& text);
    QString focusSearchText() const;
    void focusFit();
    void focusZoomIn();
    void focusZoomOut();
    void refreshThemePresentation();
    void focusInspector();
    QVariantMap saveViewState() const;
    void restoreViewState(const QVariantMap& state);
    bool saveSession(const QString& path,QString* error=nullptr) const;
    bool openSession(const QString& path,QString* error=nullptr);
    GraphExportResult exportGraph(const QString& path,const GraphExportOptions& options={}) const;
    bool exportHeatmap(const QString& path,QString* error=nullptr) const;
    SignalTraceObject currentObject() const;
    SignalTraceObject selectedObject() const;
    SignalTraceHeatModel* heatModel() const;
    QTreeWidget* traceTree(SignalTraceDirection direction) const;
    QTableView* heatTable() const;
    bool busy() const;
    void goBack();
    void goForward();
    void pinSelection();
    void openPin(int index);
    void analyzeFeedback();
    void expandCone(int depth);
protected:
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* object,QEvent* event) override;
private:
    struct Private;
    std::unique_ptr<Private> d;
};
