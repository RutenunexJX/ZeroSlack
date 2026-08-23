#ifndef CONTEXTPEEKHOST_H
#define CONTEXTPEEKHOST_H

#include "contextresource.h"
#include "zeroslackexport.h"

#include <QPointer>
#include <QWidget>

class QEvent;
class QLabel;
class QToolButton;
class QVBoxLayout;

class ZEROSLACK_API ContextPeekHost final : public QWidget
{
    Q_OBJECT

public:
    explicit ContextPeekHost(QWidget* editorRegion);
    ~ContextPeekHost() override;

    bool hasResource() const;
    ContextResource resource() const;
    QWidget* view() const;
    int preferredWidth() const;

    void setPreferredWidth(int width);
    void setActionsAvailable(bool pinAvailable,
                             bool fullViewAvailable);
    void setView(const ContextResource& resource,
                 QWidget* view);
    bool updateResource(const ContextResource& resource);
    QWidget* takeView();
    void clearView();

signals:
    void pinRequested();
    void closeRequested();
    void fullViewRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* header = nullptr;
    QWidget* contentHost = nullptr;
    QVBoxLayout* contentLayout = nullptr;
    QLabel* titleLabel = nullptr;
    QToolButton* pinButton = nullptr;
    QToolButton* fullViewButton = nullptr;
    QToolButton* closeButton = nullptr;
    QPointer<QWidget> currentView;
    ContextResource currentResource;
    int preferredWidthValue = 520;

    void buildUi();
    void synchronizeGeometry();
};

#endif // CONTEXTPEEKHOST_H
