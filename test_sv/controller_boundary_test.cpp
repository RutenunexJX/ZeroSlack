#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

#include <iostream>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const QString& message)
{
    ++checks;
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: "
              << message.toStdString()
              << '\n';
}

QString readSource(const QString& root,
                   const QString& relativePath)
{
    QFile file(QDir(root).absoluteFilePath(relativePath));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

int sourceLineCount(const QString& source)
{
    if (source.isEmpty())
        return 0;
    return source.count(QLatin1Char('\n'))
        + (source.endsWith(QLatin1Char('\n')) ? 0 : 1);
}

bool containsAll(const QString& source,
                 const QStringList& fragments)
{
    for (const QString& fragment : fragments) {
        if (!source.contains(fragment))
            return false;
    }
    return true;
}

bool containsNone(const QString& source,
                  const QStringList& fragments)
{
    for (const QString& fragment : fragments) {
        if (source.contains(fragment))
            return false;
    }
    return true;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QString root = argc == 2
        ? QDir::cleanPath(
              QString::fromLocal8Bit(argv[1]))
        : QString();
    check(!root.isEmpty(),
          QStringLiteral("source root argument is present"));

    const QString runtimeHeader =
        readSource(root, QStringLiteral("editorruntime.h"));
    const QString runtimeSource =
        readSource(root, QStringLiteral("editorruntime.cpp"));
    const QString insightsHeader =
        readSource(
            root,
            QStringLiteral(
                "rtlinsightspanelcoordinator.h"));
    const QString insightsSource =
        readSource(
            root,
            QStringLiteral(
                "rtlinsightspanelcoordinator.cpp"));
    const QString cmake =
        readSource(root, QStringLiteral("CMakeLists.txt"));

    const QStringList editorModules = {
        QStringLiteral("editortemplateslotcontroller"),
        QStringLiteral("editorcolumnmodecontroller"),
        QStringLiteral("editorsignalselectioncontroller"),
        QStringLiteral("editorfolding"),
        QStringLiteral("editorsourcenavigation"),
    };
    for (const QString& module : editorModules) {
        check(!readSource(root, module + QStringLiteral(".h"))
                   .isEmpty()
                  && !readSource(
                          root,
                          module
                              + QStringLiteral(".cpp"))
                          .isEmpty(),
              module
                  + QStringLiteral(
                      " has a header and implementation"));
        check(cmake.contains(
                  module + QStringLiteral(".cpp")),
              module
                  + QStringLiteral(
                      " is an explicit build dependency"));
    }

    check(containsAll(
              runtimeHeader,
              {QStringLiteral(
                   "EditorTemplateSlotController"),
               QStringLiteral(
                   "EditorColumnModeController"),
               QStringLiteral(
                   "EditorSignalSelectionController"),
               QStringLiteral(
                   "EditorFoldingController"),
               QStringLiteral(
                   "EditorSourceNavigationUi")}),
          QStringLiteral(
              "editor runtime composes dedicated controllers"));
    check(containsNone(
              runtimeHeader,
              {QStringLiteral("struct TemplateSlotRange"),
               QStringLiteral("struct SelectedSignal"),
               QStringLiteral("templateSlotRanges"),
               QStringLiteral("columnAnchorLine"),
               QStringLiteral("virtualCursorLine ="),
               QStringLiteral("selectedSignals"),
               QStringLiteral(
                   "signalSelectionDragging")}),
          QStringLiteral(
              "editor runtime has no migrated mode state"));
    check(containsNone(
              runtimeSource,
              {QStringLiteral(
                   "templateSlotHighlightRanges("),
               QStringLiteral(
                   "bool hasColumnSelection("),
               QStringLiteral(
                   "paintColumnSelectionOverlay(")}),
          QStringLiteral(
              "editor runtime has no migrated mode helpers"));
    check(sourceLineCount(runtimeSource) <= 5000,
          QStringLiteral(
              "editorruntime.cpp is at most 5000 lines"));

    const QStringList insightModules = {
        QStringLiteral("rtlinsightspanelviewstate"),
        QStringLiteral("rtlinsightsgraphscenemapper"),
        QStringLiteral("rtlinsightsgraphcontroller"),
        QStringLiteral("rtlinsightspresenter"),
    };
    for (const QString& module : insightModules) {
        check(!readSource(root, module + QStringLiteral(".h"))
                   .isEmpty()
                  && !readSource(
                          root,
                          module
                              + QStringLiteral(".cpp"))
                          .isEmpty(),
              module
                  + QStringLiteral(
                      " has a header and implementation"));
        check(cmake.contains(
                  module + QStringLiteral(".cpp")),
              module
                  + QStringLiteral(
                      " is an explicit build dependency"));
    }

    check(containsAll(
              insightsHeader,
              {QStringLiteral(
                   "RtlInsightsPanelViewState"),
               QStringLiteral(
                   "RtlInsightsGraphController"),
               QStringLiteral(
                   "RtlInsightsPresenter")}),
          QStringLiteral(
              "Insight shell composes view state, graph controller, and presenter"));
    check(containsNone(
              insightsHeader,
              {QStringLiteral(
                   "QGraphicsScene* insightsGraphScene"),
               QStringLiteral(
                   "ModuleBlockDiagramReport currentModuleBlockReport"),
               QStringLiteral(
                   "QString currentGraphMode"),
               QStringLiteral(
                   "QTableWidget* graphTable")}),
          QStringLiteral(
              "Insight shell header has no presenter or graph state"));
    check(containsNone(
              insightsSource,
              {QStringLiteral(
                   "class RtlInsightGraphNodeItem"),
               QStringLiteral(
                   "class RtlInsightGraphEdgeItem"),
               QStringLiteral(
                   "renderFsmGraphLayoutScene("),
               QStringLiteral(
                   "renderModuleBlockDiagramScene(")}),
          QStringLiteral(
              "Insight shell has no scene mapping implementation"));
    check(sourceLineCount(insightsSource) <= 900,
          QStringLiteral(
              "rtlinsightspanelcoordinator.cpp is at most 900 lines"));

    std::cout << (checks - failures)
              << "/" << checks
              << " controller boundary checks passed\n";
    return failures == 0 ? 0 : 1;
}
