#include "rtlinsightspanelcoordinator.h"

#include "activitylogservice.h"
#include "clockresetdomainservice.h"
#include "fsmgraphservice.h"
#include "modulebriefservice.h"
#include "semanticdiffservice.h"
#include "semanticpanelutils.h"
#include "signaljourneyservice.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include <exception>
#include <utility>

namespace {

QTreeWidgetItem* createGroupItem(QTreeWidget* tree,
                                 const QString& title,
                                 int count)
{
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, SemanticPanelUtils::countLabel(title, count));
    return item;
}

QTreeWidgetItem* createChildItem(QTreeWidgetItem* parent,
                                 const QString& section,
                                 const QString& name,
                                 const QString& detail,
                                 const QString& fileName,
                                 int line,
                                 int column,
                                 const QString& fileDisplayName = QString(),
                                 const QString& lineDisplayName = QString())
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, section);
    item->setText(1, name);
    item->setText(2, detail);
    item->setText(3, fileDisplayName.isEmpty()
                         ? QFileInfo(fileName).fileName()
                         : fileDisplayName);
    item->setText(4, lineDisplayName.isEmpty()
                         ? (line > 0 ? QString::number(line) : QString())
                         : lineDisplayName);
    item->setToolTip(1, name);
    item->setToolTip(2, detail);
    item->setToolTip(3, fileName);
    item->setData(0, Qt::UserRole, fileName);
    item->setData(0, Qt::UserRole + 1, line);
    item->setData(0, Qt::UserRole + 2, column);
    return item;
}

void appendSymbolGroup(QTreeWidget* tree,
                       const QString& title,
                       const QList<ModuleBriefSymbolRow>& rows)
{
    QTreeWidgetItem* group = createGroupItem(tree, title, rows.size());
    for (const ModuleBriefSymbolRow& row : rows) {
        createChildItem(group,
                        row.sectionDisplayName,
                        row.symbolDisplayName,
                        row.detailDisplayName,
                        row.codeLink.fileName,
                        row.codeLink.line,
                        row.codeLink.column,
                        row.codeLink.fileDisplayName,
                        row.codeLink.lineDisplayName);
    }
}

void appendDiagnostics(QTreeWidget* tree,
                       const QList<ModuleBriefDiagnosticRow>& diagnostics)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Diagnostics"),
                                            diagnostics.size());
    for (const ModuleBriefDiagnosticRow& row : diagnostics) {
        const SemanticDiagnostic& diagnostic = row.diagnostic;
        QTreeWidgetItem* diagnosticItem =
            createChildItem(group,
                            row.severityDisplayName,
                            diagnostic.message,
                            row.detailDisplayName,
                            row.codeLink.fileName,
                            row.codeLink.line,
                            row.codeLink.column,
                            row.codeLink.fileDisplayName,
                            row.codeLink.lineDisplayName);
        createChildItem(diagnosticItem,
                        QStringLiteral("Source Role"),
                        row.sourceRoleDisplayName,
                        row.severityDisplayName,
                        row.codeLink.fileName,
                        row.codeLink.line,
                        row.codeLink.column,
                        row.codeLink.fileDisplayName,
                        row.codeLink.lineDisplayName);
    }
}

void appendContextRows(QTreeWidget* tree,
                       const QList<ModuleBriefContextRow>& rows)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Context"),
                                            rows.size());
    for (const ModuleBriefContextRow& row : rows) {
        QTreeWidgetItem* context =
            createChildItem(group,
                            row.sectionDisplayName,
                            row.symbolDisplayName,
                            row.detailDisplayName,
                            row.codeLink.fileName,
                            row.codeLink.line,
                            row.codeLink.column,
                            row.codeLink.fileDisplayName,
                            row.codeLink.lineDisplayName);
        createChildItem(context,
                        QStringLiteral("Kind"),
                        row.contextKindDisplayName,
                        row.sectionDisplayName,
                        row.codeLink.fileName,
                        row.codeLink.line,
                        row.codeLink.column,
                        row.codeLink.fileDisplayName,
                        row.codeLink.lineDisplayName);
        createChildItem(context,
                        QStringLiteral("Type"),
                        row.symbolTypeDisplayName,
                        row.detailDisplayName,
                        row.codeLink.fileName,
                        row.codeLink.line,
                        row.codeLink.column,
                        row.codeLink.fileDisplayName,
                        row.codeLink.lineDisplayName);
        createChildItem(context,
                        QStringLiteral("Source Role"),
                        row.sourceRoleDisplayName,
                        row.sectionDisplayName,
                        row.codeLink.fileName,
                        row.codeLink.line,
                        row.codeLink.column,
                        row.codeLink.fileDisplayName,
                        row.codeLink.lineDisplayName);
    }
}

void appendRelationshipSummary(
    QTreeWidget* tree,
    const ModuleBriefRelationshipSummary& summary,
    const QList<ModuleBriefRelationshipEvidenceRow>& evidenceRows)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Relationships"),
                                            summary.totalCount);
    for (const ModuleBriefRelationshipEvidenceRow& row : evidenceRows) {
        QTreeWidgetItem* relationship =
            createChildItem(group,
                            row.directionDisplayName,
                            row.peerDisplayName,
                            row.detailDisplayName,
                            row.peerCodeLink.fileName,
                            row.peerCodeLink.line,
                            row.peerCodeLink.column,
                            row.peerCodeLink.fileDisplayName,
                            row.peerCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("From"),
                        row.fromSymbolDisplayName,
                        row.typeDisplayName,
                        row.fromCodeLink.fileName,
                        row.fromCodeLink.line,
                        row.fromCodeLink.column,
                        row.fromCodeLink.fileDisplayName,
                        row.fromCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("To"),
                        row.toSymbolDisplayName,
                        row.typeDisplayName,
                        row.toCodeLink.fileName,
                        row.toCodeLink.line,
                        row.toCodeLink.column,
                        row.toCodeLink.fileDisplayName,
                        row.toCodeLink.lineDisplayName);
    }
    for (const ModuleBriefRelationshipRow& row : summary.rows) {
        createChildItem(group,
                        row.directionDisplayName,
                        row.typeDisplayName,
                        row.detailDisplayName,
                        QString(),
                        0,
                        0);
    }
}

