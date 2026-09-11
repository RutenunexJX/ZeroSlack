#include "rtlbatcheditservice.h"

#include <QSet>
#include <QtGlobal>

std::unique_ptr<RtlBatchEditService> RtlBatchEditService::instance = nullptr;

namespace {
struct StatementSpan {
    int start = 0;
    int semicolon = -1;
};

enum class ScanState {
    Normal,
    LineComment,
    BlockComment,
    String
};

struct AssignmentCandidate {
    bool valid = false;
    bool ambiguous = false;
    int operatorStart = -1;
    int operatorEnd = -1;
    bool nonBlocking = false;
};

struct ClassifiedAssignment {
    bool supported = false;
    RtlAssignmentStatementKind kind = RtlAssignmentStatementKind::Blocking;
    QString failureReason;
};

bool isIdentifierStart(QChar ch)
{
    return ch == QLatin1Char('_') || ch == QLatin1Char('$') || ch.isLetter();
}

bool isIdentifierPart(QChar ch)
{
    return isIdentifierStart(ch) || ch.isDigit();
}

bool isOperatorNeighbor(QChar ch)
{
    return ch == QLatin1Char('<') || ch == QLatin1Char('>')
        || ch == QLatin1Char('=') || ch == QLatin1Char('!')
        || ch == QLatin1Char('+') || ch == QLatin1Char('-')
        || ch == QLatin1Char('*') || ch == QLatin1Char('/')
        || ch == QLatin1Char('%') || ch == QLatin1Char('&')
        || ch == QLatin1Char('|') || ch == QLatin1Char('^')
        || ch == QLatin1Char('?');
}

bool hasSignificantCode(const QString& text, int start, int end)
{
    for (int i = qMax(0, start); i < qMin(end, text.size()); ++i) {
        const QChar ch = text.at(i);
        if (ch.isSpace())
            continue;
        if (ch == QLatin1Char('/') && i + 1 < end) {
            const QChar next = text.at(i + 1);
            if (next == QLatin1Char('/')) {
                i += 2;
                while (i < end && text.at(i) != QLatin1Char('\n'))
                    ++i;
                continue;
            }
            if (next == QLatin1Char('*')) {
                i += 2;
                while (i + 1 < end
                       && !(text.at(i) == QLatin1Char('*')
                            && text.at(i + 1) == QLatin1Char('/'))) {
                    ++i;
                }
                if (i + 1 < end)
                    ++i;
                continue;
            }
        }
        return true;
    }
    return false;
}

int skipLeadingTrivia(const QString& text, int start, int end)
{
    int i = qMax(0, start);
    const int limit = qMin(end, text.size());
    while (i < limit) {
        const QChar ch = text.at(i);
        if (ch.isSpace()) {
            ++i;
            continue;
        }
        if (ch == QLatin1Char('/') && i + 1 < limit) {
            const QChar next = text.at(i + 1);
            if (next == QLatin1Char('/')) {
                i += 2;
                while (i < limit && text.at(i) != QLatin1Char('\n'))
                    ++i;
                continue;
            }
            if (next == QLatin1Char('*')) {
                i += 2;
                while (i + 1 < limit
                       && !(text.at(i) == QLatin1Char('*')
                            && text.at(i + 1) == QLatin1Char('/'))) {
                    ++i;
                }
                if (i + 1 < limit)
                    i += 2;
                continue;
            }
        }
        break;
    }
    return i;
}

QString firstStatementToken(const QString& text, int start, int end)
{
    int i = skipLeadingTrivia(text, start, end);
    const int limit = qMin(end, text.size());
    if (i >= limit)
        return QString();

    if (text.at(i) == QLatin1Char('\\')) {
        const int tokenStart = i;
        ++i;
        while (i < limit && !text.at(i).isSpace())
            ++i;
        return text.mid(tokenStart, i - tokenStart);
    }

    if (!isIdentifierStart(text.at(i)))
        return QString(text.at(i));

    const int tokenStart = i;
    ++i;
    while (i < limit && isIdentifierPart(text.at(i)))
        ++i;
    return text.mid(tokenStart, i - tokenStart);
}

bool collectTopLevelStatements(const QString& text,
                               QList<StatementSpan>* spans,
                               QString* failureReason)
{
    int start = 0;
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    ScanState state = ScanState::Normal;

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        const QChar next =
            i + 1 < text.size() ? text.at(i + 1) : QChar();

        if (state == ScanState::LineComment) {
            if (ch == QLatin1Char('\n'))
                state = ScanState::Normal;
            continue;
        }
        if (state == ScanState::BlockComment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                state = ScanState::Normal;
                ++i;
            }
            continue;
        }
        if (state == ScanState::String) {
            if (ch == QLatin1Char('\\') && i + 1 < text.size()) {
                ++i;
                continue;
            }
            if (ch == QLatin1Char('"'))
                state = ScanState::Normal;
            continue;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
            state = ScanState::LineComment;
            ++i;
            continue;
        }
        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            state = ScanState::BlockComment;
            ++i;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            state = ScanState::String;
            continue;
        }

        if (ch == QLatin1Char('(')) {
            ++parenDepth;
            continue;
        }
        if (ch == QLatin1Char(')')) {
            --parenDepth;
            if (parenDepth < 0) {
                if (failureReason)
                    *failureReason =
                        QStringLiteral("Selection has unmatched ')'");
                return false;
            }
            continue;
        }
        if (ch == QLatin1Char('[')) {
            ++bracketDepth;
            continue;
        }
        if (ch == QLatin1Char(']')) {
            --bracketDepth;
            if (bracketDepth < 0) {
                if (failureReason)
                    *failureReason =
                        QStringLiteral("Selection has unmatched ']'");
                return false;
            }
            continue;
        }
        if (ch == QLatin1Char('{')) {
            ++braceDepth;
            continue;
        }
        if (ch == QLatin1Char('}')) {
            --braceDepth;
            if (braceDepth < 0) {
                if (failureReason)
                    *failureReason =
                        QStringLiteral("Selection has unmatched '}'");
                return false;
            }
            continue;
        }

        if (ch == QLatin1Char(';') && parenDepth == 0
            && bracketDepth == 0 && braceDepth == 0) {
            spans->append(StatementSpan{start, i});
            start = i + 1;
        }
    }

    if (state == ScanState::BlockComment) {
        if (failureReason)
            *failureReason = QStringLiteral("Selection has unterminated comment");
        return false;
    }
    if (state == ScanState::String) {
        if (failureReason)
            *failureReason = QStringLiteral("Selection has unterminated string");
        return false;
    }
    if (parenDepth != 0 || bracketDepth != 0 || braceDepth != 0) {
        if (failureReason)
            *failureReason = QStringLiteral("Selection has unmatched delimiters");
        return false;
    }
    if (hasSignificantCode(text, start, text.size())) {
        if (failureReason)
            *failureReason =
                QStringLiteral("Selection contains incomplete statement");
        return false;
    }

    return true;
}

