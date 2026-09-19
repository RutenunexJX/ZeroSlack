#ifndef DEFERREDPANEL_H
#define DEFERREDPANEL_H

#include <QPointer>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <utility>

// A stable layout host; its content is built only by an explicit request or show.
class DeferredPanel final : public QWidget
{
public:
    using Factory = std::function<QWidget*(QWidget*)>;

    DeferredPanel(QWidget* parent, QObject* owner, Factory factory)
        : QWidget(parent), factory(std::move(factory))
    {
        auto* box = new QVBoxLayout(this);
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(0);
        if (owner) {
            connect(owner, &QObject::destroyed, this,
                    [this] { cancelCreation(); });
        }
    }

    bool isCreated() const { return content != nullptr; }

    void ensureCreated()
    {
        if (content || creating || !factory)
            return;
        creating = true;
        content = factory(this);
        if (content) {
            layout()->addWidget(content);
            setFocusProxy(content);
            factory = {};
            auto restore = std::move(afterCreation);
            if (restore)
                restore();
        }
        creating = false;
    }

    void afterCreated(std::function<void()> handler)
    {
        if (content)
            handler();
        else
            afterCreation = std::move(handler);
    }

    void cancelCreation() { factory = {}; afterCreation = {}; }

protected:
    void showEvent(QShowEvent* event) override
    {
        ensureCreated();
        QWidget::showEvent(event);
    }

private:
    Factory factory;
    std::function<void()> afterCreation;
    QPointer<QWidget> content;
    bool creating = false;
};

#endif
