#include "previewsource.h"
#include "documentmodel.h"
#include "semanticindexsnapshot.h"

namespace {
int offsetAt(const QString& text, int line, int column) {
    if (line < 1 || column < 1) return -1;
    int start = 0;
    for (int row = 1; row < line; ++row) {
        const int newline = text.indexOf('\n', start);
        if (newline < 0) return -1;
        start = newline + 1;
    }
    int end = text.indexOf('\n', start);
    if (end < 0) end = text.size();
    return column - 1 <= end - start ? start + column - 1 : -1;
}
}

PreviewSource PreviewSource::capture(const QString& fileName, DocumentModel* documents,
    const std::shared_ptr<const SemanticIndexSnapshot>& locationSnapshot)
{
    PreviewSource source;
    if (locationSnapshot) {
        const auto cached = locationSnapshot->cachedFileSource(fileName);
        source.hasLocationWitness = cached.exists;
        source.locationText = cached.text;
        source.documentId = cached.fileKey;
    }
    if (documents) {
        const auto document = documents->documentForFile(fileName);
        if (!document.documentId.isEmpty()) {
            source.exists = true;
            source.origin = QStringLiteral("open document");
            source.documentId = document.documentId;
            source.documentRevision = document.textVersion;
            source.text = document.text;
            return source;
        }
    }
    source.exists = source.hasLocationWitness;
    source.origin = source.exists ? QStringLiteral("semantic snapshot") : QStringLiteral("unavailable");
    source.text = source.locationText;
    return source;
}

PreviewExcerpt PreviewSource::excerpt(int line, int column, int endLine, int endColumn,
                                     int before, int after) const
{
    PreviewExcerpt result;
    if (!exists) {
        result.reason = QStringLiteral("Preview source unavailable.");
        return result;
    }
    if (!hasLocationWitness) {
        result.stale = true;
        result.reason = QStringLiteral("Location source version unavailable. Refresh analysis before previewing.");
        return result;
    }
    if (text.isEmpty()) {
        result.stale = text != locationText;
        result.reason = QStringLiteral("The current document is empty; no code preview is available.");
        return result;
    }
    const auto lines = text.split('\n');
    if (line <= 0 || line > lines.size()) {
        result.stale = text != locationText;
        result.reason = QStringLiteral("Preview location is no longer available. Refresh analysis.");
        return result;
    }
    if (offsetAt(locationText, line, qMax(1, column)) < 0
        || (endLine > 0 && endColumn > 0
            && (endLine < line || (endLine == line && endColumn <= column)
                || offsetAt(locationText, endLine, endColumn) < 0))) {
        result.stale = true;
        result.reason = QStringLiteral("Preview range does not match its analysis source. Refresh analysis.");
        return result;
    }
    if (text != locationText) {
        // Permit edits after the witnessed anchor (e.g. an unsaved same-line
        // note). Any changed prefix or anchor invalidates its old coordinates.
        if (endLine < line || (endLine == line && endColumn <= column)) {
            endLine = line;
            const auto oldLines = locationText.split('\n');
            endColumn = line <= oldLines.size() ? oldLines[line-1].size()+1 : 0;
        }
        const int end = offsetAt(locationText,endLine,endColumn);
        const int currentEnd = offsetAt(text,endLine,endColumn);
        if (end < 0 || end != currentEnd
            || QStringView(text).first(end) != QStringView(locationText).first(end)) {
            result.stale = true;
            result.reason = QStringLiteral("Source changed before this location. Refresh analysis before previewing.");
            return result;
        }
    }
    result.firstLine = qMax(1,line-qMax(0,before));
    const int last = qMin(int(lines.size()),line+qMax(0,after));
    for (int row=result.firstLine; row<=last; ++row) result.lines.append(lines[row-1]);
    if (!result.lines.isEmpty() && result.lines.last().isEmpty() && last == lines.size()) result.lines.removeLast();
    result.available = !result.lines.isEmpty();
    return result;
}
