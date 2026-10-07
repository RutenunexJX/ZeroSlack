#ifndef EDITORSEARCHCANDIDATE_H
#define EDITORSEARCHCANDIDATE_H

#include "editorlocation.h"

#include <QList>
#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <functional>
#include <optional>

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
    // Snapshot-backed semantic coordinates require the same logical source at
    // activation. An empty digest is unavailable evidence; nullopt is unbound.
    std::optional<QByteArray> sourceTextSha256;

    bool isValid() const
    {
        return location.isValid() && !title.trimmed().isEmpty();
    }

    bool operator==(const EditorSearchCandidate& other) const = default;
};

using EditorSearchCandidates = QList<EditorSearchCandidate>;
using EditorSearchCancellation = std::function<bool()>;
using EditorSearchTask = std::function<EditorSearchCandidates(const EditorSearchCancellation&)>;
using EditorSearchTaskProvider = std::function<EditorSearchTask(const QString&)>;

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
