#include "structuredwhitespaceformatter.h"

#include "tsdocument.h"

#include <QList>
#include <QSet>
#include <QStringView>
#include <QVector>
#include <algorithm>
#include <cstring>

namespace StructuredWhitespaceFormatter {
namespace {

struct LeafToken {
    TSNode node{};
    int start = -1;
    int end = -1;
    QString text;
};

struct WhitespaceEdit {
    int start = -1;
    int end = -1;
    QString replacement;
};

struct Part {
    int start = -1;
    int end = -1;
    int width = 0;
    QList<LeafToken> leaves;

    bool valid() const
    {
        return start >= 0 && end >= start && width >= 0;
    }
};

struct Trailer {
    LeafToken comma;
    LeafToken comment;
    int end = -1;
    bool valid = false;
};

bool typeIs(TSNode node, const char* type)
{
    return !ts_node_is_null(node)
        && std::strcmp(ts_node_type(node), type) == 0;
}

int nodeStart(TSNode node)
{
    return static_cast<int>(ts_node_start_byte(node) / 2u);
}

int nodeEnd(TSNode node);
bool isCommentNode(TSNode node);
bool isStringNode(TSNode node)
{
    return typeIs(node, "string_literal")
        || typeIs(node, "quoted_string")
        || typeIs(node, "triple_quoted_string");
}

struct ProtectedTextRange {
    int start = -1;
    int end = -1;
};

void collectTabProtectedRanges(TSNode node,
                               QList<ProtectedTextRange>* ranges)
{
    if (!ranges || ts_node_is_null(node))
        return;
    if (isCommentNode(node) || isStringNode(node)) {
        const int start = nodeStart(node);
        const int end = nodeEnd(node);
        if (start >= 0 && end > start)
            ranges->append({start, end});
        return;
    }
    const uint32_t count = ts_node_child_count(node);
    if (count == 0) {
        const int start = nodeStart(node);
        const int end = nodeEnd(node);
        if (start >= 0 && end > start)
            ranges->append({start, end});
        return;
    }
    for (uint32_t index = 0; index < count; ++index) {
        collectTabProtectedRanges(
            ts_node_child(node, index), ranges);
    }
}

const char* designUnitClosingKeyword(TSNode node)
{
    if (typeIs(node, "module_declaration"))
        return "endmodule";
    if (typeIs(node, "interface_declaration"))
        return "endinterface";
    if (typeIs(node, "package_declaration"))
        return "endpackage";
    if (typeIs(node, "program_declaration"))
        return "endprogram";
    if (typeIs(node, "checker_declaration"))
        return "endchecker";
    if (typeIs(node, "udp_declaration"))
        return "endprimitive";
    if (typeIs(node, "config_declaration"))
        return "endconfig";
    return nullptr;
}

int nodeEnd(TSNode node)
{
    return static_cast<int>(ts_node_end_byte(node) / 2u);
}

int nodeStartLine(TSNode node)
{
    return static_cast<int>(ts_node_start_point(node).row);
}

int nodeEndLine(TSNode node)
{
    return static_cast<int>(ts_node_end_point(node).row);
}

bool isCommentNode(TSNode node)
{
    return typeIs(node, "one_line_comment")
        || typeIs(node, "block_comment");
}

QList<TSNode> directNamedChildren(TSNode node, const char* type = nullptr)
{
    QList<TSNode> result;
    const uint32_t count = ts_node_named_child_count(node);
    result.reserve(static_cast<int>(count));
    for (uint32_t index = 0; index < count; ++index) {
        const TSNode child = ts_node_named_child(node, index);
        if (!type || typeIs(child, type))
            result.append(child);
    }
    return result;
}

TSNode firstDirectNamedChild(TSNode node, const char* type)
{
    const QList<TSNode> children = directNamedChildren(node, type);
    return children.isEmpty() ? TSNode{} : children.first();
}

void collectDescendants(TSNode node,
                        const char* type,
                        QList<TSNode>* result,
                        bool stopAtMatch = false)
{
    if (!result || ts_node_is_null(node))
        return;
    if (typeIs(node, type)) {
        result->append(node);
        if (stopAtMatch)
            return;
    }
    const uint32_t count = ts_node_named_child_count(node);
    for (uint32_t index = 0; index < count; ++index) {
        collectDescendants(ts_node_named_child(node, index),
                           type,
                           result,
                           stopAtMatch);
    }
}

bool isStatementWrapperNode(TSNode node)
{
    return typeIs(node, "statement_or_null")
        || typeIs(node, "statement")
        || typeIs(node, "statement_item")
        || typeIs(node, "function_statement")
        || typeIs(node, "function_statement_or_null");
}

bool isParseFailureNode(TSNode node)
{
    return typeIs(node, "ERROR") || ts_node_is_missing(node);
}

bool isCaseItemNode(TSNode node)
{
    return typeIs(node, "case_item")
        || typeIs(node, "case_inside_item")
        || typeIs(node, "case_pattern_item");
}

bool isDesignUnitNode(TSNode node)
{
    return typeIs(node, "module_declaration")
        || typeIs(node, "interface_declaration")
        || typeIs(node, "package_declaration")
        || typeIs(node, "program_declaration")
        || typeIs(node, "udp_declaration")
        || typeIs(node, "config_declaration");
}

bool isTransparentWrapperNode(TSNode node)
{
    if (isStatementWrapperNode(node))
        return true;
    const QString type = QString::fromLatin1(ts_node_type(node));
    return type.endsWith(QStringLiteral("_item"))
        || type == QStringLiteral("class_method")
        || type == QStringLiteral("module_item")
        || type == QStringLiteral("interface_item")
        || type == QStringLiteral("program_item")
        || type == QStringLiteral("package_item")
        || type == QStringLiteral("checker_item");
}

TSNode firstDescendant(TSNode node, const char* type)
{
    QList<TSNode> result;
    collectDescendants(node, type, &result, true);
    return result.isEmpty() ? TSNode{} : result.first();
}

void collectLeaves(TSNode node,
                   const QString& source,
                   QList<LeafToken>* result)
{
    if (!result || ts_node_is_null(node))
        return;
    const uint32_t count = ts_node_child_count(node);
    if (count == 0) {
        const int start = nodeStart(node);
        const int end = nodeEnd(node);
        if (end > start) {
            LeafToken token;
            token.node = node;
            token.start = start;
            token.end = end;
            token.text = source.mid(start, end - start);
            result->append(token);
        }
        return;
    }
    for (uint32_t index = 0; index < count; ++index)
        collectLeaves(ts_node_child(node, index), source, result);
}

QList<LeafToken> leavesOf(TSNode node, const QString& source)
{
    QList<LeafToken> result;
    collectLeaves(node, source, &result);
    std::sort(result.begin(), result.end(), [](const LeafToken& left,
                                                const LeafToken& right) {
        if (left.start != right.start)
            return left.start < right.start;
        return left.end < right.end;
    });
    return result;
}

QList<LeafToken> leavesInRange(TSNode root,
                               const QString& source,
                               int start,
                               int end)
{
    QList<LeafToken> all = leavesOf(root, source);
    QList<LeafToken> result;
    for (const LeafToken& leaf : all) {
        if (leaf.start >= start && leaf.end <= end)
            result.append(leaf);
    }
    return result;
}

bool allWhitespace(QStringView text)
{
    for (const QChar ch : text) {
        if (!ch.isSpace())
            return false;
    }
    return true;
}

bool tightBefore(const QString& token)
{
    return token == QStringLiteral(")")
        || token == QStringLiteral("]")
        || token == QStringLiteral("}")
        || token == QStringLiteral(",")
        || token == QStringLiteral(";")
        || token == QStringLiteral(":")
        || token == QStringLiteral(".")
        || token == QStringLiteral("::");
}

bool tightAfter(const QString& token)
{
    return token == QStringLiteral("(")
        || token == QStringLiteral("[")
        || token == QStringLiteral("{")
        || token == QStringLiteral(".")
        || token == QStringLiteral("::")
        || token == QStringLiteral("#")
        || token == QStringLiteral(":");
}

bool binaryOperator(const QString& token)
{
    static const QSet<QString> operators = {
        QStringLiteral("+"), QStringLiteral("-"), QStringLiteral("*"),
        QStringLiteral("/"), QStringLiteral("%"), QStringLiteral("=="),
        QStringLiteral("!="), QStringLiteral("==="), QStringLiteral("!=="),
        QStringLiteral("<"), QStringLiteral(">"), QStringLiteral("<="),
        QStringLiteral(">="), QStringLiteral("&&"), QStringLiteral("||"),
        QStringLiteral("&"), QStringLiteral("|"), QStringLiteral("^"),
        QStringLiteral("~^"), QStringLiteral("^~"), QStringLiteral("<<"),
        QStringLiteral(">>"), QStringLiteral("<<<"), QStringLiteral(">>>"),
        QStringLiteral("**"), QStringLiteral("="), QStringLiteral("+:"),
        QStringLiteral("-:")
    };
    return operators.contains(token);
}

bool unaryContext(const QString& previous)
{
    return previous.isEmpty()
        || previous == QStringLiteral("(")
        || previous == QStringLiteral("[")
        || previous == QStringLiteral("{")
        || previous == QStringLiteral(",")
        || previous == QStringLiteral(":")
        || binaryOperator(previous);
}

QString canonicalGap(const QList<LeafToken>& leaves, int index)
{
    const QString previous = leaves.at(index - 1).text;
    const QString current = leaves.at(index).text;
    const QString beforePrevious =
        index >= 2 ? leaves.at(index - 2).text : QString();

    if (previous.endsWith(QLatin1Char('\\'))
        || current.startsWith(QLatin1Char('\\'))) {
        return QStringLiteral(" ");
    }
    if (previous.startsWith(QLatin1Char('\''))
        || current.startsWith(QLatin1Char('\''))) {
        return QString();
    }
    if ((previous == QStringLiteral("]")
         && current == QStringLiteral("["))
        || tightAfter(previous)
        || tightBefore(current)) {
        return QString();
    }
    if ((previous == QStringLiteral("+")
         || previous == QStringLiteral("-"))
        && unaryContext(beforePrevious)) {
        return QString();
    }
    if ((current == QStringLiteral("+")
         || current == QStringLiteral("-"))
        && unaryContext(previous)) {
        return QString();
    }
    if (binaryOperator(previous) || binaryOperator(current))
        return QStringLiteral(" ");
    if (previous == QStringLiteral(","))
        return QStringLiteral(" ");
    return QStringLiteral(" ");
}

int canonicalWidth(const QList<LeafToken>& leaves)
{
    if (leaves.isEmpty())
        return 0;
    int width = 0;
    for (int index = 0; index < leaves.size(); ++index) {
        if (index > 0)
            width += canonicalGap(leaves, index).size();
        width += leaves.at(index).text.size();
    }
    return width;
}

int lineStart(const QString& source, int position)
{
    if (position <= 0)
        return 0;
    const int newline = source.lastIndexOf(
        QLatin1Char('\n'),
        std::min(position - 1, static_cast<int>(source.size()) - 1));
    return newline < 0 ? 0 : newline + 1;
}

int leadingWidthAt(const QString& source, int position)
{
    const int start = lineStart(source, position);
    int cursor = start;
    while (cursor < source.size()
           && cursor < position
           && source.at(cursor) != QLatin1Char('\n')
           && source.at(cursor).isSpace()) {
        ++cursor;
    }
    return cursor - start;
}

LeafToken firstLeafText(const QList<LeafToken>& leaves,
                        const QString& text,
                        int after = -1)
{
    for (const LeafToken& leaf : leaves) {
        if (leaf.start >= after && leaf.text == text)
            return leaf;
    }
    return {};
}

LeafToken lastLeafText(const QList<LeafToken>& leaves,
                       const QString& text)
{
    for (auto iterator = leaves.crbegin(); iterator != leaves.crend();
         ++iterator) {
        if (iterator->text == text)
            return *iterator;
    }
    return {};
}

class TreeWhitespaceFormatter
{
public:
    TreeWhitespaceFormatter(const QString& source, int indentWidth)
        : m_source(source),
          m_indentWidth(std::max(1, indentWidth))
    {
        m_document.setText(source);
        m_lineStarts.append(0);
        for (int position = 0; position < source.size(); ++position) {
            if (source.at(position) == QLatin1Char('\n'))
                m_lineStarts.append(position + 1);
        }
    }