bool isBlockingAssignmentOperator(const QString& text, int index, int end)
{
    if (text.at(index) != QLatin1Char('='))
        return false;

    const QChar prev = index > 0 ? text.at(index - 1) : QChar();
    const QChar next = index + 1 < end ? text.at(index + 1) : QChar();
    if (!prev.isNull() && isOperatorNeighbor(prev))
        return false;
    if (!next.isNull()
        && (next == QLatin1Char('=') || next == QLatin1Char('>')
            || next == QLatin1Char('?'))) {
        return false;
    }
    return true;
}

bool isNonBlockingAssignmentOperator(const QString& text, int index, int end)
{
    if (index + 1 >= end)
        return false;
    if (text.at(index) != QLatin1Char('<')
        || text.at(index + 1) != QLatin1Char('=')) {
        return false;
    }
    const QChar prev = index > 0 ? text.at(index - 1) : QChar();
    const QChar next = index + 2 < end ? text.at(index + 2) : QChar();
    if (prev == QLatin1Char('<') || next == QLatin1Char('='))
        return false;
    return true;
}

AssignmentCandidate findTopLevelAssignment(const QString& text,
                                           int start,
                                           int end)
{
    AssignmentCandidate candidate;
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    ScanState state = ScanState::Normal;

    for (int i = start; i < end; ++i) {
        const QChar ch = text.at(i);
        const QChar next = i + 1 < end ? text.at(i + 1) : QChar();

        if (state == ScanState::LineComment) {
            if (ch == QLatin1Char('\n'))
                state = ScanState::Normal;
            continue;
        }
        if (state == ScanState::BlockComment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                state = ScanState::Normal;
                ++i;
            }
            continue;
        }
        if (state == ScanState::String) {
            if (ch == QLatin1Char('\\') && i + 1 < end) {
                ++i;
                continue;
            }
            if (ch == QLatin1Char('"'))
                state = ScanState::Normal;
            continue;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
            state = ScanState::LineComment;
            ++i;
            continue;
        }
        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            state = ScanState::BlockComment;
            ++i;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            state = ScanState::String;
            continue;
        }

        if (ch == QLatin1Char('(')) {
            ++parenDepth;
            continue;
        }
        if (ch == QLatin1Char(')')) {
            --parenDepth;
            continue;
        }
        if (ch == QLatin1Char('[')) {
            ++bracketDepth;
            continue;
        }
        if (ch == QLatin1Char(']')) {
            --bracketDepth;
            continue;
        }
        if (ch == QLatin1Char('{')) {
            ++braceDepth;
            continue;
        }
        if (ch == QLatin1Char('}')) {
            --braceDepth;
            continue;
        }

        if (parenDepth != 0 || bracketDepth != 0 || braceDepth != 0)
            continue;

        bool matched = false;
        bool nonBlocking = false;
        int operatorEnd = -1;
        if (isNonBlockingAssignmentOperator(text, i, end)) {
            matched = true;
            nonBlocking = true;
            operatorEnd = i + 2;
        } else if (isBlockingAssignmentOperator(text, i, end)) {
            matched = true;
            operatorEnd = i + 1;
        }

        if (!matched)
            continue;

        if (candidate.valid) {
            candidate.ambiguous = true;
            return candidate;
        }

        candidate.valid = true;
        candidate.operatorStart = i;
        candidate.operatorEnd = operatorEnd;
        candidate.nonBlocking = nonBlocking;
        i = operatorEnd - 1;
    }

    return candidate;
}

