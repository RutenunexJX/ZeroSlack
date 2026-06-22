#include "globalcontrolservice.h"

#include "codetemplateservice.h"
#include "projectmodel.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "symboltaxonomy.h"

#include <QFileInfo>
#include <Qt>

namespace {
GlobalControlItem item(GlobalControlItemKind kind,
                       const QString& id,
                       const QString& title,
                       const QString& subtitle)
{
    GlobalControlItem result;
    result.kind = kind;
    result.id = id;
    result.title = title;
    result.subtitle = subtitle;
    return result;
}

bool matchesFilter(const GlobalControlItem& item, const QString& filter)
{
    if (filter.trimmed().isEmpty())
        return true;
    const QString needle = filter.trimmed();
    return item.title.contains(needle, Qt::CaseInsensitive)
        || item.subtitle.contains(needle, Qt::CaseInsensitive)
        || item.filePath.contains(needle, Qt::CaseInsensitive)
        || item.id.contains(needle, Qt::CaseInsensitive);
}

bool isTopLevelSymbol(const SemanticSymbolRecord& record)
{
    switch (record.declarationKind) {
    case SymbolTaxonomy::DeclarationKind::Module:
    case SymbolTaxonomy::DeclarationKind::Interface:
    case SymbolTaxonomy::DeclarationKind::Package:
    case SymbolTaxonomy::DeclarationKind::Typedef:
    case SymbolTaxonomy::DeclarationKind::Struct:
    case SymbolTaxonomy::DeclarationKind::Enum:
        return true;
    default:
        return false;
    }
}

void appendFiltered(QList<GlobalControlItem>* out,
                    const QList<GlobalControlItem>& items,
                    const QString& filter)
{
    for (const GlobalControlItem& item : items) {
        if (matchesFilter(item, filter))
            out->append(item);
    }
}
}

QList<GlobalControlItem> GlobalControlService::query(
    const QString& text,
    ProjectModel* projectModel,
    SemanticIndex* semanticIndex) const
{
    Q_UNUSED(projectModel)
    Q_UNUSED(semanticIndex)

    QList<GlobalControlItem> result;
    appendFiltered(&result, commandItems(), text);
    return result.mid(0, 80);
}

QList<GlobalControlItem> GlobalControlService::commandItems() const
{
    return {
        item(GlobalControlItemKind::Command, QStringLiteral("ow"), QStringLiteral("ow"), QStringLiteral("Open Workspace - choose and alias a workspace")),
        item(GlobalControlItemKind::Command, QStringLiteral("fd"), QStringLiteral("fd"), QStringLiteral("Fold Region - mark a custom fold block in the active editor")),
        item(GlobalControlItemKind::Command, QStringLiteral("fds"), QStringLiteral("fds"), QStringLiteral("Fold Shelf - drag custom fold blocks to or from the shelf")),
    };
}

QList<GlobalControlItem> GlobalControlService::templateItems() const
{
    QList<GlobalControlItem> result;
    for (const CodeTemplateItem& templateItem :
         CodeTemplateService::getInstance()->catalog()) {
        result.append(item(GlobalControlItemKind::Template,
                           templateItem.commandToken,
                           templateItem.commandToken,
                           QStringLiteral("Template - %1").arg(templateItem.description)));
    }
    return result;
}

QList<GlobalControlItem> GlobalControlService::rtlInsightItems() const
{
    return {
        item(GlobalControlItemKind::RtlInsight, QStringLiteral("rtlModuleBrief"), QStringLiteral("Module Brief"), QStringLiteral("RTL Insights")),
        item(GlobalControlItemKind::RtlInsight, QStringLiteral("rtlSignalJourney"), QStringLiteral("Signal Journey"), QStringLiteral("RTL Insights")),
        item(GlobalControlItemKind::RtlInsight, QStringLiteral("rtlClockReset"), QStringLiteral("Clock/Reset Domain Map"), QStringLiteral("RTL Insights")),
        item(GlobalControlItemKind::RtlInsight, QStringLiteral("rtlFsmGraph"), QStringLiteral("FSM Graph"), QStringLiteral("RTL Insights")),
        item(GlobalControlItemKind::RtlInsight, QStringLiteral("rtlSemanticDiff"), QStringLiteral("Semantic Diff"), QStringLiteral("RTL Insights")),
    };
}

QList<GlobalControlItem> GlobalControlService::fileItems(
    ProjectModel* projectModel) const
{
    QList<GlobalControlItem> result;
    if (!projectModel)
        return result;

    const ProjectSnapshot snapshot = projectModel->snapshot();
    for (const QString& filePath : snapshot.systemVerilogFiles) {
        GlobalControlItem fileItem;
        fileItem.kind = GlobalControlItemKind::File;
        fileItem.id = filePath;
        fileItem.title = QFileInfo(filePath).fileName();
        fileItem.subtitle = QStringLiteral("File");
        fileItem.filePath = filePath;
        result.append(fileItem);
    }
    return result;
}

QList<GlobalControlItem> GlobalControlService::symbolItems(
    SemanticIndex* semanticIndex) const
{
    QList<GlobalControlItem> result;
    if (!semanticIndex)
        return result;

    const std::shared_ptr<const SemanticIndexSnapshot> snapshot =
        semanticIndex->snapshot();
    if (!snapshot)
        return result;

    for (const SemanticSymbolRecord& record : snapshot->getSymbolRecords()) {
        if (!isTopLevelSymbol(record))
            continue;
        GlobalControlItem symbolItem;
        symbolItem.kind = GlobalControlItemKind::Symbol;
        symbolItem.id = record.stableKey.toString();
        symbolItem.title = record.name;
        symbolItem.subtitle = QStringLiteral("Symbol");
        symbolItem.filePath = record.location.fileName;
        symbolItem.line = record.location.startLine;
        symbolItem.column = record.location.startColumn;
        result.append(symbolItem);
    }
    return result;
}
