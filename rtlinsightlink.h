#ifndef RTLINSIGHTLINK_H
#define RTLINSIGHTLINK_H

#include <QFileInfo>
#include <QString>

struct RtlInsightCodeLink {
    QString fileName;
    int line = 0;
    int column = 0;
    QString fileDisplayName;
    QString lineDisplayName;
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
