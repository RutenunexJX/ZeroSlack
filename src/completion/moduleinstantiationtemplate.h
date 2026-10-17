#pragma once
#include "completiontypes.h"
struct ModuleInstantiationTemplate {
    QString text;
    CodeTemplateSlotList slotMetadata;
};
QString moduleInstanceStem(const QString& moduleName);
ModuleInstantiationTemplate moduleInstantiationTemplateForRecord(
    SemanticIndex* index, const SemanticSymbolRecord& module,
    const QHash<QString, QString>& values = {}, bool explicitParametersOnly = false);