void appendClockResetDomains(QTreeWidget* tree,
                             const ClockResetDomainReport& report)
{
    QTreeWidgetItem* clocks = createGroupItem(tree,
                                             report.clockGroupDisplayName.isEmpty()
                                                 ? QStringLiteral("Clock Domains")
                                                 : report.clockGroupDisplayName,
                                             report.clockRelationshipCount);
    for (const ClockResetDomainEntry& domain : report.clockDomains) {
        QTreeWidgetItem* signal = createChildItem(clocks,
                                                  domain.sectionDisplayName.isEmpty()
                                                      ? QStringLiteral("Clock")
                                                      : domain.sectionDisplayName,
                                                  domain.domainSignalDisplayName,
                                                  domain.detailDisplayName.isEmpty()
                                                      ? QStringLiteral("drives %1 modules")
                                                            .arg(domain.modules.size())
                                                      : domain.detailDisplayName,
                                                  domain.domainSignalCodeLink.fileName,
                                                  domain.domainSignalCodeLink.line,
                                                  domain.domainSignalCodeLink.column,
                                                  domain.domainSignalCodeLink.fileDisplayName,
                                                  domain.domainSignalCodeLink.lineDisplayName);
        for (const ClockResetDomainMember& member : domain.modules) {
            QTreeWidgetItem* module =
                createChildItem(signal,
                                member.sectionDisplayName.isEmpty()
                                    ? QStringLiteral("Module")
                                    : member.sectionDisplayName,
                                member.moduleDisplayName.isEmpty()
                                    ? QStringLiteral("<unnamed>")
                                    : member.moduleDisplayName,
                                member.detailDisplayName.isEmpty()
                                    ? QStringLiteral("clocked")
                                    : member.detailDisplayName,
                                member.moduleCodeLink.fileName,
                                member.moduleCodeLink.line,
                                member.moduleCodeLink.column,
                                member.moduleCodeLink.fileDisplayName,
                                member.moduleCodeLink.lineDisplayName);
            createChildItem(module,
                            QStringLiteral("Relationship Type"),
                            member.relationshipTypeDisplayName,
                            member.moduleDisplayName,
                            member.moduleCodeLink.fileName,
                            member.moduleCodeLink.line,
                            member.moduleCodeLink.column,
                            member.moduleCodeLink.fileDisplayName,
                            member.moduleCodeLink.lineDisplayName);
            createChildItem(module,
                            QStringLiteral("Source Role"),
                            member.sourceRoleDisplayName,
                            member.moduleDisplayName,
                            member.moduleCodeLink.fileName,
                            member.moduleCodeLink.line,
                            member.moduleCodeLink.column,
                            member.moduleCodeLink.fileDisplayName,
                            member.moduleCodeLink.lineDisplayName);
            createChildItem(module,
                            QStringLiteral("Domain Signal"),
                            domain.domainSignalDisplayName,
                            domain.sectionDisplayName,
                            domain.domainSignalCodeLink.fileName,
                            domain.domainSignalCodeLink.line,
                            domain.domainSignalCodeLink.column,
                            domain.domainSignalCodeLink.fileDisplayName,
                            domain.domainSignalCodeLink.lineDisplayName);
        }
    }

    QTreeWidgetItem* resets = createGroupItem(tree,
                                             report.resetGroupDisplayName.isEmpty()
                                                 ? QStringLiteral("Reset Domains")
                                                 : report.resetGroupDisplayName,
                                             report.resetRelationshipCount);
    for (const ClockResetDomainEntry& domain : report.resetDomains) {
        QTreeWidgetItem* signal = createChildItem(resets,
                                                  domain.sectionDisplayName.isEmpty()
                                                      ? QStringLiteral("Reset")
                                                      : domain.sectionDisplayName,
                                                  domain.domainSignalDisplayName,
                                                  domain.detailDisplayName.isEmpty()
                                                      ? QStringLiteral("resets %1 modules")
                                                            .arg(domain.modules.size())
                                                      : domain.detailDisplayName,
                                                  domain.domainSignalCodeLink.fileName,
                                                  domain.domainSignalCodeLink.line,
                                                  domain.domainSignalCodeLink.column,
                                                  domain.domainSignalCodeLink.fileDisplayName,
                                                  domain.domainSignalCodeLink.lineDisplayName);
        for (const ClockResetDomainMember& member : domain.modules) {
            QTreeWidgetItem* module =
                createChildItem(signal,
                                member.sectionDisplayName.isEmpty()
                                    ? QStringLiteral("Module")
                                    : member.sectionDisplayName,
                                member.moduleDisplayName.isEmpty()
                                    ? QStringLiteral("<unnamed>")
                                    : member.moduleDisplayName,
                                member.detailDisplayName.isEmpty()
                                    ? QStringLiteral("reset")
                                    : member.detailDisplayName,
                                member.moduleCodeLink.fileName,
                                member.moduleCodeLink.line,
                                member.moduleCodeLink.column,
                                member.moduleCodeLink.fileDisplayName,
                                member.moduleCodeLink.lineDisplayName);
            createChildItem(module,
                            QStringLiteral("Relationship Type"),
                            member.relationshipTypeDisplayName,
                            member.moduleDisplayName,
                            member.moduleCodeLink.fileName,
                            member.moduleCodeLink.line,
                            member.moduleCodeLink.column,
                            member.moduleCodeLink.fileDisplayName,
                            member.moduleCodeLink.lineDisplayName);
            createChildItem(module,
                            QStringLiteral("Source Role"),
                            member.sourceRoleDisplayName,
                            member.moduleDisplayName,
                            member.moduleCodeLink.fileName,
                            member.moduleCodeLink.line,
                            member.moduleCodeLink.column,
                            member.moduleCodeLink.fileDisplayName,
                            member.moduleCodeLink.lineDisplayName);
            createChildItem(module,
                            QStringLiteral("Domain Signal"),
                            domain.domainSignalDisplayName,
                            domain.sectionDisplayName,
                            domain.domainSignalCodeLink.fileName,
                            domain.domainSignalCodeLink.line,
                            domain.domainSignalCodeLink.column,
                            domain.domainSignalCodeLink.fileDisplayName,
                            domain.domainSignalCodeLink.lineDisplayName);
        }
    }
}

