#ifndef EXPOSESIGNALTOTOPSERVICE_H
#define EXPOSESIGNALTOTOPSERVICE_H

#include "editorsemanticcontextservice.h"
#include "semanticindex.h"

#include <rtledit/edit_plan.h>
#include <rtledit/expose_signal_to_top.h>
#include <rtledit/workspace_edit_transaction.h>

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <string>
#include <vector>

class HierarchyService;

struct ExposeSignalToTopQuery {
    EditorSemanticContext context;
    QString exportedPortName;
    QSet<QString> workspaceFiles;
    // Empty preserves the original active-top behavior. A non-empty value
    // must identify one exact ancestor in the selected elaborated top.
    QString targetAncestorInstancePath;
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
    rtledit::PreparedWorkspaceEditTransaction transaction;
    rtledit::WorkspaceEditSourceDiff sourceDiff;
    QString renderedDiff;

    bool ready() const
    {
        return status == ExposeSignalToTopReportStatus::Ready
            && planResult.ready() && transaction.ready()
            && sourceDiff.built();
    }
};

struct ExposeSignalToTopApplyReport {
    rtledit::PlanApplyResult result;
    rtledit::WorkspaceEditTransactionResult transactionResult;
    QString message;

    bool applied() const
    {
        return transactionResult.status
            == rtledit::TransactionStatus::Applied;
    }
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
