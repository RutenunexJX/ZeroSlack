#include "uitypography.h"
#include "instancepairconnectionpanel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QFont>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kSemanticSignalRole = Qt::UserRole + 8300;
constexpr char kTypedSignalMime[] =
    "application/x-zeroslack-instance-pair-signal";

QString normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return {};
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(fileName).absoluteFilePath()));
}

QString fromUtf8(const std::string& text)
{
    return QString::fromUtf8(
        text.data(), static_cast<qsizetype>(text.size()));
}

QString portDirectionName(SymbolTaxonomy::CollectorKind direction)
{
    using Collector = SymbolTaxonomy::CollectorKind;
    switch (direction) {
    case Collector::PortInput:
        return QStringLiteral("input");
    case Collector::PortOutput:
        return QStringLiteral("output");
    case Collector::PortInout:
        return QStringLiteral("inout");
    case Collector::PortRef:
        return QStringLiteral("ref");
    case Collector::PortInterface:
        return QStringLiteral("interface");
    case Collector::PortInterfaceModport:
        return QStringLiteral("modport");
    default:
        return QStringLiteral("port");
    }
}

QString effectiveTypeName(
    const SemanticElaboratedSymbolInfo& type)
{
    if (!type.resolvedTypeText.isEmpty())
        return type.resolvedTypeText;

    QString result;
    if (type.signedIntegral)
        result = QStringLiteral("signed");
    if (!type.packedDimensionsText.isEmpty()) {
        if (!result.isEmpty())
            result.append(QLatin1Char(' '));
        result.append(type.packedDimensionsText);
    } else if (type.bitWidth > 1) {
        if (!result.isEmpty())
            result.append(QLatin1Char(' '));
        result.append(
            QStringLiteral("[%1:0]").arg(type.bitWidth - 1));
    }
    return result.isEmpty() ? QStringLiteral("scalar") : result;
}

QString failureText(
    const InstancePairConnectionAnalysis& analysis)
{
    if (!analysis.message.isEmpty())
        return analysis.message;
    if (!analysis.blockers.isEmpty())
        return analysis.blockers.join(QLatin1Char('\n'));
    return QStringLiteral(
        "Instance-pair connection analysis is unavailable.");
}

QString proposalFailureText(
    const InstancePairConnectionProposal& proposal)
{
    if (!proposal.message.isEmpty())
        return proposal.message;
    if (!proposal.blockers.isEmpty())
        return proposal.blockers.join(QLatin1Char('\n'));
    return QStringLiteral(
        "Instance-pair connection preview is unavailable.");
}

QString renderDiffFile(const rtledit::SourceDiffFile& file)
{
    QStringList lines;
    lines.append(
        QStringLiteral("--- %1").arg(fromUtf8(file.filePath)));
    lines.append(
        QStringLiteral("+++ %1").arg(fromUtf8(file.filePath)));
    for (const rtledit::SourceDiffHunk& hunk : file.hunks) {
        lines.append(
            QStringLiteral("@@ -%1,%2 +%3,%4 @@")
                .arg(hunk.oldStartLine)
                .arg(hunk.oldLineCount)
                .arg(hunk.newStartLine)
                .arg(hunk.newLineCount));
        for (const rtledit::SourceDiffLine& line : hunk.lines) {
            QChar prefix = QLatin1Char(' ');
            if (line.kind == rtledit::SourceDiffLineKind::Removed)
                prefix = QLatin1Char('-');
            else if (line.kind == rtledit::SourceDiffLineKind::Added)
                prefix = QLatin1Char('+');
            lines.append(prefix + fromUtf8(line.text));
        }
    }
    if (file.hunks.empty())
        lines.append(QStringLiteral("(no changed hunks)"));
    return lines.join(QLatin1Char('\n'));
}

bool snapshotMatchesGeneration(
    const rtledit::SemanticIndexSnapshot& snapshot,
    std::uint64_t generation)
{
    return !snapshot.empty()
        && fromUtf8(snapshot.id)
               == QString::number(generation);
}

class InstancePairSignalTree final : public QTreeWidget
{
public:
    explicit InstancePairSignalTree(QWidget* parent = nullptr)
        : QTreeWidget(parent)
    {
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
        setDefaultDropAction(Qt::CopyAction);
    }

    std::function<InstancePairConnectionPlanRequest()>
        requestProvider;
    std::function<void(bool)> dragStateHandler;

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        QTreeWidgetItem* item = currentItem();
        if (!item
            || !item->data(0, kSemanticSignalRole).toBool()
            || !requestProvider) {
            return;
        }

        const InstancePairConnectionPlanRequest request =
            requestProvider();
        if (!request.isValid())
            return;

