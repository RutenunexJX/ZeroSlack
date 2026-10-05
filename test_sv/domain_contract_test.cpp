#include <zeroslack/documents/documentbuffer.h>
#include <zeroslack/documents/tsdocument.h>
#include <zeroslack/semantic/moduleblockdiagramservice.h>
#include <zeroslack/semantic/semanticindexsnapshot.h>
#include <zeroslack/semantic/slangmanager.h>

#include <QDir>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

class DomainContractTest : public QObject
{
    Q_OBJECT
private slots:
    void documentOwnsRevisionAndUndoWithoutEditorWidgets()
    {
        DocumentBuffer document({}, {}, QStringLiteral("module top; endmodule\n"));
        QSignalSpy revisions(&document, &DocumentBuffer::textRevisionChanged);
        QTextCursor cursor(document.textDocument());
        cursor.insertText(QStringLiteral("// edited\n"));
        QCOMPARE(document.textRevision(), std::uint64_t(1));
        QCOMPARE(revisions.size(), 1);
        QVERIFY(document.dirty());
        document.textDocument()->undo();
        QCOMPARE(document.textDocument()->toPlainText(), QStringLiteral("module top; endmodule\n"));
        QCOMPARE(document.textRevision(), std::uint64_t(2));
        document.markSaved();
        QVERIFY(!document.dirty());
        QCOMPARE(document.savedTextRevision(), document.textRevision());
        QCOMPARE(document.savedBaselineSha256().size(), 32);
    }

    void queryOwnsSnapshotAfterOriginalPublicationIsReleased()
    {
        const QString file = QDir::temp().filePath(QStringLiteral("owned_query.sv"));
        std::unique_ptr<ModuleBlockDiagramService> service;
        std::weak_ptr<const SemanticIndexSnapshot> lifetime;
        const auto globalBefore = SemanticIndex::getInstance()->snapshotToken();
        {
            const QString source = QStringLiteral("module top; endmodule\n");
            SlangManager slang;
            auto snapshot = std::make_shared<const SemanticIndexSnapshot>(
                SemanticIndexSnapshot::fromSymbolRecords(
                    slang.extractSymbolRecords(file, source), {}, {}, {{file, source}}));
            lifetime = snapshot;
            service = std::make_unique<ModuleBlockDiagramService>(SemanticSnapshotToken{snapshot, 73});
        }
        QVERIFY(!lifetime.expired());
        ModuleBlockDiagramQuery query;
        query.fileName = file;
        query.moduleName = QStringLiteral("top");
        const auto report = service->buildModuleBlockDiagram(query);
        QVERIFY(report.found);
        QCOMPARE(report.root.moduleDisplayName, QStringLiteral("top"));
        QCOMPARE(SemanticIndex::getInstance()->snapshotToken().snapshot, globalBefore.snapshot);
        service.reset();
        QVERIFY(lifetime.expired());
    }
};

QTEST_MAIN(DomainContractTest)
#include "domain_contract_test.moc"