const QSet<QString>& unsupportedLeadingKeywords()
{
    static const QSet<QString> keywords = {
        QStringLiteral("always"),
        QStringLiteral("always_comb"),
        QStringLiteral("always_ff"),
        QStringLiteral("always_latch"),
        QStringLiteral("assert"),
        QStringLiteral("assume"),
        QStringLiteral("begin"),
        QStringLiteral("case"),
        QStringLiteral("class"),
        QStringLiteral("cover"),
        QStringLiteral("end"),
        QStringLiteral("endcase"),
        QStringLiteral("endclass"),
        QStringLiteral("endfunction"),
        QStringLiteral("endmodule"),
        QStringLiteral("endpackage"),
        QStringLiteral("endtask"),
        QStringLiteral("for"),
        QStringLiteral("foreach"),
        QStringLiteral("forever"),
        QStringLiteral("force"),
        QStringLiteral("function"),
        QStringLiteral("if"),
        QStringLiteral("initial"),
        QStringLiteral("module"),
        QStringLiteral("package"),
        QStringLiteral("repeat"),
        QStringLiteral("return"),
        QStringLiteral("task"),
        QStringLiteral("while")
    };
    return keywords;
}

const QSet<QString>& declarationLeadingKeywords()
{
    static const QSet<QString> keywords = {
        QStringLiteral("bit"),
        QStringLiteral("byte"),
        QStringLiteral("genvar"),
        QStringLiteral("inout"),
        QStringLiteral("input"),
        QStringLiteral("int"),
        QStringLiteral("integer"),
        QStringLiteral("localparam"),
        QStringLiteral("logic"),
        QStringLiteral("longint"),
        QStringLiteral("output"),
        QStringLiteral("parameter"),
        QStringLiteral("reg"),
        QStringLiteral("shortint"),
        QStringLiteral("signed"),
        QStringLiteral("time"),
        QStringLiteral("tri"),
        QStringLiteral("typedef"),
        QStringLiteral("var"),
        QStringLiteral("wire")
    };
    return keywords;
}

ClassifiedAssignment classifyAssignment(const QString& text,
                                        const StatementSpan& span,
                                        const AssignmentCandidate& candidate)
{
    ClassifiedAssignment result;
    const QString firstToken =
        firstStatementToken(text, span.start, span.semicolon).toLower();

    if (firstToken.isEmpty()) {
        result.failureReason =
            QStringLiteral("Selection contains unsupported statement");
        return result;
    }
    if (firstToken.startsWith(QLatin1Char('`'))) {
        result.failureReason =
            QStringLiteral("Selection contains macro statement");
        return result;
    }
    if (firstToken == QStringLiteral("assign")) {
        if (candidate.nonBlocking) {
            result.failureReason =
                QStringLiteral("Continuous assign must use '='");
            return result;
        }
        result.supported = true;
        result.kind = RtlAssignmentStatementKind::ContinuousAssign;
        return result;
    }
    if (unsupportedLeadingKeywords().contains(firstToken)
        || declarationLeadingKeywords().contains(firstToken)) {
        result.failureReason =
            QStringLiteral("Selection contains unsupported statement");
        return result;
    }

    result.supported = true;
    result.kind = candidate.nonBlocking
        ? RtlAssignmentStatementKind::NonBlocking
        : RtlAssignmentStatementKind::Blocking;
    return result;
}