        if (dragStateHandler)
            dragStateHandler(true);
        auto* drag = new QDrag(this);
        drag->setMimeData(
            new InstancePairConnectionDragMimeData(request));
        drag->exec(
            supportedActions & Qt::CopyAction,
            Qt::CopyAction);
        if (dragStateHandler)
            dragStateHandler(false);
    }
};

class InstancePairDropTarget final : public QFrame
{
public:
    explicit InstancePairDropTarget(QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setAcceptDrops(true);
        setFocusPolicy(Qt::StrongFocus);
        setFrameShape(QFrame::StyledPanel);
        setFrameShadow(QFrame::Plain);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        title = new QLabel(
            QStringLiteral("Drop left signal on the right instance"),
            this);
        title->setAlignment(Qt::AlignCenter);
        title->setWordWrap(true);
        layout->addWidget(title);
    }

    std::function<bool(
        const InstancePairConnectionPlanRequest&)> validator;
    std::function<void(
        const InstancePairConnectionPlanRequest&)> acceptedHandler;
    std::function<void()> rejectedHandler;
    std::function<void(bool)> dragStateHandler;

    void setDropActive(bool active)
    {
        if (dropActive == active)
            return;
        dropActive = active;
        setProperty("dragActive", active);
        if (title) {
            title->setText(
                active
                    ? QStringLiteral(
                          "Release to request a Change Preview plan")
                    : QStringLiteral(
                          "Drop left signal on the right instance"));
        }
        style()->unpolish(this);
        style()->polish(this);
        update();
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        const auto* typed =
            dynamic_cast<const InstancePairConnectionDragMimeData*>(
                event->mimeData());
        if (!typed || !validator
            || !validator(typed->request())) {
            event->ignore();
            return;
        }
        setDropActive(true);
        if (dragStateHandler)
            dragStateHandler(true);
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        const auto* typed =
            dynamic_cast<const InstancePairConnectionDragMimeData*>(
                event->mimeData());
        if (typed && validator
            && validator(typed->request())) {
            event->setDropAction(Qt::CopyAction);
            event->accept();
            return;
        }
        event->ignore();
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override
    {
        setDropActive(false);
        if (dragStateHandler)
            dragStateHandler(false);
        event->accept();
    }

    void dropEvent(QDropEvent* event) override
    {
        const auto* typed =
            dynamic_cast<const InstancePairConnectionDragMimeData*>(
                event->mimeData());
        const bool accepted =
            typed && validator
            && validator(typed->request());
        setDropActive(false);
        if (dragStateHandler)
            dragStateHandler(false);
        if (!accepted) {
            if (rejectedHandler)
                rejectedHandler();
            event->ignore();
            return;
        }
        if (acceptedHandler)
            acceptedHandler(typed->request());
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }

private:
    QLabel* title = nullptr;
    bool dropActive = false;
};

void addPortItems(
    QTreeWidgetItem* parent,
    const QList<InstancePairBlockPortView>& ports)
{
    for (const InstancePairBlockPortView& port : ports) {
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, port.name);
        item->setText(1, portDirectionName(port.direction));
        item->setText(2, effectiveTypeName(port.effectiveType));
        item->setToolTip(
            0,
            port.record.stableKey.isValid()
                ? port.record.stableKey.toString()
                : port.name);
    }
}

} // namespace

bool InstancePairConnectionPlanRequest::isValid() const
{
    const auto sourceRevision =
        documentRevisions.constFind(
            normalizedFileName(
                leftSignalStableKey.fileName));
    return leftSignalStableKey.isValid()
        && !leftSignalName.isEmpty()
        && leftSignalStableKey.symbolName
            == leftSignalName
        && !leftInstancePath.isEmpty()
        && !leftModuleName.isEmpty()
        && !rightInstancePath.isEmpty()
        && !rightModuleName.isEmpty()
        && !connectionName.trimmed().isEmpty()
        && documentRevision > 0
        && semanticGeneration > 0
        && sourceRevision != documentRevisions.constEnd()
        && sourceRevision.value() == documentRevision;
}

bool InstancePairConnectionPlanRequest::sameContext(
    const InstancePairConnectionPlanRequest& other) const
{
    return leftSignalStableKey
               == other.leftSignalStableKey
        && leftSignalName == other.leftSignalName
        && leftInstancePath == other.leftInstancePath
        && leftModuleName == other.leftModuleName
        && rightInstancePath == other.rightInstancePath
        && rightModuleName == other.rightModuleName
        && connectionName == other.connectionName
        && documentRevision == other.documentRevision
        && semanticGeneration == other.semanticGeneration
        && documentRevisions == other.documentRevisions;
}

InstancePairConnectionDragMimeData::
InstancePairConnectionDragMimeData(
    InstancePairConnectionPlanRequest request)
    : typedRequest(std::move(request))
{
    setData(kTypedSignalMime, QByteArray());
}

QString InstancePairConnectionDragMimeData::mimeType()
{
    return QString::fromLatin1(kTypedSignalMime);
}

