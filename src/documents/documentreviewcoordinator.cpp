#include "documentreviewcoordinator.h"
#include "actionregistry.h"
#include "tabmanager.h"
#include "mycodeeditor.h"
#include "notificationcenter.h"
#include "crashrecoveryservice.h"
#include "externaldocumentsynccontroller.h"
#include "uicontrols.h"
#include "uidialogs.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTextCursor>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <utility>

namespace {
QString workspaceSessionRootKey(const QString& path)
{
    if (path.isEmpty())
        return QString();
    QString normalized = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

QString crashRecoverySourceStateText(
    CrashRecoverySourceState state)
{
    switch (state) {
    case CrashRecoverySourceState::Untitled:
        return QStringLiteral("untitled");
    case CrashRecoverySourceState::Missing:
        return QStringLiteral("source missing");
    case CrashRecoverySourceState::Unreadable:
        return QStringLiteral("source unreadable");
    case CrashRecoverySourceState::BaselineUnavailable:
        return QStringLiteral("baseline unavailable");
    case CrashRecoverySourceState::UnchangedSinceBaseline:
        return QStringLiteral("unchanged source");
    case CrashRecoverySourceState::ExternallyModified:
        return QStringLiteral("externally modified");
    }
    return QStringLiteral("unknown source state");
}

QString crashRecoveryDocumentLabel(
    const CrashRecoveryCandidate& candidate)
{
    if (!candidate.originalFilePath.isEmpty())
        return QDir::toNativeSeparators(candidate.originalFilePath);
    if (!candidate.untitledDocumentId.isEmpty()) {
        return QStringLiteral("Untitled (%1)")
            .arg(candidate.untitledDocumentId);
    }
    return QStringLiteral("Untitled");
}
}
DocumentReviewCoordinator::DocumentReviewCoordinator(TabManager* documents, QWidget* host)
    : QObject(host), hostWindow(host), tabManager(documents) {}
DocumentReviewCoordinator::~DocumentReviewCoordinator()
{
    delete crashRecoveryReviewDialog.data();
    delete externalConflictReviewBar.data();
}
void DocumentReviewCoordinator::bindNotifications(NotificationCenter* notifications)
{
    if (!notifications || notificationCenter == notifications) return;
    Q_ASSERT(!notificationCenter);
    notificationCenter = notifications;
    if (tabManager) {
        connect(tabManager.data(),
                &TabManager::fileSaveFailed,
                this,
                [this](const QString& fileName,
                       const QString& failureReason) {
                    NotificationDraft draft;
                    draft.key =
                        QStringLiteral("save:%1").arg(fileName);
                    draft.topic = NotificationTopic::Save;
                    draft.severity = NotificationSeverity::Error;
                    draft.source =
                        QStringLiteral("DocumentSave");
                    draft.message = failureReason;
                    notificationCenter->post(draft);
                });
        connect(tabManager.data(),
                &TabManager::fileSaved,
                this,
                [this](const QString& fileName) {
                    notificationCenter->dismissByKey(
                        QStringLiteral("save:%1")
                            .arg(fileName));
                });
        connect(tabManager.data(),
                &TabManager::externalFileConflict,
                this,
                [this](const QString& fileName) {
                    NotificationDraft draft;
                    draft.key =
                        QStringLiteral("external:%1").arg(fileName);
                    draft.topic =
                        NotificationTopic::ExternalModification;
                    draft.severity =
                        NotificationSeverity::Critical;
                    draft.source =
                        QStringLiteral("ExternalDocumentSync");
                    draft.message = QStringLiteral(
                        "Local and external changes conflict: %1")
                                        .arg(fileName);
                    draft.actions = {
                        {QStringLiteral("external.review"),
                         QStringLiteral("Compare...")},
                        {QStringLiteral("external.keep-local"),
                         QStringLiteral("Keep Local")},
                        {QStringLiteral("external.reload"),
                         QStringLiteral("Reload External")},
                        {QStringLiteral("external.save-as"),
                         QStringLiteral("Save Local As...")}};
                    const NotificationPostResult posted =
                        notificationCenter->post(draft);
                    externalConflictNotificationFiles.insert(
                        posted.id,
                        fileName);
                    if (externalConflictReviewBar
                        && externalConflictReviewBar->isVisible()
                        && reviewedExternalConflict
                        && reviewedExternalConflict->fileName
                               == fileName) {
                        openExternalConflictReview(
                            fileName);
                    }
                });
        connect(tabManager.data(),
                &TabManager::externalFileUnavailable,
                this,
                [this](const QString& fileName,
                       const QString& failureReason) {
                    NotificationDraft draft;
                    draft.key =
                        QStringLiteral("external:%1").arg(fileName);
                    draft.topic =
                        NotificationTopic::ExternalModification;
                    draft.severity =
                        NotificationSeverity::Warning;
                    draft.source =
                        QStringLiteral("ExternalDocumentSync");
                    draft.message = QStringLiteral("%1: %2")
                                        .arg(fileName,
                                             failureReason);
                    const ExternalDocumentConflictReview review =
                        tabManager
                        ? tabManager->externalConflictReview(
                              fileName)
                        : ExternalDocumentConflictReview();
                    if (review.valid) {
                        draft.actions = {
                            {QStringLiteral("external.review"),
                             QStringLiteral("Compare...")},
                            {QStringLiteral("external.keep-local"),
                             QStringLiteral("Keep Local")},
                            {QStringLiteral("external.reload"),
                             QStringLiteral("Reload External")},
                            {QStringLiteral("external.save-as"),
                             QStringLiteral("Save Local As...")}};
                    }
                    const NotificationPostResult posted =
                        notificationCenter->post(draft);
                    if (review.valid) {
                        externalConflictNotificationFiles.insert(
                            posted.id,
                            fileName);
                        if (externalConflictReviewBar
                            && externalConflictReviewBar
                                   ->isVisible()
                            && reviewedExternalConflict
                            && reviewedExternalConflict
                                   ->fileName
                                   == fileName) {
                            openExternalConflictReview(
                                fileName);
                        }
                    }
                });
        connect(tabManager.data(),
                &TabManager::externalFileReloaded,
                this,
                [this](const QString& fileName) {
                    notificationCenter->dismissByKey(
                        QStringLiteral("external:%1")
                            .arg(fileName));
                    if (reviewedExternalConflict
                        && reviewedExternalConflict->fileName
                               == fileName) {
                        closeExternalConflictReview();
                    }
                });
        connect(tabManager.data(),
                &TabManager::crashRecoveryCandidatesAvailable,
                this,
                &DocumentReviewCoordinator::notifyCrashRecoveryCandidates);
        connect(tabManager.data(),
                &TabManager::crashRecoveryOperationFailed,
                this,
                &DocumentReviewCoordinator::postCrashRecoveryFailure);
    }
    connect(notificationCenter.data(),
            &NotificationCenter::actionRequested,
            this,
            [this](const QString& notificationId,
                   const QString& actionId) {
                const QString conflictFile =
                    externalConflictNotificationFiles
                        .value(notificationId);
                if (!conflictFile.isEmpty()) {
                    if (actionId
                        == QStringLiteral(
                            "external.review")) {
                        openExternalConflictReview(
                            conflictFile);
                    } else {
                        ExternalDocumentConflictReview review =
                            tabManager
                            ? tabManager->externalConflictReview(
                                  conflictFile)
                            : ExternalDocumentConflictReview();
                        if (!review.valid) {
                            postExternalConflictActionFailure(
                                conflictFile,
                                review.failureReason);
                            return;
                        }
                        reviewedExternalConflict =
                            std::make_unique<
                                ExternalDocumentConflictReview>(
                                std::move(review));
                        if (actionId
                            == QStringLiteral(
                                "external.keep-local")) {
                            keepReviewedExternalConflict();
                        } else if (actionId
                                   == QStringLiteral(
                                       "external.reload")) {
                            reloadReviewedExternalConflict();
                        } else if (actionId
                                   == QStringLiteral(
                                       "external.save-as")) {
                            saveReviewedExternalConflictAs();
                        }
                    }
                    return;
                }
                if (actionId
                    != QString::fromLatin1(
                        ActionIds::
                            ReviewCrashRecovery)) {
                    return;
                }
                const QString workspaceRoot =
                    crashRecoveryNotificationWorkspaces
                        .value(notificationId);
                if (!workspaceRoot.isEmpty())
                    openCrashRecoveryReview(workspaceRoot);
            });
    connect(notificationCenter.data(),
            &NotificationCenter::notificationRemoved,
            this,
            [this](const NotificationItem& item,
                   NotificationRemovalReason) {
                crashRecoveryNotificationWorkspaces
                    .remove(item.id);
                externalConflictNotificationFiles
                    .remove(item.id);
            });
}

void DocumentReviewCoordinator::setupExternalConflictReviewUi(
    QVBoxLayout* editorLayout,
    QWidget* parent)
{
    if (!editorLayout || externalConflictReviewBar)
        return;

    externalConflictReviewBar =
        new QWidget(parent ? parent : hostWindow.data());
    externalConflictReviewBar->setObjectName(
        QStringLiteral("externalConflictReview"));
    externalConflictReviewBar->setVisible(false);
    externalConflictReviewBar->setMinimumHeight(180);
    externalConflictReviewBar->setMaximumHeight(330);

    auto* outer =
        new QVBoxLayout(externalConflictReviewBar);
    outer->setContentsMargins(8, 6, 8, 8);
    outer->setSpacing(5);

    auto* heading = new QHBoxLayout();
    externalConflictReviewTitle =
        UiControls::label(externalConflictReviewBar);
    externalConflictReviewTitle->setObjectName(
        QStringLiteral("externalConflictReviewTitle"));
    externalConflictReviewTitle->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    heading->addWidget(externalConflictReviewTitle, 1);
    auto* closeButton =
        UiControls::pushButton(
            QStringLiteral("Close"),
            externalConflictReviewBar);
    closeButton->setObjectName(
        QStringLiteral("externalConflictCloseButton"));
    heading->addWidget(closeButton);
    outer->addLayout(heading);

    auto* comparison =
        new QSplitter(
            Qt::Horizontal,
            externalConflictReviewBar);
    comparison->setObjectName(
        QStringLiteral("externalConflictComparison"));
    auto* localGroup =
        new QGroupBox(
            QStringLiteral("Local buffer (unsaved)"),
            comparison);
    auto* localLayout =
        new QVBoxLayout(localGroup);
    localLayout->setContentsMargins(4, 4, 4, 4);
    externalConflictLocalText =
        UiControls::readOnlyText(localGroup);
    externalConflictLocalText->setObjectName(
        QStringLiteral("externalConflictLocalText"));
    externalConflictLocalText->setReadOnly(true);
    externalConflictLocalText->setPlaceholderText(
        QStringLiteral("Local buffer"));
    localLayout->addWidget(
        externalConflictLocalText);
    auto* diskGroup =
        new QGroupBox(
            QStringLiteral("External file (disk)"),
            comparison);
    auto* diskLayout =
        new QVBoxLayout(diskGroup);
    diskLayout->setContentsMargins(4, 4, 4, 4);
    externalConflictDiskText =
        UiControls::readOnlyText(diskGroup);
    externalConflictDiskText->setObjectName(
        QStringLiteral("externalConflictDiskText"));
    externalConflictDiskText->setReadOnly(true);
    externalConflictDiskText->setPlaceholderText(
        QStringLiteral("External file"));
    diskLayout->addWidget(
        externalConflictDiskText);
    comparison->addWidget(localGroup);
    comparison->addWidget(diskGroup);
    comparison->setStretchFactor(0, 1);
    comparison->setStretchFactor(1, 1);
    outer->addWidget(comparison, 1);

    auto* actions = new QHBoxLayout();
    externalConflictReviewStatus =
        UiControls::label(externalConflictReviewBar);
    externalConflictReviewStatus->setObjectName(
        QStringLiteral("externalConflictReviewStatus"));
    externalConflictReviewStatus->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    actions->addWidget(externalConflictReviewStatus, 1);
    externalConflictKeepLocalButton =
        UiControls::pushButton(
            QStringLiteral("Keep Local"),
            externalConflictReviewBar);
    externalConflictKeepLocalButton->setObjectName(
        QStringLiteral("externalConflictKeepLocalButton"));
    externalConflictSaveAsButton =
        UiControls::pushButton(
            QStringLiteral("Save Local As..."),
            externalConflictReviewBar);
    externalConflictSaveAsButton->setObjectName(
        QStringLiteral("externalConflictSaveAsButton"));
    externalConflictReloadButton =
        UiControls::pushButton(
            QStringLiteral("Reload External"),
            externalConflictReviewBar);
    externalConflictReloadButton->setObjectName(
        QStringLiteral("externalConflictReloadButton"));
    actions->addWidget(externalConflictKeepLocalButton);
    actions->addWidget(externalConflictSaveAsButton);
    actions->addWidget(externalConflictReloadButton);
    outer->addLayout(actions);

    connect(closeButton,
            &QPushButton::clicked,
            this,
            &DocumentReviewCoordinator::closeExternalConflictReview);
    connect(externalConflictKeepLocalButton,
            &QPushButton::clicked,
            this,
            &DocumentReviewCoordinator::keepReviewedExternalConflict);
    connect(externalConflictReloadButton,
            &QPushButton::clicked,
            this,
            &DocumentReviewCoordinator::reloadReviewedExternalConflict);
    connect(externalConflictSaveAsButton,
            &QPushButton::clicked,
            this,
            &DocumentReviewCoordinator::saveReviewedExternalConflictAs);
    auto* closeShortcut =
        new QShortcut(
            QKeySequence(Qt::Key_Escape),
            externalConflictReviewBar);
    closeShortcut->setContext(
        Qt::WidgetWithChildrenShortcut);
    connect(closeShortcut,
            &QShortcut::activated,
            this,
            &DocumentReviewCoordinator::closeExternalConflictReview);

    editorLayout->addWidget(externalConflictReviewBar);
}

void DocumentReviewCoordinator::openExternalConflictReview(
    const QString& fileName)
{
    if (!tabManager || !externalConflictReviewBar)
        return;
    ExternalDocumentConflictReview review =
        tabManager->externalConflictReview(fileName);
    if (!review.valid) {
        if (reviewedExternalConflict
            && reviewedExternalConflict->fileName
                   == fileName
            && externalConflictReviewStatus) {
            externalConflictReviewStatus->setText(
                review.failureReason);
            externalConflictKeepLocalButton
                ->setEnabled(false);
            externalConflictReloadButton
                ->setEnabled(false);
            externalConflictSaveAsButton
                ->setEnabled(false);
        }
        postExternalConflictActionFailure(
            fileName,
            review.failureReason.isEmpty()
                ? QStringLiteral(
                      "The conflict comparison is no longer available.")
                : review.failureReason);
        return;
    }

    if (!externalConflictReviewBar->isVisible())
        externalConflictPreviousFocus = QApplication::focusWidget();
    reviewedExternalConflict =
        std::make_unique<ExternalDocumentConflictReview>(
            std::move(review));
    externalConflictReviewTitle->setText(
        QStringLiteral("External conflict: %1")
            .arg(QDir::toNativeSeparators(
                reviewedExternalConflict->fileName)));
    externalConflictReviewStatus->setText(
        reviewedExternalConflict->externalAvailable
        ? QStringLiteral(
              "Compared local revision %1 with the current external generation.")
              .arg(
                  reviewedExternalConflict
                      ->documentRevision)
        : QStringLiteral(
              "The external file is unavailable. Save Local As remains available."));
    externalConflictKeepLocalButton->setEnabled(
        reviewedExternalConflict->externalAvailable);
    externalConflictReloadButton->setEnabled(
        reviewedExternalConflict->externalAvailable);
    externalConflictSaveAsButton->setEnabled(true);
    if (MyCodeEditor* editor =
            tabManager->getCurrentEditor()) {
        externalConflictLocalText->setFont(
            editor->font());
        externalConflictDiskText->setFont(
            editor->font());
    }
    externalConflictLocalText->setPlainText(
        reviewedExternalConflict->localText);
    externalConflictDiskText->setPlainText(
        reviewedExternalConflict->externalAvailable
        ? reviewedExternalConflict->externalText
        : reviewedExternalConflict->failureReason);
    externalConflictLocalText->moveCursor(
        QTextCursor::Start);
    externalConflictDiskText->moveCursor(
        QTextCursor::Start);
    externalConflictReviewBar->setVisible(true);
}

void DocumentReviewCoordinator::closeExternalConflictReview()
{
    if (externalConflictReviewBar)
        externalConflictReviewBar->setVisible(false);
    reviewedExternalConflict.reset();
    if (externalConflictPreviousFocus)
        externalConflictPreviousFocus->setFocus(
            Qt::OtherFocusReason);
    externalConflictPreviousFocus.clear();
}

void DocumentReviewCoordinator::keepReviewedExternalConflict()
{
    if (!tabManager || !reviewedExternalConflict)
        return;
    const ExternalDocumentConflictReview review =
        *reviewedExternalConflict;
    const QString fileName = review.fileName;
    const ExternalDocumentConflictActionResult result =
        tabManager->keepLocalExternalConflict(
            review);
    if (!result.applied()) {
        postExternalConflictActionFailure(
            fileName,
            result.failureReason);
        if (result.currentReview.valid)
            openExternalConflictReview(fileName);
        return;
    }
    if (notificationCenter) {
        notificationCenter->dismissByKey(
            QStringLiteral("external:%1")
                .arg(fileName));
    }
    closeExternalConflictReview();
    {
        postActivityMessage(
            QStringLiteral(
                "Kept the local version. Saving this path will recheck the external generation."),
            5000);
    }
}

void DocumentReviewCoordinator::reloadReviewedExternalConflict()
{
    if (!tabManager || !reviewedExternalConflict)
        return;
    const ExternalDocumentConflictReview review =
        *reviewedExternalConflict;
    const QString fileName = review.fileName;
    const ExternalDocumentConflictActionResult result =
        tabManager->reloadExternalConflict(
            review);
    if (!result.applied()) {
        postExternalConflictActionFailure(
            fileName,
            result.failureReason);
        if (result.currentReview.valid)
            openExternalConflictReview(fileName);
        return;
    }
    closeExternalConflictReview();
}

void DocumentReviewCoordinator::saveReviewedExternalConflictAs()
{
    if (!tabManager || !reviewedExternalConflict)
        return;
    const ExternalDocumentConflictReview review =
        *reviewedExternalConflict;
    const QString originalFileName =
        review.fileName;
    QString failureReason;
    const bool comparisonWasVisible =
        externalConflictReviewBar
        && externalConflictReviewBar->isVisible();
    if (!tabManager->saveExternalConflictLocalAs(
            review,
            QString(),
            &failureReason)) {
        if (failureReason != QStringLiteral(
                "Save As was cancelled.")) {
            postExternalConflictActionFailure(
                originalFileName,
                failureReason);
            if (comparisonWasVisible)
                openExternalConflictReview(
                    originalFileName);
        }
        return;
    }
    if (notificationCenter) {
        notificationCenter->dismissByKey(
            QStringLiteral("external:%1")
                .arg(originalFileName));
    }
    closeExternalConflictReview();
}

void DocumentReviewCoordinator::postExternalConflictActionFailure(
    const QString& fileName,
    const QString& failureReason)
{
    if (!notificationCenter)
        return;
    NotificationDraft draft;
    draft.key =
        QStringLiteral("external:%1").arg(fileName);
    draft.topic =
        NotificationTopic::ExternalModification;
    draft.severity = NotificationSeverity::Critical;
    draft.source =
        QStringLiteral("ExternalDocumentSync");
    draft.message = failureReason.isEmpty()
        ? QStringLiteral(
              "The external conflict action could not be applied.")
        : failureReason;
    draft.actions = {
        {QStringLiteral("external.review"),
         QStringLiteral("Compare...")},
        {QStringLiteral("external.keep-local"),
         QStringLiteral("Keep Local")},
        {QStringLiteral("external.reload"),
         QStringLiteral("Reload External")},
        {QStringLiteral("external.save-as"),
         QStringLiteral("Save Local As...")}};
    const NotificationPostResult posted =
        notificationCenter->post(draft);
    externalConflictNotificationFiles.insert(
        posted.id,
        fileName);
}

QString DocumentReviewCoordinator::crashRecoveryNotificationKey(
    const QString& workspaceRoot) const
{
    return QStringLiteral("crash-recovery:%1")
        .arg(workspaceSessionRootKey(workspaceRoot));
}

QString DocumentReviewCoordinator::crashRecoveryHandledKey(
    const QString& workspaceRoot,
    const QString& recoveryId) const
{
    return QStringLiteral("%1|%2")
        .arg(workspaceSessionRootKey(workspaceRoot),
             recoveryId);
}

void DocumentReviewCoordinator::notifyCrashRecoveryCandidates(
    const QString& workspaceRoot,
    int candidateCount,
    int isolatedRecordCount)
{
    if (!notificationCenter || workspaceRoot.isEmpty())
        return;

    const QString workspaceKey =
        workspaceSessionRootKey(workspaceRoot);
    crashRecoveryIsolatedRecordCounts.insert(
        workspaceKey,
        qMax(crashRecoveryIsolatedRecordCounts
                 .value(workspaceKey),
             isolatedRecordCount));

    NotificationDraft draft;
    draft.key =
        crashRecoveryNotificationKey(workspaceRoot);
    draft.topic = NotificationTopic::General;
    draft.severity =
        candidateCount > 0
        ? NotificationSeverity::Warning
        : NotificationSeverity::Error;
    draft.source = QStringLiteral("CrashRecovery");
    if (candidateCount > 0) {
        draft.message = QStringLiteral(
            "%1 crash recovery snapshot(s) are available for review.")
                            .arg(candidateCount);
    } else {
        draft.message = QStringLiteral(
            "%1 invalid crash recovery record(s) were quarantined.")
                            .arg(isolatedRecordCount);
    }
    if (isolatedRecordCount > 0 && candidateCount > 0) {
        draft.message += QStringLiteral(
            " %1 invalid record(s) were quarantined.")
                             .arg(isolatedRecordCount);
    }
    if (const ActionDescriptor* action =
            findActionById(
                QString::fromLatin1(
                    ActionIds::
                        ReviewCrashRecovery))) {
        draft.actions.append(
            {action->id,
             action->canonicalName});
    }
    const NotificationPostResult posted =
        notificationCenter->post(draft);
    if (!posted.id.isEmpty()) {
        crashRecoveryNotificationWorkspaces.insert(
            posted.id,
            workspaceRoot);
    }
}

void DocumentReviewCoordinator::postCrashRecoveryFailure(
    const QString& documentId,
    const QString& failureReason)
{
    if (!notificationCenter)
        return;

    NotificationDraft draft;
    draft.key =
        QStringLiteral("crash-recovery-error:%1")
            .arg(documentId.isEmpty()
                 ? QStringLiteral("general")
                 : documentId);
    draft.topic = NotificationTopic::General;
    draft.severity = NotificationSeverity::Error;
    draft.source = QStringLiteral("CrashRecovery");
    draft.message =
        failureReason.isEmpty()
        ? QStringLiteral("Crash recovery operation failed.")
        : failureReason;
    notificationCenter->post(draft);
}

void DocumentReviewCoordinator::setupCrashRecoveryReviewUi()
{
    if (crashRecoveryReviewDialog)
        return;

    crashRecoveryReviewDialog = new UiDialog(hostWindow);
    crashRecoveryReviewDialog->setObjectName(
        QStringLiteral("crashRecoveryReviewDialog"));
    crashRecoveryReviewDialog->setWindowTitle(
        tr("Crash Recovery Review"));
    crashRecoveryReviewDialog->setModal(false);
    crashRecoveryReviewDialog->setWindowModality(
        Qt::NonModal);
    crashRecoveryReviewDialog->setAttribute(
        Qt::WA_ShowWithoutActivating,
        true);
    crashRecoveryReviewDialog->setWindowFlag(
        Qt::WindowStaysOnTopHint,
        false);
    crashRecoveryReviewDialog->resize(1040, 720);

    auto* rootLayout =
        new QVBoxLayout(crashRecoveryReviewDialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(8);

    auto* introduction = UiControls::label(
        tr("Review each recovery snapshot against the current source. "
           "Restoring changes the in-memory document only; saving remains "
           "an explicit action."),
        crashRecoveryReviewDialog);
    introduction->setObjectName(
        QStringLiteral("crashRecoveryIntroduction"));
    introduction->setWordWrap(true);
    rootLayout->addWidget(introduction);

    crashRecoveryCandidateList =
        UiControls::treeWidget(crashRecoveryReviewDialog);
    crashRecoveryCandidateList->setObjectName(
        QStringLiteral("crashRecoveryCandidateList"));
    crashRecoveryCandidateList->setColumnCount(3);
    crashRecoveryCandidateList->setHeaderLabels(
        {tr("Document"), tr("Snapshot"), tr("Source State")});
    crashRecoveryCandidateList->setRootIsDecorated(false);
    crashRecoveryCandidateList->setUniformRowHeights(true);
    crashRecoveryCandidateList->setSelectionMode(
        QAbstractItemView::SingleSelection);
    crashRecoveryCandidateList->header()
        ->setSectionResizeMode(0, QHeaderView::Stretch);
    crashRecoveryCandidateList->header()
        ->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    crashRecoveryCandidateList->header()
        ->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    rootLayout->addWidget(crashRecoveryCandidateList, 1);

    auto* comparisonSplitter =
        new QSplitter(Qt::Horizontal,
                      crashRecoveryReviewDialog);
    comparisonSplitter->setObjectName(
        QStringLiteral("crashRecoveryComparisonSplitter"));
    auto* sourceGroup =
        new QGroupBox(tr("Current Source"),
                      comparisonSplitter);
    auto* sourceLayout = new QVBoxLayout(sourceGroup);
    crashRecoverySourceText =
        UiControls::readOnlyText(sourceGroup);
    crashRecoverySourceText->setObjectName(
        QStringLiteral("crashRecoverySourceText"));
    crashRecoverySourceText->setReadOnly(true);
    crashRecoverySourceText->setLineWrapMode(
        QPlainTextEdit::NoWrap);
    sourceLayout->addWidget(crashRecoverySourceText);

    auto* recoveredGroup =
        new QGroupBox(tr("Recovered Snapshot"),
                      comparisonSplitter);
    auto* recoveredLayout =
        new QVBoxLayout(recoveredGroup);
    crashRecoveryRecoveredText =
        UiControls::readOnlyText(recoveredGroup);
    crashRecoveryRecoveredText->setObjectName(
        QStringLiteral("crashRecoveryRecoveredText"));
    crashRecoveryRecoveredText->setReadOnly(true);
    crashRecoveryRecoveredText->setLineWrapMode(
        QPlainTextEdit::NoWrap);
    recoveredLayout->addWidget(
        crashRecoveryRecoveredText);
    comparisonSplitter->addWidget(sourceGroup);
    comparisonSplitter->addWidget(recoveredGroup);
    comparisonSplitter->setSizes({500, 500});
    rootLayout->addWidget(comparisonSplitter, 3);

    crashRecoveryReviewStatus =
        UiControls::label(crashRecoveryReviewDialog);
    crashRecoveryReviewStatus->setObjectName(
        QStringLiteral("crashRecoveryReviewStatus"));
    crashRecoveryReviewStatus->setWordWrap(true);
    crashRecoveryReviewStatus->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    rootLayout->addWidget(crashRecoveryReviewStatus);

    auto* buttons = UiDialogs::buttonBox(
        QDialogButtonBox::Close,
        crashRecoveryReviewDialog);
    buttons->setObjectName(
        QStringLiteral("crashRecoveryReviewButtons"));
    crashRecoveryRestoreButton = UiDialogs::addButton(buttons,
        tr("Restore Selected"),
        QDialogButtonBox::AcceptRole);
    crashRecoveryRestoreButton->setObjectName(
        QStringLiteral("crashRecoveryRestoreButton"));
    crashRecoveryDiscardButton = UiDialogs::addButton(buttons,
        tr("Discard Selected"),
        QDialogButtonBox::DestructiveRole);
    crashRecoveryDiscardButton->setObjectName(
        QStringLiteral("crashRecoveryDiscardButton"));
    crashRecoveryRestoreButton->setEnabled(false);
    crashRecoveryDiscardButton->setEnabled(false);
    rootLayout->addWidget(buttons);

    connect(crashRecoveryCandidateList,
            &QTreeWidget::currentItemChanged,
            this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) {
                reviewCrashRecoverySelection();
            });
    connect(crashRecoveryRestoreButton,
            &QPushButton::clicked,
            this,
            &DocumentReviewCoordinator::applyReviewedCrashRecovery);
    connect(crashRecoveryDiscardButton,
            &QPushButton::clicked,
            this,
            &DocumentReviewCoordinator::discardReviewedCrashRecovery);
    connect(buttons,
            &QDialogButtonBox::rejected,
            crashRecoveryReviewDialog,
            &QDialog::hide);
}

void DocumentReviewCoordinator::openCrashRecoveryReview(
    const QString& workspaceRoot)
{
    const QPointer<QWidget> preservedFocus =
        QApplication::focusWidget();
    setupCrashRecoveryReviewUi();
    if (!workspaceRoot.isEmpty())
        crashRecoveryReviewWorkspace = workspaceRoot;
    if (crashRecoveryReviewWorkspace.isEmpty()) {
        crashRecoveryReviewStatus->setText(
            tr("Open a workspace before reviewing crash recovery data."));
        crashRecoveryCandidateList->clear();
        crashRecoverySourceText->clear();
        crashRecoveryRecoveredText->clear();
        crashRecoveryRestoreButton->setEnabled(false);
        crashRecoveryDiscardButton->setEnabled(false);
    } else {
        reloadCrashRecoveryReview();
    }
    crashRecoveryReviewDialog->show();
    if (preservedFocus
        && preservedFocus->isVisible()
        && preservedFocus->isEnabled()) {
        if (QWidget* const focusWindow =
                preservedFocus->window()) {
            focusWindow->activateWindow();
        }
        preservedFocus->setFocus(
            Qt::OtherFocusReason);
    }
    const QPointer<QWidget> reviewDialog(
        crashRecoveryReviewDialog);
    QMetaObject::invokeMethod(
        crashRecoveryReviewDialog,
        [preservedFocus, reviewDialog]() {
            if (!preservedFocus
                || !preservedFocus->isVisible()
                || !preservedFocus->isEnabled()) {
                return;
            }
            QWidget* const currentFocus =
                QApplication::focusWidget();
            if (!currentFocus
                || (reviewDialog
                    && (currentFocus == reviewDialog
                        || reviewDialog->isAncestorOf(
                            currentFocus)))) {
                if (QWidget* const focusWindow =
                        preservedFocus->window()) {
                    focusWindow->activateWindow();
                }
                preservedFocus->setFocus(
                    Qt::OtherFocusReason);
            }
        },
        Qt::QueuedConnection);
}

void DocumentReviewCoordinator::reloadCrashRecoveryReview(
    const QString& preferredRecoveryId)
{
    if (!crashRecoveryReviewDialog
        || !crashRecoveryCandidateList
        || !tabManager) {
        return;
    }

    reviewedCrashRecoveryCandidate.reset();
    crashRecoveryRestoreButton->setEnabled(false);
    crashRecoveryDiscardButton->setEnabled(false);
    crashRecoverySourceText->clear();
    crashRecoveryRecoveredText->clear();

    const CrashRecoveryListResult result =
        tabManager->listCrashRecoveryCandidates(
            crashRecoveryReviewWorkspace);
    if (!result.succeeded()) {
        crashRecoveryCandidateList->clear();
        crashRecoveryReviewStatus->setText(result.reason);
        postCrashRecoveryFailure(
            crashRecoveryReviewWorkspace,
            result.reason);
        return;
    }

    const QString workspaceKey =
        workspaceSessionRootKey(
            crashRecoveryReviewWorkspace);
    crashRecoveryIsolatedRecordCounts.insert(
        workspaceKey,
        qMax(crashRecoveryIsolatedRecordCounts
                 .value(workspaceKey),
             static_cast<int>(
                 result.isolatedRecords.size())));

    QTreeWidgetItem* preferredItem = nullptr;
    {
        const QSignalBlocker blocker(
            crashRecoveryCandidateList);
        crashRecoveryCandidateList->clear();
        for (const CrashRecoveryCandidate& candidate :
             result.candidates) {
            const QString candidateWorkspace =
                candidate.workspacePath.isEmpty()
                ? crashRecoveryReviewWorkspace
                : candidate.workspacePath;
            if (handledCrashRecoveryCandidates.contains(
                    crashRecoveryHandledKey(
                        candidateWorkspace,
                        candidate.recoveryId))) {
                continue;
            }

            auto* item = new QTreeWidgetItem(
                crashRecoveryCandidateList);
            item->setText(
                0,
                crashRecoveryDocumentLabel(candidate));
            item->setText(
                1,
                candidate.snapshotCreatedUtc
                    .toLocalTime()
                    .toString(Qt::ISODate));
            item->setText(
                2,
                crashRecoverySourceStateText(
                    candidate.sourceState));
            item->setData(
                0,
                Qt::UserRole,
                candidate.recoveryId);
            item->setData(
                0,
                Qt::UserRole + 1,
                candidateWorkspace);
            item->setToolTip(
                0,
                QStringLiteral(
                    "%1\nRecovery id: %2\nRevision: %3")
                    .arg(crashRecoveryDocumentLabel(candidate),
                         candidate.recoveryId)
                    .arg(candidate.documentRevision));
            if (candidate.recoveryId
                == preferredRecoveryId) {
                preferredItem = item;
            }
        }
    }

    const int isolatedCount =
        crashRecoveryIsolatedRecordCounts
            .value(workspaceKey);
    const int visibleCount =
        crashRecoveryCandidateList
            ->topLevelItemCount();
    if (visibleCount == 0) {
        crashRecoveryReviewStatus->setText(
            isolatedCount > 0
            ? tr("No recoverable snapshots remain. "
                 "%1 invalid record(s) were quarantined.")
                  .arg(isolatedCount)
            : tr("No crash recovery snapshots are pending."));
        return;
    }

    crashRecoveryReviewStatus->setText(
        isolatedCount > 0
        ? tr("%1 snapshot(s) await review; "
             "%2 invalid record(s) were quarantined.")
              .arg(visibleCount)
              .arg(isolatedCount)
        : tr("%1 snapshot(s) await review.")
              .arg(visibleCount));
    if (!preferredItem) {
        preferredItem =
            crashRecoveryCandidateList
                ->topLevelItem(0);
    }
    crashRecoveryCandidateList->setCurrentItem(
        preferredItem);
}

void DocumentReviewCoordinator::reviewCrashRecoverySelection()
{
    reviewedCrashRecoveryCandidate.reset();
    if (crashRecoveryRestoreButton)
        crashRecoveryRestoreButton->setEnabled(false);
    if (crashRecoveryDiscardButton)
        crashRecoveryDiscardButton->setEnabled(false);
    if (!crashRecoveryCandidateList
        || !tabManager) {
        return;
    }

    QTreeWidgetItem* item =
        crashRecoveryCandidateList->currentItem();
    if (!item) {
        crashRecoverySourceText->clear();
        crashRecoveryRecoveredText->clear();
        return;
    }
    const QString recoveryId =
        item->data(0, Qt::UserRole).toString();
    const QString workspaceRoot =
        item->data(0, Qt::UserRole + 1)
            .toString();
    const CrashRecoveryReadResult comparison =
        tabManager->compareCrashRecoveryCandidate(
            recoveryId,
            workspaceRoot);
    if (!comparison.succeeded()) {
        crashRecoverySourceText->clear();
        crashRecoveryRecoveredText->clear();
        crashRecoveryReviewStatus->setText(
            comparison.reason);
        postCrashRecoveryFailure(
            recoveryId,
            comparison.reason);
        return;
    }

    reviewedCrashRecoveryCandidate =
        std::make_unique<CrashRecoveryCandidate>(
            comparison.candidate);
    if (comparison.candidate.sourceReadable) {
        crashRecoverySourceText->setPlainText(
            QString::fromUtf8(
                comparison.currentSourceBytes));
    } else {
        crashRecoverySourceText->setPlainText(
            QStringLiteral("<%1>")
                .arg(crashRecoverySourceStateText(
                    comparison.candidate.sourceState)));
    }
    crashRecoveryRecoveredText->setPlainText(
        comparison.recoveredText);
    crashRecoveryReviewStatus->setText(
        tr("%1 | recovery revision %2 | source %3")
            .arg(crashRecoveryDocumentLabel(
                     comparison.candidate))
            .arg(comparison.candidate
                     .documentRevision)
            .arg(crashRecoverySourceStateText(
                comparison.candidate.sourceState)));
    crashRecoveryRestoreButton->setEnabled(true);
    crashRecoveryDiscardButton->setEnabled(true);
}

void DocumentReviewCoordinator::applyReviewedCrashRecovery()
{
    if (!reviewedCrashRecoveryCandidate
        || !tabManager) {
        return;
    }

    const CrashRecoveryCandidate reviewed =
        *reviewedCrashRecoveryCandidate;
    const CrashRecoveryApplyResult result =
        tabManager->applyCrashRecoveryCandidate(
            reviewed);
    if (!result.succeeded()) {
        crashRecoveryReviewStatus->setText(
            result.reason);
        postCrashRecoveryFailure(
            reviewed.recoveryId,
            result.reason);
        reviewCrashRecoverySelection();
        return;
    }

    handledCrashRecoveryCandidates.insert(
        crashRecoveryHandledKey(
            reviewed.workspacePath,
            reviewed.recoveryId));
    if (notificationCenter) {
        notificationCenter->dismissByKey(
            QStringLiteral(
                "crash-recovery-error:%1")
                .arg(reviewed.recoveryId));
    }
    reloadCrashRecoveryReview();
    refreshCrashRecoveryAvailability(
        reviewed.workspacePath);
    {
        postActivityMessage(
            tr("Recovered text was applied in memory; "
               "save explicitly after review."),
            5000);
    }
}

void DocumentReviewCoordinator::discardReviewedCrashRecovery()
{
    if (!reviewedCrashRecoveryCandidate
        || !tabManager) {
        return;
    }

    const CrashRecoveryCandidate reviewed =
        *reviewedCrashRecoveryCandidate;
    const CrashRecoveryOperationResult result =
        tabManager->discardCrashRecoveryCandidate(
            reviewed.recoveryId,
            reviewed.workspacePath);
    if (!result.succeeded()) {
        crashRecoveryReviewStatus->setText(
            result.reason);
        postCrashRecoveryFailure(
            reviewed.recoveryId,
            result.reason);
        reviewCrashRecoverySelection();
        return;
    }

    handledCrashRecoveryCandidates.insert(
        crashRecoveryHandledKey(
            reviewed.workspacePath,
            reviewed.recoveryId));
    if (notificationCenter) {
        notificationCenter->dismissByKey(
            QStringLiteral(
                "crash-recovery-error:%1")
                .arg(reviewed.recoveryId));
    }
    reloadCrashRecoveryReview();
    refreshCrashRecoveryAvailability(
        reviewed.workspacePath);
    {
        postActivityMessage(
            tr("The selected recovery snapshot was discarded."),
            5000);
    }
}

void DocumentReviewCoordinator::refreshCrashRecoveryAvailability(
    const QString& workspaceRoot)
{
    if (!tabManager || !notificationCenter
        || workspaceRoot.isEmpty()) {
        return;
    }

    const CrashRecoveryListResult result =
        tabManager->listCrashRecoveryCandidates(
            workspaceRoot);
    if (!result.succeeded()) {
        postCrashRecoveryFailure(
            workspaceRoot,
            result.reason);
        return;
    }

    int pendingCount = 0;
    for (const CrashRecoveryCandidate& candidate :
         result.candidates) {
        const QString candidateWorkspace =
            candidate.workspacePath.isEmpty()
            ? workspaceRoot
            : candidate.workspacePath;
        if (!handledCrashRecoveryCandidates.contains(
                crashRecoveryHandledKey(
                    candidateWorkspace,
                    candidate.recoveryId))) {
            ++pendingCount;
        }
    }
    const QString workspaceKey =
        workspaceSessionRootKey(workspaceRoot);
    const int isolatedCount =
        qMax(crashRecoveryIsolatedRecordCounts
                 .value(workspaceKey),
             static_cast<int>(
                 result.isolatedRecords.size()));
    crashRecoveryIsolatedRecordCounts.insert(
        workspaceKey,
        isolatedCount);
    if (pendingCount == 0 && isolatedCount == 0) {
        notificationCenter->dismissByKey(
            crashRecoveryNotificationKey(
                workspaceRoot));
        return;
    }
    notifyCrashRecoveryCandidates(
        workspaceRoot,
        pendingCount,
        isolatedCount);
}
