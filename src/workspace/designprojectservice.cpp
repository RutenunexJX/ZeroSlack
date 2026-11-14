#include "designprojectservice.h"
#include "../simulation/simdock/core/workspace.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QSettings>
#include <QUuid>

namespace {
QJsonObject encode(const QString& workspace, const QList<DesignHierarchyRoot>& roots, const QString& active, bool migrated)
{
    QJsonArray items;
    for (const auto& root : roots) items.append(QJsonObject{{"id", root.projectId}, {"name", root.name},
        {"module", root.module}, {"file", root.file.isEmpty() ? QString() : QDir(workspace).relativeFilePath(root.file)},
        {"expanded", root.expanded}});
    return {{"roots", items}, {"activeProject", active}, {"legacyImported", true}, {"explicitTopMigrated", migrated}};
}
}
DesignProjectService::DesignProjectService(QObject* parent) : QObject(parent)
{ setObjectName(QStringLiteral("designProjectService")); }
QString DesignProjectService::setWorkspace(const QString& path)
{
    const auto root = path.isEmpty() ? QString() : QFileInfo(path).canonicalFilePath();
    if (!path.isEmpty() && (root.isEmpty() || !QFileInfo(root).isDir())) return tr("The workspace does not exist.");
    if (root == m_root) return {};
    if (root.isEmpty()) {
        m_root.clear(); m_active.clear(); m_roots.clear(); m_sourceTop = {}; m_store = {}; m_explicitTopMigrated = false;
        emit designProjectsChanged(); return {};
    }
    // A broken compatibility record must not retain another workspace's view
    // state or block automatic semantic browsing. Keep the file untouched.
    m_root = root; m_active.clear(); m_roots.clear(); m_sourceTop = {}; m_store = {}; m_explicitTopMigrated = false;
    simdock::WorkspaceStateFile store;
    QString error, active;
    QList<DesignHierarchyRoot> roots;
    bool migrated = false;
    if (!store.open(root, QStringLiteral("design"), QStringLiteral("zeroslack.design/v1"), &error)) return error;
    if (store.exists()) {
        const auto state = store.value();
        if (!state.value("roots").isArray() || !state.value("activeProject").isString())
            return tr("Invalid Design state; the existing record was preserved.");
        QSet<QString> identities;
        for (const auto& value : state.value("roots").toArray()) {
            const auto item = value.toObject();
            const auto id = item.value("id").toString(), file = item.value("file").toString();
            const auto module = item.value("module").toString();
            if (id.isEmpty() || module.isEmpty() || identities.contains(id)
                || (!file.isEmpty() && (QDir::isAbsolutePath(file) || !simdock::insideWorkspace(root, QDir(root).filePath(file)))))
                return tr("Invalid Design state; the existing record was preserved.");
            identities.insert(id);
            roots << DesignHierarchyRoot{id, item.value("name").toString(), module,
                file.isEmpty() ? QString() : QDir(root).filePath(file), item.value("expanded").toBool(true)};
        }
        active = state.value("activeProject").toString();
        if (!active.isEmpty() && !identities.contains(active)) return tr("The active Design project is absent from the saved roots.");
        migrated = state.value("explicitTopMigrated").toBool();
    } else {
        QStringList errors;
        QSet<QString> ambiguousIds;
        const auto projects = simdock::loadProjects(root, &errors, false, &ambiguousIds);
        const auto key = QStringLiteral("integrations/simdock/workspaces/") + QString::fromLatin1(
            QCryptographicHash::hash(QDir::fromNativeSeparators(root).toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex());
        QSettings settings;
        const auto previousActive = settings.value(key + "/activeProject").toString();
        for (const auto& project : projects) {
            if (project.designTopModule.isEmpty()) continue;
            if (ambiguousIds.contains(project.id))
                return tr("Conflicting legacy Design project identity: %1. Original records were preserved.").arg(project.id);
            roots << DesignHierarchyRoot{project.id, project.name, project.designTopModule,
                project.designTopFile.isEmpty() ? QString() : QDir(root).filePath(project.designTopFile),
                settings.value(key + "/expanded/" + project.id, true).toBool()};
            if (project.id == previousActive) active = project.id;
        }
        migrated = settings.value(key + "/explicitTopMigrated", false).toBool();
        auto record = encode(root, roots, active, migrated);
        record.insert("migrationWarnings", QJsonArray::fromStringList(errors));
        // Legacy projects are compatibility input only. Browsing does not
        // create or grow a persisted list of allowed roots.
    }
    const auto record = store.value();
    if (record.contains("sourceTop")) {
        const auto target = record.value("sourceTop").toObject();
        const auto file = target.value("file").toString(), module = target.value("module").toString();
        if (!record.value("sourceTop").isObject() || file.isEmpty() != module.isEmpty()
            || (!file.isEmpty() && (QDir::isAbsolutePath(file) || !simdock::insideWorkspace(root, QDir(root).filePath(file)))))
            return tr("Invalid source top; the existing record was preserved.");
        m_sourceTop = {file.isEmpty() ? QString() : QDir(root).filePath(file), module};
    } else {
        // Only a file-bound, explicitly active legacy root identifies a source top.
        for (const auto& item : roots) if (item.projectId == active && !item.file.isEmpty())
            m_sourceTop = {item.file, item.module};
    }
    m_root = root; m_roots = roots; m_active = active; m_explicitTopMigrated = migrated; m_store = std::move(store);
    emit designProjectsChanged();
    return {};
}
QString DesignProjectService::setSourceTop(const QString& file, const QString& module)
{
    if (m_root.isEmpty()) return tr("Open a workspace before selecting a source top.");
    if (file.isEmpty() != module.trimmed().isEmpty()
        || (!file.isEmpty() && !simdock::insideWorkspace(m_root, file)))
        return tr("Choose a module definition in this workspace.");
    const QPair<QString, QString> next(file.isEmpty() ? QString() : simdock::resolvedPath(file), module.trimmed());
    simdock::WorkspaceStateFile store;
    QString error;
    if (!store.open(m_root, QStringLiteral("design"), QStringLiteral("zeroslack.design/v1"), &error)) return error;
    auto record = store.value();
    if (!record.contains("roots")) record.insert("roots", QJsonArray());
    if (!record.contains("activeProject")) record.insert("activeProject", QString());
    record.insert("sourceTop", QJsonObject{{"file", next.first.isEmpty() ? QString() : QDir(m_root).relativeFilePath(next.first)}, {"module", next.second}});
    if (!store.save(record, &error)) return error;
    m_store = std::move(store); m_sourceTop = next;
    emit designProjectsChanged();
    return {};
}
QString DesignProjectService::commit(const QList<DesignHierarchyRoot>& roots, const QString& active, bool migrated)
{
    if (m_root.isEmpty()) return tr("Open a workspace before changing Design projects.");
    QString error;
    auto record = m_store.value();
    const auto fields = encode(m_root, roots, active, migrated);
    for (auto it = fields.begin(); it != fields.end(); ++it) record.insert(it.key(), it.value());
    if (!m_store.save(record, &error)) return error;
    const auto previous = m_roots;
    bool structureChanged = previous.size() != roots.size() || m_active != active;
    for (int i = 0; !structureChanged && i < roots.size(); ++i) {
        auto old = previous[i]; old.expanded = roots[i].expanded;
        structureChanged = old != roots[i];
    }
    m_roots = roots; m_active = active; m_explicitTopMigrated = migrated;
    if (structureChanged) emit designProjectsChanged();
    else for (int i = 0; i < roots.size(); ++i)
        if (previous[i].expanded != roots[i].expanded) emit designExpansionChanged(roots[i].projectId, roots[i].expanded);
    return {};
}
QString DesignProjectService::activateProject(const QString& id)
{
    if (id == m_active) return {};
    bool found = id.isEmpty();
    for (const auto& root : m_roots) found |= root.projectId == id;
    return found ? commit(m_roots, id, m_explicitTopMigrated) : tr("The Design project no longer exists.");
}
QString DesignProjectService::addDesignProject(const QString& file, const QString& module, bool activate)
{
    if (m_root.isEmpty() || module.isEmpty()) return tr("Open a workspace and choose a module first.");
    if (!file.isEmpty() && !simdock::insideWorkspace(m_root, file)) return tr("The design module must belong to this workspace.");
    const auto path = file.isEmpty() ? QString() : simdock::resolvedPath(file);
    auto roots = m_roots;
    for (auto& root : roots) if (root.module == module && (root.file == path || root.file.isEmpty())) {
        root.file = path;
        return commit(roots, activate ? root.projectId : m_active, m_explicitTopMigrated);
    }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    roots << DesignHierarchyRoot{id, module, module, path, true};
    return commit(roots, activate ? id : m_active, m_explicitTopMigrated);
}
QString DesignProjectService::removeDesignRoot(const QString& id)
{
    auto roots = m_roots;
    if (!roots.removeIf([&](const auto& root) { return root.projectId == id; })) return tr("The Design project no longer exists.");
    return commit(roots, m_active == id ? QString() : m_active, m_explicitTopMigrated);
}
QString DesignProjectService::setDesignExpanded(const QString& id, bool expanded)
{
    auto roots = m_roots;
    for (auto& root : roots) if (root.projectId == id) {
        if (root.expanded == expanded) return {};
        root.expanded = expanded;
        return commit(roots, m_active, m_explicitTopMigrated);
    }
    return tr("The Design project no longer exists.");
}
QString DesignProjectService::markDesignMigrationCompleted()
{ return m_explicitTopMigrated ? QString() : commit(m_roots, m_active, true); }

QString DesignProjectService::saveViewState(const QString& workspace, const QJsonObject& state)
{
    if (workspace.isEmpty() || state.value("version").toInt() != 1) return {};
    simdock::WorkspaceStateFile store;
    QString error;
    if (!store.open(workspace, QStringLiteral("design"), QStringLiteral("zeroslack.design/v1"), &error)) return error;
    auto record = store.value();
    // A semantic publication restores the same view state. Rewriting that
    // record would notify the workspace watcher and trigger another analysis.
    if (store.exists() && record.value("viewState").toObject() == state) {
        if (QFileInfo(workspace).canonicalFilePath() == m_root) m_store = std::move(store);
        return {};
    }
    // Preserve all old fields, including roots/activeProject and fields written
    // by newer versions. A malformed file is never silently replaced.
    if (!record.contains("roots")) record.insert("roots", QJsonArray());
    if (!record.contains("activeProject")) record.insert("activeProject", QString());
    record.insert("viewState", state);
    if (!store.save(record, &error)) return error;
    if (QFileInfo(workspace).canonicalFilePath() == m_root) m_store = std::move(store);
    return {};
}
