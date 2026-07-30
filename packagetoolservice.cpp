#include "packagetoolservice.h"

#include "actionregistry.h"

#include <QtGlobal>

namespace {
CodeTemplateSlot makeSlot(const QString& name, int start, int length)
{
    CodeTemplateSlot slot;
    slot.name = name;
    slot.start = start;
    slot.length = length;
    return slot;
}

int newlineCountBefore(const QString& text, int offset)
{
    int count = 0;
    const int limit = qBound(0, offset, text.size());
    for (int i = 0; i < limit; ++i) {
        if (text.at(i) == QLatin1Char('\n'))
            ++count;
    }
    return count;
}

int newlineCountInRange(const QString& text, int start, int length)
{
    int count = 0;
    const int rangeStart = qBound(0, start, text.size());
    const int rangeEnd = qBound(rangeStart, start + length, text.size());
    for (int i = rangeStart; i < rangeEnd; ++i) {
        if (text.at(i) == QLatin1Char('\n'))
            ++count;
    }
    return count;
}

void appendNeedleSlot(CodeTemplateItem& item,
                      const QString& name,
                      const QString& needle)
{
    const int start = item.insertText.indexOf(needle);
    if (start < 0)
        return;
    item.templateSlots.append(makeSlot(name, start, needle.size()));
}

QString adapterKeyForPackageToolKind(PackageToolKind kind)
{
    switch (kind) {
    case PackageToolKind::Parameter:
        return QStringLiteral("parameter");
    case PackageToolKind::Localparam:
        return QStringLiteral("localparam");
    case PackageToolKind::TypedefEnum:
        return QStringLiteral("typedefEnum");
    case PackageToolKind::TypedefStruct:
        return QStringLiteral("typedefStruct");
    case PackageToolKind::TypedefStructPacked:
        return QStringLiteral("typedefStructPacked");
    case PackageToolKind::Function:
        return QStringLiteral("function");
    }
    return QString();
}

bool packageToolKindForAdapterKey(const QString& adapterKey,
                                  PackageToolKind* kind)
{
    for (const PackageToolKind candidate :
         {PackageToolKind::Parameter,
          PackageToolKind::Localparam,
          PackageToolKind::TypedefEnum,
          PackageToolKind::TypedefStruct,
          PackageToolKind::TypedefStructPacked,
          PackageToolKind::Function}) {
        if (adapterKeyForPackageToolKind(candidate)
            != adapterKey) {
            continue;
        }
        if (kind)
            *kind = candidate;
        return true;
    }
    return false;
}

const ActionDescriptor* actionForPackageToolKind(
    PackageToolKind kind,
    const ActionAliasDescriptor** resultAlias = nullptr)
{
    const QString adapterKey =
        adapterKeyForPackageToolKind(kind);
    for (const ActionDescriptor* descriptor :
         actionDescriptorsForSurface(
             ActionSurface::PackageTools)) {
        if (!descriptor)
            continue;
        for (const ActionAliasDescriptor& packageAlias :
             descriptor->aliases) {
            if (packageAlias.surface
                    == ActionSurface::PackageTools
                && packageAlias.adapterKey == adapterKey) {
                if (resultAlias)
                    *resultAlias = &packageAlias;
                return descriptor;
            }
        }
    }
    if (resultAlias)
        *resultAlias = nullptr;
    return nullptr;
}

CodeTemplateItem makePackageTemplate(PackageToolKind kind,
                                     const QString& text)
{
    CodeTemplateItem item;
    const ActionAliasDescriptor* packageAlias = nullptr;
    const ActionDescriptor* descriptor =
        actionForPackageToolKind(kind, &packageAlias);
    item.commandToken =
        QStringLiteral("package:%1").arg(PackageToolService::idForKind(kind));
    item.label = packageAlias
        ? packageAlias->label : QString();
    item.description = packageAlias
        ? packageAlias->description : QString();
    item.insertText = text;
    item.defaultValue = text;
    if (descriptor) {
        item.actionId = descriptor->id;
        item.executionRoute = descriptor->executionRoute;
    }
    return item;
}

QString indentMultiline(const QString& text, const QString& indent)
{
    QString result;
    result.reserve(text.size() + indent.size() * 4);
    result += indent;
    for (const QChar ch : text) {
        result += ch;
        if (ch == QLatin1Char('\n'))
            result += indent;
    }
    return result;
}
} // namespace

QList<PackageToolKind> PackageToolService::toolOrder()
{
    QList<PackageToolKind> result;
    for (const ActionDescriptor* descriptor :
         actionDescriptorsForSurface(
             ActionSurface::PackageTools)) {
        if (!descriptor)
            continue;
        const ActionAliasDescriptor* packageAlias =
            findActionAlias(*descriptor,
                            ActionSurface::PackageTools);
        PackageToolKind kind;
        if (packageAlias
            && packageToolKindForAdapterKey(
                packageAlias->adapterKey, &kind)) {
            result.append(kind);
        }
    }
    return result;
}

QString PackageToolService::actionIdForKind(PackageToolKind kind)
{
    const ActionDescriptor* descriptor =
        actionForPackageToolKind(kind);
    return descriptor ? descriptor->id : QString();
}

