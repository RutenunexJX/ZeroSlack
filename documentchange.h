#ifndef DOCUMENTCHANGE_H
#define DOCUMENTCHANGE_H

#include <QMetaType>
#include <QString>

#include <cstdint>

struct DocumentChange {
    int position = 0;
    int removedLength = 0;
    QString removedText;
    QString insertedText;
    int oldLength = 0;
    int newLength = 0;
    int startLine = 0;
    int startColumn = 0;
    int oldEndLine = 0;
    int newEndLine = 0;
    int lineDelta = 0;
    std::uint64_t revision = 0;

    int oldEnd() const { return position + removedLength; }
    int newEnd() const { return position + insertedText.size(); }
    int characterDelta() const
    {
        return insertedText.size() - removedLength;
    }
    bool changesText() const { return removedText != insertedText; }
};

struct EditorHotPathMetrics {
    std::uint64_t documentChanges = 0;
    std::uint64_t fullTextMaterializations = 0;
    std::uint64_t fullFoldingRebuilds = 0;
    std::uint64_t incrementalFoldingUpdates = 0;
    std::uint64_t fullGhostQueries = 0;
    std::uint64_t ghostRemaps = 0;
    std::uint64_t occurrenceFullBuilds = 0;
    std::uint64_t occurrenceIncrementalUpdates = 0;
    std::uint64_t cachedTextSliceReads = 0;
    std::uint64_t cachedTextSliceCharacters = 0;
    std::uint64_t gutterBlockProbes = 0;
};

Q_DECLARE_METATYPE(DocumentChange)

#endif // DOCUMENTCHANGE_H