    QString run()
    {
        QList<TSNode> modules;
        collectDescendants(m_document.rootNode(),
                           "module_declaration",
                           &modules,
                           true);
        for (const TSNode module : modules)
            formatModule(module);

        QList<TSNode> instances;
        collectDescendants(m_document.rootNode(),
                           "module_instantiation",
                           &instances,
                           true);
        for (const TSNode instance : instances)
            formatInstantiation(instance);

        collectConservativeErrors(m_document.rootNode());

        return applyEdits();
    }

    QString runStructuralIndentation(bool indentConditionalBranches,
                                    bool indentCaseItemBodies,
                                    bool alignCaseItems,
                                    bool preservePreprocessorIndent)
    {
        m_indentConditionalBranches = indentConditionalBranches;
        m_indentCaseItemBodies = indentCaseItemBodies;
        m_alignCaseItems = alignCaseItems;
        m_preservePreprocessorIndent = preservePreprocessorIndent;
        collectConservativeErrors(m_document.rootNode());
        for (const TSNode child
             : directNamedChildren(m_document.rootNode())) {
            formatOwnedNode(child, 0);
        }
        return applyEdits();
    }

    QList<LineRange> conservativeRanges() const
    {
        return m_conservativeRanges;
    }

    QList<LineRange> syntaxErrorRanges()
    {
        m_conservativeRanges.clear();
        collectConservativeErrors(m_document.rootNode());
        return m_conservativeRanges;
    }

private:
    struct ParameterItem {
        TSNode node{};
        Part keyword;
        Part type;
        Part name;
        Part unpacked;
        Part value;
        LeafToken equal;
        Trailer trailer;
    };

    struct PortItem {
        TSNode node{};
        Part direction;
        Part type;
        Part packed;
        Part name;
        Part unpacked;
        Trailer trailer;
    };

    struct AssociationItem {
        TSNode node{};
        Part formal;
        Part actual;
        LeafToken openParen;
        LeafToken closeParen;
        Trailer trailer;
        bool hasActual = false;
        bool multiline = false;
    };

    struct CaseItemLayout {
        LeafToken labelFirst;
        LeafToken labelLast;
        LeafToken colon;
        LeafToken branchFirst;
        LeafToken comment;
        int indent = 0;
        int labelWidth = 0;
        int codeEnd = -1;
        bool branchOnLabelLine = false;

        bool valid() const
        {
            return labelFirst.start >= 0
                && labelLast.end > labelFirst.start
                && colon.start >= labelLast.end;
        }
    };

    QString applyEdits()
    {
        if (!m_valid)
            return m_source;
        std::sort(m_edits.begin(),
                  m_edits.end(),
                  [](const WhitespaceEdit& left,
                     const WhitespaceEdit& right) {
            if (left.start != right.start)
                return left.start > right.start;
            return left.end > right.end;
        });

        QString result = m_source;
        int previousStart = m_source.size() + 1;
        for (const WhitespaceEdit& edit : m_edits) {
            if (edit.end > previousStart) {
                m_valid = false;
                return m_source;
            }
            result.replace(edit.start,
                           edit.end - edit.start,
                           edit.replacement);
            previousStart = edit.start;
        }
        return result;
    }

    bool setLineLeading(const LeafToken& leaf, int indent)
    {
        if (leaf.start < 0 || lineIsConservative(nodeStartLine(leaf.node)))
            return true;
        const int startOfLine =
            lineStart(m_source, leaf.start);
        const QStringView gap =
            QStringView(m_source).mid(
                startOfLine,
                leaf.start - startOfLine);
        if (!allWhitespace(gap))
            return true;
        if (m_preservePreprocessorIndent) {
            int first = startOfLine;
            while (first < m_source.size()
                   && first < leaf.start
                   && m_source.at(first).isSpace()
                   && m_source.at(first) != QLatin1Char('\n')) {
                ++first;
            }
            if (first < m_source.size()
                && m_source.at(first) == QLatin1Char('`')) {
                return true;
            }
        }
        return addEdit(
            startOfLine,
            leaf.start,
            QString(std::max(0, indent),
                    QLatin1Char(' ')));
    }

    LeafToken firstLeafOf(TSNode node) const
    {
        const QList<LeafToken> leaves = leavesOf(node, m_source);
        return leaves.isEmpty() ? LeafToken{} : leaves.first();
    }

    LeafToken leafForNode(TSNode node) const
    {
        if (ts_node_is_null(node))
            return {};
        const int start = nodeStart(node);
        const int end = nodeEnd(node);
        if (start < 0 || end <= start)
            return {};
        return {node, start, end, m_source.mid(start, end - start)};
    }

    TSNode semanticChild(TSNode wrapper) const
    {
        const uint32_t count = ts_node_named_child_count(wrapper);
        for (uint32_t index = 0; index < count; ++index) {
            const TSNode child = ts_node_named_child(wrapper, index);
            if (isCommentNode(child)
                || typeIs(child, "attribute_instance")
                || isParseFailureNode(child)) {
                continue;
            }
            return child;
        }
        return {};
    }

    TSNode unwrapTransparentNode(TSNode node) const
    {
        TSNode current = node;
        while (!ts_node_is_null(current)
               && isTransparentWrapperNode(current)) {
            const TSNode child = semanticChild(current);
            if (ts_node_is_null(child)
                || (nodeStart(child) == nodeStart(current)
                    && nodeEnd(child) == nodeEnd(current)
                    && std::strcmp(ts_node_type(child),
                                   ts_node_type(current)) == 0)) {
                break;
            }
            current = child;
        }
        return current;
    }

    void setOwnedLines(TSNode node,
                       int indent,
                       int rangeStart = -1,
                       int rangeEnd = -1)
    {
        if (ts_node_is_null(node))
            return;
        const int start =
            rangeStart >= 0 ? rangeStart : nodeStart(node);
        const int end =
            rangeEnd >= 0 ? rangeEnd : nodeEnd(node);
        if (start < 0 || end <= start)
            return;

        QSet<int> formattedLines;
        for (const LeafToken& leaf : leavesOf(node, m_source)) {
            if (leaf.start < start || leaf.start >= end)
                continue;
            const int line = nodeStartLine(leaf.node);
            if (formattedLines.contains(line))
                continue;
            formattedLines.insert(line);
            setLineLeading(leaf, indent);
        }
    }

    void formatBodyNode(TSNode body,
                        int ownerIndent,
                        int ownerLine)
    {
        const TSNode actual = unwrapTransparentNode(body);
        const LeafToken first = firstLeafOf(actual);
        if (first.start < 0)
            return;
        const int bodyIndent =
            nodeStartLine(first.node) == ownerLine
                ? ownerIndent
                : ownerIndent + m_indentWidth;
        formatOwnedNode(actual, bodyIndent);
    }

    int ownerLineBeforeChild(TSNode parent,
                             TSNode target,
                             int fallbackLine) const
    {
        int ownerLine = fallbackLine;
        const uint32_t childCount = ts_node_child_count(parent);
        for (uint32_t index = 0; index < childCount; ++index) {
            const TSNode child = ts_node_child(parent, index);
            if (ts_node_eq(child, target))
                break;
            if (isCommentNode(child)
                || typeIs(child, "conditional_compilation_directive")) {
                continue;
            }
            ownerLine = nodeEndLine(child);
        }
        return ownerLine;
    }

    bool isGenericKeywordContainer(TSNode node) const
    {
        return typeIs(node, "checker_declaration")
            || typeIs(node, "class_declaration")
            || typeIs(node, "interface_class_declaration")
            || typeIs(node, "clocking_declaration")
            || typeIs(node, "covergroup_declaration")
            || typeIs(node, "property_declaration")
            || typeIs(node, "sequence_declaration")
            || typeIs(node, "specify_block")
            || typeIs(node, "combinational_body")
            || typeIs(node, "sequential_body");
    }

    void formatOwnedNode(TSNode node, int indent)
    {
        if (ts_node_is_null(node) || isParseFailureNode(node))
            return;

        if (isTransparentWrapperNode(node)) {
            const TSNode child = semanticChild(node);
            if (!ts_node_is_null(child)) {
                formatOwnedNode(child, indent);
                return;
            }
        }
        if (isDesignUnitNode(node)) {
            formatDesignUnit(node);
        } else if (typeIs(node, "seq_block")) {
            formatSequentialBlock(node, indent);
        } else if (typeIs(node, "par_block")) {
            formatParallelBlock(node, indent);
        } else if (typeIs(node, "conditional_statement")) {
            formatConditionalStatement(node, indent);
        } else if (typeIs(node, "case_statement")) {
            formatCaseStatement(node, indent);
        } else if (typeIs(node, "loop_statement")) {
            formatLoopStatement(node, indent);
        } else if (typeIs(node, "always_construct")
                   || typeIs(node, "initial_construct")
                   || typeIs(node, "final_construct")
                   || typeIs(
                       node,
                       "procedural_timing_control_statement")) {
            formatProceduralConstruct(node, indent);
        } else if (typeIs(node, "function_declaration")
                   || typeIs(node, "task_declaration")
                   || typeIs(
                       node,
                       "class_constructor_declaration")) {
            formatSubroutine(node, indent);
        } else if (typeIs(node, "generate_region")) {
            formatGenerateRegion(node, indent);
        } else if (typeIs(node, "generate_block")) {
            formatGenerateBlock(node, indent);
        } else if (typeIs(node, "conditional_generate_construct")
                   || typeIs(node, "if_generate_construct")
                   || typeIs(node, "case_generate_construct")) {
            formatGenerateConditional(node, indent);
        } else if (typeIs(node, "loop_generate_construct")) {
            formatGenerateLoop(node, indent);
        } else if (isGenericKeywordContainer(node)) {
            formatKeywordContainer(node, indent);
        } else if (typeIs(node, "conditional_compilation_directive")) {
            for (const TSNode child : directNamedChildren(node)) {
                if (!isCommentNode(child)
                    && !isParseFailureNode(child)) {
                    formatOwnedNode(child, indent);
                }
            }
        } else {
            setOwnedLines(node, indent);
        }
    }

    void formatBranch(TSNode statementOrNull,
                      int ownerIndent,
                      int ownerLine)
    {
        if (!m_indentConditionalBranches)
            return;
        formatBodyNode(statementOrNull, ownerIndent, ownerLine);
    }

