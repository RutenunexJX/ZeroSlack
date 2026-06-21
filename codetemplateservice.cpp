#include "codetemplateservice.h"

#include <Qt>
#include <memory>

namespace {
std::unique_ptr<CodeTemplateService> s_instance;

CodeTemplateItem makeItem(const QString& token,
                          const QString& label,
                          const QString& description,
                          const QString& defaultValue)
{
    CodeTemplateItem item;
    item.commandToken = token;
    item.label = label;
    item.description = description;
    item.defaultValue = defaultValue;
    return item;
}
}

CodeTemplateService* CodeTemplateService::getInstance()
{
    if (!s_instance)
        s_instance = std::make_unique<CodeTemplateService>();
    return s_instance.get();
}

QList<CodeTemplateItem> CodeTemplateService::catalog() const
{
    return {
        makeItem(QStringLiteral(";;l"), QStringLiteral("logic"), QStringLiteral("logic declaration"), QStringLiteral("logic signal;")),
        makeItem(QStringLiteral(";;w"), QStringLiteral("wire"), QStringLiteral("wire declaration"), QStringLiteral("wire signal;")),
        makeItem(QStringLiteral(";;r"), QStringLiteral("reg"), QStringLiteral("reg declaration"), QStringLiteral("reg signal;")),
        makeItem(QStringLiteral(";;p"), QStringLiteral("parameter"), QStringLiteral("parameter declaration"), QStringLiteral("parameter int NAME = 0;")),
        makeItem(QStringLiteral(";;lp"), QStringLiteral("localparam"), QStringLiteral("localparam declaration"), QStringLiteral("localparam int NAME = 0;")),
        makeItem(QStringLiteral(";;c"), QStringLiteral("assign"), QStringLiteral("continuous assignment"), QStringLiteral("assign lhs = rhs;")),
        makeItem(QStringLiteral(";;a"), QStringLiteral("always"), QStringLiteral("always process"), QStringLiteral("always_comb begin\nend")),
        makeItem(QStringLiteral(";;m"), QStringLiteral("module"), QStringLiteral("module skeleton"), QStringLiteral("module name();\nendmodule")),
        makeItem(QStringLiteral(";;i"), QStringLiteral("interface"), QStringLiteral("interface skeleton"), QStringLiteral("interface name();\nendinterface")),
        makeItem(QStringLiteral(";;t"), QStringLiteral("task"), QStringLiteral("task skeleton"), QStringLiteral("task automatic name();\nendtask")),
        makeItem(QStringLiteral(";;f"), QStringLiteral("function"), QStringLiteral("function skeleton"), QStringLiteral("function automatic void name();\nendfunction")),
        makeItem(QStringLiteral(";;ne"), QStringLiteral("enum type"), QStringLiteral("typedef enum"), QStringLiteral("typedef enum logic [0:0] {\n} name_e;")),
        makeItem(QStringLiteral(";;nsp"), QStringLiteral("packed struct"), QStringLiteral("packed struct type"), QStringLiteral("typedef struct packed {\n} name_t;")),
        makeItem(QStringLiteral(";;ns"), QStringLiteral("unpacked struct"), QStringLiteral("unpacked struct type"), QStringLiteral("typedef struct {\n} name_t;")),
        makeItem(QStringLiteral(";;d"), QStringLiteral("define"), QStringLiteral("define / ifdef block"), QStringLiteral("`define NAME\n`ifdef NAME\n`endif")),
    };
}

QList<CodeTemplateItem> CodeTemplateService::matchingTemplates(
    const QString& commandToken,
    const QString& seedText) const
{
    QList<CodeTemplateItem> result;
    for (CodeTemplateItem item : catalog()) {
        if (!commandToken.isEmpty()
            && item.commandToken.compare(commandToken, Qt::CaseInsensitive) != 0) {
            continue;
        }
        item.insertText = expandTemplate(item.commandToken, seedText);
        item.defaultValue = item.insertText;
        result.append(item);
    }
    return result;
}

CodeTemplateItem CodeTemplateService::templateForCommand(
    const QString& commandToken,
    const QString& seedText) const
{
    const QList<CodeTemplateItem> matches =
        matchingTemplates(commandToken, seedText);
    return matches.isEmpty() ? CodeTemplateItem() : matches.first();
}

QString CodeTemplateService::seededName(
    const QString& seedText,
    const QString& fallback) const
{
    const QString trimmed = seedText.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

QString CodeTemplateService::expandTemplate(
    const QString& commandToken,
    const QString& seedText) const
{
    const QString name = seededName(seedText, QStringLiteral("name"));
    const QString signal = seededName(seedText, QStringLiteral("signal"));
    const QString upper = seededName(seedText, QStringLiteral("NAME")).toUpper();

    if (commandToken == QStringLiteral(";;l"))
        return QStringLiteral("logic %1;").arg(signal);
    if (commandToken == QStringLiteral(";;w"))
        return QStringLiteral("wire %1;").arg(signal);
    if (commandToken == QStringLiteral(";;r"))
        return QStringLiteral("reg %1;").arg(signal);
    if (commandToken == QStringLiteral(";;p"))
        return QStringLiteral("parameter int %1 = 0;").arg(name);
    if (commandToken == QStringLiteral(";;lp"))
        return QStringLiteral("localparam int %1 = 0;").arg(name);
    if (commandToken == QStringLiteral(";;c"))
        return seedText.trimmed().isEmpty()
            ? QStringLiteral("assign lhs = rhs;")
            : QStringLiteral("assign %1 = rhs;").arg(name);
    if (commandToken == QStringLiteral(";;a"))
        return QStringLiteral("always_ff @(posedge clk or negedge rst_n) begin\n"
                              "    if (!rst_n) begin\n"
                              "    end else begin\n"
                              "    end\n"
                              "end");
    if (commandToken == QStringLiteral(";;m"))
        return QStringLiteral("module %1(\n"
                              ");\n"
                              "endmodule").arg(name);
    if (commandToken == QStringLiteral(";;i"))
        return QStringLiteral("interface %1(\n"
                              ");\n"
                              "endinterface").arg(name);
    if (commandToken == QStringLiteral(";;t"))
        return QStringLiteral("task automatic %1();\n"
                              "endtask").arg(name);
    if (commandToken == QStringLiteral(";;f"))
        return QStringLiteral("function automatic void %1();\n"
                              "endfunction").arg(name);
    if (commandToken == QStringLiteral(";;ne"))
        return QStringLiteral("typedef enum logic [0:0] {\n"
                              "} %1_e;").arg(name);
    if (commandToken == QStringLiteral(";;nsp"))
        return QStringLiteral("typedef struct packed {\n"
                              "} %1_t;").arg(name);
    if (commandToken == QStringLiteral(";;ns"))
        return QStringLiteral("typedef struct {\n"
                              "} %1_t;").arg(name);
    if (commandToken == QStringLiteral(";;d"))
        return QStringLiteral("`define %1\n"
                              "`ifdef %1\n"
                              "`endif").arg(upper);

    return QString();
}
