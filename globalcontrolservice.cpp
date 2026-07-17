#include "globalcontrolservice.h"

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
        || item.id.contains(needle, Qt::CaseInsensitive);
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

QList<GlobalControlItem> workspaceSessionDomainItems(const QString& query)
{
    const QList<GlobalControlItem> sessionItems = {
        item(GlobalControlItemKind::Command,
             QStringLiteral("ow s save"),
             QStringLiteral("ow s save"),
             QStringLiteral("Workspace Session - save current workspace state")),
        item(GlobalControlItemKind::Command,
             QStringLiteral("ow s restore"),
             QStringLiteral("ow s restore"),
             QStringLiteral("Workspace Session - restore saved workspace state")),
        item(GlobalControlItemKind::Command,
             QStringLiteral("ow s clean"),
             QStringLiteral("ow s clean"),
             QStringLiteral("Workspace Session - ignore saved state for this activation")),
    };

    const QStringList parts = query.split(QLatin1Char(' '),
                                         Qt::SkipEmptyParts);
    QString filter = query;
    if (parts.size() >= 3) {
        const QString action = parts.at(2);
        if (QStringLiteral("w").startsWith(action, Qt::CaseInsensitive)
            || QStringLiteral("save").startsWith(action, Qt::CaseInsensitive)) {
            return {sessionItems.at(0)};
        }
        if (QStringLiteral("r").startsWith(action, Qt::CaseInsensitive)
            || QStringLiteral("restore").startsWith(action, Qt::CaseInsensitive)) {
            return {sessionItems.at(1)};
        }
        if (QStringLiteral("c").startsWith(action, Qt::CaseInsensitive)
            || QStringLiteral("clean").startsWith(action, Qt::CaseInsensitive)) {
            return {sessionItems.at(2)};
        }
        filter = action;
    }

    QList<GlobalControlItem> result;
    appendFiltered(&result, sessionItems, filter);
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
        item(GlobalControlItemKind::Domain,
             QStringLiteral("ow s"),
             QStringLiteral("ow s"),
             QStringLiteral("Workspace Session - save, restore, or clean")),
    };

    const QStringList parts = query.split(QLatin1Char(' '),
                                         Qt::SkipEmptyParts);
    if (parts.size() >= 2) {
        const QString argument = parts.at(1);
        if (QStringLiteral("s").startsWith(argument, Qt::CaseInsensitive)
            || argument.compare(QStringLiteral("session"),
                                Qt::CaseInsensitive) == 0) {
            return workspaceSessionDomainItems(query);
        }
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
    const QString& text) const
{
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
