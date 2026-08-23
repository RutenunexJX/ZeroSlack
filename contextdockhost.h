#ifndef CONTEXTDOCKHOST_H
#define CONTEXTDOCKHOST_H

#include "contextresource.h"
#include "zeroslackexport.h"

#include <QHash>
#include <QStringList>
#include <QWidget>

class QTabWidget;
class QToolButton;

class ZEROSLACK_API ContextDockHost final : public QWidget
{
    Q_OBJECT

public:
    explicit ContextDockHost(QWidget* parent = nullptr);

    int resourceCount() const;
    QStringList resourceKeys() const;
    bool containsResource(const QString& key) const;
    ContextResource resourceAt(int index) const;
    ContextResource currentResource() const;
    QWidget* viewForResource(const QString& key) const;

    bool addResource(const ContextResource& resource,
                     QWidget* view,
                     bool fullViewAvailable = false);
    bool updateResource(const ContextResource& resource);
    bool setFullViewAvailable(const QString& key,
                              bool available);
    bool activateResource(const QString& key);
    QWidget* takeResource(const QString& key);
    bool removeResource(const QString& key);

signals:
    void closeResourceRequested(const QString& key);
    void unpinResourceRequested(const QString& key);
    void fullViewResourceRequested(const ContextResource& resource);
    void currentResourceChanged(const ContextResource& resource);
    void resourceOrderChanged();

private:
    QTabWidget* tabs = nullptr;
    QToolButton* unpinButton = nullptr;
    QToolButton* fullViewButton = nullptr;
    QHash<QString, ContextResource> resources;
    QHash<QString, bool> fullViewAvailability;

    int indexOfResource(const QString& key) const;
    void updateCurrentActions();
};

#endif // CONTEXTDOCKHOST_H
