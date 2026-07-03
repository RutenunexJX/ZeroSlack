#include "signalusagehotspotpanel.h"

#include <QApplication>
#include <QDebug>

namespace {
SignalUsageHotspotItem item(SignalUsageHotspotRole role,
                            const QString& moduleName,
                            const QString& fileName,
                            int line,
                            const QString& snippet)
{
    SignalUsageHotspotItem result;
    result.role = role;
    result.moduleName = moduleName;
    result.fileName = fileName;
    result.line = line;
    result.column = 3;
    result.snippet = snippet;
    result.evidenceText = QStringLiteral("evidence");
    result.roleReasonDisplayName =
        SignalUsageHotspotService::roleDisplayName(role);
    return result;
}

SignalUsageHotspotReport sampleReport()
{
    SignalUsageHotspotReport report;
    report.found = true;
    report.declarationDisplayName = QStringLiteral("mcs");
    report.items.append(item(SignalUsageHotspotRole::Write,
                             QStringLiteral("chl_ctrl"),
                             QStringLiteral("chl_ctrl.sv"),
                             18,
                             QStringLiteral("mcs <= next_mcs;")));
    report.items.append(item(SignalUsageHotspotRole::Read,
                             QStringLiteral("chl_ctrl"),
                             QStringLiteral("chl_ctrl.sv"),
                             42,
                             QStringLiteral("if (mcs) begin")));

    SignalUsageHotspotTrackLane lane;
    lane.moduleName = QStringLiteral("chl_ctrl");
    lane.fileName = QStringLiteral("chl_ctrl.sv");
    lane.startLine = 18;
    lane.endLine = 42;
    lane.count = 2;
    for (int i = 0; i < report.items.size(); ++i) {
        SignalUsageHotspotTrackPosition position;
        position.itemIndex = i;
        position.role = report.items.at(i).role;
        position.roleDisplayName =
            SignalUsageHotspotService::roleDisplayName(position.role);
        position.line = report.items.at(i).line;
        position.column = report.items.at(i).column;
        lane.positions.append(position);
    }
    report.trackLanes.append(lane);

    for (SignalUsageHotspotRole role :
         {SignalUsageHotspotRole::Write, SignalUsageHotspotRole::Read}) {
        SignalUsageHotspotMatrixCell cell;
        cell.moduleName = QStringLiteral("chl_ctrl");
        cell.fileName = QStringLiteral("chl_ctrl.sv");
        cell.role = role;
        cell.roleDisplayName = SignalUsageHotspotService::roleDisplayName(role);
        cell.count = 1;
        report.matrixCells.append(cell);
    }

    return report;
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    SignalUsageHotspotPanel panel;
    QString navigatedFile;
    int navigatedLine = -1;
    panel.setNavigationHandler(
        [&](const QString& fileName, int line, int) {
            navigatedFile = fileName;
            navigatedLine = line;
            return true;
        });
    panel.renderReportForTest(sampleReport());

    if (panel.trackBlockCountForTest() != 2) {
        qWarning() << "Expected 2 track blocks, got"
                   << panel.trackBlockCountForTest();
        return 1;
    }
    if (panel.matrixNonEmptyCellCountForTest() != 2) {
        qWarning() << "Expected 2 matrix cells, got"
                   << panel.matrixNonEmptyCellCountForTest();
        return 1;
    }
    if (!panel.triggerFirstUsageNavigationForTest()
        || navigatedFile != QStringLiteral("chl_ctrl.sv")
        || navigatedLine != 18) {
        qWarning() << "Navigation hook failed" << navigatedFile << navigatedLine;
        return 1;
    }
    return 0;
}
