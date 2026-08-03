#include "workspaceconfigurationdialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

#include <iostream>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

QString absolutePath(
    const QString& root,
    const QString& relative)
{
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(
                QDir(root)
                    .absoluteFilePath(relative))
                .absoluteFilePath()));
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setFont(
        QFont(
            QStringLiteral("Segoe UI"),
            9));
    QTemporaryDir workspace;
    check(workspace.isValid(),
          "temporary workspace is valid");
    if (!workspace.isValid())
        return 1;

    const QString top =
        absolutePath(
            workspace.path(),
            QStringLiteral("rtl/top.sv"));
    const QString helper =
        absolutePath(
            workspace.path(),
            QStringLiteral("rtl/helper.svh"));
    const QString testbench =
        absolutePath(
            workspace.path(),
            QStringLiteral("tb/top_tb.sv"));

    WorkspaceConfiguration input;
    input.workspaceRoot =
        absolutePath(
            workspace.path(),
            QStringLiteral("."));
    input.includeDirs = {
        input.workspaceRoot};
    input.fileExtensions = {
        QStringLiteral(".sv"),
        QStringLiteral(".svh")};
    input.topModule =
        QStringLiteral("top");
    input.virtualSourceGroups = {
        WorkspaceVirtualSourceGroup{
            QStringLiteral("RTL"),
            {top, helper}},
        WorkspaceVirtualSourceGroup{
            QStringLiteral("Verification"),
            {testbench}},
    };

    WorkspaceConfigurationDialog dialog;
    dialog.setConfiguration(input);
    const WorkspaceConfiguration untouched =
        dialog.configuration();
    check(untouched.virtualSourceGroups
                  == input.virtualSourceGroups
              && untouched.workspaceRoot
                     == input.workspaceRoot
              && untouched.topModule
                     == input.topModule,
          "opening and accepting configuration preserves virtual groups");

    auto* table =
        dialog.findChild<QTableWidget*>(
            QStringLiteral(
                "workspaceVirtualSourceGroupsTable"));
    check(table
              && table->rowCount() == 2
              && table->item(0, 0)
              && table->item(0, 0)->text()
                     == QStringLiteral("RTL")
              && table->item(0, 1)
              && table->item(0, 1)
                     ->data(Qt::UserRole)
                     .toStringList()
                     == QStringList{top, helper}
              && table->item(0, 1)
                     ->text()
                     .contains(
                         QStringLiteral(
                             "rtl/top.sv")),
          "configuration dialog materializes ordered group names and file identities");

    if (table) {
        table->item(0, 0)
            ->setText(
                QStringLiteral(
                    "Synthesizable"));
        table->setCurrentCell(1, 0);
    }
    auto* moveUp =
        dialog.findChild<QPushButton*>(
            QStringLiteral(
                "workspaceMoveVirtualSourceGroupUpButton"));
    if (moveUp)
        moveUp->click();
    const WorkspaceConfiguration reordered =
        dialog.configuration();
    check(moveUp
              && reordered.virtualSourceGroups
                     == QList<
                         WorkspaceVirtualSourceGroup>{
                         WorkspaceVirtualSourceGroup{
                             QStringLiteral(
                                 "Verification"),
                             {testbench}},
                         WorkspaceVirtualSourceGroup{
                             QStringLiteral(
                                 "Synthesizable"),
                             {top, helper}}},
          "virtual groups can be renamed and reordered without changing file identity");

    const QString artifactDirectory =
        qEnvironmentVariable(
            "ZEROSLACK_TEST_ARTIFACT_DIR");
    if (!artifactDirectory.isEmpty()) {
        dialog.resize(720, 720);
        dialog.show();
        QCoreApplication::processEvents();
        const QString artifactPath =
            QDir(artifactDirectory)
                .absoluteFilePath(
                    QStringLiteral(
                        "workspace_virtual_source_groups.png"));
        check(QDir().mkpath(
                  artifactDirectory)
                  && dialog.grab().save(
                      artifactPath),
              "virtual source group configuration screenshot is saved");
    }

    auto* remove =
        dialog.findChild<QPushButton*>(
            QStringLiteral(
                "workspaceRemoveVirtualSourceGroupButton"));
    if (table)
        table->setCurrentCell(0, 0);
    if (remove)
        remove->click();
    const WorkspaceConfiguration removed =
        dialog.configuration();
    check(remove
              && removed.virtualSourceGroups
                     == QList<
                         WorkspaceVirtualSourceGroup>{
                         WorkspaceVirtualSourceGroup{
                             QStringLiteral(
                                 "Synthesizable"),
                             {top, helper}}},
          "selected virtual group can be removed without disturbing the remaining group");

    std::cout << "workspace virtual source group UI checks: "
              << checks << ", failures: "
              << failures << '\n';
    return failures == 0 ? 0 : 1;
}
