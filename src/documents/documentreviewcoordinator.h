#pragma once
#include "zeroslackexport.h"
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QSet>
#include <QString>
#include <functional>
#include <memory>
#include <utility>
class QWidget;
class QDialog;
class QTreeWidget;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QVBoxLayout;
class TabManager;
class NotificationCenter;
struct CrashRecoveryCandidate;
struct ExternalDocumentConflictReview;

// Owns document review state, notification actions and their UI lifecycle.
// MainWindow supplies its host and activity sink; reviews never access it.
class ZEROSLACK_API DocumentReviewCoordinator final : public QObject {
    Q_OBJECT
public:
    explicit DocumentReviewCoordinator(TabManager* documents, QWidget* host);
    ~DocumentReviewCoordinator() override;
    void bindNotifications(NotificationCenter* notifications);
    void setStatusMessageHandler(std::function<void(const QString&, int)> handler) {
        statusMessageHandler = std::move(handler);
    }
    void setupExternalConflictReviewUi(QVBoxLayout* editorLayout, QWidget* parent);
    void openCrashRecoveryReview(const QString& workspaceRoot = QString());
private:
    QPointer<QWidget> hostWindow;
    QPointer<TabManager> tabManager;
    QPointer<NotificationCenter> notificationCenter;
    std::function<void(const QString&, int)> statusMessageHandler;
    void postActivityMessage(const QString& text, int timeout) {
        if (statusMessageHandler) statusMessageHandler(text, timeout);
    }
    QPointer<QDialog> crashRecoveryReviewDialog;
    QTreeWidget* crashRecoveryCandidateList = nullptr;
    QPlainTextEdit* crashRecoverySourceText = nullptr;
    QPlainTextEdit* crashRecoveryRecoveredText = nullptr;
    QLabel* crashRecoveryReviewStatus = nullptr;
    QPushButton* crashRecoveryRestoreButton = nullptr;
    QPushButton* crashRecoveryDiscardButton = nullptr;
    std::unique_ptr<CrashRecoveryCandidate>
        reviewedCrashRecoveryCandidate;
    QHash<QString, QString>
        crashRecoveryNotificationWorkspaces;
    QHash<QString, QString>
        externalConflictNotificationFiles;
    QHash<QString, int>
        crashRecoveryIsolatedRecordCounts;
    QSet<QString> handledCrashRecoveryCandidates;
    QString crashRecoveryReviewWorkspace;
    QPointer<QWidget> externalConflictReviewBar;
    QLabel* externalConflictReviewTitle = nullptr;
    QLabel* externalConflictReviewStatus = nullptr;
    QPlainTextEdit* externalConflictLocalText = nullptr;
    QPlainTextEdit* externalConflictDiskText = nullptr;
    QPushButton* externalConflictKeepLocalButton = nullptr;
    QPushButton* externalConflictReloadButton = nullptr;
    QPushButton* externalConflictSaveAsButton = nullptr;
    std::unique_ptr<ExternalDocumentConflictReview>
        reviewedExternalConflict;
    QPointer<QWidget> externalConflictPreviousFocus;
    void setupCrashRecoveryReviewUi();
    void notifyCrashRecoveryCandidates(
        const QString& workspaceRoot,
        int candidateCount,
        int isolatedRecordCount);
    void postCrashRecoveryFailure(
        const QString& documentId,
        const QString& failureReason);
    void reloadCrashRecoveryReview(
        const QString& preferredRecoveryId = QString());
    void reviewCrashRecoverySelection();
    void applyReviewedCrashRecovery();
    void discardReviewedCrashRecovery();
    void refreshCrashRecoveryAvailability(
        const QString& workspaceRoot);
    QString crashRecoveryNotificationKey(
        const QString& workspaceRoot) const;
    QString crashRecoveryHandledKey(
        const QString& workspaceRoot,
        const QString& recoveryId) const;
    void openExternalConflictReview(
        const QString& fileName);
    void closeExternalConflictReview();
    void keepReviewedExternalConflict();
    void reloadReviewedExternalConflict();
    void saveReviewedExternalConflictAs();
    void postExternalConflictActionFailure(
        const QString& fileName,
        const QString& failureReason);
};
