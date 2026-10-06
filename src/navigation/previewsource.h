#pragma once

#include <QString>
#include <QStringList>
#include <memory>

class DocumentModel;
class SemanticIndexSnapshot;

struct PreviewExcerpt {
    bool available = false;
    bool stale = false;
    int firstLine = 0;
    QStringList lines;
    QString reason;
};

// Text and its location witness are captured together for one synchronous
// preview. An open empty document is a real source, never a cache miss.
struct PreviewSource {
    bool exists = false;
    bool hasLocationWitness = false;
    QString origin;
    QString documentId;
    int documentRevision = 0;
    QString text;
    QString locationText;

    static PreviewSource capture(const QString& fileName, DocumentModel* documents,
        const std::shared_ptr<const SemanticIndexSnapshot>& locationSnapshot);
    PreviewExcerpt excerpt(int line, int column, int endLine, int endColumn,
                           int before, int after) const;
};
