#ifndef EXPOSESIGNALTOTOPSERVICE_H
#define EXPOSESIGNALTOTOPSERVICE_H

#include "editorsemanticcontextservice.h"
#include "semanticindex.h"

#include <rtledit/edit_plan.h>
#include <rtledit/expose_signal_to_top.h>
#include <rtledit/workspace_document_manager.h>

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <string>
#include <vector>

class HierarchyService;
class TabManager;

struct ExposeSignalToTopQuery {
    EditorSemanticContext context;
    QString exportedPortName;
    QSet<QString> workspaceFiles;
};

struct ExposeSignalHierarchyStepView {
    int index = 0;
    QString childInstancePath;
    QString childModule;
    QString childInstance;
    QString parentInstancePath;
    QString parentModule;
    QString sourceFile;
};

enum class ExposeSignalToTopReportStatus {
    Ready,
    Rejected
};

struct ExposeSignalToTopReport {
    ExposeSignalToTopReportStatus status =
        ExposeSignalToTopReportStatus::Rejected;
    rtledit::ExposeSignalFailureReason failureReason =
        rtledit::ExposeSignalFailureReason::InvalidRequest;
    QString message;
    SemanticSymbolRecord signalRecord;
    QString sourceInstancePath;
    QString targetInstancePath;
    QString exportedPortName;
    QList<ExposeSignalHierarchyStepView> hierarchySteps;
    QStringList affectedModules;
    QStringList affectedFiles;
    QStringList affectedInstancePaths;
    QStringList blockers;
    rtledit::ExposeSignalToTopPlanResult planResult;
    rtledit::WorkspaceEditSourceDiff sourceDiff;
    QString renderedDiff;

    bool ready() const
    {
        return status == ExposeSignalToTopReportStatus::Ready
            && planResult.ready() && sourceDiff.built();
    }
};

struct ExposeSignalToTopApplyReport {
    rtledit::PlanApplyResult result;
    QString message;

    bool applied() const { return result.applied(); }
};

// Qt editor adapter for rtleditcore. Snapshots are UTF-8 because core ranges
// use UTF-8 byte columns; individual QTextCursor edits are converted back to
// UTF-16 positions before mutation.
class ZeroSlackWorkspaceDocumentManager final
    : public rtledit::WorkspaceDocumentManager
{
public:
    explicit ZeroSlackWorkspaceDocumentManager(TabManager* tabManager);

    std::optional<rtledit::WorkspaceDocumentSnapshot> snapshot(
        const std::string& filePath) const override;
    bool applyTextEdits(
        const std::string& filePath,
        rtledit::DocumentVersion expectedVersion,
        const std::vector<rtledit::WorkspaceTextEdit>& edits) override;
    bool restoreSnapshot(
        const std::string& filePath,
        const rtledit::WorkspaceDocumentSnapshot& snapshot) override;

private:
    TabManager* tabs = nullptr;
};

class ExposeSignalToTopService
{
public:
    explicit ExposeSignalToTopService(
        SemanticIndex* semanticIndex = nullptr,
        HierarchyService* hierarchyService = nullptr);

    static QString defaultExportedPortName(const QString& signalName);
    bool canOffer(const EditorSemanticContext& context,
                  QString* unavailableReason = nullptr) const;

    ExposeSignalToTopReport plan(
        const ExposeSignalToTopQuery& query,
        rtledit::WorkspaceDocumentManager& documents) const;

    ExposeSignalToTopApplyReport apply(
        const ExposeSignalToTopReport& report,
        rtledit::WorkspaceDocumentManager& documents) const;

private:
    SemanticIndex* index = nullptr;
    HierarchyService* hierarchy = nullptr;

    SemanticIndex* semanticIndex() const;
    HierarchyService* hierarchyService() const;
};

#endif // EXPOSESIGNALTOTOPSERVICE_H
