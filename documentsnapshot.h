#ifndef DOCUMENTSNAPSHOT_H
#define DOCUMENTSNAPSHOT_H

#include <QMetaType>
#include <QString>

struct DocumentSnapshot {
    QString documentId;
    QString fileName;
    int textVersion = 0;
    int savedTextVersion = 0;
    bool dirty = false;
    bool saved = true;
    int cursorPosition = 0;
    int cursorLine = 1;
    int cursorColumn = 1;
    QString currentModuleName;
};

Q_DECLARE_METATYPE(DocumentSnapshot)

#endif // DOCUMENTSNAPSHOT_H