    void formatConditionalStatement(TSNode statement, int baseIndent)
    {
        const QList<LeafToken> leaves =
            leavesOf(statement, m_source);
        if (leaves.isEmpty())
            return;

        struct BranchLayout {
            TSNode node{};
            int ownerLine = -1;
        };
        QList<BranchLayout> branches;
        for (const TSNode child : directNamedChildren(statement)) {
            if (!typeIs(child, "statement_or_null"))
                continue;
            branches.append(
                {child,
                 ownerLineBeforeChild(
                     statement,
                     child,
                     nodeStartLine(leaves.first().node))});
        }
        const int headerEnd =
            branches.isEmpty() ? nodeEnd(statement)
                               : nodeStart(branches.first().node);
        setOwnedLines(statement,
                      baseIndent,
                      nodeStart(statement),
                      headerEnd);

        for (const BranchLayout& branch : branches) {
            formatBranch(
                branch.node, baseIndent, branch.ownerLine);
        }

        const uint32_t childCount = ts_node_child_count(statement);
        for (uint32_t index = 0; index < childCount; ++index) {
            const TSNode child = ts_node_child(statement, index);
            if (ts_node_is_named(child))
                continue;
            const LeafToken token = leafForNode(child);
            if (token.text == QStringLiteral("else"))
                setLineLeading(token, baseIndent);
        }
    }

    CaseItemLayout caseItemLayout(TSNode item,
                                  int desiredIndent) const
    {
        CaseItemLayout layout;
        const QList<LeafToken> leaves =
            leavesOf(item, m_source);
        if (leaves.isEmpty())
            return layout;

        const uint32_t childCount = ts_node_child_count(item);
        for (uint32_t index = 0; index < childCount; ++index) {
            const TSNode child = ts_node_child(item, index);
            if (ts_node_is_named(child))
                continue;
            const int start = nodeStart(child);
            const int end = nodeEnd(child);
            if (start >= 0 && end > start
                && m_source.mid(start, end - start)
                       == QStringLiteral(":")) {
                layout.colon = {child, start, end, QStringLiteral(":")};
                break;
            }
        }
        if (layout.colon.start < 0)
            return {};

        for (const LeafToken& leaf : leaves) {
            if (leaf.end <= layout.colon.start
                && !isCommentNode(leaf.node)) {
                if (layout.labelFirst.start < 0)
                    layout.labelFirst = leaf;
                layout.labelLast = leaf;
            }
        }
        if (layout.labelFirst.start < 0
            || nodeStartLine(layout.labelFirst.node)
                   != nodeStartLine(layout.colon.node)) {
            return {};
        }

        layout.indent = m_indentCaseItemBodies
            ? desiredIndent
            : leadingWidthAt(m_source, layout.labelFirst.start);
        layout.labelWidth =
            layout.labelLast.end - layout.labelFirst.start;
        layout.codeEnd = layout.colon.end;

        const TSNode branch =
            firstDirectNamedChild(item, "statement_or_null");
        const QList<LeafToken> branchLeaves =
            leavesOf(branch, m_source);
        if (!branchLeaves.isEmpty()) {
            layout.branchFirst = branchLeaves.first();
            layout.branchOnLabelLine =
                nodeStartLine(layout.branchFirst.node)
                == nodeStartLine(layout.labelFirst.node);
        }

        const int labelLine = nodeStartLine(layout.labelFirst.node);
        for (const LeafToken& leaf : leaves) {
            if (nodeStartLine(leaf.node) != labelLine)
                continue;
            if (isCommentNode(leaf.node)) {
                if (layout.comment.start < 0)
                    layout.comment = leaf;
                continue;
            }
            if (layout.comment.start < 0
                || leaf.end <= layout.comment.start) {
                layout.codeEnd =
                    std::max(layout.codeEnd, leaf.end);
            }
        }
        if (layout.comment.start < 0) {
            const QList<LeafToken> parentLeaves =
                leavesOf(ts_node_parent(item), m_source);
            for (const LeafToken& leaf : parentLeaves) {
                if (leaf.start < layout.colon.end
                    || nodeStartLine(leaf.node) != labelLine
                    || !isCommentNode(leaf.node)) {
                    continue;
                }
                layout.comment = leaf;
                break;
            }
        }
        if (!allWhitespace(
                QStringView(m_source).mid(
                    layout.labelLast.end,
                    layout.colon.start - layout.labelLast.end))) {
            return {};
        }
        if (layout.branchOnLabelLine
            && !allWhitespace(
                QStringView(m_source).mid(
                    layout.colon.end,
                    layout.branchFirst.start - layout.colon.end))) {
            return {};
        }
        return layout;
    }

    void alignCaseItemGroup(const QList<TSNode>& items,
                            int desiredIndent)
    {
        if (!m_alignCaseItems || items.isEmpty())
            return;

        QList<CaseItemLayout> layouts;
        for (const TSNode item : items) {
            const CaseItemLayout layout =
                caseItemLayout(item, desiredIndent);
            if (layout.valid())
                layouts.append(layout);
        }
        if (layouts.isEmpty())
            return;

        int maxLabelWidth = 0;
        for (const CaseItemLayout& layout : layouts) {
            maxLabelWidth =
                std::max(maxLabelWidth, layout.labelWidth);
        }

        int maxCodeEndColumn = 0;
        bool hasComment = false;
        for (const CaseItemLayout& layout : layouts) {
            setGap(layout.labelLast.end,
                   layout.colon.start,
                   maxLabelWidth - layout.labelWidth);
            if (layout.branchOnLabelLine) {
                setGap(layout.colon.end,
                       layout.branchFirst.start,
                       1);
            }

            int codeEndColumn =
                layout.indent + maxLabelWidth + 1;
            if (layout.branchOnLabelLine) {
                codeEndColumn +=
                    1 + layout.codeEnd - layout.branchFirst.start;
            }
            maxCodeEndColumn =
                std::max(maxCodeEndColumn, codeEndColumn);
            hasComment = hasComment || layout.comment.start >= 0;
        }

        if (!hasComment)
            return;
        const int commentColumn = maxCodeEndColumn + 2;
        for (const CaseItemLayout& layout : layouts) {
            if (layout.comment.start < 0)
                continue;
            int codeEndColumn =
                layout.indent + maxLabelWidth + 1;
            if (layout.branchOnLabelLine) {
                codeEndColumn +=
                    1 + layout.codeEnd - layout.branchFirst.start;
            }
            if (layout.comment.start >= layout.codeEnd
                && allWhitespace(
                    QStringView(m_source).mid(
                        layout.codeEnd,
                        layout.comment.start - layout.codeEnd))) {
                setGap(layout.codeEnd,
                       layout.comment.start,
                       commentColumn - codeEndColumn);
            }
        }
    }

    void formatCaseStatement(TSNode statement, int baseIndent)
    {
        const QList<LeafToken> leaves =
            leavesOf(statement, m_source);
        if (leaves.isEmpty())
            return;
        QList<TSNode> caseItems;
        for (const TSNode child : directNamedChildren(statement)) {
            if (isCaseItemNode(child))
                caseItems.append(child);
        }
        const int headerEnd =
            caseItems.isEmpty() ? nodeEnd(statement)
                                : nodeStart(caseItems.first());
        setOwnedLines(statement,
                      baseIndent,
                      nodeStart(statement),
                      headerEnd);

        QList<TSNode> alignmentGroup;
        auto flushAlignmentGroup = [&]() {
            alignCaseItemGroup(
                alignmentGroup, baseIndent + m_indentWidth);
            alignmentGroup.clear();
        };

        const uint32_t childCount = ts_node_child_count(statement);
        for (uint32_t index = 0; index < childCount; ++index) {
            const TSNode child = ts_node_child(statement, index);
            if (!ts_node_is_named(child)) {
                const int start = nodeStart(child);
                const int end = nodeEnd(child);
                if (!m_indentCaseItemBodies
                    || start < 0 || end <= start
                    || m_source.mid(start, end - start)
                           != QStringLiteral("endcase")) {
                    continue;
                }
                LeafToken endcaseToken;
                endcaseToken.node = child;
                endcaseToken.start = start;
                endcaseToken.end = end;
                endcaseToken.text = QStringLiteral("endcase");
                setLineLeading(endcaseToken, baseIndent);
                continue;
            }

            if (!isCaseItemNode(child)) {
                const bool trailingItemComment =
                    isCommentNode(child)
                    && !alignmentGroup.isEmpty()
                    && nodeStartLine(child)
                           == nodeStartLine(alignmentGroup.last());
                if (!alignmentGroup.isEmpty()
                    && (typeIs(
                            child,
                            "conditional_compilation_directive")
                        || (isCommentNode(child)
                            && !trailingItemComment))) {
                    flushAlignmentGroup();
                }
                continue;
            }
            if (ts_node_has_error(child)) {
                flushAlignmentGroup();
                continue;
            }
            if (!ts_node_is_null(
                    firstDirectNamedChild(
                        child,
                        "conditional_compilation_directive"))) {
                flushAlignmentGroup();
                continue;
            }

            alignmentGroup.append(child);
            const QList<LeafToken> itemLeaves =
                leavesOf(child, m_source);
            if (itemLeaves.isEmpty())
                continue;
            const TSNode branch =
                firstDirectNamedChild(
                    child, "statement_or_null");
            const QList<LeafToken> branchLeaves =
                leavesOf(branch, m_source);
            const int branchOwnerLine =
                ts_node_is_null(branch)
                ? nodeStartLine(itemLeaves.first().node)
                : ownerLineBeforeChild(
                      child,
                      branch,
                      nodeStartLine(itemLeaves.first().node));
            const bool branchOnItemLine =
                !branchLeaves.isEmpty()
                && nodeStartLine(branchLeaves.first().node)
                       == branchOwnerLine;
            if (m_indentCaseItemBodies) {
                setLineLeading(
                    itemLeaves.first(),
                    baseIndent + m_indentWidth);

                if (!ts_node_is_null(branch)) {
                    formatBodyNode(
                        branch,
                        baseIndent + m_indentWidth,
                        branchOwnerLine);
                }
            }
            if (!branchOnItemLine)
                flushAlignmentGroup();
        }
        flushAlignmentGroup();
    }

    LeafToken lastLeafWithText(const QList<LeafToken>& leaves,
                               const QStringList& texts) const
    {
        for (auto iterator = leaves.crbegin();
             iterator != leaves.crend();
             ++iterator) {
            if (texts.contains(iterator->text))
                return *iterator;
        }
        return {};
    }

    LeafToken firstSemicolonBefore(const QList<LeafToken>& leaves,
                                   int limit) const
    {
        for (const LeafToken& leaf : leaves) {
            if (leaf.start >= limit)
                break;
            if (leaf.text == QStringLiteral(";"))
                return leaf;
        }
        return {};
    }

    void formatChildrenBetween(TSNode container,
                               int bodyStart,
                               int bodyEnd,
                               int indent)
    {
        for (const TSNode child : directNamedChildren(container)) {
            if (nodeEnd(child) <= bodyStart
                || nodeStart(child) >= bodyEnd
                || isParseFailureNode(child)) {
                continue;
            }
            formatOwnedNode(child, indent);
        }
    }

    void formatSequentialBlock(TSNode block, int indent)
    {
        const QList<LeafToken> leaves = leavesOf(block, m_source);
        const LeafToken opening =
            firstLeafText(leaves, QStringLiteral("begin"));
        const LeafToken closing =
            lastLeafText(leaves, QStringLiteral("end"));
        if (opening.start < 0 || closing.start < opening.end)
            return;

        setLineLeading(opening, indent);
        formatChildrenBetween(block,
                              opening.end,
                              closing.start,
                              indent + m_indentWidth);
        setLineLeading(closing, indent);
    }

    void formatParallelBlock(TSNode block, int indent)
    {
        const QList<LeafToken> leaves = leavesOf(block, m_source);
        const LeafToken opening =
            firstLeafText(leaves, QStringLiteral("fork"));
        const LeafToken closing =
            lastLeafWithText(
                leaves,
                {QStringLiteral("join"),
                 QStringLiteral("join_any"),
                 QStringLiteral("join_none")});
        if (opening.start < 0 || closing.start < opening.end)
            return;

        setLineLeading(opening, indent);
        formatChildrenBetween(block,
                              opening.end,
                              closing.start,
                              indent + m_indentWidth);
        setLineLeading(closing, indent);
    }

