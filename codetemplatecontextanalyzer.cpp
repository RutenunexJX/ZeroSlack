#include "codetemplatecontextanalyzer.h"

#include "tsdocument.h"

#include <QHash>
#include <QStringList>

#include <cstring>
#include <limits>

namespace {
bool nodeTypeIs(TSNode node, const char* expected)
{
    if (ts_node_is_null(node) || !expected)
        return false;
    const char* type = ts_node_type(node);
    return type && std::strcmp(type, expected) == 0;
}

QString nodeText(const QString& text, TSNode node)
{
    if (ts_node_is_null(node))
        return QString();
    const uint32_t startByte = ts_node_start_byte(node);
    const uint32_t endByte = ts_node_end_byte(node);
    if (endByte <= startByte)
        return QString();
    return text.mid(static_cast<int>(startByte / 2u),
                    static_cast<int>((endByte - startByte) / 2u));
}

QList<TSNode> directNamedChildren(TSNode node)
{
    QList<TSNode> result;
    const uint32_t count = ts_node_named_child_count(node);
    result.reserve(static_cast<int>(count));
    for (uint32_t index = 0; index < count; ++index)
        result.append(ts_node_named_child(node, index));
    return result;
}

TSNode directNamedChildOfType(TSNode node, const char* expected)
{
    for (const TSNode child : directNamedChildren(node)) {
        if (nodeTypeIs(child, expected))
            return child;
    }
    return {};
}

TSNode firstDescendantOfType(TSNode node, const char* expected)
{
    if (nodeTypeIs(node, expected))
        return node;
    for (const TSNode child : directNamedChildren(node)) {
        const TSNode match = firstDescendantOfType(child, expected);
        if (!ts_node_is_null(match))
            return match;
    }
    return {};
}

bool identifierNode(TSNode node)
{
    return nodeTypeIs(node, "simple_identifier")
        || nodeTypeIs(node, "escaped_identifier");
}

TSNode firstIdentifierDescendant(TSNode node)
{
    if (identifierNode(node))
        return node;
    for (const TSNode child : directNamedChildren(node)) {
        const TSNode identifier = firstIdentifierDescendant(child);
        if (!ts_node_is_null(identifier))
            return identifier;
    }
    return {};
}

TSNode enclosingModule(TSNode node)
{
    while (!ts_node_is_null(node)) {
        if (nodeTypeIs(node, "module_declaration"))
            return node;
        node = ts_node_parent(node);
    }
    return {};
}

TSNode containingModule(TSNode node, uint32_t cursorByte)
{
    if (nodeTypeIs(node, "module_declaration")
        && cursorByte >= ts_node_start_byte(node)
        && cursorByte < ts_node_end_byte(node)) {
        return node;
    }
    for (const TSNode child : directNamedChildren(node)) {
        if (cursorByte < ts_node_start_byte(child)
            || cursorByte >= ts_node_end_byte(child)) {
            continue;
        }
        const TSNode match = containingModule(child, cursorByte);
        if (!ts_node_is_null(match))
            return match;
    }
    return {};
}

TSNode moduleAt(TSNode root, int cursorPosition, int textSize)
{
    if (textSize <= 0)
        return {};
    const int bounded = qBound(0, cursorPosition, textSize - 1);
    const uint32_t byte = static_cast<uint32_t>(bounded) * 2u;
    const TSNode leaf =
        ts_node_named_descendant_for_byte_range(root, byte, byte);
    const TSNode ancestor = enclosingModule(leaf);
    if (!ts_node_is_null(ancestor))
        return ancestor;
    return containingModule(root, byte);
}

QString normalizedIdentifier(QString name)
{
    name = name.trimmed().toCaseFolded();
    if (name.startsWith(QLatin1Char('\\')))
        name.remove(0, 1);
    return name;
}

QStringList identifierSegments(const QString& name)
{
    return normalizedIdentifier(name).split(
        QLatin1Char('_'), Qt::SkipEmptyParts);
}

bool hasSegment(const QStringList& segments,
                const QString& expected)
{
    for (const QString& segment : segments) {
        if (segment == expected)
            return true;
    }
    return false;
}

int clockNameScore(const QString& name)
{
    const QString normalized = normalizedIdentifier(name);
    const QStringList segments = identifierSegments(name);
    if (normalized == QStringLiteral("clk"))
        return 120;
    if (normalized == QStringLiteral("clock"))
        return 115;
    if (hasSegment(segments, QStringLiteral("clk")))
        return 105;
    if (hasSegment(segments, QStringLiteral("clock")))
        return 100;
    if (normalized.startsWith(QStringLiteral("clk"))
        || normalized.endsWith(QStringLiteral("clk"))) {
        return 90;
    }
    if (normalized.startsWith(QStringLiteral("clock"))
        || normalized.endsWith(QStringLiteral("clock"))) {
        return 85;
    }
    return 0;
}

int resetNameScore(const QString& name)
{
    const QString normalized = normalizedIdentifier(name);
    const QStringList segments = identifierSegments(name);
    if (normalized == QStringLiteral("rst")
        || normalized == QStringLiteral("reset")) {
        return 120;
    }
    if (hasSegment(segments, QStringLiteral("rst"))
        || hasSegment(segments, QStringLiteral("reset"))) {
        return 110;
    }
    if (hasSegment(segments, QStringLiteral("arst"))
        || hasSegment(segments, QStringLiteral("srst"))
        || hasSegment(segments, QStringLiteral("clear"))
        || hasSegment(segments, QStringLiteral("clearb"))) {
        return 100;
    }
    const QStringList compactNames = {
        QStringLiteral("rstn"),
        QStringLiteral("rstni"),
        QStringLiteral("resetn"),
        QStringLiteral("resetni"),
        QStringLiteral("aresetn"),
        QStringLiteral("aresetni"),
        QStringLiteral("arstn"),
        QStringLiteral("arstni"),
        QStringLiteral("srstn"),
        QStringLiteral("srstni"),
    };
    if (compactNames.contains(normalized))
        return 105;
    if (normalized.startsWith(QStringLiteral("rst"))
        || normalized.startsWith(QStringLiteral("reset"))
        || normalized.endsWith(QStringLiteral("rst"))
        || normalized.endsWith(QStringLiteral("reset"))) {
        return 90;
    }
    return 0;
}

struct SignalCandidate {
    QString name;
    int order = -1;
    int inputCount = 0;
    int clockEdgeCount = 0;
    int resetEdgeCount = 0;
    int conditionCount = 0;
};

class CandidateSet
{
public:
    SignalCandidate& ensure(const QString& name)
    {
        const QString key = normalizedIdentifier(name);
        auto it = values.find(key);
        if (it == values.end()) {
            SignalCandidate candidate;
            candidate.name = name.trimmed();
            candidate.order = nextOrder++;
            it = values.insert(key, candidate);
        }
        return it.value();
    }