void appendClockResetEvidenceRows(
    QTreeWidget* tree,
    const QString& groupDisplayName,
    const QList<ClockResetDomainEvidenceRow>& rows)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            groupDisplayName,
                                            rows.size());
    for (const ClockResetDomainEvidenceRow& row : rows) {
        QTreeWidgetItem* evidence =
            createChildItem(group,
                            row.sectionDisplayName,
                            row.signalDisplayName,
                            row.detailDisplayName,
                            row.signalCodeLink.fileName,
                            row.signalCodeLink.line,
                            row.signalCodeLink.column,
                            row.signalCodeLink.fileDisplayName,
                            row.signalCodeLink.lineDisplayName);
        createChildItem(evidence,
                        QStringLiteral("Signal"),
                        row.signalDisplayName,
                        row.sectionDisplayName,
                        row.signalCodeLink.fileName,
                        row.signalCodeLink.line,
                        row.signalCodeLink.column,
                        row.signalCodeLink.fileDisplayName,
                        row.signalCodeLink.lineDisplayName);
        createChildItem(evidence,
                        QStringLiteral("Module"),
                        row.moduleDisplayName,
                        row.sectionDisplayName,
                        row.moduleCodeLink.fileName,
                        row.moduleCodeLink.line,
                        row.moduleCodeLink.column,
                        row.moduleCodeLink.fileDisplayName,
                        row.moduleCodeLink.lineDisplayName);
        createChildItem(evidence,
                        QStringLiteral("Relationship Type"),
                        row.relationshipTypeDisplayName,
                        row.sectionDisplayName,
                        row.signalCodeLink.fileName,
                        row.signalCodeLink.line,
                        row.signalCodeLink.column,
                        row.signalCodeLink.fileDisplayName,
                        row.signalCodeLink.lineDisplayName);
        createChildItem(evidence,
                        QStringLiteral("Category"),
                        row.categoryDisplayName,
                        row.sectionDisplayName,
                        row.signalCodeLink.fileName,
                        row.signalCodeLink.line,
                        row.signalCodeLink.column,
                        row.signalCodeLink.fileDisplayName,
                        row.signalCodeLink.lineDisplayName);
        createChildItem(evidence,
                        QStringLiteral("Reason"),
                        row.evidenceReasonDisplayName,
                        row.detailDisplayName,
                        row.signalCodeLink.fileName,
                        row.signalCodeLink.line,
                        row.signalCodeLink.column,
                        row.signalCodeLink.fileDisplayName,
                        row.signalCodeLink.lineDisplayName);
        createChildItem(evidence,
                        QStringLiteral("Source Role"),
                        row.sourceRoleDisplayName,
                        row.sectionDisplayName,
                        row.signalCodeLink.fileName,
                        row.signalCodeLink.line,
                        row.signalCodeLink.column,
                        row.signalCodeLink.fileDisplayName,
                        row.signalCodeLink.lineDisplayName);
    }
}

