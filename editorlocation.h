#ifndef EDITORLOCATION_H
#define EDITORLOCATION_H

#include "editorfileidentity.h"

#include <QFileInfo>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtGlobal>

#include <optional>

// All positions in this model are one-based.  The range is navigation
// metadata only: callers always open the complete document and may use the
// range for the initial selection/highlight.
struct EditorSelectionRange {
    int startLine = 0;
    int startColumn = 0;
    int endLine = 0;
    int endColumn = 0;

    bool isValid() const
    {
        if (startLine < 1 || startColumn < 1
            || endLine < 1 || endColumn < 1) {
            return false;
        }
        return startLine < endLine
            || (startLine == endLine
                && startColumn <= endColumn);
    }

    bool isEmpty() const
    {
        return isValid()
            && startLine == endLine
            && startColumn == endColumn;
    }

    bool operator==(const EditorSelectionRange& other) const = default;
};

struct EditorLocation {
    // documentId is the stable identity supplied by DocumentModel.  filePath
    // remains present so unresolved navigation results can be materialized by
    // the controller before a shared editor view is attached.
    QString documentId;
    QString filePath;
    int line = 1;
    int column = 1;
    std::optional<EditorSelectionRange> selection;
    QString symbolKey;
    QString sourceLinkId;

    bool hasDocumentIdentity() const
    {
        return !documentId.trimmed().isEmpty()
            || !filePath.trimmed().isEmpty();
    }

    bool hasStableDocumentId() const
    {
        return !documentId.trimmed().isEmpty();
    }

    bool isValid() const
    {
        return hasDocumentIdentity()
            && line >= 1
            && column >= 1
            && (!selection || selection->isValid());
    }

    QString documentKey() const
    {
        const QString stableId = documentId.trimmed();
        if (!stableId.isEmpty())
            return stableId;
        return EditorFileIdentity::lookupKey(filePath);
    }

    bool refersToSameDocument(const EditorLocation& other) const
    {
        const QString lhsId = documentId.trimmed();
        const QString rhsId = other.documentId.trimmed();
        if (!lhsId.isEmpty() && !rhsId.isEmpty())
            return lhsId == rhsId;
        return EditorFileIdentity::same(filePath, other.filePath);
    }

    bool equivalentTo(const EditorLocation& other) const
    {
        return refersToSameDocument(other)
            && line == other.line
            && column == other.column
            && selection == other.selection
            && symbolKey == other.symbolKey
            && sourceLinkId == other.sourceLinkId;
    }

    QString displayText() const
    {
        QString fileName = QFileInfo(filePath).fileName();
        if (fileName.isEmpty())
            fileName = filePath.trimmed();
        if (fileName.isEmpty()) {
            const QString stableId = documentId.trimmed();
            fileName = QFileInfo(stableId).fileName();
            if (fileName.isEmpty())
                fileName = stableId;
        }
        if (fileName.isEmpty())
            fileName = QStringLiteral("Untitled");

        const QString symbol = symbolKey.trimmed();
        if (!symbol.isEmpty()) {
            return QStringLiteral("%1 \u00b7 %2")
                .arg(fileName, symbol);
        }
        if (line >= 1) {
            return QStringLiteral("%1 : %2")
                .arg(fileName)
                .arg(line);
        }
        return fileName;
    }

    bool operator==(const EditorLocation& other) const
    {
        return documentId == other.documentId
            && filePath == other.filePath
            && line == other.line
            && column == other.column
            && selection == other.selection
            && symbolKey == other.symbolKey
            && sourceLinkId == other.sourceLinkId;
    }

    bool operator!=(const EditorLocation& other) const
    {
        return !(*this == other);
    }
};

using OpenTarget = EditorLocation;

inline EditorLocation editorLocationFromActionParameters(
    const QVariantMap& parameters)
{
    EditorLocation location;
    location.documentId = parameters
                              .value(QStringLiteral("documentId"))
                              .toString();
    location.filePath = parameters
                            .value(QStringLiteral("path"))
                            .toString();
    location.line = qMax(
        1,
        parameters.value(QStringLiteral("line"), 1).toInt());
    location.column = qMax(
        1,
        parameters.value(QStringLiteral("column"), 1).toInt());
    location.symbolKey = parameters
                            .value(QStringLiteral("symbolId"))
                            .toString();
    location.sourceLinkId = parameters
                                .value(QStringLiteral("sourceLinkId"))
                                .toString();

    const QStringList selectionKeys = {
        QStringLiteral("selectionStartLine"),
        QStringLiteral("selectionStartColumn"),
        QStringLiteral("selectionEndLine"),
        QStringLiteral("selectionEndColumn"),
    };
    bool completeSelection = true;
    for (const QString& key : selectionKeys)
        completeSelection = parameters.contains(key) && completeSelection;
    if (completeSelection) {
        EditorSelectionRange selection;
        selection.startLine = parameters
                                  .value(selectionKeys.at(0))
                                  .toInt();
        selection.startColumn = parameters
                                    .value(selectionKeys.at(1))
                                    .toInt();
        selection.endLine = parameters
                                .value(selectionKeys.at(2))
                                .toInt();
        selection.endColumn = parameters
                                  .value(selectionKeys.at(3))
                                  .toInt();
        location.selection = selection;
    }
    return location;
}

inline QVariantMap editorLocationActionParameters(
    const EditorLocation& location)
{
    QVariantMap parameters;
    if (!location.documentId.trimmed().isEmpty()) {
        parameters.insert(
            QStringLiteral("documentId"),
            location.documentId);
    }
    if (!location.filePath.trimmed().isEmpty()) {
        parameters.insert(
            QStringLiteral("path"),
            location.filePath);
    }
    parameters.insert(
        QStringLiteral("line"),
        qMax(1, location.line));
    parameters.insert(
        QStringLiteral("column"),
        qMax(1, location.column));
    if (!location.symbolKey.trimmed().isEmpty()) {
        parameters.insert(
            QStringLiteral("symbolId"),
            location.symbolKey);
    }
    if (!location.sourceLinkId.trimmed().isEmpty()) {
        parameters.insert(
            QStringLiteral("sourceLinkId"),
            location.sourceLinkId);
    }
    if (location.selection
        && location.selection->isValid()) {
        parameters.insert(
            QStringLiteral("selectionStartLine"),
            location.selection->startLine);
        parameters.insert(
            QStringLiteral("selectionStartColumn"),
            location.selection->startColumn);
        parameters.insert(
            QStringLiteral("selectionEndLine"),
            location.selection->endLine);
        parameters.insert(
            QStringLiteral("selectionEndColumn"),
            location.selection->endColumn);
    }
    return parameters;
}

Q_DECLARE_METATYPE(EditorSelectionRange)
Q_DECLARE_METATYPE(EditorLocation)

#endif // EDITORLOCATION_H
