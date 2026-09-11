#ifndef RTLINSIGHTLINK_H
#define RTLINSIGHTLINK_H

#include <QFileInfo>
#include <QString>
#include <QtGlobal>

struct RtlInsightCodeLink {
    QString fileName;
    int line = 0;
    int column = 0;
    QString fileDisplayName;
    QString lineDisplayName;
};

// Stable hand-off between the editor and an already materialized insight
// graph. A zero graphGeneration means "use the current graph"; a non-zero
// value is checked so an asynchronous/stale source selection cannot select an
// item from a newer graph.
struct RtlInsightSourceLocation {
    QString fileName;
    int line = 0;
    int column = 0;
    QString moduleName;
    QString symbolName;
    QString workspacePath;
    QString activeTopModule;
    QString instancePath;
    QString elementKind;
    QString secondarySymbolName;
    quint64 documentRevision = 0;
    quint64 graphGeneration = 0;

    bool hasSourcePosition() const
    {
        return !fileName.isEmpty() && line > 0;
    }
};

namespace RtlInsightLink {

inline QString fileDisplayName(const QString& fileName)
{
    QString displayName = QFileInfo(fileName).fileName();
    if (displayName.isEmpty())
        displayName = fileName;
    return displayName;
}

inline QString lineDisplayName(int line)
{
    return line > 0 ? QString::number(line) : QString();
}

inline RtlInsightCodeLink fromFileLine(
    const QString& fileName,
    int line,
    int column)
{
    RtlInsightCodeLink link;
    link.fileName = fileName;
    link.line = line;
    link.column = column;
    link.fileDisplayName = fileDisplayName(fileName);
    link.lineDisplayName = lineDisplayName(line);
    return link;
}

template <typename Diagnostic>
RtlInsightCodeLink fromDiagnostic(const Diagnostic& diagnostic)
{
    return fromFileLine(diagnostic.fileName, diagnostic.line, diagnostic.column);
}

} // namespace RtlInsightLink

#endif // RTLINSIGHTLINK_H
