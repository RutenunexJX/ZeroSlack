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
#include <QSemaphore>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QLockFile>
#include <QTextStream>
#include <thread>

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

int recoveryPeer(const QStringList& args)
{
    CrashRecoveryService service(args.at(2));
    auto request = fileRequest(args.at(3), args.at(4), {}, 1, readFile(args.at(4)), QFileInfo(args.at(4)).lastModified());
    QTextStream input(stdin), output(stdout);
    while (true) {
        const auto line = input.readLine();
        if (line.isNull() || line == "quit") break;
        QJsonObject response;
        if (line == "clear") {
            response.insert("ok", service.clearAfterNormalSave(request.document).succeeded());
        } else {
            request.text = line; ++request.documentRevision;
            const auto write = service.writeSnapshot(request);
            response.insert("ok", write.succeeded()); response.insert("id", write.recoveryId);
            response.insert("path", write.storagePath);
        }
        output << QJsonDocument(response).toJson(QJsonDocument::Compact) << Qt::endl;
    }
    return 0;
}

void checkIndependentEditingSessions(const QString& recoveryRoot = {})
{
    QTemporaryDir temp;
    const auto root = recoveryRoot.isEmpty() ? temp.filePath("recovery") : recoveryRoot;
    const auto workspace = temp.filePath("workspace");
    const auto source = workspace + "/unit.sv";
    expect("multi-process source exists", replaceFile(source, "module base; endmodule\n"));
    QProcess p, q;
    const QStringList args{"--recovery-peer", root, workspace, source};
    p.start(QCoreApplication::applicationFilePath(), args);
    q.start(QCoreApplication::applicationFilePath(), args);
    const bool started = p.waitForStarted(5000) && q.waitForStarted(5000);
    expect("two independent recovery processes start", started);
    if (!started) return;
    const auto ask = [&](QProcess& peer, const QByteArray& command) {
        peer.write(command + '\n'); peer.waitForBytesWritten(5000);
        while (!peer.canReadLine() && peer.waitForReadyRead(5000)) {}
        return QJsonDocument::fromJson(peer.readLine()).object();
    };
    const auto firstP = ask(p, "P generation 1");
    const auto firstQ = ask(q, "Q generation 1");
    const auto secondP = ask(p, "P generation 2");
    const auto secondQ = ask(q, "Q generation 2");
    const auto pId = firstP.value("id").toString(), qId = firstQ.value("id").toString();
    expect("sequential interleaved writes retain distinct owners", firstP.value("ok").toBool()
        && firstQ.value("ok").toBool() && !pId.isEmpty() && pId != qId
        && pId == secondP.value("id").toString() && qId == secondQ.value("id").toString());
    CrashRecoveryService observer(root);
    expect("enumeration keeps both live candidates", observer.listCandidates(workspace).candidates.size() == 2);
    expect("each process retains its newest text", observer.readComparison(workspace, pId).recoveredText == "P generation 2"
        && observer.readComparison(workspace, qId).recoveredText == "Q generation 2");
    expect("live foreign records cannot be claimed or discarded",
        observer.recoverText(workspace, qId).status == CrashRecoveryStatus::OwnedByAnotherSession
        && observer.discard(workspace, qId).status == CrashRecoveryStatus::OwnedByAnotherSession);
    expect("P normal save leaves Q intact", ask(p, "clear").value("ok").toBool()
        && !QFileInfo::exists(firstP.value("path").toString())
        && observer.readComparison(workspace, qId).recoveredText == "Q generation 2");
    CrashRecoveryDocumentKey key{workspace, source, {}};
    expect("unrelated session close cannot clear Q", observer.clearAfterNormalClose(key).succeeded()
        && QFileInfo::exists(firstQ.value("path").toString()));
    // Terminate only this test's child, simulating an ungraceful owner exit.
    q.kill(); q.waitForFinished(5000);
    expect("later session can claim crash survivor", observer.recoverText(workspace, qId).text == "Q generation 2");
    CrashRecoveryService competitor(root);
    expect("claim is exclusive across independent services", competitor.recoverText(workspace, qId).status
        == CrashRecoveryStatus::OwnedByAnotherSession);
    expect("claim owner can explicitly retire recovered record", observer.discard(workspace, qId).succeeded());
    const auto lastP = ask(p, "P survives normal application exit");
    p.write("quit\n"); p.waitForBytesWritten(5000); p.waitForFinished(5000);
    expect("normal process exit preserves unclean snapshot", observer.recoverText(workspace,
        lastP.value("id").toString()).text == "P survives normal application exit");

    // A version-1 record has no editing-session component in its digest.
    auto legacy = QJsonDocument::fromJson(readFile(lastP.value("path").toString())).object();
    const auto identity = "workspace:" + legacy.value("workspaceIdentity").toString()
        + "\nfile:" + legacy.value("canonicalFileIdentity").toString();
    const auto legacyId = QString::fromLatin1(CrashRecoveryService::sha256(identity.toUtf8()).toHex());
    legacy.insert("version", 1); legacy.remove("ownerSessionId"); legacy.insert("recoveryId", legacyId);
    const auto legacyPath = QFileInfo(lastP.value("path").toString()).absolutePath() + '/' + legacyId + ".json";
    expect("legacy ownerless candidate remains readable and claimable",
        replaceFile(legacyPath, QJsonDocument(legacy).toJson()) && competitor.recoverText(workspace, legacyId).succeeded());
    CrashRecoveryService unrelated(root);
    expect("automatic cleanup never sweeps unclaimed legacy", unrelated.clearAfterNormalSave(key).succeeded()
        && QFileInfo::exists(legacyPath));
    expect("explicitly claimed legacy is retired by its recovering session", competitor.clearAfterNormalSave(key).succeeded()
        && !QFileInfo::exists(legacyPath));
}

