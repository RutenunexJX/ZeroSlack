// Headless harness: dump semantic records produced by SlangManager.
// Build/link against the already-compiled slangmanager.cpp.obj + slang libs + Qt6Core.
#include "slangmanager.h"
#include <QFile>
#include <QTextStream>
#include <QString>
#include <cstdio>

int main(int argc, char** argv) {
    QString path = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                              : QStringLiteral("test_sv/test_symbols.sv");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QFile::Text)) {
        fprintf(stderr, "cannot open %s\n", path.toLocal8Bit().constData());
        return 1;
    }
    QString content = QTextStream(&f).readAll();
    f.close();

    SlangManager mgr;
    QList<SemanticSymbolRecord> records = mgr.extractSymbolRecords(path, content);

    printf("extracted %d symbol records from %s\n\n", (int)records.size(),
           path.toLocal8Bit().constData());
    printf("%-20s %-20s %5s  %-14s %-12s\n",
           "name", "type", "line", "moduleScope", "dataType");
    printf("--------------------------------------------------------------------------\n");
    for (const SemanticSymbolRecord& record : records) {
        const QString typeLabel =
            SymbolTaxonomy::symbolTypeLabel(
                semanticMetadataForSymbolRecord(record));
        printf("%-20s %-20s %5d  %-14s %-12s\n",
               record.name.toLocal8Bit().constData(),
               typeLabel.toLocal8Bit().constData(),
               record.location.startLine,
               record.owner.name.toLocal8Bit().constData(),
               record.type.rawTypeText.toLocal8Bit().constData());
    }
    return 0;
}