const InstancePairConnectionPlanRequest&
InstancePairConnectionDragMimeData::request() const
{
    return typedRequest;
}

InstancePairConnectionPanel::InstancePairConnectionPanel(
    QWidget* parent)
    : QWidget(parent)
{
    qRegisterMetaType<InstancePairConnectionPlanRequest>();
    setObjectName(QStringLiteral("instancePairConnectionPanel"));
    setFocusPolicy(Qt::NoFocus);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto* heading = new QLabel(
        QStringLiteral("Instance Pair Connection"),
        this);
    heading->setObjectName(
        QStringLiteral("instancePairConnectionHeading"));
    QFont headingFont = UiTypography::font(UiTypography::Role::PanelTitle);
    heading->setFont(headingFont);
    root->addWidget(heading);

    stateLabel = new QLabel(this);
    stateLabel->setObjectName(
        QStringLiteral("instancePairConnectionState"));
    stateLabel->setWordWrap(true);
    stateLabel->setText(
        QStringLiteral("No instance-pair analysis is loaded."));
    root->addWidget(stateLabel);

    auto* blockSplitter =
        new QSplitter(Qt::Horizontal, this);
    blockSplitter->setObjectName(
        QStringLiteral("instancePairBlockSplitter"));

    auto buildSide = [&](InstancePairSide side,
                         QLabel** instanceLabel,
                         QLabel** moduleLabel,
                         QTreeWidget** tree) {
        auto* group = new QGroupBox(
            side == InstancePairSide::Left
                ? QStringLiteral("Left source")
                : QStringLiteral("Right target"),
            blockSplitter);
        group->setObjectName(
            side == InstancePairSide::Left
                ? QStringLiteral("instancePairLeftBlock")
                : QStringLiteral("instancePairRightBlock"));
        auto* layout = new QVBoxLayout(group);

        *instanceLabel = new QLabel(group);
        (*instanceLabel)->setObjectName(
            side == InstancePairSide::Left
                ? QStringLiteral("instancePairLeftInstancePath")
                : QStringLiteral("instancePairRightInstancePath"));
        (*instanceLabel)->setTextInteractionFlags(
            Qt::TextSelectableByMouse);
        (*instanceLabel)->setWordWrap(true);
        layout->addWidget(*instanceLabel);

        *moduleLabel = new QLabel(group);
        (*moduleLabel)->setObjectName(
            side == InstancePairSide::Left
                ? QStringLiteral("instancePairLeftModule")
                : QStringLiteral("instancePairRightModule"));
        (*moduleLabel)->setTextInteractionFlags(
            Qt::TextSelectableByMouse);
        layout->addWidget(*moduleLabel);

        if (side == InstancePairSide::Left) {
            auto* signalTree =
                new InstancePairSignalTree(group);
            *tree = signalTree;
            signalTree->requestProvider = [this] {
                return requestContext;
            };
            signalTree->dragStateHandler =
                [this](bool active) {
                    setDragActive(active);
                };
        } else {
            *tree = new QTreeWidget(group);
        }
        (*tree)->setObjectName(
            side == InstancePairSide::Left
                ? QStringLiteral("instancePairLeftItems")
                : QStringLiteral("instancePairRightItems"));
        (*tree)->setColumnCount(3);
        (*tree)->setHeaderLabels(
            {QStringLiteral("Signal / port"),
             QStringLiteral("Role"),
             QStringLiteral("Type")});
        (*tree)->setRootIsDecorated(true);
        (*tree)->setAlternatingRowColors(true);
        (*tree)->setSelectionMode(
            QAbstractItemView::SingleSelection);
        (*tree)->header()->setSectionResizeMode(
            0, QHeaderView::Stretch);
        (*tree)->header()->setSectionResizeMode(
            1, QHeaderView::ResizeToContents);
        (*tree)->header()->setSectionResizeMode(
            2, QHeaderView::ResizeToContents);
        layout->addWidget(*tree, 1);

        if (side == InstancePairSide::Right) {
            auto* target =
                new InstancePairDropTarget(group);
            target->setObjectName(
                QStringLiteral("instancePairRightDropTarget"));
            target->validator =
                [this](
                    const InstancePairConnectionPlanRequest&
                        request) {
                    return acceptsDropRequest(request);
                };
            target->acceptedHandler =
                [this](
                    const InstancePairConnectionPlanRequest&
                        request) {
                    stateLabel->setText(
                        QStringLiteral(
                            "Planning requested for %1 → %2.")
                            .arg(request.leftInstancePath,
                                 request.rightInstancePath));
                    emit planRequested(request);
                };
            target->rejectedHandler = [this] {
                rejectStalePayload(
                    QStringLiteral(
                        "The drag token does not match the current "
                        "semantic or document revision."));
            };
            target->dragStateHandler =
                [this](bool active) {
                    setDragActive(active);
                };
            dropTarget = target;
            layout->addWidget(target);
        }
        return group;
    };

    blockSplitter->addWidget(
        buildSide(
            InstancePairSide::Left,
            &leftInstanceLabel,
            &leftModuleLabel,
            &leftItems));
    blockSplitter->addWidget(
        buildSide(
            InstancePairSide::Right,
            &rightInstanceLabel,
            &rightModuleLabel,
            &rightItems));
    blockSplitter->setStretchFactor(0, 1);
    blockSplitter->setStretchFactor(1, 1);
    root->addWidget(blockSplitter, 2);

    auto* previewGroup = new QGroupBox(
        QStringLiteral("Change Preview transaction preview"),
        this);
    previewGroup->setObjectName(
        QStringLiteral("instancePairTransactionPreview"));
    auto* previewLayout = new QVBoxLayout(previewGroup);

    transactionSummary = new QLabel(previewGroup);
    transactionSummary->setObjectName(
        QStringLiteral("instancePairTransactionSummary"));
    transactionSummary->setWordWrap(true);
    transactionSummary->setText(
        QStringLiteral(
            "No transaction plan has been received."));
    previewLayout->addWidget(transactionSummary);

    diffTabs = new QTabWidget(previewGroup);
    diffTabs->setObjectName(
        QStringLiteral("instancePairDiffTabs"));
    diffTabs->setDocumentMode(true);
    previewLayout->addWidget(diffTabs, 1);

    auto* actions = new QHBoxLayout;
    actions->addStretch(1);
    previewButton = new QPushButton(
        QStringLiteral("Preview Change Preview"),
        previewGroup);
    previewButton->setObjectName(
        QStringLiteral("instancePairPreviewButton"));
    previewButton->setEnabled(false);
    confirmButton = new QPushButton(
        QStringLiteral("Confirm Transaction"),
        previewGroup);
    confirmButton->setObjectName(
        QStringLiteral("instancePairConfirmButton"));
    confirmButton->setEnabled(false);
    undoButton = new QPushButton(
        QStringLiteral("Undo Applied Transaction"),
        previewGroup);
    undoButton->setObjectName(
        QStringLiteral("instancePairUndoButton"));
    undoButton->setEnabled(false);
    actions->addWidget(previewButton);
    actions->addWidget(confirmButton);
    actions->addWidget(undoButton);
    previewLayout->addLayout(actions);
    root->addWidget(previewGroup, 3);

    connect(
        previewButton,
        &QPushButton::clicked,
        this,
        [this] {
            if (!requestContext.isValid())
                return;
            emit previewRequested(requestContext);
        });
    connect(
        confirmButton,
        &QPushButton::clicked,
        this,
        [this] {
            if (!currentProposal
                || !currentProposal->ready()
                || !requestContext.isValid()) {
                return;
            }
            stateLabel->setText(
                QStringLiteral(
                    "Confirmation requested. No source was changed "
                    "by this panel."));
            emit confirmRequested(requestContext);
        });
    connect(
        undoButton,
        &QPushButton::clicked,
        this,
        &InstancePairConnectionPanel::undoRequested);

    installEventFilter(this);
    for (QWidget* child : findChildren<QWidget*>())
        child->installEventFilter(this);
}

