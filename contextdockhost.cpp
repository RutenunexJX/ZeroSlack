#include "contextdockhost.h"

#include <QTabBar>
#include <QTabWidget>
#include <QStyle>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace {
const char kResourceKeyProperty[] = "contextResourceKey";
}

ContextDockHost::ContextDockHost(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("contextDockHost"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("contextDockTabs"));
    tabs->setDocumentMode(true);
    tabs->setMovable(true);
    tabs->setTabsClosable(true);
    auto* corner = new QWidget(tabs);
    auto* cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 2, 0);
    cornerLayout->setSpacing(2);
    fullViewButton = new QToolButton(corner);
    fullViewButton->setObjectName(
        QStringLiteral("contextDockFullView"));
    fullViewButton->setIcon(
        style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    fullViewButton->setToolTip(tr("Open current tab in main area"));
    fullViewButton->setVisible(false);
    cornerLayout->addWidget(fullViewButton);

    unpinButton = new QToolButton(corner);
    unpinButton->setObjectName(
        QStringLiteral("contextDockUnpin"));
    unpinButton->setIcon(
        style()->standardIcon(QStyle::SP_TitleBarNormalButton));
    unpinButton->setToolTip(tr("Move current tab to preview"));
    unpinButton->setEnabled(false);
    cornerLayout->addWidget(unpinButton);
    tabs->setCornerWidget(corner, Qt::TopRightCorner);
    layout->addWidget(tabs);

    connect(unpinButton,
            &QToolButton::clicked,
            this,
            [this]() {
                const ContextResource resource = currentResource();
                if (resource.isValid()) {
                    emit unpinResourceRequested(
                        resource.stableKey());
                }
            });
    connect(fullViewButton,
            &QToolButton::clicked,
            this,
            [this]() {
                const ContextResource resource = currentResource();
                if (resource.isValid())
                    emit fullViewResourceRequested(resource);
            });

    connect(tabs,
            &QTabWidget::tabCloseRequested,
            this,
            [this](int index) {
                const ContextResource resource = resourceAt(index);
                if (resource.isValid())
                    emit closeResourceRequested(resource.stableKey());
            });
    connect(tabs,
            &QTabWidget::currentChanged,
            this,
            [this](int index) {
                updateCurrentActions();
                emit currentResourceChanged(resourceAt(index));
            });
    connect(tabs->tabBar(),
            &QTabBar::tabMoved,
            this,
            [this](int, int) { emit resourceOrderChanged(); });
}

int ContextDockHost::resourceCount() const
{
    return tabs->count();
}

QStringList ContextDockHost::resourceKeys() const
{
    QStringList result;
    for (int index = 0; index < tabs->count(); ++index) {
        if (QWidget* page = tabs->widget(index))
            result.append(page->property(kResourceKeyProperty).toString());
    }
    return result;
}

bool ContextDockHost::containsResource(const QString& key) const
{
    return indexOfResource(key) >= 0;
}

ContextResource ContextDockHost::resourceAt(int index) const
{
    QWidget* page = tabs->widget(index);
    if (!page)
        return {};
    return resources.value(
        page->property(kResourceKeyProperty).toString());
}

ContextResource ContextDockHost::currentResource() const
{
    return resourceAt(tabs->currentIndex());
}

QWidget* ContextDockHost::viewForResource(const QString& key) const
{
    const int index = indexOfResource(key);
    return index >= 0 ? tabs->widget(index) : nullptr;
}

bool ContextDockHost::addResource(
    const ContextResource& resource,
    QWidget* view,
    bool fullViewAvailable)
{
    const QString key = resource.stableKey();
    if (!resource.isValid() || key.isEmpty() || !view)
        return false;
    const int existing = indexOfResource(key);
    if (existing >= 0) {
        tabs->setCurrentIndex(existing);
        return false;
    }

    view->setProperty(kResourceKeyProperty, key);
    const QString title = resource.title.isEmpty()
        ? resource.uri.fileName()
        : resource.title;
    const int index = tabs->addTab(view, title);
    tabs->setTabToolTip(index, resource.uri.toString());
    resources.insert(key, resource);
    fullViewAvailability.insert(key, fullViewAvailable);
    tabs->setCurrentIndex(index);
    updateCurrentActions();
    return true;
}

bool ContextDockHost::updateResource(
    const ContextResource& resource)
{
    const QString key = resource.stableKey();
    const int index = indexOfResource(key);
    if (!resource.isValid() || index < 0)
        return false;
    resources.insert(key, resource);
    const QString title = resource.title.isEmpty()
        ? resource.uri.fileName()
        : resource.title;
    tabs->setTabText(index, title);
    tabs->setTabToolTip(index, resource.uri.toString());
    return true;
}

bool ContextDockHost::setFullViewAvailable(
    const QString& key,
    bool available)
{
    if (indexOfResource(key) < 0)
        return false;
    fullViewAvailability.insert(key, available);
    updateCurrentActions();
    return true;
}

bool ContextDockHost::activateResource(const QString& key)
{
    const int index = indexOfResource(key);
    if (index < 0)
        return false;
    tabs->setCurrentIndex(index);
    return true;
}

QWidget* ContextDockHost::takeResource(const QString& key)
{
    const int index = indexOfResource(key);
    if (index < 0)
        return nullptr;
    QWidget* page = tabs->widget(index);
    tabs->removeTab(index);
    resources.remove(key);
    fullViewAvailability.remove(key);
    if (page) {
        page->setProperty(kResourceKeyProperty, QVariant());
        page->hide();
        page->setParent(nullptr);
    }
    return page;
}

bool ContextDockHost::removeResource(const QString& key)
{
    QWidget* page = takeResource(key);
    if (!page)
        return false;
    page->deleteLater();
    return true;
}

int ContextDockHost::indexOfResource(const QString& key) const
{
    const QString normalized = key.trimmed();
    if (normalized.isEmpty())
        return -1;
    for (int index = 0; index < tabs->count(); ++index) {
        QWidget* page = tabs->widget(index);
        if (page
            && page->property(kResourceKeyProperty).toString()
                   == normalized) {
            return index;
        }
    }
    return -1;
}

void ContextDockHost::updateCurrentActions()
{
    const ContextResource resource = currentResource();
    const bool hasResource = resource.isValid();
    unpinButton->setEnabled(hasResource);
    const bool fullViewAvailable = hasResource
        && fullViewAvailability.value(resource.stableKey(), false);
    fullViewButton->setVisible(fullViewAvailable);
    fullViewButton->setEnabled(fullViewAvailable);
}
