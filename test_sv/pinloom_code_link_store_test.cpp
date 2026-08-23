#include "pinloomcodelinkstore.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

class PinloomCodeLinkStoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void persistsAndRelocatesWorkspaceRelativeLinks();
};

void PinloomCodeLinkStoreTest::persistsAndRelocatesWorkspaceRelativeLinks()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString filePath =
        QDir(workspace.path()).filePath(QStringLiteral("rtl/top.sv"));
    const QString original = QStringLiteral(
        "module top;\n"
        "  logic reset_value;\n"
        "  assign reset_value = 1'b0;\n"
        "endmodule\n");
    const int start = original.indexOf(QStringLiteral("assign"));
    const int end = original.indexOf(QLatin1Char(';'), start) + 1;
    const PinloomSourceSelection source =
        PinloomSourceSelection::fromDocumentSelection(
            workspace.path(), filePath, QStringLiteral("top"),
            original, start, end);
    QVERIFY(source.isValid());

    PinloomCodeLinkStore store;
    store.setWorkspaceRoot(workspace.path());
    QString failure;
    QVERIFY2(store.addLink(
                 source,
                 QUrl(QStringLiteral(
                     "pinloom://entry/anchor:test?resource=r&anchor=a")),
                 QStringLiteral("Reset assignment"),
                 {{QStringLiteral("resourceId"), QStringLiteral("r")},
                  {QStringLiteral("anchorId"), QStringLiteral("a")}},
                 &failure),
             qPrintable(failure));
    QVERIFY(QFileInfo::exists(store.storagePath()));

    PinloomCodeLinkStore restored;
    restored.setWorkspaceRoot(workspace.path());
    QCOMPARE(restored.records().size(), 1);
    QList<ResolvedPinloomCodeLink> resolved =
        restored.linksForDocument(filePath, original);
    QCOMPARE(resolved.size(), 1);
    QCOMPARE(resolved.first().resolution,
             PinloomCodeLinkResolution::Exact);
    QCOMPARE(resolved.first().startPosition, start);

    const QString moved = QStringLiteral("// inserted\n") + original;
    resolved = restored.linksForDocument(filePath, moved);
    QCOMPARE(resolved.size(), 1);
    QCOMPARE(resolved.first().resolution,
             PinloomCodeLinkResolution::Moved);
    QCOMPARE(resolved.first().startPosition,
             start + QStringLiteral("// inserted\n").size());
    QCOMPARE(restored.linksAtPosition(
                 filePath, moved, resolved.first().startPosition + 2).size(),
             1);
}

QTEST_MAIN(PinloomCodeLinkStoreTest)
#include "pinloom_code_link_store_test.moc"