void InstancePairConnectionPanel::setAnalysis(
    const InstancePairConnectionAnalysis& analysis)
{
    cancelDragState();
    currentAnalysis = analysis;
    rebuildRequestContext();
    rebuildBlocks();
    clearProposal();

    const bool ready =
        currentAnalysis.ready()
        && requestContext.isValid();
    previewButton->setEnabled(ready);
    stateLabel->setText(
        ready
            ? QStringLiteral(
                  "Drag the selected left signal to the right "
                  "instance, or request an explicit Change Preview preview.")
            : QStringLiteral("Blocked: %1")
                  .arg(failureText(currentAnalysis)));
}

bool InstancePairConnectionPanel::setProposal(
    const InstancePairConnectionProposal& proposal)
{
    QString mismatchReason;
    if (!proposalMatchesCurrentContext(
            proposal, &mismatchReason)) {
        clearProposal();
        rejectStalePayload(mismatchReason);
        return false;
    }

    currentProposal = proposal;
    if (!proposal.ready()) {
        renderRejectedProposal(proposal);
        return true;
    }

    renderProposal(proposal);
    return true;
}

void InstancePairConnectionPanel::clearProposal()
{
    currentProposal.reset();
    clearDiffTabs();
    if (transactionSummary) {
        transactionSummary->setText(
            QStringLiteral(
                "No transaction plan has been received."));
    }
    if (confirmButton)
        confirmButton->setEnabled(false);
    if (undoButton)
        undoButton->setEnabled(false);
}