    void formatDesignUnit(TSNode unit)
    {
        const char* closingKeyword =
            designUnitClosingKeyword(unit);
        if (!closingKeyword)
            return;

        const QList<LeafToken> leaves =
            leavesOf(unit, m_source);
        if (leaves.isEmpty())
            return;

        LeafToken headerSemicolon;
        LeafToken closing;
        for (const LeafToken& leaf : leaves) {
            if (headerSemicolon.start < 0
                && leaf.text == QStringLiteral(";")) {
                headerSemicolon = leaf;
            }
            if (leaf.text == QString::fromLatin1(closingKeyword))
                closing = leaf;
        }
        if (headerSemicolon.start < 0 || closing.start < 0
            || headerSemicolon.end > closing.start) {
            return;
        }

        setOwnedLines(unit,
                      0,
                      nodeStart(unit),
                      headerSemicolon.end);
        for (const TSNode child : directNamedChildren(unit)) {
            if (nodeEnd(child) <= headerSemicolon.end
                || nodeStart(child) >= closing.start) {
                continue;
            }
            formatOwnedNode(child, 0);
        }
        setLineLeading(closing, 0);
    }

    void formatLoopStatement(TSNode statement, int indent)
    {
        TSNode body;
        for (const TSNode child : directNamedChildren(statement)) {
            if (typeIs(child, "statement_or_null")
                || typeIs(child, "statement")) {
                body = child;
            }
        }
        if (ts_node_is_null(body)) {
            setOwnedLines(statement, indent);
            return;
        }
        setOwnedLines(statement,
                      indent,
                      nodeStart(statement),
                      nodeStart(body));
        formatBodyNode(body,
                       indent,
                       nodeStartLine(statement));
    }

    void formatProceduralConstruct(TSNode construct, int indent)
    {
        TSNode body;
        for (const TSNode child : directNamedChildren(construct)) {
            if (typeIs(child, "statement")
                || typeIs(child, "statement_or_null")
                || typeIs(child, "function_statement")
                || typeIs(child, "function_statement_or_null")) {
                body = child;
            }
        }
        if (ts_node_is_null(body)) {
            setOwnedLines(construct, indent);
            return;
        }

        setOwnedLines(construct,
                      indent,
                      nodeStart(construct),
                      nodeStart(body));
        formatBodyNode(body,
                       indent,
                       nodeStartLine(construct));
    }

    void formatSubroutine(TSNode declaration, int indent)
    {
        const QList<LeafToken> leaves =
            leavesOf(declaration, m_source);
        const QString closingText =
            typeIs(declaration, "function_declaration")
                || typeIs(
                    declaration,
                    "class_constructor_declaration")
                ? QStringLiteral("endfunction")
                : QStringLiteral("endtask");
        const LeafToken closing =
            lastLeafText(leaves, closingText);
        if (leaves.isEmpty() || closing.start < 0)
            return;
        const LeafToken headerSemicolon =
            firstSemicolonBefore(leaves, closing.start);
        if (headerSemicolon.start < 0)
            return;

        setOwnedLines(declaration,
                      indent,
                      nodeStart(declaration),
                      headerSemicolon.end);
        TSNode bodyDeclaration =
            firstDirectNamedChild(
                declaration,
                typeIs(declaration, "function_declaration")
                    ? "function_body_declaration"
                    : "task_body_declaration");
        if (ts_node_is_null(bodyDeclaration))
            bodyDeclaration = declaration;
        formatChildrenBetween(bodyDeclaration,
                              headerSemicolon.end,
                              closing.start,
                              indent + m_indentWidth);
        setLineLeading(closing, indent);
    }

    void formatGenerateRegion(TSNode region, int indent)
    {
        const QList<LeafToken> leaves = leavesOf(region, m_source);
        const LeafToken opening =
            firstLeafText(leaves, QStringLiteral("generate"));
        const LeafToken closing =
            lastLeafText(leaves, QStringLiteral("endgenerate"));
        if (opening.start < 0 || closing.start < opening.end)
            return;
        setLineLeading(opening, indent);
        formatChildrenBetween(region,
                              opening.end,
                              closing.start,
                              indent + m_indentWidth);
        setLineLeading(closing, indent);
    }

    void formatGenerateBlock(TSNode block, int indent)
    {
        const QList<LeafToken> leaves = leavesOf(block, m_source);
        const LeafToken opening =
            firstLeafText(leaves, QStringLiteral("begin"));
        const LeafToken closing =
            lastLeafText(leaves, QStringLiteral("end"));
        if (opening.start < 0 || closing.start < opening.end)
            return;
        setLineLeading(opening, indent);

        for (const TSNode child : directNamedChildren(block)) {
            if (nodeEnd(child) <= opening.end
                || nodeStart(child) >= closing.start
                || typeIs(child, "simple_identifier")
                || typeIs(child, "escaped_identifier")
                || isParseFailureNode(child)) {
                continue;
            }
            formatOwnedNode(child, indent + m_indentWidth);
        }
        setLineLeading(closing, indent);
    }

    void formatGenerateConditional(TSNode construct, int indent)
    {
        TSNode actual = construct;
        if (typeIs(actual, "conditional_generate_construct")) {
            const TSNode child = semanticChild(actual);
            if (!ts_node_is_null(child))
                actual = child;
        }
        if (typeIs(actual, "case_generate_construct")) {
            formatGenerateCase(actual, indent);
            return;
        }

        QList<TSNode> branches;
        for (const TSNode child : directNamedChildren(actual)) {
            if (typeIs(child, "generate_block"))
                branches.append(child);
        }
        const int headerEnd =
            branches.isEmpty() ? nodeEnd(actual)
                               : nodeStart(branches.first());
        setOwnedLines(actual,
                      indent,
                      nodeStart(actual),
                      headerEnd);
        const int ownerLine = nodeStartLine(actual);
        for (const TSNode branch : branches)
            formatBodyNode(branch, indent, ownerLine);

        const uint32_t count = ts_node_child_count(actual);
        for (uint32_t index = 0; index < count; ++index) {
            const TSNode child = ts_node_child(actual, index);
            if (!ts_node_is_named(child)) {
                const LeafToken token = leafForNode(child);
                if (token.text == QStringLiteral("else"))
                    setLineLeading(token, indent);
            }
        }
    }

    void formatGenerateCase(TSNode construct, int indent)
    {
        QList<TSNode> items;
        for (const TSNode child : directNamedChildren(construct)) {
            if (typeIs(child, "case_generate_item"))
                items.append(child);
        }
        const int headerEnd =
            items.isEmpty() ? nodeEnd(construct)
                            : nodeStart(items.first());
        setOwnedLines(construct,
                      indent,
                      nodeStart(construct),
                      headerEnd);
        for (const TSNode item : items) {
            const LeafToken first = firstLeafOf(item);
            if (first.start < 0 || ts_node_has_error(item))
                continue;
            setLineLeading(first, indent + m_indentWidth);
            const TSNode body =
                firstDirectNamedChild(item, "generate_block");
            if (!ts_node_is_null(body)) {
                formatBodyNode(
                    body,
                    indent + m_indentWidth,
                    nodeStartLine(first.node));
            }
        }
        const QList<LeafToken> leaves =
            leavesOf(construct, m_source);
        const LeafToken closing =
            lastLeafText(leaves, QStringLiteral("endcase"));
        if (closing.start >= 0)
            setLineLeading(closing, indent);
    }

    void formatGenerateLoop(TSNode construct, int indent)
    {
        TSNode body =
            firstDirectNamedChild(construct, "generate_block");
        if (ts_node_is_null(body)) {
            setOwnedLines(construct, indent);
            return;
        }
        setOwnedLines(construct,
                      indent,
                      nodeStart(construct),
                      nodeStart(body));
        formatBodyNode(body,
                       indent,
                       nodeStartLine(construct));
    }

    const char* keywordContainerClosing(TSNode node) const
    {
        if (typeIs(node, "checker_declaration"))
            return "endchecker";
        if (typeIs(node, "class_declaration")
            || typeIs(node, "interface_class_declaration")) {
            return "endclass";
        }
        if (typeIs(node, "clocking_declaration"))
            return "endclocking";
        if (typeIs(node, "covergroup_declaration"))
            return "endgroup";
        if (typeIs(node, "property_declaration"))
            return "endproperty";
        if (typeIs(node, "sequence_declaration"))
            return "endsequence";
        if (typeIs(node, "specify_block"))
            return "endspecify";
        if (typeIs(node, "combinational_body")
            || typeIs(node, "sequential_body")) {
            return "endtable";
        }
        return nullptr;
    }

    void formatKeywordContainer(TSNode container, int indent)
    {
        const char* closingText =
            keywordContainerClosing(container);
        if (!closingText)
            return;
        const QList<LeafToken> leaves =
            leavesOf(container, m_source);
        const LeafToken closing =
            lastLeafText(
                leaves, QString::fromLatin1(closingText));
        if (leaves.isEmpty() || closing.start < 0)
            return;

        if (typeIs(container, "specify_block")
            || typeIs(container, "combinational_body")
            || typeIs(container, "sequential_body")) {
            const LeafToken opening =
                firstLeafText(
                    leaves,
                    typeIs(container, "specify_block")
                        ? QStringLiteral("specify")
                        : QStringLiteral("table"));
            if (opening.start < 0)
                return;
            setOwnedLines(container,
                          indent,
                          nodeStart(container),
                          opening.end);
            formatChildrenBetween(container,
                                  opening.end,
                                  closing.start,
                                  indent + m_indentWidth);
            setLineLeading(closing, indent);
            return;
        }

        const LeafToken headerSemicolon =
            firstSemicolonBefore(leaves, closing.start);
        if (headerSemicolon.start < 0) {
            setLineLeading(leaves.first(), indent);
            setLineLeading(closing, indent);
            return;
        }
        setOwnedLines(container,
                      indent,
                      nodeStart(container),
                      headerSemicolon.end);
        formatChildrenBetween(container,
                              headerSemicolon.end,
                              closing.start,
                              indent + m_indentWidth);
        setLineLeading(closing, indent);
    }

    bool addEdit(int start, int end, const QString& replacement)
    {
        if (start < 0 || end < start || end > m_source.size()) {
            m_valid = false;
            return false;
        }
        const int firstLine = sourceLineAt(start);
        const int lastPosition =
            end > start ? end - 1 : start;
        const int lastLine = sourceLineAt(lastPosition);
        for (int line = firstLine; line <= lastLine; ++line) {
            if (lineIsConservative(line))
                return true;
        }
        if (!allWhitespace(QStringView(m_source).mid(start, end - start))) {
            m_valid = false;
            return false;
        }
        if (m_source.mid(start, end - start) == replacement)
            return true;

        for (const WhitespaceEdit& existing : m_edits) {
            if (existing.start == start && existing.end == end) {
                if (existing.replacement != replacement)
                    m_valid = false;
                return existing.replacement == replacement;
            }
            if (start < existing.end && end > existing.start) {
                m_valid = false;
                return false;
            }
        }
        m_edits.append({start, end, replacement});
        return true;
    }

    bool lineIsConservative(int line) const
    {
        for (const LineRange& range : m_conservativeRanges) {
            if (range.valid()
                && line >= range.firstLine
                && line <= range.lastLine) {
                return true;
            }
        }
        return false;
    }

    int sourceLineAt(int position) const
    {
        const int bounded =
            qBound(0, position, static_cast<int>(m_source.size()));
        const auto iterator =
            std::upper_bound(m_lineStarts.cbegin(),
                             m_lineStarts.cend(),
                             bounded);
        return std::max(
            0,
            static_cast<int>(
                std::distance(m_lineStarts.cbegin(), iterator))
                - 1);
    }