    const QHash<QString, SignalCandidate>& all() const
    {
        return values;
    }

private:
    QHash<QString, SignalCandidate> values;
    int nextOrder = 0;
};

void addInputPort(const QString& text,
                  TSNode identifier,
                  CandidateSet* candidates)
{
    if (!candidates || ts_node_is_null(identifier))
        return;
    const QString name = nodeText(text, identifier).trimmed();
    if (!name.isEmpty())
        ++candidates->ensure(name).inputCount;
}

void collectAnsiInputPorts(const QString& text,
                           TSNode module,
                           CandidateSet* candidates)
{
    const TSNode header =
        directNamedChildOfType(module, "module_ansi_header");
    const TSNode portList =
        directNamedChildOfType(header, "list_of_port_declarations");
    if (ts_node_is_null(portList))
        return;

    bool inputDirection = false;
    for (const TSNode declaration : directNamedChildren(portList)) {
        if (!nodeTypeIs(declaration, "ansi_port_declaration"))
            continue;
        const TSNode direction =
            firstDescendantOfType(declaration, "port_direction");
        if (!ts_node_is_null(direction)) {
            inputDirection =
                nodeText(text, direction).trimmed()
                    == QStringLiteral("input");
        }
        if (!inputDirection)
            continue;
        const TSNode portName = ts_node_child_by_field_name(
            declaration, "port_name", 9);
        addInputPort(text, portName, candidates);
    }
}

void collectIdentifierChildren(const QString& text,
                               TSNode node,
                               CandidateSet* candidates)
{
    if (identifierNode(node)) {
        addInputPort(text, node, candidates);
        return;
    }
    for (const TSNode child : directNamedChildren(node))
        collectIdentifierChildren(text, child, candidates);
}

void collectNonAnsiInputPorts(const QString& text,
                              TSNode node,
                              CandidateSet* candidates)
{
    if (nodeTypeIs(node, "module_declaration"))
        return;
    if (nodeTypeIs(node, "input_declaration")) {
        TSNode names =
            directNamedChildOfType(node, "list_of_port_identifiers");
        if (ts_node_is_null(names)) {
            names = directNamedChildOfType(
                node, "list_of_variable_identifiers");
        }
        collectIdentifierChildren(text, names, candidates);
        return;
    }
    for (const TSNode child : directNamedChildren(node))
        collectNonAnsiInputPorts(text, child, candidates);
}

struct EdgeSignal {
    QString name;
};

void collectEventEdges(const QString& text,
                       TSNode node,
                       QList<EdgeSignal>* edges)
{
    if (!edges)
        return;
    if (nodeTypeIs(node, "event_expression")) {
        const TSNode edge =
            directNamedChildOfType(node, "edge_identifier");
        if (!ts_node_is_null(edge)) {
            TSNode expression =
                directNamedChildOfType(node, "expression");
            const TSNode identifier =
                firstIdentifierDescendant(expression);
            const QString name = nodeText(text, identifier).trimmed();
            if (!name.isEmpty())
                edges->append({name});
            return;
        }
    }
    for (const TSNode child : directNamedChildren(node))
        collectEventEdges(text, child, edges);
}

void collectConditionalSignals(const QString& text,
                               TSNode node,
                               CandidateSet* candidates)
{
    if (nodeTypeIs(node, "conditional_statement")) {
        const TSNode predicate =
            directNamedChildOfType(node, "cond_predicate");
        const TSNode identifier =
            firstIdentifierDescendant(predicate);
        const QString name = nodeText(text, identifier).trimmed();
        if (!name.isEmpty())
            ++candidates->ensure(name).conditionCount;
        return;
    }
    for (const TSNode child : directNamedChildren(node))
        collectConditionalSignals(text, child, candidates);
}

void collectAlwaysSignals(const QString& text,
                          TSNode node,
                          CandidateSet* candidates)
{
    if (nodeTypeIs(node, "always_construct")) {
        const TSNode eventControl =
            firstDescendantOfType(node, "event_control");
        QList<EdgeSignal> edges;
        if (!ts_node_is_null(eventControl))
            collectEventEdges(text, eventControl, &edges);
        for (int index = 0; index < edges.size(); ++index) {
            SignalCandidate& candidate =
                candidates->ensure(edges.at(index).name);
            if (index == 0)
                ++candidate.clockEdgeCount;
            else
                ++candidate.resetEdgeCount;
        }
        collectConditionalSignals(text, node, candidates);
        return;
    }
    for (const TSNode child : directNamedChildren(node))
        collectAlwaysSignals(text, child, candidates);
}

QString bestClock(const CandidateSet& candidates)
{
    QString result;
    int resultScore = std::numeric_limits<int>::min();
    int resultOrder = std::numeric_limits<int>::max();
    for (const SignalCandidate& candidate : candidates.all()) {
        const int nameScore = clockNameScore(candidate.name);
        if (candidate.clockEdgeCount == 0 && nameScore == 0)
            continue;
        const int score =
            candidate.clockEdgeCount * 10000
            + nameScore * 100
            + candidate.inputCount * 10;
        if (score > resultScore
            || (score == resultScore
                && candidate.order < resultOrder)) {
            result = candidate.name;
            resultScore = score;
            resultOrder = candidate.order;
        }
    }
    return result;
}

QString bestReset(const CandidateSet& candidates)
{
    QString result;
    int resultScore = std::numeric_limits<int>::min();
    int resultOrder = std::numeric_limits<int>::max();
    for (const SignalCandidate& candidate : candidates.all()) {
        const int nameScore = resetNameScore(candidate.name);
        if (candidate.resetEdgeCount == 0 && nameScore == 0)
            continue;
        const int conditionScore =
            nameScore > 0 ? candidate.conditionCount * 200 : 0;
        const int score =
            candidate.resetEdgeCount * 10000
            + nameScore * 100
            + conditionScore
            + candidate.inputCount * 10;
        if (score > resultScore
            || (score == resultScore
                && candidate.order < resultOrder)) {
            result = candidate.name;
            resultScore = score;
            resultOrder = candidate.order;
        }
    }
    return result;
}
}

CodeTemplateSignalContext CodeTemplateContextAnalyzer::analyze(
    const QString& documentText,
    int cursorPosition)
{
    CodeTemplateSignalContext result;
    if (documentText.isEmpty() || cursorPosition < 0)
        return result;

    TSDocument document;
    document.setText(documentText);
    const TSNode module =
        moduleAt(document.rootNode(),
                 cursorPosition,
                 documentText.size());
    if (ts_node_is_null(module))
        return result;

    result.currentModuleFound = true;
    CandidateSet candidates;
    collectAnsiInputPorts(documentText, module, &candidates);
    for (const TSNode child : directNamedChildren(module))
        collectNonAnsiInputPorts(documentText, child, &candidates);
    collectAlwaysSignals(documentText, module, &candidates);
    result.clockName = bestClock(candidates);
    result.resetName = bestReset(candidates);
    return result;
}