void appendFsmGraphs(QTreeWidget* tree, const FsmGraphReport& report)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            report.groupDisplayName.isEmpty()
                                                ? QStringLiteral("FSM Graphs")
                                                : report.groupDisplayName,
                                            report.graphs.size());
    for (const FsmGraph& graph : report.graphs) {
        QTreeWidgetItem* stateRegister =
            createChildItem(group,
                            graph.stateRegisterSectionDisplayName.isEmpty()
                                ? QStringLiteral("State Register")
                                : graph.stateRegisterSectionDisplayName,
                            graph.stateRegisterDisplayName,
                            graph.stateRegisterDetailDisplayName.isEmpty()
                                ? QStringLiteral("state register")
                                : graph.stateRegisterDetailDisplayName,
                            graph.stateRegisterCodeLink.fileName,
                            graph.stateRegisterCodeLink.line,
                            graph.stateRegisterCodeLink.column,
                            graph.stateRegisterCodeLink.fileDisplayName,
                            graph.stateRegisterCodeLink.lineDisplayName);
        createChildItem(stateRegister,
                        QStringLiteral("Type"),
                        graph.stateRegisterTypeDisplayName,
                        graph.stateRegisterDisplayName,
                        graph.stateRegisterCodeLink.fileName,
                        graph.stateRegisterCodeLink.line,
                        graph.stateRegisterCodeLink.column,
                        graph.stateRegisterCodeLink.fileDisplayName,
                        graph.stateRegisterCodeLink.lineDisplayName);
        createChildItem(stateRegister,
                        QStringLiteral("Source Role"),
                        graph.stateRegisterSourceRoleDisplayName,
                        graph.stateRegisterDisplayName,
                        graph.stateRegisterCodeLink.fileName,
                        graph.stateRegisterCodeLink.line,
                        graph.stateRegisterCodeLink.column,
                        graph.stateRegisterCodeLink.fileDisplayName,
                        graph.stateRegisterCodeLink.lineDisplayName);
        if (!graph.nextStateSignalDisplayName.isEmpty()) {
            QTreeWidgetItem* nextState =
                createChildItem(stateRegister,
                                QStringLiteral("Next State Signal"),
                                graph.nextStateSignalDisplayName,
                                graph.nextStateSignalTypeDisplayName,
                                graph.nextStateSignalCodeLink.fileName,
                                graph.nextStateSignalCodeLink.line,
                                graph.nextStateSignalCodeLink.column,
                                graph.nextStateSignalCodeLink.fileDisplayName,
                                graph.nextStateSignalCodeLink.lineDisplayName);
            createChildItem(nextState,
                            QStringLiteral("Source Role"),
                            graph.nextStateSignalSourceRoleDisplayName,
                            graph.nextStateSignalDisplayName,
                            graph.nextStateSignalCodeLink.fileName,
                            graph.nextStateSignalCodeLink.line,
                            graph.nextStateSignalCodeLink.column,
                            graph.nextStateSignalCodeLink.fileDisplayName,
                            graph.nextStateSignalCodeLink.lineDisplayName);
        }

        QTreeWidgetItem* states = new QTreeWidgetItem(stateRegister);
        const QString statesGroup = graph.statesGroupDisplayName.isEmpty()
            ? QStringLiteral("States")
            : graph.statesGroupDisplayName;
        states->setText(0, SemanticPanelUtils::countLabel(statesGroup,
                                                          graph.stateCount));
        for (const FsmStateRow& row : graph.stateRows) {
            QTreeWidgetItem* state =
                createChildItem(states,
                                row.sectionDisplayName.isEmpty()
                                    ? QStringLiteral("State")
                                    : row.sectionDisplayName,
                                row.stateDisplayName,
                                row.detailDisplayName,
                                row.codeLink.fileName,
                                row.codeLink.line,
                                row.codeLink.column,
                                row.codeLink.fileDisplayName,
                                row.codeLink.lineDisplayName);
            createChildItem(state,
                            QStringLiteral("Type"),
                            row.typeDisplayName,
                            row.stateDisplayName,
                            row.codeLink.fileName,
                            row.codeLink.line,
                            row.codeLink.column,
                            row.codeLink.fileDisplayName,
                            row.codeLink.lineDisplayName);
            createChildItem(state,
                            QStringLiteral("Source Role"),
                            row.sourceRoleDisplayName,
                            row.stateDisplayName,
                            row.codeLink.fileName,
                            row.codeLink.line,
                            row.codeLink.column,
                            row.codeLink.fileDisplayName,
                            row.codeLink.lineDisplayName);
            createChildItem(state,
                            QStringLiteral("Module"),
                            row.moduleDisplayName,
                            row.stateDisplayName,
                            row.codeLink.fileName,
                            row.codeLink.line,
                            row.codeLink.column,
                            row.codeLink.fileDisplayName,
                            row.codeLink.lineDisplayName);
        }

        QTreeWidgetItem* transitions = new QTreeWidgetItem(stateRegister);
        const QString transitionsGroup =
            graph.transitionsGroupDisplayName.isEmpty()
                ? QStringLiteral("Transitions")
                : graph.transitionsGroupDisplayName;
        transitions->setText(0, SemanticPanelUtils::countLabel(
                                    transitionsGroup,
                                    graph.transitionCount));
        for (const FsmTransitionRow& row : graph.transitionRows) {
            QTreeWidgetItem* transition =
                createChildItem(transitions,
                                row.sectionDisplayName.isEmpty()
                                    ? row.fromStateDisplayName
                                    : row.sectionDisplayName,
                                row.toStateDisplayName,
                                row.detailDisplayName,
                                row.codeLink.fileName,
                                row.codeLink.line,
                                row.codeLink.column,
                                row.codeLink.fileDisplayName,
                                row.codeLink.lineDisplayName);
            createChildItem(transition,
                            QStringLiteral("From State"),
                            row.fromStateDisplayName,
                            row.conditionDisplayName,
                            row.fromStateCodeLink.fileName,
                            row.fromStateCodeLink.line,
                            row.fromStateCodeLink.column,
                            row.fromStateCodeLink.fileDisplayName,
                            row.fromStateCodeLink.lineDisplayName);
            createChildItem(transition,
                            QStringLiteral("To State"),
                            row.toStateDisplayName,
                            row.conditionDisplayName,
                            row.toStateCodeLink.fileName,
                            row.toStateCodeLink.line,
                            row.toStateCodeLink.column,
                            row.toStateCodeLink.fileDisplayName,
                            row.toStateCodeLink.lineDisplayName);
            createChildItem(transition,
                            QStringLiteral("Source Role"),
                            row.sourceRoleDisplayName,
                            row.sourceLineDisplayName,
                            row.codeLink.fileName,
                            row.codeLink.line,
                            row.codeLink.column,
                            row.codeLink.fileDisplayName,
                            row.codeLink.lineDisplayName);
        }
    }
}

