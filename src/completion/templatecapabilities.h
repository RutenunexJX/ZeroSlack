#pragma once
#include "completiontypes.h"

struct TemplateBodyPlan {
    QString text;
    CodeTemplateSlotList placeholders;
    QString error;
};
// Data-only capabilities. They compose the existing semantic generator and
// document transaction; they cannot execute host code or access files.
class TemplateCapabilities {
public:
    static QString validate(const QVariantMap& config);
    static TemplateBodyPlan render(const QVariantMap& config, const QString& body,
                                   const CodeTemplateSlotList& placeholders);
};