QString PackageToolService::idForKind(PackageToolKind kind)
{
    const ActionAliasDescriptor* packageAlias = nullptr;
    actionForPackageToolKind(kind, &packageAlias);
    return packageAlias ? packageAlias->token : QString();
}

QString PackageToolService::labelForKind(PackageToolKind kind)
{
    const ActionAliasDescriptor* packageAlias = nullptr;
    actionForPackageToolKind(kind, &packageAlias);
    return packageAlias ? packageAlias->label : QString();
}

CodeTemplateItem PackageToolService::templateForKind(PackageToolKind kind) const
{
    CodeTemplateItem item;
    switch (kind) {
    case PackageToolKind::Parameter:
        item = makePackageTemplate(
            kind,
            QStringLiteral("parameter int PARAM = 0;"));
        appendNeedleSlot(item, QStringLiteral("type"), QStringLiteral("int"));
        appendNeedleSlot(item, QStringLiteral("name"), QStringLiteral("PARAM"));
        appendNeedleSlot(item, QStringLiteral("value"), QStringLiteral("0"));
        return item;
    case PackageToolKind::Localparam:
        item = makePackageTemplate(
            kind,
            QStringLiteral("localparam int LOCAL_PARAM = 0;"));
        appendNeedleSlot(item, QStringLiteral("type"), QStringLiteral("int"));
        appendNeedleSlot(item,
                         QStringLiteral("name"),
                         QStringLiteral("LOCAL_PARAM"));
        appendNeedleSlot(item, QStringLiteral("value"), QStringLiteral("0"));
        return item;
    case PackageToolKind::TypedefEnum:
        item = makePackageTemplate(
            kind,
            QStringLiteral("typedef enum logic [1:0] {\n"
                           "    IDLE,\n"
                           "    BUSY\n"
                           "} state_e;"));
        appendNeedleSlot(item,
                         QStringLiteral("state_items"),
                         QStringLiteral("IDLE,\n    BUSY"));
        appendNeedleSlot(item,
                         QStringLiteral("enum_name"),
                         QStringLiteral("state_e"));
        return item;
    case PackageToolKind::TypedefStruct:
        item = makePackageTemplate(
            kind,
            QStringLiteral("typedef struct {\n"
                           "    logic field_name;\n"
                           "} type_name_t;"));
        appendNeedleSlot(item,
                         QStringLiteral("field_type"),
                         QStringLiteral("logic"));
        appendNeedleSlot(item,
                         QStringLiteral("field_name"),
                         QStringLiteral("field_name"));
        appendNeedleSlot(item,
                         QStringLiteral("type_name"),
                         QStringLiteral("type_name_t"));
        return item;
    case PackageToolKind::TypedefStructPacked:
        item = makePackageTemplate(
            kind,
            QStringLiteral("typedef struct packed {\n"
                           "    logic field_name;\n"
                           "} type_name_t;"));
        appendNeedleSlot(item,
                         QStringLiteral("field_type"),
                         QStringLiteral("logic"));
        appendNeedleSlot(item,
                         QStringLiteral("field_name"),
                         QStringLiteral("field_name"));
        appendNeedleSlot(item,
                         QStringLiteral("type_name"),
                         QStringLiteral("type_name_t"));
        return item;
    case PackageToolKind::Function:
        item = makePackageTemplate(
            kind,
            QStringLiteral("function automatic int function_name(input int arg);\n"
                           "    return '0;\n"
                           "endfunction"));
        appendNeedleSlot(item, QStringLiteral("return_type"), QStringLiteral("int"));
        appendNeedleSlot(item,
                         QStringLiteral("function_name"),
                         QStringLiteral("function_name"));
        appendNeedleSlot(item,
                         QStringLiteral("arguments"),
                         QStringLiteral("input int arg"));
        appendNeedleSlot(item, QStringLiteral("body"), QStringLiteral("return '0;"));
        return item;
    }
    return item;
}

CodeTemplateItem PackageToolService::templateForInsertion(
    PackageToolKind kind,
    const QString& lineIndent,
    bool insertAfterLine) const
{
    const CodeTemplateItem base = templateForKind(kind);

    CodeTemplateItem expanded = base;
    const QString leading = insertAfterLine ? QStringLiteral("\n") : QString();
    const QString trailing = insertAfterLine ? QString() : QStringLiteral("\n");
    expanded.insertText =
        leading + indentMultiline(base.insertText, lineIndent) + trailing;
    expanded.defaultValue = expanded.insertText;
    expanded.templateSlots.clear();

    for (const CodeTemplateSlot& slot : base.templateSlots) {
        const int indentsBeforeSlot =
            1 + newlineCountBefore(base.insertText, slot.start);
        const int indentsInsideSlot =
            newlineCountInRange(base.insertText, slot.start, slot.length);
        expanded.templateSlots.append(
            makeSlot(slot.name,
                     leading.size() + slot.start
                         + lineIndent.size() * indentsBeforeSlot,
                     slot.length + lineIndent.size() * indentsInsideSlot));
    }

    return expanded;
}