void lineColumnForOffset(const QString& text, int offset, int* line, int* column)
{
    int currentLine = 1;
    int currentColumn = 1;
    const int limit = qBound(0, offset, text.size());
    for (int i = 0; i < limit; ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            ++currentLine;
            currentColumn = 1;
        } else {
            ++currentColumn;
        }
    }
    if (line)
        *line = currentLine;
    if (column)
        *column = currentColumn;
}

CodeTemplateSlot makeRhsSlot(int index, int start)
{
    CodeTemplateSlot slot;
    slot.name = QStringLiteral("rhs%1").arg(index + 1);
    slot.start = start;
    slot.length = 0;
    return slot;
}
}

RtlBatchEditService* RtlBatchEditService::getInstance()
{
    if (!instance)
        instance = std::make_unique<RtlBatchEditService>();
    return instance.get();
}

RtlClearAssignmentRhsReport RtlBatchEditService::planClearAssignmentRhs(
    const RtlClearAssignmentRhsQuery& query) const
{
    RtlClearAssignmentRhsReport report;
    report.replacementText = query.selectedText;

    if (!hasSignificantCode(query.selectedText, 0, query.selectedText.size())) {
        report.status = RtlClearAssignmentRhsStatus::EmptySelection;
        report.failureReason = QStringLiteral("Empty selection");
        return report;
    }

    QList<StatementSpan> statements;
    QString statementFailure;
    if (!collectTopLevelStatements(query.selectedText,
                                   &statements,
                                   &statementFailure)) {
        report.status = RtlClearAssignmentRhsStatus::UnsupportedSelection;
        report.failureReason = statementFailure.isEmpty()
            ? QStringLiteral("Selection contains unsupported statement")
            : statementFailure;
        return report;
    }

    bool sawStatement = false;
    for (const StatementSpan& span : statements) {
        if (!hasSignificantCode(query.selectedText, span.start, span.semicolon))
            continue;

        sawStatement = true;
        const AssignmentCandidate candidate =
            findTopLevelAssignment(query.selectedText, span.start, span.semicolon);
        if (candidate.ambiguous) {
            report.status = RtlClearAssignmentRhsStatus::AmbiguousAssignment;
            report.failureReason =
                QStringLiteral("Selection contains ambiguous assignment RHS");
            report.edits.clear();
            return report;
        }
        if (!candidate.valid) {
            report.status = sawStatement
                ? RtlClearAssignmentRhsStatus::UnsupportedSelection
                : RtlClearAssignmentRhsStatus::NoAssignments;
            report.failureReason =
                QStringLiteral("Selection contains unsupported statement");
            report.edits.clear();
            return report;
        }

        const ClassifiedAssignment classified =
            classifyAssignment(query.selectedText, span, candidate);
        if (!classified.supported) {
            report.status = RtlClearAssignmentRhsStatus::UnsupportedSelection;
            report.failureReason = classified.failureReason;
            report.edits.clear();
            return report;
        }

        RtlClearAssignmentRhsEdit edit;
        edit.kind = classified.kind;
        edit.selectionRhsStart = candidate.operatorEnd;
        edit.selectionRhsEnd = span.semicolon;
        edit.documentRhsStart =
            query.selectionStartPosition + edit.selectionRhsStart;
        edit.documentRhsEnd = query.selectionStartPosition + edit.selectionRhsEnd;
        edit.removedText =
            query.selectedText.mid(edit.selectionRhsStart,
                                   edit.selectionRhsEnd - edit.selectionRhsStart);
        lineColumnForOffset(query.selectedText,
                            edit.selectionRhsStart,
                            &edit.line,
                            &edit.column);
        report.edits.append(edit);
    }

    if (report.edits.isEmpty()) {
        report.status = RtlClearAssignmentRhsStatus::NoAssignments;
        report.failureReason = QStringLiteral("No assignment RHS found");
        return report;
    }

    int offsetAdjustment = 0;
    for (int i = 0; i < report.edits.size(); ++i) {
        RtlClearAssignmentRhsEdit& edit = report.edits[i];
        const int replacementStart = edit.selectionRhsStart + offsetAdjustment;
        const int removedLength = edit.selectionRhsEnd - edit.selectionRhsStart;
        report.replacementText.replace(replacementStart,
                                       removedLength,
                                       QStringLiteral(" "));
        edit.replacementSlotStart = replacementStart + 1;
        report.templateSlots.append(makeRhsSlot(i, edit.replacementSlotStart));
        offsetAdjustment += 1 - removedLength;
    }

    report.status = RtlClearAssignmentRhsStatus::Ready;
    report.failureReason.clear();
    return report;
}