    void markConservative(TSNode node)
    {
        if (ts_node_is_null(node))
            return;
        LineRange range;
        range.firstLine = nodeStartLine(node);
        range.lastLine = nodeEndLine(node);
        if (!range.valid())
            return;
        for (LineRange& existing : m_conservativeRanges) {
            if (range.firstLine > existing.lastLine + 1
                || range.lastLine + 1 < existing.firstLine) {
                continue;
            }
            existing.firstLine =
                std::min(existing.firstLine, range.firstLine);
            existing.lastLine =
                std::max(existing.lastLine, range.lastLine);
            return;
        }
        m_conservativeRanges.append(range);
    }

    void collectConservativeErrors(TSNode node)
    {
        if (ts_node_is_null(node))
            return;
        if (typeIs(node, "ERROR") || ts_node_is_missing(node)) {
            markConservative(node);
            if (typeIs(node, "ERROR"))
                return;
        }
        const uint32_t count = ts_node_child_count(node);
        for (uint32_t index = 0; index < count; ++index) {
            collectConservativeErrors(
                ts_node_child(node, index));
        }
    }

    void rollbackEdits(int editCount, bool validBefore)
    {
        while (m_edits.size() > editCount)
            m_edits.removeLast();
        m_valid = validBefore;
    }

    bool setGap(int leftEnd, int rightStart, int spaces)
    {
        return addEdit(leftEnd,
                       rightStart,
                       QString(std::max(0, spaces), QLatin1Char(' ')));
    }

    bool setBreak(int leftEnd, int rightStart, int indent)
    {
        return addEdit(
            leftEnd,
            rightStart,
            QStringLiteral("\n")
                + QString(std::max(0, indent), QLatin1Char(' ')));
    }

    bool setLeading(int tokenStart, int indent)
    {
        return addEdit(
            lineStart(m_source, tokenStart),
            tokenStart,
            QString(std::max(0, indent), QLatin1Char(' ')));
    }

    bool placeOnIndentedLine(int leftEnd,
                             int tokenStart,
                             int indent)
    {
        if (leftEnd < 0 || tokenStart < leftEnd)
            return false;
        const QStringView gap =
            QStringView(m_source).mid(leftEnd,
                                      tokenStart - leftEnd);
        if (!gap.contains(QLatin1Char('\n')))
            return setBreak(leftEnd, tokenStart, indent);
        return setLeading(tokenStart, indent);
    }

    void indentStandaloneComments(TSNode container, int indent)
    {
        for (const TSNode child : directNamedChildren(container)) {
            if (!isCommentNode(child))
                continue;
            const int start = nodeStart(child);
            const int startOfLine = lineStart(m_source, start);
            if (allWhitespace(
                    QStringView(m_source).mid(
                        startOfLine, start - startOfLine))) {
                setLeading(start, indent);
            }
        }
    }

    void trimLineEnds(TSNode node)
    {
        if (ts_node_is_null(node))
            return;
        int cursor = lineStart(m_source, nodeStart(node));
        const int finalPosition =
            std::min(nodeEnd(node),
                     static_cast<int>(m_source.size()));
        while (cursor <= finalPosition && cursor < m_source.size()) {
            int lineEnd = m_source.indexOf(
                QLatin1Char('\n'), cursor);
            if (lineEnd < 0)
                lineEnd = m_source.size();
            int contentLimit = lineEnd;
            if (contentLimit > cursor
                && m_source.at(contentLimit - 1) == QLatin1Char('\r')) {
                --contentLimit;
            }
            int contentEnd = contentLimit;
            while (contentEnd > cursor
                   && (m_source.at(contentEnd - 1) == QLatin1Char(' ')
                       || m_source.at(contentEnd - 1)
                          == QLatin1Char('\t'))) {
                --contentEnd;
            }
            if (contentEnd < contentLimit)
                addEdit(contentEnd, contentLimit, QString());
            if (lineEnd >= finalPosition
                || lineEnd >= m_source.size()) {
                break;
            }
            cursor = lineEnd + 1;
        }
    }

    Part canonicalPart(TSNode scope, int start, int end)
    {
        Part part;
        if (start < 0 || end < start)
            return part;
        part.leaves = leavesInRange(scope, m_source, start, end);
        if (part.leaves.isEmpty()) {
            part.start = start;
            part.end = start;
            part.width = 0;
            return part;
        }
        for (const LeafToken& leaf : part.leaves) {
            if (isCommentNode(leaf.node))
                return {};
        }
        part.start = part.leaves.first().start;
        part.end = part.leaves.last().end;
        part.width = canonicalWidth(part.leaves);
        return part;
    }

    Part rawPart(TSNode node)
    {
        Part part;
        if (ts_node_is_null(node))
            return part;
        part.start = nodeStart(node);
        part.end = nodeEnd(node);
        part.width = part.end - part.start;
        return part;
    }

    void canonicalize(const Part& part)
    {
        if (!part.valid() || part.leaves.size() < 2)
            return;
        for (int index = 1; index < part.leaves.size(); ++index) {
            addEdit(part.leaves.at(index - 1).end,
                    part.leaves.at(index).start,
                    canonicalGap(part.leaves, index));
        }
    }

    Trailer trailerFor(TSNode container,
                       TSNode item,
                       int boundaryEnd)
    {
        Trailer trailer;
        trailer.end = nodeEnd(item);
        const QList<LeafToken> leaves = leavesOf(container, m_source);
        for (const LeafToken& leaf : leaves) {
            if (leaf.start < nodeEnd(item))
                continue;
            if (leaf.start >= boundaryEnd)
                break;
            if (nodeStartLine(leaf.node) > nodeEndLine(item))
                break;
            if (leaf.text == QStringLiteral(",")
                && trailer.comma.start < 0) {
                trailer.comma = leaf;
                trailer.end = leaf.end;
                continue;
            }
            if (isCommentNode(leaf.node)
                && trailer.comment.start < 0) {
                if (!typeIs(leaf.node, "one_line_comment"))
                    return trailer;
                trailer.comment = leaf;
                trailer.end = leaf.end;
                continue;
            }
            if (!allWhitespace(
                    QStringView(m_source).mid(leaf.start,
                                              leaf.end - leaf.start))) {
                return trailer;
            }
        }
        trailer.valid = true;
        return trailer;
    }

    TSNode childField(TSNode node, const char* field) const
    {
        return ts_node_child_by_field_name(
            node,
            field,
            static_cast<uint32_t>(std::strlen(field)));
    }

    ParameterItem parseParameterItem(TSNode container,
                                     TSNode node,
                                     int boundaryEnd)
    {
        ParameterItem item;
        item.node = node;
        if (ts_node_has_error(node)
            || nodeStartLine(node) != nodeEndLine(node)) {
            return {};
        }

        TSNode declaration = firstDirectNamedChild(
            node, "parameter_declaration");
        if (ts_node_is_null(declaration)) {
            declaration = firstDirectNamedChild(
                node, "local_parameter_declaration");
        }
        if (ts_node_is_null(declaration))
            return {};

        const QList<LeafToken> declarationLeaves =
            leavesOf(declaration, m_source);
        if (declarationLeaves.isEmpty())
            return {};
        const LeafToken keyword = declarationLeaves.first();
        if (keyword.text != QStringLiteral("parameter")
            && keyword.text != QStringLiteral("localparam")) {
            return {};
        }
        item.keyword.start = keyword.start;
        item.keyword.end = keyword.end;
        item.keyword.width = keyword.text.size();
        item.keyword.leaves = {keyword};

        const TSNode assignments = firstDescendant(
            declaration, "list_of_param_assignments");
        const QList<TSNode> assignmentNodes =
            directNamedChildren(assignments, "param_assignment");
        if (assignmentNodes.size() != 1)
            return {};
        const TSNode assignment = assignmentNodes.first();
        const QList<LeafToken> assignmentLeaves =
            leavesOf(assignment, m_source);
        if (assignmentLeaves.size() < 3)
            return {};

        int equalIndex = -1;
        for (int index = 0; index < assignmentLeaves.size(); ++index) {
            if (assignmentLeaves.at(index).text == QStringLiteral("=")) {
                equalIndex = index;
                break;
            }
        }
        if (equalIndex < 1 || equalIndex + 1 >= assignmentLeaves.size())
            return {};
        item.equal = assignmentLeaves.at(equalIndex);

        const LeafToken nameLeaf = assignmentLeaves.first();
        if (nameLeaf.start >= item.equal.start)
            return {};
        item.name.start = nameLeaf.start;
        item.name.end = nameLeaf.end;
        item.name.width = nameLeaf.text.size();
        item.name.leaves = {nameLeaf};

        item.type = canonicalPart(
            declaration,
            keyword.end,
            nameLeaf.start);
        if (!item.type.valid())
            return {};

        if (nameLeaf.end < item.equal.start) {
            item.unpacked = canonicalPart(
                assignment,
                nameLeaf.end,
                item.equal.start);
            if (!item.unpacked.valid())
                return {};
        } else {
            item.unpacked.start = nameLeaf.end;
            item.unpacked.end = nameLeaf.end;
            item.unpacked.width = 0;
        }

        const LeafToken valueFirst = assignmentLeaves.at(equalIndex + 1);
        const LeafToken valueLast = assignmentLeaves.last();
        item.value = canonicalPart(
            assignment, valueFirst.start, valueLast.end);
        if (!item.value.valid())
            return {};
        if (m_source.mid(item.value.start,
                         item.value.end - item.value.start)
                .contains(QLatin1Char('\n'))) {
            return {};
        }
        item.trailer = trailerFor(
            m_document.rootNode(), node, boundaryEnd);
        if (!item.trailer.valid)
            return {};
        return item;
    }

    PortItem parsePortItem(TSNode container,
                           TSNode node,
                           int boundaryEnd)
    {
        PortItem item;
        item.node = node;
        if (ts_node_has_error(node)
            || nodeStartLine(node) != nodeEndLine(node)) {
            return {};
        }
        const TSNode nameNode = childField(node, "port_name");
        if (ts_node_is_null(nameNode))
            return {};
        item.name = rawPart(nameNode);
        item.name.leaves = leavesOf(nameNode, m_source);

        TSNode header = firstDirectNamedChild(node, "interface_port_header");
        if (!ts_node_is_null(header)) {
            item.direction = canonicalPart(
                header, nodeStart(header), nodeEnd(header));
            if (!item.direction.valid())
                return {};
            item.type.start = item.direction.end;
            item.type.end = item.direction.end;
            item.type.width = 0;
            item.packed = item.type;
        } else {
            header = firstDirectNamedChild(node, "variable_port_header");
            if (ts_node_is_null(header))
                header = firstDirectNamedChild(node, "net_port_header");
            if (ts_node_is_null(header))
                return {};

            const TSNode direction = firstDescendant(
                header, "port_direction");
            if (ts_node_is_null(direction))
                return {};
            item.direction = canonicalPart(
                direction, nodeStart(direction), nodeEnd(direction));

            QList<TSNode> packedNodes;
            collectDescendants(header,
                               "packed_dimension",
                               &packedNodes,
                               true);
            const int typeEnd = packedNodes.isEmpty()
                ? nodeEnd(header)
                : nodeStart(packedNodes.first());
            item.type = canonicalPart(
                header, nodeEnd(direction), typeEnd);
            if (!item.type.valid())
                return {};
            if (packedNodes.isEmpty()) {
                item.packed.start = typeEnd;
                item.packed.end = typeEnd;
                item.packed.width = 0;
            } else {
                item.packed = canonicalPart(
                    header,
                    nodeStart(packedNodes.first()),
                    nodeEnd(packedNodes.last()));
            }
            if (!item.direction.valid() || !item.packed.valid())
                return {};
        }

        QList<TSNode> unpackedNodes;
        static const QList<const char*> unpackedTypes = {
            "unpacked_dimension",
            "unsized_dimension",
            "associative_dimension",
            "queue_dimension"
        };
        for (const char* type : unpackedTypes) {
            QList<TSNode> matches;
            collectDescendants(node, type, &matches, true);
            for (const TSNode match : matches) {
                if (nodeStart(match) >= item.name.end)
                    unpackedNodes.append(match);
            }
        }
        std::sort(unpackedNodes.begin(),
                  unpackedNodes.end(),
                  [](TSNode left, TSNode right) {
            return nodeStart(left) < nodeStart(right);
        });
        if (unpackedNodes.isEmpty()) {
            item.unpacked.start = item.name.end;
            item.unpacked.end = item.name.end;
            item.unpacked.width = 0;
        } else {
            item.unpacked = canonicalPart(
                node,
                nodeStart(unpackedNodes.first()),
                nodeEnd(unpackedNodes.last()));
        }
        if (!item.unpacked.valid())
            return {};

        const int expectedEnd = item.unpacked.width > 0
            ? item.unpacked.end
            : item.name.end;
        const QList<LeafToken> trailingDeclarationLeaves =
            leavesInRange(node, m_source, expectedEnd, nodeEnd(node));
        if (!trailingDeclarationLeaves.isEmpty())
            return {};

        item.trailer = trailerFor(
            m_document.rootNode(), node, boundaryEnd);
        if (!item.trailer.valid)
            return {};
        return item;
    }

