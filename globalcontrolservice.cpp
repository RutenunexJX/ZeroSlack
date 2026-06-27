#include "globalcontrolservice.h"

#include "codetemplateservice.h"
#include "projectmodel.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "symboltaxonomy.h"

#include <QFileInfo>
#include <Qt>

namespace {
QString normalizedQuery(const QString& text)
{
    return text.simplified();
}

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

QList<GlobalControlItem> rootDomainItems()
{
    return {
        item(GlobalControlItemKind::Domain,
             QStringLiteral("ow"),
             QStringLiteral("ow"),
             QStringLiteral("Workspace")),
        item(GlobalControlItemKind::Domain,
             QStringLiteral("fd"),
             QStringLiteral("fd"),
             QStringLiteral("Fold")),
    };
}

QList<GlobalControlItem> foldDomainItems(const QString& filter)
{
    QList<GlobalControlItem> items = {
        item(GlobalControlItemKind::Command,
             QStringLiteral("fd r"),
             QStringLiteral("fd r"),
             QStringLiteral("Fold Region - mark a custom fold block in the active editor")),
        item(GlobalControlItemKind::Command,
             QStringLiteral("fd s"),
             QStringLiteral("fd s"),
             QStringLiteral("Fold Shelf - drag custom fold blocks to or from the shelf")),
    };
    QList<GlobalControlItem> result;
    appendFiltered(&result, items, filter);
    return result;
}

QList<GlobalControlItem> workspaceDomainItems(const QString& query)
{
    const QList<GlobalControlItem> baseItems = {
        item(GlobalControlItemKind::Command,
             QStringLiteral("ow 1"),
             QStringLiteral("ow 1"),
             QStringLiteral("Open 1 workspace")),
        item(GlobalControlItemKind::Command,
             QStringLiteral("ow 2"),
             QStringLiteral("ow 2"),
             QStringLiteral("Open 2 workspaces")),
        item(GlobalControlItemKind::Command,
             QStringLiteral("ow r"),
             QStringLiteral("ow r"),
             QStringLiteral("Recent Workspaces")),
    };

    const QStringList parts = query.split(QLatin1Char(' '),
                                         Qt::SkipEmptyParts);
    if (parts.size() >= 2) {
        const QString argument = parts.at(1);
        if (QStringLiteral("r").startsWith(argument, Qt::CaseInsensitive)) {
            QList<GlobalControlItem> result;
            appendFiltered(&result, baseItems, query);
            return result;
        }

        bool ok = false;
        const int count = argument.toInt(&ok);
        if (ok && count > 0) {
            return {
                item(GlobalControlItemKind::Command,
                     QStringLiteral("ow %1").arg(count),
                     QStringLiteral("ow %1").arg(count),
                     QStringLiteral("Open %1 workspace%2")
                         .arg(count)
                         .arg(count == 1 ? QString() : QStringLiteral("s"))),
            };
        }
        return {
            item(GlobalControlItemKind::Domain,
                 QStringLiteral("ow"),
                 QStringLiteral("ow <num>"),
                 QStringLiteral("Workspace - enter how many workspaces to open")),
        };
    }

    return baseItems;
}
}

QList<GlobalControlItem> GlobalControlService::query(
    const QString& text,
    ProjectModel* projectModel,
    SemanticIndex* semanticIndex) const
{
    Q_UNUSED(projectModel)
    Q_UNUSED(semanticIndex)

    const QString queryText = normalizedQuery(text);
    if (queryText.isEmpty())
        return rootDomainItems();

    if (queryText == QStringLiteral("fd")
        || queryText.startsWith(QStringLiteral("fd "))) {
        return foldDomainItems(queryText);
    }
    if (queryText == QStringLiteral("ow")
        || queryText.startsWith(QStringLiteral("ow "))) {
        return workspaceDomainItems(queryText);
    }

    QList<GlobalControlItem> result;
    appendFiltered(&result, rootDomainItems(), queryText);
    return result.mid(0, 80);
}

QList<GlobalControlItem> GlobalControlService::commandItems() const
{
    return {
        item(GlobalControlItemKind::Domain, QStringLiteral("ow"), QStringLiteral("ow"), QStringLiteral("Workspace")),
        item(GlobalControlItemKind::Domain, QStringLiteral("fd"), QStringLiteral("fd"), QStringLiteral("Fold")),
        item(GlobalControlItemKind::Command, QStringLiteral("ow 1"), QStringLiteral("ow 1"), QStringLiteral("Open 1 workspace")),
        item(GlobalControlItemKind::Command, QStringLiteral("ow 2"), QStringLiteral("ow 2"), QStringLiteral("Open 2 workspaces")),
        item(GlobalControlItemKind::Command, QStringLiteral("ow r"), QStringLiteral("ow r"), QStringLiteral("Recent Workspaces")),
        item(GlobalControlItemKind::Command, QStringLiteral("fd r"), QStringLiteral("fd r"), QStringLiteral("Fold Region - mark a custom fold block in the active editor")),
        item(GlobalControlItemKind::Command, QStringLiteral("fd s"), QStringLiteral("fd s"), QStringLiteral("Fold Shelf - drag custom fold blocks to or from the shelf")),
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
