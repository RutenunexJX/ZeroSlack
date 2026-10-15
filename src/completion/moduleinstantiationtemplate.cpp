#include "moduleinstantiationtemplate.h"
#include <QSet>
#include <algorithm>

QString moduleInstanceStem(const QString& moduleName)
{
    QString stem;
    for (const QChar ch : moduleName) {
        if (ch.isLetterOrNumber()
            || ch == QLatin1Char('_')
            || ch == QLatin1Char('$')) {
            stem.append(ch);
        } else if (!stem.endsWith(QLatin1Char('_'))) {
            stem.append(QLatin1Char('_'));
        }
    }

    while (stem.startsWith(QLatin1Char('_')))
        stem.remove(0, 1);
    while (stem.endsWith(QLatin1Char('_')))
        stem.chop(1);
    if (stem.isEmpty())
        stem = QStringLiteral("module");
    if (stem.at(0).isDigit())
        stem.prepend(QStringLiteral("module_"));
    return stem;
}

QString simpleModuleInstantiationText(const QString& moduleName)
{
    return QStringLiteral("%1 u_%2 (\n);")
        .arg(moduleName, moduleInstanceStem(moduleName));
}

bool moduleMemberOrderLess(const SemanticSymbolRecord& left,
                           const SemanticSymbolRecord& right)
{
    if (left.compilationUnitSourceOrder && right.compilationUnitSourceOrder) {
        if (left.compilationUnitSourceOrder != right.compilationUnitSourceOrder)
            return left.compilationUnitSourceOrder < right.compilationUnitSourceOrder;
        if (left.compilationUnitSourceOffset != right.compilationUnitSourceOffset)
            return left.compilationUnitSourceOffset < right.compilationUnitSourceOffset;
        if (left.location.fileName != right.location.fileName)
            return left.localHandle < right.localHandle;
    }
    if (left.location.startLine != right.location.startLine)
        return left.location.startLine < right.location.startLine;
    if (left.location.startColumn != right.location.startColumn)
        return left.location.startColumn < right.location.startColumn;
    if (left.location.position != right.location.position)
        return left.location.position < right.location.position;
    if (left.localHandle != right.localHandle)
        return left.localHandle < right.localHandle;
    return QString::compare(left.name, right.name, Qt::CaseInsensitive) < 0;
}

bool isModuleInstantiationParameterRecord(
    const SemanticSymbolRecord& record,
    const QString& moduleName)
{
    return record.owner.name == moduleName
        && record.declarationKind == SymbolTaxonomy::DeclarationKind::Parameter
        && record.usageRole == SymbolTaxonomy::SymbolUsageRole::Declaration
        && !record.name.isEmpty();
}

bool isModuleInstantiationPortRecord(
    const SemanticSymbolRecord& record,
    const QString& moduleName)
{
    return record.owner.name == moduleName
        && record.declarationKind == SymbolTaxonomy::DeclarationKind::Port
        && record.usageRole == SymbolTaxonomy::SymbolUsageRole::Declaration
        && !record.name.isEmpty();
}

QList<SemanticSymbolRecord> recordsForSelectedModule(
    SemanticIndex* semanticIndex,
    const SemanticSymbolRecord& moduleRecord)
{
    return semanticIndex ? semanticIndex->getDeclarationMemberRecords(moduleRecord)
                         : QList<SemanticSymbolRecord>{};
}

QList<SemanticSymbolRecord> uniqueOrderedModuleMembers(
    QList<SemanticSymbolRecord> records)
{
    std::stable_sort(records.begin(), records.end(), moduleMemberOrderLess);

    QList<SemanticSymbolRecord> result;
    QSet<QString> seenNames;
    for (const SemanticSymbolRecord& record : records) {
        const QString key = record.name;
        if (key.isEmpty() || seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(record);
    }
    return result;
}

CodeTemplateSlot instantiationSlot(const QString& name, int start, int length)
{
    CodeTemplateSlot slot;
    slot.name = name;
    slot.start = start;
    slot.length = length;
    return slot;
}

ModuleInstantiationTemplate moduleInstantiationTemplateForRecord(
    SemanticIndex* semanticIndex,
    const SemanticSymbolRecord& moduleRecord, const QHash<QString, QString>& values)
{
    ModuleInstantiationTemplate result;
    const QString moduleName = moduleRecord.name;
    result.text = simpleModuleInstantiationText(moduleName);
    if (!semanticIndex || moduleName.isEmpty())
        return result;

    QList<SemanticSymbolRecord> parameters;
    QList<SemanticSymbolRecord> ports;
    const QList<SemanticSymbolRecord> memberRecords =
        recordsForSelectedModule(semanticIndex, moduleRecord);
    for (const SemanticSymbolRecord& record : memberRecords) {
        if (isModuleInstantiationParameterRecord(record, moduleName)) {
            parameters.append(record);
        } else if (isModuleInstantiationPortRecord(record, moduleName)) {
            ports.append(record);
        }
    }

    parameters = uniqueOrderedModuleMembers(parameters);
    ports = uniqueOrderedModuleMembers(ports);

    result.text.clear();
    result.slotMetadata.clear();
    const QString instanceName =
        values.value("instance", QStringLiteral("u_%1").arg(moduleInstanceStem(moduleName)));
    int instanceSlotStart = -1;

    result.text.append(moduleName);
    if (!parameters.isEmpty()) {
        result.text.append(QStringLiteral(" #(\n"));
        for (int i = 0; i < parameters.size(); ++i) {
            const QString name = parameters.at(i).name;
            result.text.append(QStringLiteral("    .%1(").arg(name));
            const int slotStart = result.text.size();
            const QString value = values.value("parameter:" + name, name);
            result.text.append(value);
            result.slotMetadata.append(instantiationSlot(
                QStringLiteral("parameter:%1").arg(name),
                slotStart,
                value.size()));
            result.text.append(i + 1 == parameters.size()
                                   ? QStringLiteral(")\n")
                                   : QStringLiteral("),\n"));
        }
        result.text.append(QStringLiteral(") "));
    } else {
        result.text.append(QLatin1Char(' '));
    }

    instanceSlotStart = result.text.size();
    result.text.append(instanceName);
    result.text.append(QStringLiteral(" (\n"));
    for (int i = 0; i < ports.size(); ++i) {
        const QString name = ports.at(i).name;
        result.text.append(QStringLiteral("    .%1(").arg(name));
        const int slotStart = result.text.size();
        const QString value = values.value("port:" + name, name);
        result.text.append(value);
        result.slotMetadata.append(instantiationSlot(
            QStringLiteral("port:%1").arg(name),
            slotStart,
            value.size()));
        result.text.append(i + 1 == ports.size()
                               ? QStringLiteral(")\n")
                               : QStringLiteral("),\n"));
    }
    result.text.append(QStringLiteral(");"));
    result.slotMetadata.prepend(instantiationSlot(QStringLiteral("instance"),
                                                  instanceSlotStart,
                                                  instanceName.size()));
    return result;
}
