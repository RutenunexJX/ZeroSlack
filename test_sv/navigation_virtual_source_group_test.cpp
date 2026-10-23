#include "navigationwidget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidget>

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

QString normalized(const QString& path)
{
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
}

bool writeFile(const QString& path)
{
    if (!QDir().mkpath(
            QFileInfo(path).absolutePath())) {
        return false;
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(
               "module source_group_fixture; "
               "endmodule\n")
               > 0;
}

QTreeWidgetItem* itemWithText(
    QTreeWidget* tree,
    const QString& text)
{
    if (!tree)
        return nullptr;
    const QList<QTreeWidgetItem*> matches =
        tree->findItems(
            text,
            Qt::MatchExactly
                | Qt::MatchRecursive,
            0);
    return matches.isEmpty()
        ? nullptr
        : matches.first();
}

int fileItemCount(
    QTreeWidgetItem* item,
    const QString& path)
{
    if (!item)
        return 0;
    int count =
        item->data(
                0,
                NavigationWidget::
                    FileTreeKindRole)
                    .toInt()
                == NavigationWidget::FileItem
            && normalized(
                   item->data(
                           0,
                           Qt::UserRole)
                           .toString())
                   == normalized(path)
        ? 1
        : 0;
    for (int child = 0;
         child < item->childCount();
         ++child) {
        count += fileItemCount(
            item->child(child), path);
    }
    return count;
}

int fileItemCount(
    QTreeWidget* tree,
    const QString& path)
{
    int count = 0;
    if (!tree)
        return count;
    for (int i = 0;
         i < tree->topLevelItemCount();
         ++i) {
        count += fileItemCount(
            tree->topLevelItem(i), path);
    }
    return count;
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
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, workspace.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, workspace.path());

    const QString top =
        normalized(
            QDir(workspace.path())
                .absoluteFilePath(
                    QStringLiteral(
                        "rtl/top.sv")));
    const QString helper =
        normalized(
            QDir(workspace.path())
                .absoluteFilePath(
                    QStringLiteral(
                        "rtl/helper.sv")));
    const QString testbench =
        normalized(
            QDir(workspace.path())
                .absoluteFilePath(
                    QStringLiteral(
                        "tests/check.sv")));
    check(writeFile(top)
              && writeFile(helper)
              && writeFile(testbench),
          "source fixtures are written");

    NavigationWidget widget;
    widget.setWorkspaceRoot(
        workspace.path());
    widget.setVirtualSourceGroups({
        WorkspaceVirtualSourceGroup{
            QStringLiteral("RTL Sources"),
            {top}},
        WorkspaceVirtualSourceGroup{
            QStringLiteral(
                "Simulation Sources"),
            {testbench}},
        WorkspaceVirtualSourceGroup{
            QStringLiteral("Empty Group"),
            {}},
    });
    widget.updateFileHierarchy(
        {top, helper, testbench});

    auto* tree =
        widget.findChild<QTreeWidget*>(
            QStringLiteral(
                "navigationFileTree"));
    check(tree != nullptr,
          "file tree is available");
    QTreeWidgetItem* rtlGroup =
        itemWithText(
            tree,
            QStringLiteral("RTL Sources"));
    QTreeWidgetItem* simulationGroup =
        itemWithText(
            tree,
            QStringLiteral(
                "Simulation Sources"));
    QTreeWidgetItem* emptyGroup =
        itemWithText(
            tree,
            QStringLiteral("Empty Group"));
    check(rtlGroup
              && rtlGroup->data(
                     0,
                     NavigationWidget::
                         FileTreeKindRole)
                         .toInt()
                     == NavigationWidget::
                            VirtualSourceGroupItem
              && rtlGroup->childCount() == 1
              && simulationGroup
              && simulationGroup->childCount()
                     == 1
              && emptyGroup
              && emptyGroup->childCount() == 0,
          "virtual groups are distinct tree nodes with configured members");
    check(fileItemCount(tree, top) == 1
              && fileItemCount(
                     tree, testbench)
                     == 1
              && fileItemCount(
                     tree, helper)
                     == 1,
          "grouped files do not duplicate their physical entries");

    widget.setSearchFilter(
        NavigationWidget::FileTab,
        QStringLiteral("Simulation"));
    simulationGroup =
        itemWithText(
            tree,
            QStringLiteral(
                "Simulation Sources"));
    check(simulationGroup
              && simulationGroup->childCount()
                     == 1
              && fileItemCount(
                     tree, testbench)
                     == 1
              && fileItemCount(tree, top)
                     == 0,
          "group-name search retains every member of the matching virtual group");

    widget.setSearchFilter(
        NavigationWidget::FileTab,
        QStringLiteral("top"));
    rtlGroup =
        itemWithText(
            tree,
            QStringLiteral("RTL Sources"));
    check(rtlGroup
              && rtlGroup->childCount() == 1
              && fileItemCount(tree, top)
                     == 1
              && fileItemCount(
                     tree, testbench)
                     == 0,
          "file-name search retains the containing virtual group");

    widget.setSearchFilter(
        NavigationWidget::FileTab,
        QStringLiteral(
            "no-such-source"));
    check(tree->topLevelItemCount() == 1
              && tree->topLevelItem(0)
                     ->data(
                         0,
                         NavigationWidget::
                             FileTreeKindRole)
                     .toInt()
                     == NavigationWidget::
                            PlaceholderItem,
          "unmatched virtual and physical files produce one empty placeholder");

    widget.setSearchFilter(
        NavigationWidget::FileTab,
        QString());
    const QString artifactDirectory =
        qEnvironmentVariable(
            "ZEROSLACK_TEST_ARTIFACT_DIR");
    if (!artifactDirectory.isEmpty()) {
        widget.resize(420, 520);
        widget.show();
        QCoreApplication::processEvents();
        const QString artifactPath =
            QDir(artifactDirectory)
                .absoluteFilePath(
                    QStringLiteral(
                        "navigation_virtual_source_groups.png"));
        check(QDir().mkpath(
                  artifactDirectory)
                  && widget.grab().save(
                      artifactPath),
              "virtual source group navigation screenshot is saved");
    }

    const QString alternateTop = workspace.path() + QStringLiteral("/rtl/../rtl/top.sv");
    widget.setVirtualSourceGroups({
        {QStringLiteral("Aliases"), {top, top, alternateTop}},
        {QStringLiteral("Shared"), {top}},
    });
    auto* aliases = itemWithText(tree, QStringLiteral("Aliases"));
    auto* shared = itemWithText(tree, QStringLiteral("Shared"));
    check(aliases && aliases->childCount() == 1 && shared && shared->childCount() == 1
              && fileItemCount(tree, top) == 2 && fileItemCount(tree, helper) == 1,
          "exact and alternate paths deduplicate within a group and may occur in separate groups");

    const QString movedTop = workspace.path() + QStringLiteral("/rtl/moved.sv");
    check(QFile::rename(top, movedTop), "source can move between hierarchy refreshes");
    widget.updateFileHierarchy({movedTop, helper, testbench});
    check(!itemWithText(tree, QStringLiteral("Aliases")) && !itemWithText(tree, QStringLiteral("Shared"))
              && fileItemCount(tree, movedTop) == 1,
          "a later hierarchy refresh does not retain an identity for a moved group member");
    widget.setVirtualSourceGroups({{QStringLiteral("Moved"), {movedTop, movedTop}}});
    auto* moved = itemWithText(tree, QStringLiteral("Moved"));
    check(moved && moved->childCount() == 1 && fileItemCount(tree, movedTop) == 1,
          "updated group membership resolves the new path without a physical duplicate");

    std::cout << "navigation virtual source group checks: "
              << checks << ", failures: "
              << failures << '\n';
    return failures == 0 ? 0 : 1;
}
