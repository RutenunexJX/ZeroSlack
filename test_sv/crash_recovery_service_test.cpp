#include "crashrecoveryservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* name, bool value)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("[%s] %s\n",
                value ? "PASS" : "FAIL",
                name);
}

bool replaceFile(const QString& path,
                 const QByteArray& bytes)
{
    if (!QDir().mkpath(
            QFileInfo(path).absolutePath())) {
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(bytes) == bytes.size()
        && file.commit();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool hasExplicitResult(
    const CrashRecoveryOperationResult& result)
{
    return !result.reason.trimmed().isEmpty();
}

CrashRecoverySnapshotRequest fileRequest(
    const QString& workspace,
    const QString& filePath,
    const QString& text,
    quint64 revision,
    const QByteArray& baseline,
    const QDateTime& baselineTime)
{
    CrashRecoverySnapshotRequest request;
    request.document.workspacePath = workspace;
    request.document.originalFilePath = filePath;
    request.text = text;
    request.documentRevision = revision;
    request.savedBaselineSha256 =
        CrashRecoveryService::sha256(baseline);
    request.savedBaselineModifiedUtc =
        baselineTime;
    return request;
}

CrashRecoverySnapshotRequest untitledRequest(
    const QString& workspace,
    const QString& documentId,
    const QString& text,
    quint64 revision)
{
    CrashRecoverySnapshotRequest request;
    request.document.workspacePath = workspace;
    request.document.untitledDocumentId = documentId;
    request.text = text;
    request.documentRevision = revision;
    return request;
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(
        QStringLiteral("ZeroSlackTest"));
    QCoreApplication::setApplicationName(
        QStringLiteral("CrashRecoveryServiceTest"));

    QTemporaryDir temporary;
    expect("temporary fixture is valid",
           temporary.isValid());
    if (!temporary.isValid())
        return 1;

    const QString workspaceA =
        temporary.filePath(
            QStringLiteral("workspace-a"));
    const QString workspaceB =
        temporary.filePath(
            QStringLiteral("workspace-b"));
    const QString recoveryRoot =
        temporary.filePath(
            QStringLiteral("app-local/recovery"));
    expect("workspace fixtures are created",
           QDir().mkpath(workspaceA)
               && QDir().mkpath(workspaceB));

    const QString sourcePath =
        QDir(workspaceA).absoluteFilePath(
            QStringLiteral("rtl/top.sv"));
    const QByteArray baseline =
        "module top;\n"
        "  logic saved_value;\n"
        "endmodule\n";
    const QString firstDirtyText =
        QStringLiteral(
            "module top;\n"
            "  logic local_edit;\n"
            "endmodule\n");
    const QString secondDirtyText =
        QStringLiteral(
            "module top;\n"
            "  logic second_local_edit;\n"
            "endmodule\n");
    expect("source fixture is written",
           replaceFile(sourcePath, baseline));
    const QDateTime baselineTime =
        QFileInfo(sourcePath)
            .lastModified()
            .toUTC();

    CrashRecoveryService service(recoveryRoot);
    const CrashRecoveryService defaultService;
    const QString applicationLocalData =
        QDir::cleanPath(
            QStandardPaths::writableLocation(
                QStandardPaths::AppLocalDataLocation));
    expect("injected storage is outside workspace metadata",
           service.recoveryRootPath()
                   == QDir::cleanPath(recoveryRoot)
               && !service.recoveryRootPath().startsWith(
                   QDir(workspaceA).absoluteFilePath(
                       QStringLiteral(".zs"))));
    expect("production default uses application local data",
           !applicationLocalData.isEmpty()
               && defaultService.recoveryRootPath()
                      .startsWith(applicationLocalData)
               && !defaultService.recoveryRootPath()
                      .startsWith(workspaceA));

    CrashRecoverySnapshotRequest request =
        fileRequest(
            workspaceA,
            sourcePath,
            firstDirtyText,
            41,
            baseline,
            baselineTime);
    const CrashRecoveryWriteResult firstWrite =
        service.writeSnapshot(request);
    expect("dirty snapshot is atomically committed",
           firstWrite.succeeded()
               && firstWrite.status
                      == CrashRecoveryStatus::Success
               && hasExplicitResult(firstWrite)
               && QFileInfo(firstWrite.storagePath).isFile()
               && firstWrite.storagePath.startsWith(
                   service.recoveryRootPath()));
    expect("snapshot creation never changes source",
           readFile(sourcePath) == baseline);

    request.text = secondDirtyText;
    request.documentRevision = 42;
    const CrashRecoveryWriteResult secondWrite =
        service.writeSnapshot(request);
    const QByteArray committedBytes =
        readFile(secondWrite.storagePath);
    const QJsonDocument committedDocument =
        QJsonDocument::fromJson(committedBytes);
    const QJsonObject committedObject =
        committedDocument.object();
    const QByteArray committedText =
        QByteArray::fromBase64(
            committedObject.value(
                    QStringLiteral("recoveredTextUtf8"))
                .toString()
                .toLatin1());
    const QDir recordDirectory(
        QFileInfo(secondWrite.storagePath)
            .absolutePath());
    expect("replacement snapshot remains one complete JSON generation",
           secondWrite.succeeded()
               && secondWrite.storagePath
                      == firstWrite.storagePath
               && committedDocument.isObject()
               && committedObject.value(
                      QStringLiteral("schema"))
                      .toString()
                      == QStringLiteral(
                          "ZeroSlack.CrashRecoverySnapshot")
               && committedObject.value(
                      QStringLiteral("documentRevision"))
                      .toString()
                      == QStringLiteral("42")
               && QString::fromUtf8(committedText)
                      == secondDirtyText
               && recordDirectory.entryList(
                      {QStringLiteral("*.json")},
                      QDir::Files)
                      .size()
                      == 1);

    CrashRecoveryService restartedService(
        recoveryRoot);
    const CrashRecoveryListResult restartedList =
        restartedService.listCandidates(
            workspaceA);
    expect("simulated abnormal restart discovers snapshot",
           restartedList.succeeded()
               && hasExplicitResult(restartedList)
               && restartedList.candidates.size() == 1
               && restartedList.candidates.first().recoveryId
                      == secondWrite.recoveryId
               && restartedList.candidates.first()
                      .documentRevision
                      == 42
               && restartedList.candidates.first()
                      .savedBaselineSha256
                      == CrashRecoveryService::sha256(
                          baseline)
               && restartedList.candidates.first()
                      .savedBaselineModifiedUtc
                      == baselineTime
               && restartedList.candidates.first()
                      .sourceState
                      == CrashRecoverySourceState::
                          UnchangedSinceBaseline);
    const CrashRecoveryListResult isolatedWorkspaceList =
        restartedService.listCandidates(
            workspaceB);
    expect("workspace candidate discovery is isolated",
           isolatedWorkspaceList.succeeded()
               && isolatedWorkspaceList.candidates.isEmpty()
               && isolatedWorkspaceList
                      .isolatedRecords.isEmpty());

    const QByteArray externalSource =
        "module top;\n"
        "  logic external_edit;\n"
        "endmodule\n";
    expect("external source generation is written",
           replaceFile(sourcePath, externalSource));
    const CrashRecoveryListResult changedList =
        restartedService.listCandidates(
            workspaceA);
    const CrashRecoveryReadResult comparison =
        restartedService.readComparison(
            workspaceA,
            secondWrite.recoveryId);
    expect("external source change preserves recovery comparison",
           changedList.succeeded()
               && changedList.candidates.size() == 1
               && changedList.candidates.first()
                      .sourceState
                      == CrashRecoverySourceState::
                          ExternallyModified
               && changedList.candidates.first()
                      .sourceChangedSinceBaseline
               && comparison.succeeded()
               && hasExplicitResult(comparison)
               && comparison.recoveredText
                      == secondDirtyText
               && comparison.currentSourceBytes
                      == externalSource
               && comparison.candidate
                      .savedBaselineSha256
                      == CrashRecoveryService::sha256(
                          baseline)
               && comparison.candidate
                      .currentSourceSha256
                      == CrashRecoveryService::sha256(
                          externalSource));

    const QByteArray sourceBeforeRecover =
        readFile(sourcePath);
    const CrashRecoveryRecoverResult recovered =
        restartedService.recoverText(
            workspaceA,
            secondWrite.recoveryId);
    expect("recover returns text without writing source file",
           recovered.succeeded()
               && hasExplicitResult(recovered)
               && recovered.text == secondDirtyText
               && readFile(sourcePath)
                      == sourceBeforeRecover);

    expect("normal save fixture writes recovered content",
           replaceFile(
               sourcePath,
               secondDirtyText.toUtf8()));
    const CrashRecoveryOperationResult savedCleanup =
        restartedService.clearAfterNormalSave(
            request.document);
    expect("normal save removes recovery snapshot explicitly",
           savedCleanup.succeeded()
               && hasExplicitResult(savedCleanup)
               && !QFileInfo::exists(
                   secondWrite.storagePath)
               && restartedService
                      .listCandidates(workspaceA)
                      .candidates.isEmpty());
    const CrashRecoveryOperationResult savedAgain =
        restartedService.clearAfterNormalSave(
            request.document);
    expect("normal cleanup is idempotent and explicit",
           savedAgain.status
                   == CrashRecoveryStatus::AlreadyClean
               && savedAgain.succeeded()
               && hasExplicitResult(savedAgain));

    const QByteArray secondBaseline =
        secondDirtyText.toUtf8();
    CrashRecoverySnapshotRequest closeRequest =
        fileRequest(
            workspaceA,
            sourcePath,
            QStringLiteral(
                "module top;\n"
                "  logic close_pending;\n"
                "endmodule\n"),
            43,
            secondBaseline,
            QFileInfo(sourcePath)
                .lastModified()
                .toUTC());
    const CrashRecoveryWriteResult closeWrite =
        restartedService.writeSnapshot(
            closeRequest);
    const CrashRecoveryOperationResult closeCleanup =
        restartedService.clearAfterNormalClose(
            closeRequest.document);
    expect("normal close removes recovery snapshot",
           closeWrite.succeeded()
               && closeCleanup.succeeded()
               && hasExplicitResult(closeCleanup)
               && !QFileInfo::exists(
                   closeWrite.storagePath));

    CrashRecoverySnapshotRequest discardRequest =
        closeRequest;
    discardRequest.text =
        QStringLiteral(
            "module top;\n"
            "  logic discard_pending;\n"
            "endmodule\n");
    discardRequest.documentRevision = 44;
    const CrashRecoveryWriteResult discardWrite =
        restartedService.writeSnapshot(
            discardRequest);
    const CrashRecoveryOperationResult discarded =
        restartedService.discard(
            workspaceA,
            discardWrite.recoveryId);
    expect("explicit discard removes only requested snapshot",
           discardWrite.succeeded()
               && discarded.succeeded()
               && hasExplicitResult(discarded)
               && !QFileInfo::exists(
                   discardWrite.storagePath));

    const CrashRecoveryWriteResult untitledA =
        restartedService.writeSnapshot(
            untitledRequest(
                workspaceA,
                QStringLiteral("new-document-1"),
                QStringLiteral("workspace A text"),
                7));
    const CrashRecoveryWriteResult untitledB =
        restartedService.writeSnapshot(
            untitledRequest(
                workspaceB,
                QStringLiteral("new-document-1"),
                QStringLiteral("workspace B text"),
                8));
    const CrashRecoveryRecoverResult recoveredUntitledA =
        restartedService.recoverText(
            workspaceA,
            untitledA.recoveryId);
    const CrashRecoveryRecoverResult recoveredUntitledB =
        restartedService.recoverText(
            workspaceB,
            untitledB.recoveryId);
    expect("untitled identity is isolated by workspace",
           untitledA.succeeded()
               && untitledB.succeeded()
               && untitledA.recoveryId
                      != untitledB.recoveryId
               && untitledA.storagePath
                      != untitledB.storagePath
               && recoveredUntitledA.text
                      == QStringLiteral("workspace A text")
               && recoveredUntitledB.text
                      == QStringLiteral("workspace B text"));
    expect("untitled cleanup is document scoped",
           restartedService
                   .clearAfterNormalClose(
                       untitledRequest(
                           workspaceA,
                           QStringLiteral("new-document-1"),
                           QString(),
                           0)
                           .document)
                   .succeeded()
               && restartedService
                      .listCandidates(workspaceA)
                      .candidates.isEmpty()
               && restartedService
                      .listCandidates(workspaceB)
                      .candidates.size()
                      == 1
               && restartedService
                      .discard(
                          workspaceB,
                          untitledB.recoveryId)
                      .succeeded());

    expect("source reset before corrupt-record test",
           replaceFile(sourcePath, secondBaseline));
    CrashRecoverySnapshotRequest corruptFixtureRequest =
        fileRequest(
            workspaceA,
            sourcePath,
            QStringLiteral(
                "module top;\n"
                "  logic still_recoverable;\n"
                "endmodule\n"),
            45,
            secondBaseline,
            QFileInfo(sourcePath)
                .lastModified()
                .toUTC());
    const CrashRecoveryWriteResult goodAlongsideCorrupt =
        restartedService.writeSnapshot(
            corruptFixtureRequest);
    const QString corruptPath =
        QDir(
            QFileInfo(
                goodAlongsideCorrupt.storagePath)
                .absolutePath())
            .absoluteFilePath(
                QStringLiteral("broken.json"));
    expect("corrupt record fixture is written",
           replaceFile(
               corruptPath,
               QByteArray("{not-valid-json")));
    const QByteArray sourceBeforeCorruptScan =
        readFile(sourcePath);
    const CrashRecoveryListResult corruptScan =
        restartedService.listCandidates(
            workspaceA);
    expect("corrupt record is quarantined without hiding valid candidate",
           corruptScan.succeeded()
               && corruptScan.candidates.size() == 1
               && corruptScan.isolatedRecords.size() == 1
               && corruptScan.isolatedRecords.first().status
                      == CrashRecoveryStatus::CorruptRecord
               && !QFileInfo::exists(corruptPath)
               && QFileInfo(
                      corruptScan.isolatedRecords.first()
                          .quarantinePath)
                      .isFile()
               && readFile(sourcePath)
                      == sourceBeforeCorruptScan);
    expect("valid candidate remains readable after corrupt isolation",
           restartedService
                   .recoverText(
                       workspaceA,
                       goodAlongsideCorrupt.recoveryId)
                   .text
               == corruptFixtureRequest.text);
    expect("valid corrupt-test candidate is discarded",
           restartedService
               .discard(
                   workspaceA,
                   goodAlongsideCorrupt.recoveryId)
               .succeeded());

    CrashRecoverySnapshotRequest staleRequest =
        fileRequest(
            workspaceA,
            sourcePath,
            QStringLiteral(
                "module top;\n"
                "  logic already_saved;\n"
                "endmodule\n"),
            46,
            secondBaseline,
            QFileInfo(sourcePath)
                .lastModified()
                .toUTC());
    const CrashRecoveryWriteResult staleWrite =
        restartedService.writeSnapshot(
            staleRequest);
    QByteArray logicallyEquivalentSource =
        QByteArray::fromHex("efbbbf");
    logicallyEquivalentSource +=
        staleRequest.text.toUtf8().replace(
            "\n",
            "\r\n");
    expect("stale fixture snapshot is written",
           staleWrite.succeeded()
               && replaceFile(
                   sourcePath,
                   logicallyEquivalentSource));
    const QByteArray sourceBeforeStaleScan =
        readFile(sourcePath);
    const CrashRecoveryListResult staleScan =
        restartedService.listCandidates(
            workspaceA);
    bool staleWasQuarantined = false;
    for (const CrashRecoveryIsolatedRecord& isolated :
         staleScan.isolatedRecords) {
        if (isolated.status
            == CrashRecoveryStatus::StaleRecord) {
            staleWasQuarantined =
                !isolated.quarantinePath.isEmpty()
                && QFileInfo(
                       isolated.quarantinePath)
                       .isFile();
        }
    }
    expect("BOM/CRLF-equivalent stale record is quarantined without source overwrite",
           staleScan.succeeded()
               && staleScan.candidates.isEmpty()
               && staleWasQuarantined
               && !QFileInfo::exists(
                   staleWrite.storagePath)
               && readFile(sourcePath)
                      == sourceBeforeStaleScan);

    CrashRecoverySnapshotRequest invalidRequest;
    invalidRequest.document.workspacePath = workspaceA;
    const CrashRecoveryWriteResult invalidWrite =
        restartedService.writeSnapshot(
            invalidRequest);
    const CrashRecoveryOperationResult invalidDiscard =
        restartedService.discard(
            workspaceA,
            QStringLiteral("../../not-a-recovery-id"));
    expect("invalid requests return explicit status and reason",
           !invalidWrite.succeeded()
               && invalidWrite.status
                      == CrashRecoveryStatus::InvalidArgument
               && hasExplicitResult(invalidWrite)
               && !invalidDiscard.succeeded()
               && invalidDiscard.status
                      == CrashRecoveryStatus::InvalidArgument
               && hasExplicitResult(invalidDiscard)
               && readFile(sourcePath)
                      == sourceBeforeStaleScan);

    std::printf(
        "Crash recovery service checks: %d, failures: %d\n",
        checks,
        failures);
    return failures == 0 ? 0 : 1;
}