void appendSignalJourneyItems(QTreeWidgetItem* parent,
                              const QString& section,
                              const QList<SignalJourneyItem>& items)
{
    QTreeWidgetItem* group = new QTreeWidgetItem(parent);
    group->setText(0, SemanticPanelUtils::countLabel(section, items.size()));
    for (const SignalJourneyItem& item : items) {
        QTreeWidgetItem* relationship =
            createChildItem(group,
                            section,
                            item.peerSymbolDisplayName,
                            item.detailDisplayName,
                            item.peerCodeLink.fileName,
                            item.peerCodeLink.line,
                            item.peerCodeLink.column,
                            item.peerCodeLink.fileDisplayName,
                            item.peerCodeLink.lineDisplayName);
        QTreeWidgetItem* fromItem =
            createChildItem(relationship,
                            QStringLiteral("From"),
                            item.fromSymbolDisplayName,
                            item.relationshipTypeDisplayName,
                            item.fromCodeLink.fileName,
                            item.fromCodeLink.line,
                            item.fromCodeLink.column,
                            item.fromCodeLink.fileDisplayName,
                            item.fromCodeLink.lineDisplayName);
        createChildItem(fromItem,
                        QStringLiteral("Type"),
                        item.fromTypeDisplayName,
                        item.fromSymbolDisplayName,
                        item.fromCodeLink.fileName,
                        item.fromCodeLink.line,
                        item.fromCodeLink.column,
                        item.fromCodeLink.fileDisplayName,
                        item.fromCodeLink.lineDisplayName);
        createChildItem(fromItem,
                        QStringLiteral("Source Role"),
                        item.fromSourceRoleDisplayName,
                        item.fromSymbolDisplayName,
                        item.fromCodeLink.fileName,
                        item.fromCodeLink.line,
                        item.fromCodeLink.column,
                        item.fromCodeLink.fileDisplayName,
                        item.fromCodeLink.lineDisplayName);
        QTreeWidgetItem* toItem =
            createChildItem(relationship,
                            QStringLiteral("To"),
                            item.toSymbolDisplayName,
                            item.relationshipTypeDisplayName,
                            item.toCodeLink.fileName,
                            item.toCodeLink.line,
                            item.toCodeLink.column,
                            item.toCodeLink.fileDisplayName,
                            item.toCodeLink.lineDisplayName);
        createChildItem(toItem,
                        QStringLiteral("Type"),
                        item.toTypeDisplayName,
                        item.toSymbolDisplayName,
                        item.toCodeLink.fileName,
                        item.toCodeLink.line,
                        item.toCodeLink.column,
                        item.toCodeLink.fileDisplayName,
                        item.toCodeLink.lineDisplayName);
        createChildItem(toItem,
                        QStringLiteral("Source Role"),
                        item.toSourceRoleDisplayName,
                        item.toSymbolDisplayName,
                        item.toCodeLink.fileName,
                        item.toCodeLink.line,
                        item.toCodeLink.column,
                        item.toCodeLink.fileDisplayName,
                        item.toCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("Connection"),
                        item.connectionKindDisplayName,
                        item.detailDisplayName,
                        item.peerCodeLink.fileName,
                        item.peerCodeLink.line,
                        item.peerCodeLink.column,
                        item.peerCodeLink.fileDisplayName,
                        item.peerCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("Peer Type"),
                        item.peerTypeDisplayName,
                        item.relationshipTypeDisplayName,
                        item.peerCodeLink.fileName,
                        item.peerCodeLink.line,
                        item.peerCodeLink.column,
                        item.peerCodeLink.fileDisplayName,
                        item.peerCodeLink.lineDisplayName);
        if (!item.interfaceBaseDisplayName.isEmpty()) {
            createChildItem(relationship,
                            QStringLiteral("Interface"),
                            item.interfaceBaseDisplayName,
                            item.connectionKindDisplayName,
                            item.peerCodeLink.fileName,
                            item.peerCodeLink.line,
                            item.peerCodeLink.column,
                            item.peerCodeLink.fileDisplayName,
                            item.peerCodeLink.lineDisplayName);
        }
        createChildItem(relationship,
                        QStringLiteral("Source Role"),
                        item.peerSourceRoleDisplayName,
                        item.peerSymbolDisplayName,
                        item.peerCodeLink.fileName,
                        item.peerCodeLink.line,
                        item.peerCodeLink.column,
                        item.peerCodeLink.fileDisplayName,
                        item.peerCodeLink.lineDisplayName);
    }
}

void appendSignalJourney(QTreeWidget* tree,
                         const QString& fileName,
                         const QString& moduleName,
                         const QString& signalName)
{
    if (signalName.isEmpty())
        return;

    SignalJourneyQuery query;
    query.fileName = fileName;
    query.moduleName = moduleName;
    query.signalName = signalName;
    const SignalJourneyReport report =
        SignalJourneyService::getInstance()->buildSignalJourney(query);
    if (!report.found)
        return;

    const int totalItems = 1
        + report.assignments.size()
        + report.reads.size()
        + report.portConnections.size()
        + report.interfaceConnections.size()
        + report.timingConnections.size();
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Signal Journey: %1")
                                                .arg(report.declarationDisplayName),
                                            totalItems);
    QTreeWidgetItem* declaration =
        createChildItem(group,
                        QStringLiteral("Declaration"),
                        report.declarationDisplayName,
                        report.declarationTypeDisplayName,
                        report.declarationCodeLink.fileName,
                        report.declarationCodeLink.line,
                        report.declarationCodeLink.column,
                        report.declarationCodeLink.fileDisplayName,
                        report.declarationCodeLink.lineDisplayName);
    createChildItem(declaration,
                    QStringLiteral("Source Role"),
                    report.declarationSourceRoleDisplayName,
                    report.declarationDisplayName,
                    report.declarationCodeLink.fileName,
                    report.declarationCodeLink.line,
                    report.declarationCodeLink.column,
                    report.declarationCodeLink.fileDisplayName,
                    report.declarationCodeLink.lineDisplayName);
    appendSignalJourneyItems(group, QStringLiteral("Assignments"), report.assignments);
    appendSignalJourneyItems(group, QStringLiteral("Reads"), report.reads);
    appendSignalJourneyItems(group,
                             QStringLiteral("Port Connections"),
                             report.portConnections);
    appendSignalJourneyItems(group,
                             QStringLiteral("Interface Connections"),
                             report.interfaceConnections);
    appendSignalJourneyItems(group,
                             QStringLiteral("Timing Connections"),
                             report.timingConnections);
}

