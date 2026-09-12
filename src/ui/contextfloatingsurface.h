#ifndef CONTEXTFLOATINGSURFACE_H
#define CONTEXTFLOATINGSURFACE_H

#include "contextresource.h"

class QWidget;

class ContextFloatingSurface {
public:
    virtual ~ContextFloatingSurface() = default;
    virtual bool hasResource() const = 0;
    virtual ContextResource resource() const = 0;
    virtual QWidget* view() const = 0;
    virtual void setActionsAvailable(bool pinAvailable, bool fullViewAvailable) = 0;
    virtual void setView(const ContextResource& resource, QWidget* view) = 0;
    virtual bool updateResource(const ContextResource& resource) = 0;
    virtual QWidget* takeView() = 0;
    virtual void clearView() = 0;
};

#endif // CONTEXTFLOATINGSURFACE_H
