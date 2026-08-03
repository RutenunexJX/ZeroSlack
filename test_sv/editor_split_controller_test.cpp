#include "actionregistry.h"
#include "editorsplitcontroller.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QTabWidget>
#include <QWidget>

#include <algorithm>
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

QWidget* page(const QString& id, const QString& text)
{
    auto* value = new QLabel(text);
    value->setProperty("editorViewId", id);
    return value;
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QWidget host;
    auto* hostLayout = new QHBoxLayout(&host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    auto* initial = new QTabWidget(&host);
    hostLayout->addWidget(initial);
    QWidget* first = page(QStringLiteral("view-a"),
                          QStringLiteral("A"));
    QWidget* second = page(QStringLiteral("view-b"),
                           QStringLiteral("B"));
    initial->addTab(first, QStringLiteral("a.sv"));
    initial->addTab(second, QStringLiteral("b.sv"));

    EditorSplitController controller(initial);
    controller.setHost(&host);
    host.resize(900, 600);
    host.show();
    QApplication::processEvents();

    const QList<EditorTabContextAction> initialContext =
        controller.tabContextActions(initial, 0);
    const QStringList expectedContextIds = {
        QString::fromLatin1(ActionIds::ViewEditorTabClose),
        QString::fromLatin1(ActionIds::ViewEditorTabCloseOthers),
        QString::fromLatin1(ActionIds::ViewEditorTabCloseRight),
        QString::fromLatin1(ActionIds::ViewEditorTabCloseAll),
        QString::fromLatin1(ActionIds::ViewReopenClosedTab),
        QString::fromLatin1(ActionIds::ViewEditorTabDuplicate),
        QString::fromLatin1(ActionIds::ViewEditorSplitLeft),
        QString::fromLatin1(ActionIds::ViewEditorSplitRight),
        QString::fromLatin1(ActionIds::ViewEditorSplitAbove),
        QString::fromLatin1(ActionIds::ViewEditorSplitBelow),
        QString::fromLatin1(ActionIds::ViewEditorSplitMerge),
        QString::fromLatin1(ActionIds::ViewEditorTabToggleLocked),
    };
    QStringList actualContextIds;
    bool contextMetadataMatches =
        initialContext.size()
        == expectedContextIds.size();
    for (const EditorTabContextAction& item :
         initialContext) {
        actualContextIds.append(item.actionId);
        const ActionDescriptor* descriptor =
            findActionById(item.actionId);
        contextMetadataMatches =
            contextMetadataMatches
            && descriptor
            && item.executionRoute
                   == descriptor->executionRoute
            && item.label
                   == descriptor
                          ->aliasForSurface(
                              ActionSurface::TabContextMenu)
                          .label;
    }
    check(actualContextIds == expectedContextIds
              && contextMetadataMatches,
          "Tab context model consumes Registry ids, labels, and routes");
    check(initialContext.size() == 12
              && !initialContext.value(10).enabled
              && initialContext.value(5).separatorBefore
              && initialContext.value(11).separatorBefore,
          "Tab context model preserves contextual enablement and grouping");
    first->setProperty("editorTabLocked", true);
    const QList<EditorTabContextAction> lockedContext =
        controller.tabContextActions(initial, 0);
    check(!lockedContext.isEmpty()
              && lockedContext.constLast().label
              == QStringLiteral("Unlock Tab"),
          "Tab context lock label follows selected view state");
    first->setProperty("editorTabLocked", false);

    QTabWidget* right = controller.createSplit(
        initial, EditorSplitDirection::Right);
    check(right && controller.groupCount() == 2,
          "right split creates a second group");
    const QList<EditorTabContextAction> splitContext =
        controller.tabContextActions(initial, 0);
    check(splitContext.size() > 10
              && splitContext.value(10).enabled,
          "Merge Group Action becomes available with multiple splits");
    check(controller.movePage(second, right)
              && initial->count() == 1
              && right->count() == 1,
          "a page moves across groups");

    QTabWidget* below = controller.movePageToSplit(
        first,
        EditorSplitDirection::Below,
        initial);
    check(below && controller.groupCount() == 2
              && below->currentWidget() == first,
          "moving the last page creates a nested split and removes empty group");

    check(controller.handleDropForTest(
              QStringLiteral("view-b"),
              below,
              EditorSplitDirection::Left)
              && controller.groupCount() == 2,
          "edge drop creates a directional split");
    check(controller.groupForPage(second)
              != right,
          "edge drop moves the original page");

    controller.setActiveGroup(below);
    controller.toggleActiveGroupMaximized();
    QApplication::processEvents();
    check(controller.isGroupMaximized()
              && below->isVisible(),
          "current split can be temporarily maximized");
    controller.restoreGroupLayout();
    QApplication::processEvents();
    check(!controller.isGroupMaximized(),
          "maximized split restores the previous layout");

    controller.equalizeSplitSizes();
    bool equalized = true;
    for (QSplitter* splitter :
         host.findChildren<QSplitter*>()) {
        const QList<int> sizes = splitter->sizes();
        if (sizes.size() > 1) {
            const int minimum = *std::min_element(
                sizes.cbegin(), sizes.cend());
            const int maximum = *std::max_element(
                sizes.cbegin(), sizes.cend());
            equalized = equalized && maximum - minimum <= 2;
        }
    }
    check(equalized,
          "equalize assigns the same size within every splitter");

    check(controller.mergeGroup(
              controller.groupForPage(second),
              below),
          "a group can merge into another group");
    QApplication::processEvents();
    check(controller.groupForPage(second) == below,
          "merged pages share the destination group");

    std::cout << (checks - failures) << "/" << checks
              << " editor split checks passed\n";
    return failures == 0 ? 0 : 1;
}