void InstancePairConnectionPanel::cancelDragState()
{
    const bool wasActive = activeDrag;
    setDragActive(false);
    if (auto* target =
            static_cast<InstancePairDropTarget*>(dropTarget)) {
        target->setDropActive(false);
    }
    if (wasActive && stateLabel) {
        stateLabel->setText(
            QStringLiteral(
                "Signal drag cancelled; no plan was requested."));
    }
}

const InstancePairConnectionAnalysis&
InstancePairConnectionPanel::analysis() const
{
    return currentAnalysis;
}

const InstancePairConnectionProposal*
InstancePairConnectionPanel::proposalForConfirmation() const
{
    return currentProposal ? &*currentProposal : nullptr;
}

InstancePairConnectionPlanRequest
InstancePairConnectionPanel::currentPlanRequest() const
{
    return requestContext;
}

bool InstancePairConnectionPanel::hasDisplayableProposal() const
{
    return currentProposal.has_value()
        && currentProposal->ready();
}

bool InstancePairConnectionPanel::dragActive() const
{
    return activeDrag;
}

int InstancePairConnectionPanel::diffFileCount() const
{
    return diffTabs ? diffTabs->count() : 0;
}

void InstancePairConnectionPanel::setWorkflowOutcome(
    const QString& message,
    bool applied,
    bool undoAvailable)
{
    if (stateLabel)
        stateLabel->setText(message);
    if (confirmButton)
        confirmButton->setEnabled(false);
    if (undoButton)
        undoButton->setEnabled(
            applied && undoAvailable);
}

