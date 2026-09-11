#include "wavesimulationresultnavigationcoordinator.h"

#include "actionregistry.h"
#include "editorfileidentity.h"
#include "editorsemanticcontextservice.h"
#include "mycodeeditor.h"
#include "navigationcommandcoordinator.h"
#include "projectmodel.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "tabmanager.h"
#include "wavesimulationmanifestservice.h"
#include "workspacemanager.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QVariant>
#include <QWidget>

#include <algorithm>

namespace {
bool isPortableRelativePath(const QString& path)
{
    return !path.isEmpty()
        && !QDir::isAbsolutePath(path)
        && path != QStringLiteral("..")
        && !path.startsWith(QStringLiteral("../"));
}

QString normalizedPortablePath(const QString& path)
{
    return QDir::cleanPath(
        QDir::fromNativeSeparators(path.trimmed()));
}

bool pathBelongsToRoot(const QString& candidate,
                       const QString& root)
{
    const QString candidateKey =
        EditorFileIdentity::lookupKey(candidate);
    const QString rootKey =
        EditorFileIdentity::lookupKey(root);
    return !candidateKey.isEmpty()
        && !rootKey.isEmpty()
        && (candidateKey == rootKey
            || candidateKey.startsWith(
                rootKey + QLatin1Char('/')));
}

void setFailure(QString* destination, const QString& message)
{
    if (destination)
        *destination = message;
}
}

WaveSimulationResultNavigationCoordinator::
    WaveSimulationResultNavigationCoordinator(
        TabManager* tabManager,
        WorkspaceManager* workspaceManager,
        NavigationCommandCoordinator* navigationCoordinator,
        QObject* parent)
    : QObject(parent)
    , tabManager(tabManager)
    , workspaceManager(workspaceManager)
    , navigationCoordinator(navigationCoordinator)
{
}

WaveSimulationResultNavigationCoordinator::
    ~WaveSimulationResultNavigationCoordinator() = default;

void WaveSimulationResultNavigationCoordinator::registerWorkspace(
    QWidget* workspace,
    const QString& stableId,
    const QString& workspaceRoot)
{
    if (!workspace)
        return;
    purgeClosedWorkspaces();
    workspaces.append({workspace, stableId, workspaceRoot});
    connect(
        workspace,
        SIGNAL(sourceNavigationRequested(QString,int,int,QString,QString)),
        this,
        SLOT(handleSourceNavigationRequest(QString,int,int,QString,QString)));
    connect(workspace,
            &QObject::destroyed,
            this,
            &WaveSimulationResultNavigationCoordinator::removeWorkspace);
}

bool WaveSimulationResultNavigationCoordinator::revealSignalInResult(
    const ActionInvocation& invocation,
    QString* failureReason)
{
    setFailure(failureReason, QString());
    const auto fail = [failureReason](const QString& message) {
        setFailure(failureReason, message);
        return false;
    };
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()
        || !tabManager) {
        return fail(QStringLiteral("Open an analyzed workspace first."));
    }
    MyCodeEditor* editor = tabManager->editorActionTarget(
        invocation.parameters.value(
            QStringLiteral("editorViewId")).toString());
    if (!editor)
        return fail(QStringLiteral("No editor tab is available."));

    const int cursorPosition = invocation.parameters.value(
        QStringLiteral("cursorPosition"), -1).toInt();
    const EditorSemanticContext context =
        editor->editorSemanticContextForPosition(
            cursorPosition, false);
    const QString symbolName = invocation.parameters.value(
        QStringLiteral("symbolName")).toString().trimmed();
    if (symbolName.isEmpty())
        return fail(QStringLiteral("Select a signal to reveal."));

    QString semanticId;
    QString sourceFile = context.fileName;
    int sourceLine = invocation.parameters.value(
        QStringLiteral("line"), context.cursorLine + 1).toInt();
    int sourceColumn = invocation.parameters.value(
        QStringLiteral("column"), context.column + 1).toInt();
    const ProjectSnapshot project =
        workspaceManager->projectSnapshot();
    const auto snapshot =
        SemanticIndex::getInstance()->snapshot();
    if (snapshot) {
        SemanticQueryContext query;
        query.fileName = context.fileName;
        query.moduleName = context.moduleName;
        query.packageName = context.packageName;
        query.cursorLine = context.cursorLine + 1;
        query.cursorPosition = cursorPosition;
        const QList<SemanticSymbolRecord> definitions =
            snapshot->findDefinitionRecords(symbolName, query);
        if (!definitions.isEmpty()) {
            const SemanticSymbolRecord& definition =
                definitions.constFirst();
            semanticId =
                WaveSimulationManifestService::
                    portableSemanticIdentity(
                        definition, project.workspaceRoot);
            sourceFile = definition.location.fileName;
            sourceLine = definition.location.startLine;
            sourceColumn = std::max(
                1, definition.location.startColumn);
        }
    }

    const QString relativeSourceFile = normalizedPortablePath(
        QDir(project.workspaceRoot).relativeFilePath(
            QFileInfo(sourceFile).absoluteFilePath()));
    if (!isPortableRelativePath(relativeSourceFile)) {
        return fail(QStringLiteral(
            "The selected signal source is outside the active workspace."));
    }
    const QString accessPath = invocation.parameters.value(
        QStringLiteral("signalAccessPath"), symbolName).toString();

    purgeClosedWorkspaces();
    const QString workspaceKey =
        EditorFileIdentity::lookupKey(project.workspaceRoot);
    for (auto iterator = workspaces.crbegin();
         iterator != workspaces.crend(); ++iterator) {
        QWidget* workspace = iterator->widget.data();
        if (!workspace
            || EditorFileIdentity::lookupKey(
                   iterator->workspaceRoot) != workspaceKey
            || !workspace->property(
                    "wavewidgets.capabilities")
                    .toStringList()
                    .contains(QStringLiteral(
                        "result-source-navigation/v1"))) {
            continue;
        }
        bool canReveal = false;
        const bool queried = QMetaObject::invokeMethod(
            workspace,
            "canRevealSourceObject",
            Qt::DirectConnection,
            Q_RETURN_ARG(bool, canReveal),
            Q_ARG(QString, semanticId),
            Q_ARG(QString, relativeSourceFile),
            Q_ARG(int, sourceLine),
            Q_ARG(int, sourceColumn),
            Q_ARG(QString, symbolName),
            Q_ARG(QString, accessPath));
        if (!queried || !canReveal)
            continue;

        bool revealed = false;
        const bool invoked = QMetaObject::invokeMethod(
            workspace,
            "revealSourceObject",
            Qt::DirectConnection,
            Q_RETURN_ARG(bool, revealed),
            Q_ARG(QString, semanticId),
            Q_ARG(QString, relativeSourceFile),
            Q_ARG(int, sourceLine),
            Q_ARG(int, sourceColumn),
            Q_ARG(QString, symbolName),
            Q_ARG(QString, accessPath));
        if (!invoked || !revealed)
            continue;
        tabManager->activateToolPage(iterator->stableId);
        return true;
    }
    return fail(QStringLiteral(
        "No open Wave Simulation result contains this signal."));
}

