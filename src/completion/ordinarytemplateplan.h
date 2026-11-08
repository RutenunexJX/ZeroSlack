#pragma once
#include "usertemplateservice.h"

struct OrdinaryTemplatePlan {
    QString text;
    CodeTemplateSlotList placeholders;
    int finalCursor = -1;
    QString error;
};

// A deterministic, data-only expansion. It never creates persistent template
// instances or asks the simulation command planner to mutate a document.
class OrdinaryTemplatePlanner {
public:
    static QList<UserTemplateRecord> builtins();
    static QList<UserTemplateRecord> catalog(UserTemplateService* service, const QString& file);
    static OrdinaryTemplatePlan prepare(const UserTemplateRecord& resource,
                                        const QList<UserTemplateRecord>& catalog,
                                        const QString& selection = {});
    static QString validate(const QList<UserTemplateRecord>& catalog);
};