void appendSemanticDiff(QTreeWidget* tree, const SemanticDiffReport& report)
{
    QTreeWidgetItem* symbols = createGroupItem(tree,
                                              report.symbolGroupDisplayName.isEmpty()
                                                  ? QStringLiteral("Semantic Diff Symbols")
                                                  : report.symbolGroupDisplayName,
                                              report.symbolChangeCount);
    for (const SemanticDiffSymbolChange& change : report.symbolChanges) {
        QTreeWidgetItem* symbolChange =
            createChildItem(symbols,
                            QStringLiteral("%1 %2")
                                .arg(change.kindDisplayName,
                                     change.categoryGroupDisplayName),
                            change.symbolDisplayName,
                            change.detailDisplayName,
                            change.codeLink.fileName,
                            change.codeLink.line,
                            change.codeLink.column,
                            change.codeLink.fileDisplayName,
                            change.codeLink.lineDisplayName);
        if (!change.beforeSymbolTypeDisplayName.isEmpty()) {
            createChildItem(symbolChange,
                            QStringLiteral("Before"),
                            change.beforeSymbolTypeDisplayName,
                            change.beforeDataTypeDisplayName.isEmpty()
                                ? change.beforeScopeDisplayName
                                : QStringLiteral("%1, %2")
                                      .arg(change.beforeDataTypeDisplayName,
                                           change.beforeScopeDisplayName),
                            change.beforeCodeLink.fileName,
                            change.beforeCodeLink.line,
                            change.beforeCodeLink.column,
                            change.beforeCodeLink.fileDisplayName,
                            change.beforeCodeLink.lineDisplayName);
        }
        if (!change.afterSymbolTypeDisplayName.isEmpty()) {
            createChildItem(symbolChange,
                            QStringLiteral("After"),
                            change.afterSymbolTypeDisplayName,
                            change.afterDataTypeDisplayName.isEmpty()
                                ? change.afterScopeDisplayName
                                : QStringLiteral("%1, %2")
                                      .arg(change.afterDataTypeDisplayName,
                                           change.afterScopeDisplayName),
                            change.afterCodeLink.fileName,
                            change.afterCodeLink.line,
                            change.afterCodeLink.column,
                            change.afterCodeLink.fileDisplayName,
                            change.afterCodeLink.lineDisplayName);
        }
        createChildItem(symbolChange,
                        QStringLiteral("Source Role"),
                        change.sourceRoleDisplayName,
                        change.categoryDisplayName,
                        change.codeLink.fileName,
                        change.codeLink.line,
                        change.codeLink.column,
                        change.codeLink.fileDisplayName,
                        change.codeLink.lineDisplayName);
    }

    QTreeWidgetItem* relationships =
        createGroupItem(tree,
                        report.relationshipGroupDisplayName.isEmpty()
                            ? QStringLiteral("Semantic Diff Relationships")
                            : report.relationshipGroupDisplayName,
                        report.relationshipChangeCount);
    for (const SemanticDiffRelationshipChange& change : report.relationshipChanges) {
        QTreeWidgetItem* relationship =
            createChildItem(relationships,
                            change.kindDisplayName,
                            change.relationshipTypeDisplayName,
                            change.detailDisplayName,
                            change.codeLink.fileName,
                            change.codeLink.line,
                            change.codeLink.column,
                            change.codeLink.fileDisplayName,
                            change.codeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("From"),
                        change.fromSymbolDisplayName,
                        change.relationshipTypeDisplayName,
                        change.fromCodeLink.fileName,
                        change.fromCodeLink.line,
                        change.fromCodeLink.column,
                        change.fromCodeLink.fileDisplayName,
                        change.fromCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("To"),
                        change.toSymbolDisplayName,
                        change.relationshipTypeDisplayName,
                        change.toCodeLink.fileName,
                        change.toCodeLink.line,
                        change.toCodeLink.column,
                        change.toCodeLink.fileDisplayName,
                        change.toCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("Source Role"),
                        change.sourceRoleDisplayName,
                        change.relationshipTypeDisplayName,
                        change.codeLink.fileName,
                        change.codeLink.line,
                        change.codeLink.column,
                        change.codeLink.fileDisplayName,
                        change.codeLink.lineDisplayName);
    }

    QTreeWidgetItem* diagnostics =
        createGroupItem(tree,
                        report.diagnosticGroupDisplayName.isEmpty()
                            ? QStringLiteral("Semantic Diff Diagnostics")
                            : report.diagnosticGroupDisplayName,
                        report.diagnosticChangeCount);
    for (const SemanticDiffDiagnosticChange& change : report.diagnosticChanges) {
        const SemanticDiagnostic& diagnostic = change.displayDiagnostic;
        QTreeWidgetItem* diagnosticItem =
            createChildItem(diagnostics,
                            change.kindDisplayName,
                            diagnostic.message,
                            change.detailDisplayName.isEmpty()
                                ? change.severityDisplayName
                                : change.detailDisplayName,
                            change.codeLink.fileName,
                            change.codeLink.line,
                            change.codeLink.column,
                            change.codeLink.fileDisplayName,
                            change.codeLink.lineDisplayName);
        createChildItem(diagnosticItem,
                        QStringLiteral("Source Role"),
                        change.sourceRoleDisplayName,
                        change.severityDisplayName,
                        change.codeLink.fileName,
                        change.codeLink.line,
                        change.codeLink.column,
                        change.codeLink.fileDisplayName,
                        change.codeLink.lineDisplayName);
    }
}

} // namespace

RtlInsightsPanelCoordinator::RtlInsightsPanelCoordinator(QWidget* parent)
{
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* actionLayout = new QHBoxLayout;
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(4);
    moduleBriefButton = new QPushButton(QStringLiteral("Module Brief"), panel);
    moduleBriefButton->setObjectName(QStringLiteral("rtlModuleBriefButton"));
    signalJourneyButton = new QPushButton(QStringLiteral("Signal Journey"), panel);
    signalJourneyButton->setObjectName(QStringLiteral("rtlSignalJourneyButton"));
    clockResetButton = new QPushButton(QStringLiteral("Clock/Reset Map"), panel);
    clockResetButton->setObjectName(QStringLiteral("rtlClockResetButton"));
    fsmGraphButton = new QPushButton(QStringLiteral("FSM Graph"), panel);
    fsmGraphButton->setObjectName(QStringLiteral("rtlFsmGraphButton"));
    actionLayout->addWidget(moduleBriefButton);
    actionLayout->addWidget(signalJourneyButton);
    actionLayout->addWidget(clockResetButton);
    actionLayout->addWidget(fsmGraphButton);
    actionLayout->addStretch(1);
    layout->addLayout(actionLayout);

    insightsTree = new QTreeWidget(panel);
    insightsTree->setObjectName(QStringLiteral("rtlInsightsTree"));
    insightsTree->setColumnCount(5);
    insightsTree->setHeaderLabels({"Section", "Symbol", "Detail", "File", "Line"});
    insightsTree->setRootIsDecorated(true);
    insightsTree->setAlternatingRowColors(true);
    insightsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    insightsTree->header()->setStretchLastSection(true);
    insightsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    insightsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    insightsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    insightsTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    layout->addWidget(insightsTree);

    insightsDock = new QDockWidget(QStringLiteral("RTL Insights"), parent);
    insightsDock->setObjectName(QStringLiteral("rtlInsightsDock"));
    insightsDock->setWidget(panel);
    insightsDock->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetFloatable |
                              QDockWidget::DockWidgetClosable);

    QObject::connect(insightsTree, &QTreeWidget::itemDoubleClicked,
                     insightsDock, [this](QTreeWidgetItem* item, int) {
                         if (!item || !navigationHandler)
                             return;
                         const QString fileName = item->data(0, Qt::UserRole).toString();
                         if (fileName.isEmpty())
                             return;
                         const int line = item->data(0, Qt::UserRole + 1).toInt();
                         const int column = item->data(0, Qt::UserRole + 2).toInt();
                         navigationHandler(fileName, line, column);
                     });
    QObject::connect(moduleBriefButton, &QPushButton::clicked,
                     insightsDock, [this]() { showModuleBrief(); });
    QObject::connect(signalJourneyButton, &QPushButton::clicked,
                     insightsDock, [this]() { showSignalJourney(); });
    QObject::connect(clockResetButton, &QPushButton::clicked,
                     insightsDock, [this]() { showClockResetDomainMap(); });
    QObject::connect(fsmGraphButton, &QPushButton::clicked,
                     insightsDock, [this]() { showFsmGraph(); });

    renderNoContext();
}

