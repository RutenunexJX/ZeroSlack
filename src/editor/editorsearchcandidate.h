#ifndef EDITORSEARCHCANDIDATE_H
#define EDITORSEARCHCANDIDATE_H

#include "editorlocation.h"

#include <QList>
#include <QMetaType>
#include <QString>

enum class EditorSearchCandidateType {
    File,
    Module,
    Package,
    Symbol,
};

struct EditorSearchCandidate {
    EditorLocation location;
    QString title;
    EditorSearchCandidateType type = EditorSearchCandidateType::File;
    QString disambiguation;
    int score = 0;

    bool isValid() const
    {
        return location.isValid() && !title.trimmed().isEmpty();
    }

    bool operator==(const EditorSearchCandidate& other) const = default;
};

using EditorSearchCandidates = QList<EditorSearchCandidate>;

inline QString editorSearchCandidateTypeLabel(
    EditorSearchCandidateType type)
{
    switch (type) {
    case EditorSearchCandidateType::File:
        return QStringLiteral("file");
    case EditorSearchCandidateType::Module:
        return QStringLiteral("module");
    case EditorSearchCandidateType::Package:
        return QStringLiteral("package");
    case EditorSearchCandidateType::Symbol:
        return QStringLiteral("symbol");
    }
    return QStringLiteral("symbol");
}

Q_DECLARE_METATYPE(EditorSearchCandidateType)
Q_DECLARE_METATYPE(EditorSearchCandidate)

#endif // EDITORSEARCHCANDIDATE_H