bool InstancePairConnectionPanel::eventFilter(
    QObject* watched,
    QEvent* event)
{
    if (event
        && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape
            && activeDrag) {
            cancelDragState();
            keyEvent->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void InstancePairConnectionPanel::rebuildBlocks()
{
    const InstancePairBlockView& view =
        currentAnalysis.blockView;
    auto setSideLabels =
        [](const InstancePairBlockSideView& side,
           QLabel* instanceLabel,
           QLabel* moduleLabel) {
            if (instanceLabel) {
                instanceLabel->setText(
                    QStringLiteral("Instance: %1")
                        .arg(
                            side.instancePath.isEmpty()
                                ? QStringLiteral("<unavailable>")
                                : side.instancePath));
                instanceLabel->setToolTip(
                    side.instanceFile);
            }
            if (moduleLabel) {
                moduleLabel->setText(
                    QStringLiteral("Module: %1")
                        .arg(
                            side.moduleName.isEmpty()
                                ? QStringLiteral("<unavailable>")
                                : side.moduleName));
                moduleLabel->setToolTip(
                    side.definitionFile);
            }
        };
    setSideLabels(
        view.left, leftInstanceLabel, leftModuleLabel);
    setSideLabels(
        view.right, rightInstanceLabel, rightModuleLabel);

    if (leftItems) {
        leftItems->clear();
        auto* signalGroup = new QTreeWidgetItem(leftItems);
        signalGroup->setText(0, QStringLiteral("Signals"));
        signalGroup->setFirstColumnSpanned(true);
        auto* selected = new QTreeWidgetItem(signalGroup);
        selected->setText(
            0,
            currentAnalysis.leftSignal.name.isEmpty()
                ? QStringLiteral("<unavailable>")
                : currentAnalysis.leftSignal.name);
        selected->setText(1, QStringLiteral("selected source"));
        selected->setText(
            2, currentAnalysis.renderedSignalType);
        selected->setData(
            0, kSemanticSignalRole, true);
        selected->setToolTip(
            0,
            currentAnalysis.leftSignal.stableKey.isValid()
                ? currentAnalysis.leftSignal.stableKey.toString()
                : QString());

        auto* ports = new QTreeWidgetItem(leftItems);
        ports->setText(
            0,
            QStringLiteral("Ports (%1)")
                .arg(view.left.ports.size()));
        ports->setFirstColumnSpanned(true);
        addPortItems(ports, view.left.ports);
        leftItems->expandAll();
        leftItems->setCurrentItem(selected);
    }

    if (rightItems) {
        rightItems->clear();
        auto* targetSignal =
            new QTreeWidgetItem(rightItems);
        targetSignal->setText(
            0, QStringLiteral("Target signal"));
        targetSignal->setFirstColumnSpanned(true);
        auto* proposed =
            new QTreeWidgetItem(targetSignal);
        proposed->setText(
            0,
            currentAnalysis.query.connectionName.isEmpty()
                ? QStringLiteral("<planned connection>")
                : currentAnalysis.query.connectionName);
        proposed->setText(
            1, QStringLiteral("planned"));
        proposed->setText(
            2, currentAnalysis.renderedSignalType);

        auto* ports = new QTreeWidgetItem(rightItems);
        ports->setText(
            0,
            QStringLiteral("Ports (%1)")
                .arg(view.right.ports.size()));
        ports->setFirstColumnSpanned(true);
        addPortItems(ports, view.right.ports);
        rightItems->expandAll();
    }
}

void InstancePairConnectionPanel::rebuildRequestContext()
{
    requestContext = {};
    requestContext.leftSignalStableKey =
        currentAnalysis.leftSignal.stableKey;
    requestContext.leftSignalName =
        currentAnalysis.leftSignal.name;
    requestContext.leftInstancePath =
        currentAnalysis.blockView.left.instancePath;
    requestContext.leftModuleName =
        currentAnalysis.blockView.left.moduleName;
    requestContext.rightInstancePath =
        currentAnalysis.blockView.right.instancePath;
    requestContext.rightModuleName =
        currentAnalysis.blockView.right.moduleName;
    requestContext.connectionName =
        currentAnalysis.query.connectionName;
    requestContext.semanticGeneration =
        currentAnalysis.blockView.semanticGeneration;

    for (auto it =
             currentAnalysis.capturedDocuments.constBegin();
         it != currentAnalysis.capturedDocuments.constEnd();
         ++it) {
        const InstancePairDocumentSnapshot& document =
            it.value();
        const QString fileName = normalizedFileName(
            document.fileName.isEmpty()
                ? it.key() : document.fileName);
        if (!fileName.isEmpty()
            && document.revision > 0) {
            requestContext.documentRevisions.insert(
                fileName, document.revision);
        }
    }
    if (requestContext.documentRevisions.isEmpty()) {
        for (auto it =
                 currentAnalysis.query.documents.constBegin();
             it != currentAnalysis.query.documents.constEnd();
             ++it) {
            const InstancePairDocumentSnapshot& document =
                it.value();
            const QString fileName = normalizedFileName(
                document.fileName.isEmpty()
                    ? it.key() : document.fileName);
            if (!fileName.isEmpty()
                && document.revision > 0) {
                requestContext.documentRevisions.insert(
                    fileName, document.revision);
            }
        }
    }

    const QString signalFile = normalizedFileName(
        currentAnalysis.leftSignal.location.fileName);
    requestContext.documentRevision =
        requestContext.documentRevisions.value(
            signalFile,
            currentAnalysis.query.leftSignalContext
                .documentRevision);
}

void InstancePairConnectionPanel::renderProposal(
    const InstancePairConnectionProposal& proposal)
{
    clearDiffTabs();
    const rtledit::WorkspaceEditPreview& preview =
        proposal.transaction.preview;
    transactionSummary->setText(
        QStringLiteral(
            "Risk: High  |  Preview: Diff  |  Files: %1  |  "
            "Edits: %2  |  Confirmed: no")
            .arg(preview.fileCount)
            .arg(preview.editCount));

    int fileIndex = 0;
    for (const rtledit::SourceDiffFile& file :
         proposal.sourceDiff.files) {
        auto* view = new QPlainTextEdit(diffTabs);
        view->setObjectName(
            QStringLiteral("instancePairDiffFile_%1")
                .arg(fileIndex));
        view->setReadOnly(true);
        view->setLineWrapMode(
            QPlainTextEdit::NoWrap);
        view->setPlainText(renderDiffFile(file));
        const QString fileName =
            fromUtf8(file.filePath);
        const QString leaf =
            QFileInfo(fileName).fileName();
        const int tab = diffTabs->addTab(
            view,
            leaf.isEmpty() ? fileName : leaf);
        diffTabs->setTabToolTip(tab, fileName);
        ++fileIndex;
    }
    stateLabel->setText(
        proposal.dryRun || proposal.transaction.dryRun
            ? QStringLiteral(
                  "Dry-run Change Preview preview is ready; workspace "
                  "mutation is disabled.")
            : QStringLiteral(
                  "Change Preview preview is ready. Confirmation remains "
                  "an explicit external transaction request."));
    confirmButton->setEnabled(
        !proposal.dryRun
        && !proposal.transaction.dryRun);
}

void InstancePairConnectionPanel::renderRejectedProposal(
    const InstancePairConnectionProposal& proposal)
{
    clearDiffTabs();
    transactionSummary->setText(
        QStringLiteral("Preview blocked: %1")
            .arg(proposalFailureText(proposal)));
    stateLabel->setText(
        QStringLiteral("Blocked: %1")
            .arg(proposalFailureText(proposal)));
    confirmButton->setEnabled(false);
}

void InstancePairConnectionPanel::clearDiffTabs()
{
    if (!diffTabs)
        return;
    while (diffTabs->count() > 0) {
        QWidget* page = diffTabs->widget(0);
        diffTabs->removeTab(0);
        delete page;
    }
}

void InstancePairConnectionPanel::setDragActive(
    bool active)
{
    activeDrag = active;
    setProperty("dragActive", active);
}

bool InstancePairConnectionPanel::acceptsDropRequest(
    const InstancePairConnectionPlanRequest& request) const
{
    return request.isValid()
        && requestContext.isValid()
        && request.sameContext(requestContext);
}

bool InstancePairConnectionPanel::
proposalMatchesCurrentContext(
    const InstancePairConnectionProposal& proposal,
    QString* reason) const
{
    auto reject = [reason](const QString& message) {
        if (reason)
            *reason = message;
        return false;
    };

    if (!currentAnalysis.ready()
        || !requestContext.isValid()) {
        return reject(
            QStringLiteral(
                "No current analyzed instance-pair context exists."));
    }
    if (proposal.blockView.semanticGeneration
            != requestContext.semanticGeneration
        || proposal.blockView.left.instancePath
            != requestContext.leftInstancePath
        || proposal.blockView.left.moduleName
            != requestContext.leftModuleName
        || proposal.blockView.right.instancePath
            != requestContext.rightInstancePath
        || proposal.blockView.right.moduleName
            != requestContext.rightModuleName
        || !(proposal.leftSignal.stableKey
             == requestContext.leftSignalStableKey)
        || proposal.connectionName
            != requestContext.connectionName) {
        return reject(
            QStringLiteral(
                "The proposal semantic identity or instance context "
                "is stale."));
    }

    if (!proposal.ready())
        return true;
    if (!snapshotMatchesGeneration(
            proposal.workspaceEdit.semanticSnapshot,
            requestContext.semanticGeneration)
        || !snapshotMatchesGeneration(
            proposal.transaction.plan.semanticSnapshot,
            requestContext.semanticGeneration)
        || (proposal.transaction.preview.semanticSnapshot.empty()
                ? false
                : !snapshotMatchesGeneration(
                      proposal.transaction.preview.semanticSnapshot,
                      requestContext.semanticGeneration))
        || (proposal.sourceDiff.semanticSnapshot.empty()
                ? false
                : !snapshotMatchesGeneration(
                      proposal.sourceDiff.semanticSnapshot,
                      requestContext.semanticGeneration))
        || (proposal.transaction.sourceDiff
                    .semanticSnapshot.empty()
                ? false
                : !snapshotMatchesGeneration(
                      proposal.transaction.sourceDiff
                          .semanticSnapshot,
                      requestContext.semanticGeneration))) {
        return reject(
            QStringLiteral(
                "The proposal semantic generation is stale."));
    }
    if (proposal.workspaceEdit.riskLevel
            != rtledit::RiskLevel::High
        || proposal.workspaceEdit.previewPolicy
            != rtledit::PreviewPolicy::Diff
        || proposal.transaction.plan.riskLevel
            != rtledit::RiskLevel::High
        || proposal.transaction.plan.previewPolicy
            != rtledit::PreviewPolicy::Diff
        || proposal.transaction.preview.riskLevel
            != rtledit::RiskLevel::High
        || proposal.transaction.preview.previewPolicy
            != rtledit::PreviewPolicy::Diff
        || proposal.transaction.previewConfirmed) {
        return reject(
            QStringLiteral(
                "Only an unconfirmed Change Preview transaction may be "
                "displayed."));
    }

    auto baselinesMatch =
        [this](const std::vector<rtledit::DocumentBaseline>&
                   baselines) {
            if (baselines.empty())
                return false;
            for (const rtledit::DocumentBaseline& baseline :
                 baselines) {
                const QString fileName = normalizedFileName(
                    fromUtf8(baseline.filePath));
                const auto found =
                    requestContext.documentRevisions.constFind(
                        fileName);
                if (found
                        == requestContext.documentRevisions
                               .constEnd()
                    || found.value()
                           != baseline.version.value) {
                    return false;
                }
            }
            return true;
        };
    if (!baselinesMatch(proposal.workspaceEdit.baselines)
        || !baselinesMatch(
            proposal.transaction.plan.baselines)) {
        return reject(
            QStringLiteral(
                "A proposal document baseline no longer matches the "
                "captured revisions."));
    }

    auto diffMatchesRevisions =
        [this](const rtledit::WorkspaceEditSourceDiff& diff) {
            if (!diff.built() || diff.files.empty())
                return false;
            for (const rtledit::SourceDiffFile& file :
                 diff.files) {
                const QString fileName =
                    normalizedFileName(
                        fromUtf8(file.filePath));
                const auto found =
                    requestContext.documentRevisions
                        .constFind(fileName);
                if (found
                        == requestContext
                               .documentRevisions.constEnd()
                    || found.value()
                           != file.version.value) {
                    return false;
                }
            }
            return true;
        };
    if (!diffMatchesRevisions(proposal.sourceDiff)
        || !diffMatchesRevisions(
            proposal.transaction.sourceDiff)) {
        return reject(
            QStringLiteral(
                "A diff file no longer matches its captured "
                "document revision."));
    }
    std::size_t displayedEditCount = 0;
    for (const rtledit::SourceDiffFile& file :
         proposal.sourceDiff.files) {
        displayedEditCount += file.editCount;
    }
    if (proposal.transaction.preview.fileCount
            != proposal.sourceDiff.files.size()
        || proposal.transaction.preview.editCount
            != displayedEditCount) {
        return reject(
            QStringLiteral(
                "The transaction summary and source diff disagree."));
    }
    if (proposal.sourceDiff.files.size()
            != proposal.transaction.sourceDiff.files.size()) {
        return reject(
            QStringLiteral(
                "The displayed diff and transaction diff disagree."));
    }
    for (std::size_t index = 0;
         index < proposal.sourceDiff.files.size();
         ++index) {
        const auto& displayed =
            proposal.sourceDiff.files[index];
        const auto& transaction =
            proposal.transaction.sourceDiff.files[index];
        if (displayed.filePath != transaction.filePath
            || displayed.version != transaction.version
            || displayed.editCount
                != transaction.editCount) {
            return reject(
                QStringLiteral(
                    "The displayed diff and transaction diff "
                    "disagree."));
        }
    }
    const QString displayedCanonicalDiff =
        fromUtf8(
            rtledit::renderWorkspaceEditSourceDiffHunks(
                proposal.sourceDiff));
    const QString transactionCanonicalDiff =
        fromUtf8(
            rtledit::renderWorkspaceEditSourceDiffHunks(
                proposal.transaction.sourceDiff));
    if (displayedCanonicalDiff
            != transactionCanonicalDiff
        || proposal.renderedDiff
            != transactionCanonicalDiff) {
        return reject(
            QStringLiteral(
                "The rendered Change Preview preview does not match the "
                "prepared transaction."));
    }
    return true;
}

void InstancePairConnectionPanel::rejectStalePayload(
    const QString& reason)
{
    if (stateLabel) {
        stateLabel->setText(
            QStringLiteral("Stale request rejected: %1")
                .arg(reason));
    }
}

InstancePairConnectionCoordinator::
InstancePairConnectionCoordinator(
    QStackedWidget* host,
    QObject* parent)
    : QObject(parent),
      hostStack(host)
{
    ensurePanel();
}

void InstancePairConnectionCoordinator::wirePanel(
    InstancePairConnectionPanel* panel)
{
    if (!panel)
        return;
    connect(
        panel,
        &InstancePairConnectionPanel::planRequested,
        this,
        &InstancePairConnectionCoordinator::planRequested,
        Qt::UniqueConnection);
    connect(
        panel,
        &InstancePairConnectionPanel::previewRequested,
        this,
        &InstancePairConnectionCoordinator::previewRequested,
        Qt::UniqueConnection);
    connect(
        panel,
        &InstancePairConnectionPanel::confirmRequested,
        this,
        &InstancePairConnectionCoordinator::confirmRequested,
        Qt::UniqueConnection);
    connect(
        panel,
        &InstancePairConnectionPanel::undoRequested,
        this,
        &InstancePairConnectionCoordinator::undoRequested,
        Qt::UniqueConnection);
}

InstancePairConnectionPanel*
InstancePairConnectionCoordinator::panel() const
{
    return panelWidget;
}

QString InstancePairConnectionCoordinator::panelId()
{
    return QStringLiteral("instancePairConnection");
}

void InstancePairConnectionCoordinator::presentAnalysis(
    const InstancePairConnectionAnalysis& analysis)
{
    if (InstancePairConnectionPanel* page = ensurePanel())
        page->setAnalysis(analysis);
}

bool InstancePairConnectionCoordinator::presentProposal(
    const InstancePairConnectionProposal& proposal)
{
    InstancePairConnectionPanel* page = ensurePanel();
    return page && page->setProposal(proposal);
}

void InstancePairConnectionCoordinator::clearProposal()
{
    if (panelWidget)
        panelWidget->clearProposal();
}

InstancePairConnectionPanel*
InstancePairConnectionCoordinator::ensurePanel()
{
    if (panelWidget) {
        if (hostStack
            && hostStack->indexOf(panelWidget) < 0) {
            hostStack->addWidget(panelWidget);
        }
        wirePanel(panelWidget);
        return panelWidget;
    }
    if (!hostStack)
        return nullptr;

    for (int index = 0;
         index < hostStack->count();
         ++index) {
        auto* existing =
            qobject_cast<InstancePairConnectionPanel*>(
                hostStack->widget(index));
        if (!existing)
            continue;
        panelWidget = existing;
        wirePanel(panelWidget);
        return panelWidget;
    }

    panelWidget =
        new InstancePairConnectionPanel(hostStack);
    hostStack->addWidget(panelWidget);
    wirePanel(panelWidget);
    return panelWidget;
}