    AssociationItem parseAssociationItem(TSNode container,
                                         TSNode node,
                                         int boundaryEnd)
    {
        AssociationItem item;
        item.node = node;
        if (ts_node_has_error(node))
            return {};
        const QList<LeafToken> leaves = leavesOf(node, m_source);
        if (leaves.size() < 4
            || leaves.first().text != QStringLiteral(".")) {
            return {};
        }
        const LeafToken open = firstLeafText(
            leaves, QStringLiteral("("), leaves.first().end);
        const LeafToken close = lastLeafText(
            leaves, QStringLiteral(")"));
        if (open.start < 0 || close.start < open.end)
            return {};

        item.formal.start = leaves.first().start;
        item.formal.leaves.clear();
        for (const LeafToken& leaf : leaves) {
            if (leaf.start >= item.formal.start
                && leaf.end <= open.start) {
                item.formal.leaves.append(leaf);
            }
        }
        if (item.formal.leaves.isEmpty())
            return {};
        item.formal.end = item.formal.leaves.last().end;
        item.formal.width = canonicalWidth(item.formal.leaves);
        if (item.formal.leaves.size() < 2)
            return {};

        item.openParen = open;
        item.closeParen = close;
        TSNode actualNode = childField(node, "connection");
        if (ts_node_is_null(actualNode)) {
            for (const TSNode child : directNamedChildren(node)) {
                if (nodeStart(child) >= open.end
                    && nodeEnd(child) <= close.start
                    && !isCommentNode(child)) {
                    actualNode = child;
                    break;
                }
            }
        }
        if (!ts_node_is_null(actualNode)) {
            item.actual = rawPart(actualNode);
            item.hasActual = true;
            item.multiline =
                nodeStartLine(actualNode) != nodeEndLine(actualNode);
        } else {
            item.actual.start = close.start;
            item.actual.end = close.start;
            item.actual.width = 0;
        }
        item.multiline = item.multiline
            || nodeStartLine(node) != nodeEndLine(node);
        item.trailer = trailerFor(
            m_document.rootNode(), node, boundaryEnd);
        if (!item.trailer.valid)
            return {};
        if (item.trailer.comment.start < 0) {
            for (const LeafToken& leaf : leaves) {
                if (leaf.start >= close.end
                    && typeIs(leaf.node, "one_line_comment")) {
                    item.trailer.comment = leaf;
                    item.trailer.end =
                        std::max(item.trailer.end, leaf.end);
                    break;
                }
            }
        }
        return item;
    }

    template<typename Item>
    bool completeItems(const QList<Item>& items) const
    {
        if (items.isEmpty())
            return false;
        for (const Item& item : items) {
            if (ts_node_is_null(item.node) || !item.trailer.valid)
                return false;
        }
        return true;
    }

    bool formatParameterItems(TSNode container,
                              const QList<TSNode>& nodes,
                              int itemIndent,
                              LeafToken opening,
                              LeafToken closing)
    {
        if (nodes.isEmpty()) {
            return setGap(opening.end, closing.start, 0);
        }
        QList<ParameterItem> items;
        for (int index = 0; index < nodes.size(); ++index) {
            const int boundary = index + 1 < nodes.size()
                ? nodeStart(nodes.at(index + 1))
                : closing.start;
            items.append(parseParameterItem(
                container, nodes.at(index), boundary));
        }
        if (!completeItems(items))
            return false;

        int keywordWidth = 0;
        int typeWidth = 0;
        int nameWidth = 0;
        int unpackedWidth = 0;
        int valueWidth = 0;
        for (const ParameterItem& item : items) {
            keywordWidth = std::max(keywordWidth, item.keyword.width);
            typeWidth = std::max(typeWidth, item.type.width);
            nameWidth = std::max(nameWidth, item.name.width);
            unpackedWidth =
                std::max(unpackedWidth, item.unpacked.width);
            valueWidth = std::max(valueWidth, item.value.width);
        }

        const int keywordColumn = itemIndent;
        const int typeColumn = keywordColumn + keywordWidth + 1;
        const int nameColumn = typeColumn + typeWidth + 1;
        const int unpackedColumn = nameColumn + nameWidth + 1;
        const int equalColumn = unpackedColumn
            + (unpackedWidth > 0 ? unpackedWidth + 1 : 0);
        const int valueColumn = equalColumn + 2;
        const int commaColumn = valueColumn + valueWidth + 1;
        const int commentColumn = commaColumn + 2;

        if (!placeOnIndentedLine(
                opening.end,
                items.first().keyword.start,
                itemIndent)) {
            return false;
        }
        indentStandaloneComments(container, itemIndent);
        for (int index = 0; index < items.size(); ++index) {
            const ParameterItem& item = items.at(index);
            canonicalize(item.type);
            canonicalize(item.unpacked);
            canonicalize(item.value);

            Part previous = item.keyword;
            int previousColumnEnd =
                keywordColumn + item.keyword.width;
            if (item.type.width > 0) {
                setGap(previous.end,
                       item.type.start,
                       typeColumn - previousColumnEnd);
                previous = item.type;
                previousColumnEnd = typeColumn + item.type.width;
            }
            setGap(previous.end,
                   item.name.start,
                   nameColumn - previousColumnEnd);
            previous = item.name;
            previousColumnEnd = nameColumn + item.name.width;
            if (item.unpacked.width > 0) {
                setGap(previous.end,
                       item.unpacked.start,
                       unpackedColumn - previousColumnEnd);
                previous = item.unpacked;
                previousColumnEnd =
                    unpackedColumn + item.unpacked.width;
            }
            setGap(previous.end,
                   item.equal.start,
                   equalColumn - previousColumnEnd);
            setGap(item.equal.end,
                   item.value.start,
                   valueColumn - (equalColumn + item.equal.text.size()));

            if (item.trailer.comma.start >= 0) {
                setGap(item.value.end,
                       item.trailer.comma.start,
                       commaColumn - (valueColumn + item.value.width));
            }
            if (item.trailer.comment.start >= 0) {
                const int previousEnd =
                    item.trailer.comma.start >= 0
                        ? item.trailer.comma.end
                        : item.value.end;
                const int previousColumn =
                    item.trailer.comma.start >= 0
                        ? commaColumn + 1
                        : valueColumn + item.value.width;
                setGap(previousEnd,
                       item.trailer.comment.start,
                       commentColumn - previousColumn);
            }

            if (index + 1 < items.size()) {
                if (!placeOnIndentedLine(
                        item.trailer.end,
                        items.at(index + 1).keyword.start,
                        itemIndent)) {
                    return false;
                }
            }
        }
        if (!placeOnIndentedLine(
                items.last().trailer.end,
                closing.start,
                std::max(0, itemIndent - m_indentWidth))) {
            return false;
        }
        return m_valid;
    }

    bool formatPortItems(TSNode container,
                         const QList<TSNode>& nodes,
                         int itemIndent,
                         LeafToken opening,
                         LeafToken closing)
    {
        if (nodes.isEmpty()) {
            return setGap(opening.end, closing.start, 0);
        }
        QList<PortItem> items;
        for (int index = 0; index < nodes.size(); ++index) {
            const int boundary = index + 1 < nodes.size()
                ? nodeStart(nodes.at(index + 1))
                : closing.start;
            items.append(parsePortItem(
                container, nodes.at(index), boundary));
        }
        if (!completeItems(items))
            return false;

        int directionWidth = 0;
        int typeWidth = 0;
        int packedWidth = 0;
        int nameWidth = 0;
        int unpackedWidth = 0;
        for (const PortItem& item : items) {
            directionWidth =
                std::max(directionWidth, item.direction.width);
            typeWidth = std::max(typeWidth, item.type.width);
            packedWidth = std::max(packedWidth, item.packed.width);
            nameWidth = std::max(nameWidth, item.name.width);
            unpackedWidth =
                std::max(unpackedWidth, item.unpacked.width);
        }

        const int directionColumn = itemIndent;
        const int typeColumn = directionColumn + directionWidth + 1;
        const int packedColumn = typeColumn
            + (typeWidth > 0 ? typeWidth + 1 : 0);
        const int nameColumn = packedColumn
            + (packedWidth > 0 ? packedWidth + 1 : 0);
        const int unpackedColumn = nameColumn + nameWidth + 1;
        const int declarationEndColumn = unpackedColumn
            + (unpackedWidth > 0 ? unpackedWidth : -1);
        const int commaColumn = declarationEndColumn + 1;
        const int commentColumn = commaColumn + 2;

        if (!placeOnIndentedLine(
                opening.end,
                items.first().direction.start,
                itemIndent)) {
            return false;
        }
        indentStandaloneComments(container, itemIndent);
        for (int index = 0; index < items.size(); ++index) {
            const PortItem& item = items.at(index);
            canonicalize(item.direction);
            canonicalize(item.type);
            canonicalize(item.packed);
            canonicalize(item.unpacked);

            Part previous = item.direction;
            int previousColumnEnd =
                directionColumn + item.direction.width;
            if (item.type.width > 0) {
                setGap(previous.end,
                       item.type.start,
                       typeColumn - previousColumnEnd);
                previous = item.type;
                previousColumnEnd = typeColumn + item.type.width;
            }
            if (item.packed.width > 0) {
                setGap(previous.end,
                       item.packed.start,
                       packedColumn - previousColumnEnd);
                previous = item.packed;
                previousColumnEnd = packedColumn + item.packed.width;
            }
            setGap(previous.end,
                   item.name.start,
                   nameColumn - previousColumnEnd);
            previous = item.name;
            previousColumnEnd = nameColumn + item.name.width;
            if (item.unpacked.width > 0) {
                setGap(previous.end,
                       item.unpacked.start,
                       unpackedColumn - previousColumnEnd);
                previous = item.unpacked;
                previousColumnEnd =
                    unpackedColumn + item.unpacked.width;
            }

            if (item.trailer.comma.start >= 0) {
                setGap(previous.end,
                       item.trailer.comma.start,
                       commaColumn - previousColumnEnd);
            }
            if (item.trailer.comment.start >= 0) {
                const int previousEnd =
                    item.trailer.comma.start >= 0
                        ? item.trailer.comma.end
                        : previous.end;
                const int commentPreviousColumn =
                    item.trailer.comma.start >= 0
                        ? commaColumn + 1
                        : previousColumnEnd;
                setGap(previousEnd,
                       item.trailer.comment.start,
                       commentColumn - commentPreviousColumn);
            }
            if (index + 1 < items.size()) {
                if (!placeOnIndentedLine(
                        item.trailer.end,
                        items.at(index + 1).direction.start,
                        itemIndent)) {
                    return false;
                }
            }
        }
        if (!placeOnIndentedLine(
                items.last().trailer.end,
                closing.start,
                std::max(0, itemIndent - m_indentWidth))) {
            return false;
        }
        return m_valid;
    }