void RtlInsightsPanelCoordinator::setNavigationHandler(
    std::function<void(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
}

void RtlInsightsPanelCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void RtlInsightsPanelCoordinator::updateModuleContext(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName)
{
    currentFileName = fileName;
    currentModuleName = moduleName;
    currentSignalName = signalName;
    renderActionList();
}

void RtlInsightsPanelCoordinator::showModuleInsights(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName)
{
    updateModuleContext(fileName, moduleName, signalName);
    if (insightsDock) {
        insightsDock->show();
        insightsDock->raise();
    }
}

void RtlInsightsPanelCoordinator::showSemanticDiff(
    std::shared_ptr<const SemanticIndexSnapshot> beforeSnapshot,
    std::shared_ptr<const SemanticIndexSnapshot> afterSnapshot,
    const QString& moduleName,
    const QString& beforeFileName,
    const QString& afterFileName)
{
    if (!insightsTree)
        return;

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("Semantic Diff"));

    const bool hadExpandableItems =
        SemanticPanelUtils::treeHasExpandableItems(insightsTree);
    const QSet<QString> expandedKeys =
        SemanticPanelUtils::collectExpandedKeys(insightsTree);
    insightsTree->clear();

    SemanticDiffQuery query;
    query.beforeSnapshot = std::move(beforeSnapshot);
    query.afterSnapshot = std::move(afterSnapshot);
    query.moduleName = moduleName;
    query.beforeFileName = beforeFileName;
    query.afterFileName = afterFileName;
    SemanticDiffReport report;
    try {
        report = SemanticDiffService::getInstance()->buildSemanticDiff(query);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("Semantic Diff"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("Semantic Diff"),
                       QStringLiteral("unknown error"));
        return;
    }

    appendSemanticDiff(insightsTree, report);
    SemanticPanelUtils::restoreTreeExpansion(insightsTree,
                                             hadExpandableItems,
                                             expandedKeys);

    const int totalChanges = report.symbolChanges.size()
        + report.relationshipChanges.size()
        + report.diagnosticChanges.size();
    if (insightsDock) {
        const QString title = moduleName.isEmpty()
            ? QStringLiteral("RTL Insights: Semantic Diff")
            : QStringLiteral("RTL Insights: Semantic Diff %1").arg(moduleName);
        insightsDock->setWindowTitle(title);
        insightsDock->show();
        insightsDock->raise();
    }
    if (statusMessageHandler) {
        statusMessageHandler(QStringLiteral("Rendered semantic diff (%1 changes)")
                                 .arg(totalChanges),
                             1500);
    }
    logReportDone(QStringLiteral("Semantic Diff"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPanelCoordinator::refresh()
{
    showModuleBrief();
}

void RtlInsightsPanelCoordinator::renderNoContext()
{
    if (!insightsTree)
        return;

    insightsTree->clear();
    createGroupItem(insightsTree, QStringLiteral("No module context"), 0);
    if (insightsDock)
        insightsDock->setWindowTitle(QStringLiteral("RTL Insights"));
    updateActionState();
}

void RtlInsightsPanelCoordinator::renderActionList()
{
    if (!insightsTree)
        return;

    insightsTree->clear();
    if (currentFileName.isEmpty() || currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    createGroupItem(
        insightsTree,
        QStringLiteral("Ready: %1").arg(currentModuleName),
        4);
    createGroupItem(
        insightsTree,
        currentSignalName.isEmpty()
            ? QStringLiteral("Select a signal or click Module Brief / Clock/Reset / FSM")
            : QStringLiteral("Current signal: %1").arg(currentSignalName),
        0);
    if (insightsDock)
        insightsDock->setWindowTitle(QStringLiteral("RTL Insights: %1")
                                         .arg(currentModuleName));
    updateActionState();
}

void RtlInsightsPanelCoordinator::showModuleBrief()
{
    if (!insightsTree)
        return;

    const bool hadExpandableItems =
        SemanticPanelUtils::treeHasExpandableItems(insightsTree);
    const QSet<QString> expandedKeys =
        SemanticPanelUtils::collectExpandedKeys(insightsTree);
    insightsTree->clear();

    if (currentFileName.isEmpty() || currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("Module Brief"));

    ModuleBriefQuery moduleQuery;
    moduleQuery.fileName = currentFileName;
    moduleQuery.moduleName = currentModuleName;
    ModuleBriefReport moduleReport;
    try {
        moduleReport =
            ModuleBriefService::getInstance()->buildModuleBrief(moduleQuery);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("Module Brief"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("Module Brief"),
                       QStringLiteral("unknown error"));
        return;
    }

    if (!moduleReport.found) {
        createGroupItem(insightsTree,
                        QStringLiteral("Module not found: %1").arg(currentModuleName),
                        0);
        if (insightsDock)
            insightsDock->setWindowTitle(QStringLiteral("RTL Insights: %1")
                                             .arg(currentModuleName));
        logReportDone(QStringLiteral("Module Brief"),
                      static_cast<int>(timer.elapsed()));
        return;
    }

    appendSymbolGroup(insightsTree,
                      QStringLiteral("Ports"),
                      moduleReport.portRows);
    appendSymbolGroup(insightsTree,
                      QStringLiteral("Parameters"),
                      moduleReport.parameterRows);
    appendSymbolGroup(insightsTree,
                      QStringLiteral("Instances"),
                      moduleReport.instanceRows);
    appendSymbolGroup(insightsTree,
                      QStringLiteral("Imports"),
                      moduleReport.importRows);
    appendContextRows(insightsTree, moduleReport.contextRows);
    appendDiagnostics(insightsTree, moduleReport.diagnosticRows);
    appendRelationshipSummary(insightsTree,
                              moduleReport.relationshipSummary,
                              moduleReport.relationshipEvidenceRows);
    SemanticPanelUtils::restoreTreeExpansion(insightsTree,
                                             hadExpandableItems,
                                             expandedKeys);

    if (insightsDock) {
        insightsDock->setWindowTitle(QStringLiteral("RTL Insights: %1")
                                         .arg(moduleReport.moduleDisplayName));
    }
    if (statusMessageHandler) {
        statusMessageHandler(QStringLiteral("Updated RTL insights for %1")
                                 .arg(moduleReport.moduleDisplayName),
                             1500);
    }
    logReportDone(QStringLiteral("Module Brief"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPanelCoordinator::showSignalJourney()
{
    if (!insightsTree)
        return;

    insightsTree->clear();
    if (currentFileName.isEmpty() || currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("Signal Journey"));
    try {
        appendSignalJourney(insightsTree,
                            currentFileName,
                            currentModuleName,
                            currentSignalName);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("Signal Journey"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("Signal Journey"),
                       QStringLiteral("unknown error"));
        return;
    }
    if (insightsDock)
        insightsDock->setWindowTitle(QStringLiteral("RTL Insights: Signal Journey %1")
                                         .arg(currentSignalName));
    if (statusMessageHandler)
        statusMessageHandler(QStringLiteral("Rendered signal journey"), 1500);
    logReportDone(QStringLiteral("Signal Journey"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPanelCoordinator::showClockResetDomainMap()
{
    if (!insightsTree)
        return;

    insightsTree->clear();
    if (currentFileName.isEmpty() || currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("Clock/Reset Domain Map"));
    ClockResetDomainReport report;
    try {
        ClockResetDomainQuery query;
        query.fileName = currentFileName;
        query.moduleName = currentModuleName;
        report = ClockResetDomainService::getInstance()->buildClockResetDomainMap(query);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("Clock/Reset Domain Map"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("Clock/Reset Domain Map"),
                       QStringLiteral("unknown error"));
        return;
    }
    appendClockResetDomains(insightsTree, report);
    appendClockResetEvidenceRows(
        insightsTree,
        report.evidenceGroupDisplayName.isEmpty()
            ? QStringLiteral("Domain Evidence")
            : report.evidenceGroupDisplayName,
        report.evidenceRows);
    appendClockResetEvidenceRows(
        insightsTree,
        report.ambiguityGroupDisplayName.isEmpty()
            ? QStringLiteral("Ambiguity")
            : report.ambiguityGroupDisplayName,
        report.ambiguityRows);
    appendClockResetEvidenceRows(
        insightsTree,
        report.unmappedGroupDisplayName.isEmpty()
            ? QStringLiteral("Unmapped Timing Signals")
            : report.unmappedGroupDisplayName,
        report.unmappedRows);
    if (insightsDock)
        insightsDock->setWindowTitle(QStringLiteral("RTL Insights: Clock/Reset Map %1")
                                         .arg(currentModuleName));
    if (statusMessageHandler)
        statusMessageHandler(QStringLiteral("Rendered clock/reset domain map"), 1500);
    logReportDone(QStringLiteral("Clock/Reset Domain Map"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPanelCoordinator::showFsmGraph()
{
    if (!insightsTree)
        return;

    insightsTree->clear();
    if (currentFileName.isEmpty() || currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("FSM Graph"));
    FsmGraphReport report;
    try {
        FsmGraphQuery query;
        query.fileName = currentFileName;
        query.moduleName = currentModuleName;
        report = FsmGraphService::getInstance()->buildFsmGraph(query);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("FSM Graph"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("FSM Graph"),
                       QStringLiteral("unknown error"));
        return;
    }
    appendFsmGraphs(insightsTree, report);
    if (insightsDock)
        insightsDock->setWindowTitle(QStringLiteral("RTL Insights: FSM Graph %1")
                                         .arg(currentModuleName));
    if (statusMessageHandler)
        statusMessageHandler(QStringLiteral("Rendered FSM graph"), 1500);
    logReportDone(QStringLiteral("FSM Graph"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPanelCoordinator::updateActionState()
{
    const bool hasModule = !currentFileName.isEmpty() && !currentModuleName.isEmpty();
    if (moduleBriefButton)
        moduleBriefButton->setEnabled(hasModule);
    if (signalJourneyButton)
        signalJourneyButton->setEnabled(hasModule);
    if (clockResetButton)
        clockResetButton->setEnabled(hasModule);
    if (fsmGraphButton)
        fsmGraphButton->setEnabled(hasModule);
}

void RtlInsightsPanelCoordinator::logReportStart(const QString& reportName) const
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("RTL Insights"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 start for %2")
            .arg(reportName,
                 currentModuleName.isEmpty()
                     ? QStringLiteral("<no module>")
                     : currentModuleName));
}

void RtlInsightsPanelCoordinator::logReportDone(
    const QString& reportName,
    int durationMs) const
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("RTL Insights"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 done for %2")
            .arg(reportName,
                 currentModuleName.isEmpty()
                     ? QStringLiteral("<no module>")
                     : currentModuleName),
        durationMs);
}

void RtlInsightsPanelCoordinator::logReportError(
    const QString& reportName,
    const QString& message) const
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("RTL Insights"),
        ActivityLogLevel::Error,
        QStringLiteral("%1 failed: %2").arg(reportName, message));
}
