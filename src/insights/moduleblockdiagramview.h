#pragma once
#include "moduleblockdiagramservice.h"
#include "zeroslackexport.h"
#include <QWidget>
#include <QVariantMap>
#include <functional>
#include <memory>

class ZEROSLACK_API ModuleBlockDiagramView final : public QWidget {
public:
    explicit ModuleBlockDiagramView(QWidget* parent = nullptr);
    ~ModuleBlockDiagramView() override;
    void setReport(const ModuleBlockDiagramReport& report, const QString& workspace);
    void setNavigationHandler(std::function<void(const ModuleBlockDiagramNode&, bool)> handler);
    void setSourceNavigationEnabled(bool enabled);
    void fit();
    void zoom(qreal factor);
    bool enter(const QString& stableId);
    bool up();
    bool back();
    bool forward();
    QString focusId() const;
    bool layoutReady() const;
    QRectF nodeRect(const QString& stableId) const;
    QVariantMap saveViewState() const;
    void restoreViewState(const QVariantMap& state);
protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