    bool formatNamedAssociations(TSNode container,
                                 const QList<TSNode>& nodes,
                                 int baseIndent,
                                 LeafToken opening,
                                 LeafToken closing)
    {
        if (nodes.isEmpty())
            return setGap(opening.end, closing.start, 0);
        QList<AssociationItem> items;
        for (int index = 0; index < nodes.size(); ++index) {
            const int boundary = index + 1 < nodes.size()
                ? nodeStart(nodes.at(index + 1))
                : closing.start;
            items.append(parseAssociationItem(
                container, nodes.at(index), boundary));
        }
        if (!completeItems(items))
            return false;

        const int itemIndent = baseIndent + m_indentWidth;
        if (!placeOnIndentedLine(
                opening.end,
                items.first().formal.start,
                itemIndent)) {
            return false;
        }
        indentStandaloneComments(container, itemIndent);

        int blockStart = 0;
        while (blockStart < items.size()) {
            if (items.at(blockStart).multiline) {
                const AssociationItem& item = items.at(blockStart);
                canonicalize(item.formal);
                if (blockStart + 1 < items.size()) {
                    if (!placeOnIndentedLine(
                            item.trailer.end,
                            items.at(blockStart + 1).formal.start,
                            itemIndent)) {
                        return false;
                    }
                }
                ++blockStart;
                continue;
            }
            int blockEnd = blockStart;
            while (blockEnd + 1 < items.size()
                   && !items.at(blockEnd + 1).multiline) {
                ++blockEnd;
            }

            int formalWidth = 0;
            int actualWidth = 0;
            for (int index = blockStart; index <= blockEnd; ++index) {
                formalWidth =
                    std::max(formalWidth, items.at(index).formal.width);
                actualWidth =
                    std::max(actualWidth, items.at(index).actual.width);
            }
            const int formalColumn = itemIndent;
            const int openColumn = formalColumn + formalWidth + 1;
            const int actualColumn = openColumn + 2;
            const int closeColumn = actualColumn + actualWidth + 1;
            const int commaColumn = closeColumn + 1;
            const int commentColumn = commaColumn + 2;

            for (int index = blockStart; index <= blockEnd; ++index) {
                const AssociationItem& item = items.at(index);
                canonicalize(item.formal);
                setGap(item.formal.end,
                       item.openParen.start,
                       openColumn
                           - (formalColumn + item.formal.width));
                if (item.hasActual) {
                    setGap(item.openParen.end,
                           item.actual.start,
                           actualColumn - (openColumn + 1));
                    setGap(item.actual.end,
                           item.closeParen.start,
                           closeColumn
                               - (actualColumn + item.actual.width));
                } else {
                    setGap(item.openParen.end,
                           item.closeParen.start,
                           closeColumn - (openColumn + 1));
                }
                if (item.trailer.comma.start >= 0) {
                    setGap(item.closeParen.end,
                           item.trailer.comma.start,
                           commaColumn - (closeColumn + 1));
                }
                if (item.trailer.comment.start >= 0) {
                    const int previousEnd =
                        item.trailer.comma.start >= 0
                            ? item.trailer.comma.end
                            : item.closeParen.end;
                    const int previousColumn =
                        item.trailer.comma.start >= 0
                            ? commaColumn + 1
                            : closeColumn + 1;
                    setGap(previousEnd,
                           item.trailer.comment.start,
                           commentColumn - previousColumn);
                }
                if (index + 1 < items.size()) {
                    if (!placeOnIndentedLine(
                            item.trailer.end,
                            items.at(index + 1).formal.start,
                            itemIndent)) {
                        return false;
                    }
                }
            }
            blockStart = blockEnd + 1;
        }
        if (!placeOnIndentedLine(
                items.last().trailer.end,
                closing.start,
                baseIndent)) {
            return false;
        }
        return m_valid;
    }

    bool formatOrderedAssociations(TSNode container,
                                   const QList<TSNode>& nodes,
                                   int baseIndent,
                                   LeafToken opening,
                                   LeafToken closing)
    {
        if (nodes.isEmpty())
            return setGap(opening.end, closing.start, 0);
        const int itemIndent = baseIndent + m_indentWidth;
        QList<Trailer> trailers;
        for (int index = 0; index < nodes.size(); ++index) {
            const int boundary = index + 1 < nodes.size()
                ? nodeStart(nodes.at(index + 1))
                : closing.start;
            const Trailer trailer = trailerFor(
                m_document.rootNode(), nodes.at(index), boundary);
            if (!trailer.valid)
                return false;
            trailers.append(trailer);
        }
        if (!placeOnIndentedLine(
                opening.end,
                nodeStart(nodes.first()),
                itemIndent)) {
            return false;
        }
        indentStandaloneComments(container, itemIndent);
        for (int index = 0; index < nodes.size(); ++index) {
            if (index + 1 < nodes.size()) {
                if (!placeOnIndentedLine(
                        trailers.at(index).end,
                        nodeStart(nodes.at(index + 1)),
                        itemIndent)) {
                    return false;
                }
            }
        }
        if (!placeOnIndentedLine(
                trailers.last().end,
                closing.start,
                baseIndent)) {
            return false;
        }
        return m_valid;
    }

    bool formatAssociationContainer(TSNode outer,
                                    TSNode container,
                                    const char* namedType,
                                    const char* orderedType,
                                    int baseIndent)
    {
        if (ts_node_is_null(outer)
            || ts_node_is_null(container)) {
            return false;
        }
        const QList<LeafToken> outerLeaves = leavesOf(outer, m_source);
        const LeafToken opening = firstLeafText(
            outerLeaves, QStringLiteral("("));
        const LeafToken closing = lastLeafText(
            outerLeaves, QStringLiteral(")"));
        if (opening.start < 0 || closing.start < opening.end)
            return false;

        const QList<TSNode> named =
            directNamedChildren(container, namedType);
        const QList<TSNode> ordered =
            directNamedChildren(container, orderedType);
        const int allowedCount = named.size() + ordered.size();
        int actualCount = 0;
        for (const TSNode child : directNamedChildren(container)) {
            if (typeIs(child, namedType)
                || typeIs(child, orderedType)
                || isCommentNode(child)) {
                if (!isCommentNode(child))
                    ++actualCount;
                continue;
            }
            return false;
        }
        if (actualCount != allowedCount)
            return false;
        if (actualCount == 0)
            return setGap(opening.end, closing.start, 0);
        if (!named.isEmpty() && !ordered.isEmpty())
            return false;
        if (!named.isEmpty()) {
            return formatNamedAssociations(
                container, named, baseIndent, opening, closing);
        }
        return formatOrderedAssociations(
            container, ordered, baseIndent, opening, closing);
    }

    bool formatModuleImpl(TSNode module)
    {
        const auto fail = [](const char*) { return false; };
        TSNode header = firstDirectNamedChild(module, "module_ansi_header");
        const bool ansiHeader = !ts_node_is_null(header);
        if (ts_node_is_null(header)) {
            header = firstDirectNamedChild(
                module, "module_nonansi_header");
        }
        if (ts_node_is_null(header))
            return fail("no module header");
        const TSNode keywordNode = firstDescendant(
            header, "module_keyword");
        TSNode nameNode = childField(module, "name");
        if (ts_node_is_null(nameNode))
            nameNode = childField(header, "name");
        if (ts_node_is_null(nameNode))
            nameNode = firstDirectNamedChild(header, "simple_identifier");
        const TSNode parameterList = firstDirectNamedChild(
            header, "parameter_port_list");
        TSNode portList = firstDirectNamedChild(
            header,
            ansiHeader
                ? "list_of_port_declarations"
                : "list_of_ports");
        if (ts_node_is_null(keywordNode)
            || ts_node_is_null(nameNode)) {
            return fail("missing keyword or name");
        }

        const QList<LeafToken> keywordLeaves =
            leavesOf(keywordNode, m_source);
        if (keywordLeaves.isEmpty())
            return fail("missing keyword leaves");
        const LeafToken keyword = keywordLeaves.first();
        const QList<LeafToken> headerLeaves = leavesOf(header, m_source);

        if (ts_node_is_null(portList)) {
            if (!ts_node_is_null(parameterList))
                return fail("parameter list without port delimiters");
            LeafToken semicolon;
            for (const LeafToken& leaf : headerLeaves) {
                if (leaf.start >= nodeEnd(nameNode)
                    && leaf.text == QStringLiteral(";")) {
                    semicolon = leaf;
                    break;
                }
            }
            if (semicolon.start < 0
                || !setLeading(keyword.start, 0)
                || !setGap(nodeEnd(keywordNode),
                           nodeStart(nameNode),
                           1)
                || !setGap(nodeEnd(nameNode),
                           semicolon.start,
                           0)) {
                return fail("simple module header");
            }
            trimLineEnds(header);
            return m_valid;
        }

        const QList<LeafToken> portLeaves =
            leavesOf(portList, m_source);
        if (portLeaves.size() < 2)
            return fail("missing port-list leaves");
        const LeafToken portOpening = firstLeafText(
            portLeaves, QStringLiteral("("));
        const LeafToken portClosing = lastLeafText(
            portLeaves, QStringLiteral(")"));
        if (portOpening.start < 0 || portClosing.start < 0)
            return fail("missing port delimiters");

        LeafToken semicolon;
        for (const LeafToken& leaf : headerLeaves) {
            if (leaf.start >= portClosing.end
                && leaf.text == QStringLiteral(";")) {
                semicolon = leaf;
                break;
            }
        }
        if (semicolon.start < 0)
            return fail("missing semicolon");

        QList<TSNode> ports;
        if (ansiHeader) {
            ports =
                directNamedChildren(portList,
                                    "ansi_port_declaration");
            for (const TSNode child : directNamedChildren(portList)) {
                if (!typeIs(child, "ansi_port_declaration")
                    && !isCommentNode(child)) {
                    return fail(ts_node_type(child));
                }
            }
        } else {
            for (const TSNode child : directNamedChildren(portList)) {
                if (!isCommentNode(child))
                    return fail("non-ANSI ports are conservative");
            }
        }

        if (!setLeading(keyword.start, 0)
            || !setGap(nodeEnd(keywordNode),
                       nodeStart(nameNode),
                       1)) {
            return fail("module prefix edit");
        }
        if (!ts_node_is_null(parameterList)) {
            const QList<LeafToken> parameterLeaves =
                leavesOf(parameterList, m_source);
            const LeafToken hash = firstLeafText(
                parameterLeaves, QStringLiteral("#"));
            const LeafToken opening = firstLeafText(
                parameterLeaves, QStringLiteral("("));
            const LeafToken closing = lastLeafText(
                parameterLeaves, QStringLiteral(")"));
            if (hash.start < 0 || opening.start < 0 || closing.start < 0)
                return fail("parameter delimiters");

            const QList<TSNode> parameters =
                directNamedChildren(parameterList,
                                    "parameter_port_declaration");
            for (const TSNode child : directNamedChildren(parameterList)) {
                if (!typeIs(child, "parameter_port_declaration")
                    && !isCommentNode(child)) {
                    return fail(ts_node_type(child));
                }
            }
            if (!setGap(nodeEnd(nameNode), hash.start, 1)
                || !setGap(hash.end, opening.start, 0)
                || !setGap(closing.end, portOpening.start, 0)
                || !formatParameterItems(parameterList,
                                         parameters,
                                         m_indentWidth,
                                         opening,
                                         closing)) {
                return fail("parameter item format");
            }
        } else {
            if (!setGap(nodeEnd(nameNode), portOpening.start, 0))
                return fail("name to port gap");
        }

        if (!formatPortItems(portList,
                             ports,
                             m_indentWidth,
                             portOpening,
                             portClosing)) {
            return fail("port item format");
        }
        if (!setGap(portClosing.end, semicolon.start, 0))
            return fail("port closing semicolon gap");
        trimLineEnds(header);
        return m_valid;
    }