void checkLongLockPaths()
{
    // A short fixture name leaves room to exercise exact final lock boundaries
    // without changing the existing tests' temporary roots.
    QTemporaryDir temp(QDir::tempPath() + "/rXXXXXX");
    expect("lock boundary fixture is valid", temp.isValid());
    if (!temp.isValid()) return;
    const QString workspace = temp.filePath("workspace");
    const QString source = workspace + "/unit.sv";
    const QByteArray baseline("module base; endmodule\n");
    const QString claimSuffix = "/workspaces/" + QString(64, 'a')
        + "/records/" + QString(64, 'b') + ".json.claim";
    for (int length : {259, 260, 261}) {
        const auto check = [&](const char* name, bool value) {
            const auto label = QString("claim-%1: %2").arg(length).arg(name).toUtf8();
            expect(label.constData(), value);
        };
        const int padding = length - claimSuffix.size() - temp.path().size() - 1;
        check("fixture accommodates exact boundary", padding > 0);
        if (padding <= 0) continue;
        const QString root = temp.path() + '/' + QString(padding, 'r');
        check("baseline source is written", replaceFile(source, baseline));
        auto owner = std::make_unique<CrashRecoveryService>(root);
        auto request = fileRequest(workspace, source, "module changed; endmodule\n", 7,
                                   baseline, QFileInfo(source).lastModified().toUTC());
        const auto written = owner->writeSnapshot(request);
        const QString claim = written.storagePath + ".claim";
        check("final ordinary claim path has requested length",
              written.succeeded() && claim.size() == length);
        std::printf("LOCK_BOUNDARY claim requested=%d actual=%lld path=%s\n",
                    length, static_cast<long long>(claim.size()), qPrintable(claim));
        CrashRecoveryService other(root);
        check("active foreign owner rejects recovery and discard",
              other.recoverText(workspace, written.recoveryId).status == CrashRecoveryStatus::OwnedByAnotherSession
              && other.discard(workspace, written.recoveryId).status == CrashRecoveryStatus::OwnedByAnotherSession);
        check("own record is recoverable", owner->recoverText(workspace, written.recoveryId).text == request.text);
        check("discard removes record and released claim", owner->discard(workspace, written.recoveryId).succeeded()
              && !QFileInfo::exists(written.storagePath) && !QFileInfo::exists(claim));
        const auto replacement = owner->writeSnapshot(request);
        check("recreated identity can acquire a new claim", replacement.recoveryId == written.recoveryId
              && owner->recoverText(workspace, written.recoveryId).succeeded());
        owner.reset();
        check("owner teardown releases claim while retaining snapshot",
              !QFileInfo::exists(claim) && QFileInfo::exists(written.storagePath));
        if (length == 259) {
            QLockFile ordinary(claim);
            ordinary.setStaleLockTime(0);
            check("ordinary-path lock remains mutually exclusive with extended-path service",
                  ordinary.tryLock(0) && other.recoverText(workspace, written.recoveryId).status
                      == CrashRecoveryStatus::OwnedByAnotherSession);
            ordinary.unlock();
        }
        check("later session recovers original ordinary record identity",
              other.recoverText(workspace, written.recoveryId).text == request.text);
        CrashRecoveryService competitor(root);
        check("recovered claim excludes a competing session",
              competitor.recoverText(workspace, written.recoveryId).status == CrashRecoveryStatus::OwnedByAnotherSession);
        check("recovering session discards and releases claim", other.discard(workspace, written.recoveryId).succeeded()
              && !QFileInfo::exists(claim));
        const auto corrupt = other.writeSnapshot(request);
        check("full-length corrupt fixture is written", corrupt.succeeded()
              && replaceFile(corrupt.storagePath, "{invalid"));
        const auto corruptScan = other.listCandidates(workspace);
        check("corrupt isolation releases full-length claim", corruptScan.succeeded()
              && corruptScan.candidates.isEmpty() && corruptScan.isolatedRecords.size() == 1
              && corruptScan.isolatedRecords.first().status == CrashRecoveryStatus::CorruptRecord
              && QFileInfo::exists(corruptScan.isolatedRecords.first().quarantinePath)
              && !QFileInfo::exists(corrupt.storagePath) && !QFileInfo::exists(corrupt.storagePath + ".claim"));
        const auto stale = other.writeSnapshot(request);
        check("full-length stale fixture is written", stale.succeeded()
              && replaceFile(source, QByteArray::fromHex("efbbbf") + request.text.toUtf8().replace("\n", "\r\n")));
        const auto staleScan = other.listCandidates(workspace);
        check("stale isolation releases full-length claim", staleScan.succeeded()
              && staleScan.candidates.isEmpty() && staleScan.isolatedRecords.size() == 1
              && staleScan.isolatedRecords.first().status == CrashRecoveryStatus::StaleRecord
              && !QFileInfo::exists(stale.storagePath) && !QFileInfo::exists(stale.storagePath + ".claim"));
    }
    for (int length : {259, 260, 261}) {
        const QString suffix = "/sessions/" + QString(36, 'a') + ".lock";
        const int padding = length - suffix.size() - temp.path().size() - 1;
        const QString root = temp.path() + '/' + QString(padding, 's');
        auto owner = std::make_unique<CrashRecoveryService>(root);
        const auto request = untitledRequest(workspace, "long-session", "dirty text", 1);
        const auto written = owner->writeSnapshot(request);
        const QString lease = root + "/sessions/" + owner->sessionId() + ".lock";
        const auto label = QString("session-%1: lease length and active ownership").arg(length).toUtf8();
        CrashRecoveryService other(root);
        expect(label.constData(), written.succeeded() && lease.size() == length && QFileInfo::exists(lease)
               && other.recoverText(workspace, written.recoveryId).status == CrashRecoveryStatus::OwnedByAnotherSession);
        std::printf("LOCK_BOUNDARY session requested=%d actual=%lld path=%s\n",
                    length, static_cast<long long>(lease.size()), qPrintable(lease));
        owner.reset();
        const auto released = QString("session-%1: released lease permits recovery and discard").arg(length).toUtf8();
        expect(released.constData(), !QFileInfo::exists(lease)
               && other.recoverText(workspace, written.recoveryId).text == request.text
               && other.discard(workspace, written.recoveryId).succeeded()
               && !QFileInfo::exists(written.storagePath + ".claim"));
    }
    // Reuse real peer termination to cover stale long leases, not just teardown.
    checkIndependentEditingSessions(temp.filePath(QString(150, 'c')));
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().value(1) == "--recovery-peer") return recoveryPeer(application.arguments());
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

    auto service = std::make_unique<CrashRecoveryService>(recoveryRoot);
    const CrashRecoveryService defaultService;
    const QString applicationLocalData =
        QDir::cleanPath(
            QStandardPaths::writableLocation(
                QStandardPaths::AppLocalDataLocation));
    expect("injected storage is outside workspace metadata",
           service->recoveryRootPath()
                   == QDir::cleanPath(recoveryRoot)
               && !service->recoveryRootPath().startsWith(
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
        service->writeSnapshot(request);
    expect("dirty snapshot is atomically committed",
           firstWrite.succeeded()
               && firstWrite.status
                      == CrashRecoveryStatus::Success
               && hasExplicitResult(firstWrite)
               && QFileInfo(firstWrite.storagePath).isFile()
               && firstWrite.storagePath.startsWith(
                   service->recoveryRootPath()));
    expect("snapshot creation never changes source",
           readFile(sourcePath) == baseline);

    request.text = secondDirtyText;
    request.documentRevision = 42;
    const CrashRecoveryWriteResult secondWrite =
        service->writeSnapshot(request);
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

    service.reset(); // The previous editing session has exited.
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

    {
        CrashRecoverySnapshotRequest race;
        race.document.workspacePath = workspaceA;
        race.document.untitledDocumentId = "commit-race";
        race.text = QString(100000, QLatin1Char('x'));
        const auto initial = restartedService.writeSnapshot(race);
        expect("commit gate race starts with a valid record", initial.succeeded());
        QSemaphore prepared, release;
        QMutex gate;
        bool cancelled = false;
        CrashRecoveryWriteResult late;
        race.text += "changed";
        std::thread worker([&] {
            late = restartedService.writeSnapshot(race, [&](const std::function<bool()>& commit) {
                prepared.release();
                release.acquire();
                QMutexLocker lock(&gate);
                return !cancelled && commit();
            });
        });
        const bool reached = prepared.tryAcquire(1, 5000);
        {
            QMutexLocker lock(&gate);
            cancelled = true;
        }
        const auto cleared = restartedService.clearAfterNormalSave(race.document);
        release.release();
        worker.join();
        expect("clear between serialization and commit prevents record resurrection",
            reached && cleared.succeeded() && late.status == CrashRecoveryStatus::Cancelled
            && !QFileInfo::exists(initial.storagePath));
    }

    checkIndependentEditingSessions();
    checkLongLockPaths();
    std::printf(
        "Crash recovery service checks: %d, failures: %d\n",
        checks,
        failures);
    return failures == 0 ? 0 : 1;
}
