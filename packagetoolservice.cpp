#include "packagetoolservice.h"

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

CodeTemplateItem makePackageTemplate(PackageToolKind kind,
                                     const QString& label,
                                     const QString& description,
                                     const QString& text)
{
    CodeTemplateItem item;
    item.commandToken =
        QStringLiteral("package:%1").arg(PackageToolService::idForKind(kind));
    item.label = label;
    item.description = description;
    item.insertText = text;
    item.defaultValue = text;
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
    return {
        PackageToolKind::Parameter,
        PackageToolKind::Localparam,
        PackageToolKind::TypedefEnum,
        PackageToolKind::TypedefStruct,
        PackageToolKind::TypedefStructPacked,
        PackageToolKind::Function,
    };
}

QString PackageToolService::idForKind(PackageToolKind kind)
{
    switch (kind) {
    case PackageToolKind::Parameter:
        return QStringLiteral("parameter");
    case PackageToolKind::Localparam:
        return QStringLiteral("localparam");
    case PackageToolKind::TypedefEnum:
        return QStringLiteral("typedef_enum");
    case PackageToolKind::TypedefStruct:
        return QStringLiteral("typedef_struct");
    case PackageToolKind::TypedefStructPacked:
        return QStringLiteral("typedef_struct_packed");
    case PackageToolKind::Function:
        return QStringLiteral("function");
    }
    return QString();
}

QString PackageToolService::labelForKind(PackageToolKind kind)
{
    switch (kind) {
    case PackageToolKind::Parameter:
        return QStringLiteral("parameter");
    case PackageToolKind::Localparam:
        return QStringLiteral("localparam");
    case PackageToolKind::TypedefEnum:
        return QStringLiteral("typedef enum");
    case PackageToolKind::TypedefStruct:
        return QStringLiteral("typedef struct");
    case PackageToolKind::TypedefStructPacked:
        return QStringLiteral("typedef struct packed");
    case PackageToolKind::Function:
        return QStringLiteral("function");
    }
    return QString();
}

CodeTemplateItem PackageToolService::templateForKind(PackageToolKind kind) const
{
    CodeTemplateItem item;
    switch (kind) {
    case PackageToolKind::Parameter:
        item = makePackageTemplate(
            kind,
            labelForKind(kind),
            QStringLiteral("package parameter declaration"),
            QStringLiteral("parameter int PARAM = 0;"));
        appendNeedleSlot(item, QStringLiteral("type"), QStringLiteral("int"));
        appendNeedleSlot(item, QStringLiteral("name"), QStringLiteral("PARAM"));
        appendNeedleSlot(item, QStringLiteral("value"), QStringLiteral("0"));
        return item;
    case PackageToolKind::Localparam:
        item = makePackageTemplate(
            kind,
            labelForKind(kind),
            QStringLiteral("package localparam declaration"),
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
            labelForKind(kind),
            QStringLiteral("package typedef enum"),
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
            labelForKind(kind),
            QStringLiteral("package typedef struct"),
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
            labelForKind(kind),
            QStringLiteral("package typedef struct packed"),
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
            labelForKind(kind),
            QStringLiteral("package function declaration"),
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