    void formatModule(TSNode module)
    {
        const int editCount = m_edits.size();
        const bool validBefore = m_valid;
        TSNode conservativeNode =
            firstDirectNamedChild(module, "module_ansi_header");
        if (ts_node_is_null(conservativeNode)) {
            conservativeNode =
                firstDirectNamedChild(
                    module, "module_nonansi_header");
        }
        if (ts_node_is_null(conservativeNode))
            conservativeNode = module;
        const bool formatted = formatModuleImpl(module);
        if (!formatted || !m_valid) {
            rollbackEdits(editCount, validBefore);
            markConservative(conservativeNode);
        }
    }

    bool formatInstantiationImpl(TSNode instance)
    {
        const TSNode typeNode = childField(instance, "instance_type");
        const QList<TSNode> hierarchies =
            directNamedChildren(instance, "hierarchical_instance");
        if (ts_node_is_null(typeNode) || hierarchies.size() != 1)
            return false;
        const TSNode hierarchy = hierarchies.first();
        const TSNode nameOfInstance = firstDirectNamedChild(
            hierarchy, "name_of_instance");
        const TSNode portConnections = firstDirectNamedChild(
            hierarchy, "list_of_port_connections");
        if (ts_node_is_null(nameOfInstance))
            return false;

        const int baseIndent = leadingWidthAt(m_source, nodeStart(instance));
        if (!setLeading(nodeStart(typeNode), baseIndent))
            return false;
        const TSNode parameterValue = firstDirectNamedChild(
            instance, "parameter_value_assignment");
        if (!ts_node_is_null(parameterValue)) {
            const QList<LeafToken> parameterLeaves =
                leavesOf(parameterValue, m_source);
            const LeafToken hash = firstLeafText(
                parameterLeaves, QStringLiteral("#"));
            const LeafToken closing = lastLeafText(
                parameterLeaves, QStringLiteral(")"));
            if (hash.start < 0 || closing.start < 0)
                return false;
            if (!setGap(nodeEnd(typeNode), hash.start, 1)
                || !setGap(closing.end,
                           nodeStart(nameOfInstance),
                           1)) {
                return false;
            }
            const TSNode assignments = firstDirectNamedChild(
                parameterValue,
                "list_of_parameter_value_assignments");
            if (ts_node_is_null(assignments)
                || !formatAssociationContainer(
                    parameterValue,
                    assignments,
                    "named_parameter_assignment",
                    "ordered_parameter_assignment",
                    baseIndent)) {
                return false;
            }
        } else {
            if (!setGap(nodeEnd(typeNode),
                        nodeStart(nameOfInstance),
                        1)) {
                return false;
            }
        }

        const QList<LeafToken> hierarchyLeaves =
            leavesOf(hierarchy, m_source);
        const LeafToken portOpening = firstLeafText(
            hierarchyLeaves,
            QStringLiteral("("),
            nodeEnd(nameOfInstance));
        const LeafToken portClosing = lastLeafText(
            hierarchyLeaves, QStringLiteral(")"));
        if (portOpening.start < 0 || portClosing.start < 0)
            return false;
        if (!setGap(nodeEnd(nameOfInstance), portOpening.start, 0))
            return false;
        if (!ts_node_is_null(portConnections)) {
            if (!formatAssociationContainer(
                    hierarchy,
                    portConnections,
                    "named_port_connection",
                    "ordered_port_connection",
                    baseIndent)) {
                return false;
            }
        } else if (!setGap(
                       portOpening.end,
                       portClosing.start,
                       0)) {
            return false;
        }

        const QList<LeafToken> instanceLeaves =
            leavesOf(instance, m_source);
        LeafToken semicolon;
        for (const LeafToken& leaf : instanceLeaves) {
            if (leaf.start >= portClosing.end
                && leaf.text == QStringLiteral(";")) {
                semicolon = leaf;
                break;
            }
        }
        if (semicolon.start < 0
            || !setGap(portClosing.end, semicolon.start, 0)) {
            return false;
        }
        trimLineEnds(instance);
        return m_valid;
    }

    void formatInstantiation(TSNode instance)
    {
        const int editCount = m_edits.size();
        const bool validBefore = m_valid;
        if (!formatInstantiationImpl(instance) || !m_valid) {
            rollbackEdits(editCount, validBefore);
            markConservative(instance);
        }
    }

    const QString& m_source;
    int m_indentWidth = 4;
    TSDocument m_document;
    QVector<int> m_lineStarts;
    QList<WhitespaceEdit> m_edits;
    QList<LineRange> m_conservativeRanges;
    bool m_indentConditionalBranches = true;
    bool m_indentCaseItemBodies = true;
    bool m_alignCaseItems = true;
    bool m_preservePreprocessorIndent = true;
    bool m_valid = true;
};

bool hasIdenticalImmutableLeafTokens(const QString& before,
                                     const QString& after)
{
    TSDocument beforeDocument;
    TSDocument afterDocument;
    beforeDocument.setText(before);
    afterDocument.setText(after);
    const QList<LeafToken> beforeLeaves =
        leavesOf(beforeDocument.rootNode(), before);
    const QList<LeafToken> afterLeaves =
        leavesOf(afterDocument.rootNode(), after);
    if (beforeLeaves.size() != afterLeaves.size())
        return false;
    for (int index = 0; index < beforeLeaves.size(); ++index) {
        const LeafToken& left = beforeLeaves.at(index);
        const LeafToken& right = afterLeaves.at(index);
        QString leftText = left.text;
        QString rightText = right.text;
        if (isCommentNode(left.node) && isCommentNode(right.node)) {
            const auto trimLineEnds = [](QString text) {
                QStringList lines =
                    text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
                for (QString& line : lines) {
                    int contentLimit = line.size();
                    if (contentLimit > 0
                        && line.at(contentLimit - 1)
                               == QLatin1Char('\r')) {
                        --contentLimit;
                    }
                    int contentEnd = contentLimit;
                    while (contentEnd > 0
                           && (line.at(contentEnd - 1)
                                   == QLatin1Char(' ')
                               || line.at(contentEnd - 1)
                                      == QLatin1Char('\t'))) {
                        --contentEnd;
                    }
                    line.remove(contentEnd,
                                contentLimit - contentEnd);
                }
                return lines.join(QLatin1Char('\n'));
            };
            leftText = trimLineEnds(leftText);
            rightText = trimLineEnds(rightText);
        }
        if (leftText != rightText)
            return false;
    }
    return true;
}

} // namespace

QString normalizeLexicalWhitespaceTabs(const QString& text,
                                       int spacesPerTab)
{
    if (text.isEmpty()
        || !text.contains(QLatin1Char('\t')))
        return text;

    TSDocument document;
    document.setText(text);
    QList<ProtectedTextRange> protectedRanges;
    collectTabProtectedRanges(
        document.rootNode(), &protectedRanges);
    std::sort(
        protectedRanges.begin(),
        protectedRanges.end(),
        [](const ProtectedTextRange& left,
           const ProtectedTextRange& right) {
            if (left.start != right.start)
                return left.start < right.start;
            return left.end < right.end;
        });

    QString result;
    result.reserve(text.size());
    int protectedIndex = 0;
    const int replacementWidth =
        std::max(1, spacesPerTab);
    for (int position = 0; position < text.size(); ++position) {
        while (protectedIndex < protectedRanges.size()
               && position
                      >= protectedRanges.at(protectedIndex).end) {
            ++protectedIndex;
        }
        const bool protectedPosition =
            protectedIndex < protectedRanges.size()
            && position
                   >= protectedRanges.at(protectedIndex).start
            && position
                   < protectedRanges.at(protectedIndex).end;
        if (text.at(position) == QLatin1Char('\t')
            && !protectedPosition) {
            result.append(
                QString(replacementWidth, QLatin1Char(' ')));
        } else {
            result.append(text.at(position));
        }
    }
    if (!hasIdenticalNonWhitespaceStream(text, result))
        return text;
    return result;
}

QString formatStructuralIndentation(
    const QString& text,
    int indentWidth,
    bool indentConditionalBranches,
    bool indentCaseItemBodies,
    bool alignCaseItems,
    bool preservePreprocessorIndent)
{
    if (text.isEmpty())
        return text;
    TreeWhitespaceFormatter formatter(text, indentWidth);
    const QString candidate =
        formatter.runStructuralIndentation(
            indentConditionalBranches,
            indentCaseItemBodies,
            alignCaseItems,
            preservePreprocessorIndent);
    if (!hasIdenticalNonWhitespaceStream(text, candidate))
        return text;
    return candidate;
}

bool hasIdenticalNonWhitespaceStream(const QString& before,
                                     const QString& after)
{
    int left = 0;
    int right = 0;
    while (true) {
        while (left < before.size() && before.at(left).isSpace())
            ++left;
        while (right < after.size() && after.at(right).isSpace())
            ++right;
        const bool leftDone = left >= before.size();
        const bool rightDone = right >= after.size();
        if (leftDone || rightDone) {
            return leftDone && rightDone
                && hasIdenticalImmutableLeafTokens(
                    before, after);
        }
        if (before.at(left) != after.at(right))
            return false;
        ++left;
        ++right;
    }
}

QString format(const QString& text, int indentWidth)
{
    if (text.isEmpty())
        return text;
    TreeWhitespaceFormatter formatter(text, indentWidth);
    const QString candidate = formatter.run();
    if (!hasIdenticalNonWhitespaceStream(text, candidate))
        return text;
    return candidate;
}

QList<LineRange> conservativeLineRanges(const QString& text,
                                        int indentWidth)
{
    if (text.isEmpty())
        return {};
    TreeWhitespaceFormatter formatter(text, indentWidth);
    formatter.run();
    QList<LineRange> ranges = formatter.conservativeRanges();
    std::sort(ranges.begin(),
              ranges.end(),
              [](const LineRange& left, const LineRange& right) {
        if (left.firstLine != right.firstLine)
            return left.firstLine < right.firstLine;
        return left.lastLine < right.lastLine;
    });
    return ranges;
}

QList<LineRange> syntaxErrorLineRanges(const QString& text)
{
    if (text.isEmpty())
        return {};
    TreeWhitespaceFormatter formatter(text, 4);
    QList<LineRange> ranges = formatter.syntaxErrorRanges();
    std::sort(ranges.begin(),
              ranges.end(),
              [](const LineRange& left, const LineRange& right) {
        if (left.firstLine != right.firstLine)
            return left.firstLine < right.firstLine;
        return left.lastLine < right.lastLine;
    });
    return ranges;
}

} // namespace StructuredWhitespaceFormatter