void WaveSimulationResultNavigationCoordinator::
    handleSourceNavigationRequest(
        const QString& sourceFile,
        const int sourceLine,
        const int sourceColumn,
        const QString& semanticId,
        const QString& kind)
{
    Q_UNUSED(semanticId)
    Q_UNUSED(kind)
    if (!workspaceManager || !navigationCoordinator
        || !workspaceManager->isWorkspaceOpen()
        || sourceFile.trimmed().isEmpty()
        || sourceLine <= 0 || sourceColumn <= 0) {
        return;
    }
    const QObject* sourceWorkspace = sender();
    const auto entry = std::find_if(
        workspaces.cbegin(), workspaces.cend(),
        [sourceWorkspace](const EmbeddedWorkspace& candidate) {
            return !candidate.widget.isNull()
                && candidate.widget.data() == sourceWorkspace;
        });
    if (entry == workspaces.cend()
        || EditorFileIdentity::lookupKey(entry->workspaceRoot)
               != EditorFileIdentity::lookupKey(
                   workspaceManager->getWorkspacePath())) {
        emit statusMessageRequested(
            QStringLiteral(
                "The Wave result belongs to a different workspace."),
            5000);
        return;
    }

    const QString relative =
        normalizedPortablePath(sourceFile);
    if (!isPortableRelativePath(relative)) {
        emit statusMessageRequested(
            QStringLiteral(
                "Wave source navigation rejected a non-portable path."),
            5000);
        return;
    }
    const QString root =
        QFileInfo(entry->workspaceRoot).absoluteFilePath();
    const QString candidate = QFileInfo(
        QDir(root).absoluteFilePath(relative)).absoluteFilePath();
    if (!pathBelongsToRoot(candidate, root)) {
        emit statusMessageRequested(
            QStringLiteral(
                "Wave source navigation escaped the active workspace."),
            5000);
        return;
    }
    if (!navigationCoordinator->navigateToFileAndLineAndFlash(
            candidate, sourceLine, sourceColumn)) {
        emit statusMessageRequested(
            QStringLiteral(
                "Wave source location could not be opened."),
            5000);
    }
}

void WaveSimulationResultNavigationCoordinator::removeWorkspace(
    QObject* object)
{
    workspaces.erase(
        std::remove_if(
            workspaces.begin(), workspaces.end(),
            [object](const EmbeddedWorkspace& entry) {
                return entry.widget.isNull()
                    || entry.widget.data() == object;
            }),
        workspaces.end());
}

void WaveSimulationResultNavigationCoordinator::
    purgeClosedWorkspaces()
{
    removeWorkspace(nullptr);
}
