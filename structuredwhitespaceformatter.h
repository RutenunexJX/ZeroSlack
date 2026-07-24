#ifndef STRUCTUREDWHITESPACEFORMATTER_H
#define STRUCTUREDWHITESPACEFORMATTER_H

#include <QList>
#include <QString>

namespace StructuredWhitespaceFormatter {

struct LineRange {
    int firstLine = -1;
    int lastLine = -1;

    bool valid() const
    {
        return firstLine >= 0 && lastLine >= firstLine;
    }
};

// Replaces lexical-whitespace Tab characters with a fixed number of spaces.
// Tree-sitter comment and string ranges are immutable and remain byte-exact.
QString normalizeLexicalWhitespaceTabs(const QString& text,
                                       int spacesPerTab);

// Removes the synthetic design-unit indentation level while preserving the
// relative indentation of procedural and structural bodies.
QString formatDesignUnitIndentation(const QString& text,
                                    int indentWidth);

// Formats module headers, ANSI parameter / port declarations, and module
// instantiation associations from Tree-sitter spans. Every produced edit is
// confined to a whitespace gap between immutable syntax tokens.
QString format(const QString& text, int indentWidth);

// Returns syntax regions that the structured formatter cannot update as one
// complete whitespace-only transaction. FormatterService uses these ranges
// to prevent the legacy line formatter from changing only part of a header or
// instantiation before the structured formatter declines it.
QList<LineRange> conservativeLineRanges(const QString& text,
                                        int indentWidth);

// The formatter's final safety gate. Whitespace may change, but every
// non-whitespace code unit must remain byte-for-byte ordered and identical.
bool hasIdenticalNonWhitespaceStream(const QString& before,
                                     const QString& after);

} // namespace StructuredWhitespaceFormatter

#endif // STRUCTUREDWHITESPACEFORMATTER_H
