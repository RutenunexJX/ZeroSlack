#ifndef SAFERENAMESERVICE_H
#define SAFERENAMESERVICE_H

#include "semanticindex.h"

#include <QHash>
#include <QList>
#include <QString>

struct SafeRenameTextEdit {
    QString fileName;
    int startPosition = -1;
    int length = 0;
    QString oldText;
    QString newText;

    bool isValid() const
    {
        return !fileName.isEmpty()
            && startPosition >= 0
            && length > 0
            && !oldText.isEmpty();
    }
};

struct SafeRenameFileEdits {
    QString fileName;
    QList<SafeRenameTextEdit> edits;
};

enum class SafeRenamePlanStatus {
    Ready,
    InvalidSymbol,
    InvalidNewName,
    NoChange,
    DefinitionNotFound,
    ConflictingDefinition,
    MissingFileContent,
    AmbiguousEditRange
};

struct SafeRenamePlanQuery {
    QString symbolName;
    QString newName;
    QString fileName;
    QString moduleName;
    QString documentText;
    int cursorPosition = -1;
    QHash<QString, QString> openFileContents;
    bool forceConflicts = false;
};

struct SafeRenamePlan {
    SafeRenamePlanStatus status = SafeRenamePlanStatus::InvalidSymbol;
    QString message;
    QString symbolName;
    QString newName;
    SemanticSymbolRecord subjectRecord;
    SymbolStableKey subjectStableKey;
    QList<SemanticSymbolRecord> conflictingDefinitions;
    QList<SafeRenameFileEdits> fileEdits;

    bool isReady() const { return status == SafeRenamePlanStatus::Ready; }
    int editCount() const;
};

class SafeRenameService
{
public:
    explicit SafeRenameService(SemanticIndex* semanticIndex = nullptr);

    void setSemanticIndex(SemanticIndex* semanticIndex);
    SafeRenamePlan createRenamePlan(const SafeRenamePlanQuery& query) const;

    static bool isValidIdentifier(const QString& text);

private:
    SemanticIndex* index = nullptr;

    SemanticIndex* semanticIndex() const;
};

#endif // SAFERENAMESERVICE_H
