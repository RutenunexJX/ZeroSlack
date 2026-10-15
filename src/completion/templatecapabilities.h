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
    static bool root(const QVariantMap& config);
    static bool module(const QVariantMap& config);
    static QString validate(const QVariantMap& config);
    static QString activationQuery(const QVariantMap& config);
    static QVariantMap operation(const QVariantMap& config, const QString& op);
    static QString region(const QVariantMap& config, const QString& content);
    static QString expandName(QString pattern, const QHash<QString, QString>& values);
    static TemplateBodyPlan render(const QVariantMap& config, const QString& body,
                                   const CodeTemplateSlotList& placeholders);
};
