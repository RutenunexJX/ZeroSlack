#ifndef EDITORANCHOREDRANGEINDEX_H
#define EDITORANCHOREDRANGEINDEX_H

#include "annotationlayer.h"
#include "documentchange.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

struct EditorAnchoredRangeRemapReport
{
    qsizetype removedItems = 0;
    qsizetype shiftedItems = 0;
    qsizetype visitedNodes = 0;

    bool changed() const
    {
        return removedItems > 0 || shiftedItems > 0;
    }
};

struct EditorAnchoredRangeIndexStats
{
    qsizetype itemCount = 0;
    qsizetype allocatedNodeCount = 0;
    qsizetype lastRemapVisitedNodes = 0;
    qsizetype lastRemapRemovedItems = 0;
    qsizetype lastRemapShiftedItems = 0;
    std::uint64_t materializationCount = 0;
};

// Traits supplies:
//   start(item), effectiveEnd(item), firstLine(item), lastLine(item)
//   shift(item, characterDelta, lineDelta)
template<typename Item, typename Traits>
class EditorAnchoredRangeIndex
{
public:
    using value_type = Item;
    using const_iterator = typename QList<Item>::const_iterator;

    EditorAnchoredRangeIndex() = default;
    EditorAnchoredRangeIndex(const EditorAnchoredRangeIndex&) = delete;
    EditorAnchoredRangeIndex& operator=(
        const EditorAnchoredRangeIndex&) = delete;
    EditorAnchoredRangeIndex(EditorAnchoredRangeIndex&&) = delete;
    EditorAnchoredRangeIndex& operator=(
        EditorAnchoredRangeIndex&&) = delete;

    EditorAnchoredRangeIndex& operator=(const QList<Item>& items)
    {
        assign(items);
        return *this;
    }

    void assign(const QList<Item>& items)
    {
        root = nullptr;
        nodes.clear();
        nodes.reserve(static_cast<std::size_t>(items.size()));
        activeItemCount = 0;
        nextSequence = 0;
        prioritySeed = 0x9e3779b9u;
        invalidateMaterialized();

        for (const Item& item : items) {
            auto node = std::make_unique<Node>();
            node->item = item;
            node->sequence = nextSequence++;
            node->priority = nextPriority();
            initializeAggregate(node.get());
            Node* const nodePointer = node.get();
            nodes.push_back(std::move(node));
            root = insertNode(root, nodePointer);
            ++activeItemCount;
        }
        resetLastRemapStats();
    }

    template<typename Compare>
    void stableSort(Compare compare)
    {
        QList<Item> sorted = toList();
        std::stable_sort(sorted.begin(), sorted.end(), compare);
        assign(sorted);
    }

    void clear()
    {
        root = nullptr;
        nodes.clear();
        activeItemCount = 0;
        nextSequence = 0;
        prioritySeed = 0x9e3779b9u;
        invalidateMaterialized();
        resetLastRemapStats();
    }

    bool isEmpty() const { return activeItemCount == 0; }
    qsizetype size() const { return activeItemCount; }

    QList<Item> toList() const
    {
        ensureMaterialized();
        return materializedItems;
    }

    operator QList<Item>() const { return toList(); }

    const Item& at(qsizetype index) const
    {
        ensureMaterialized();
        return materializedItems.at(index);
    }

    const Item& first() const
    {
        ensureMaterialized();
        return materializedItems.first();
    }

    const_iterator begin() const
    {
        ensureMaterialized();
        return materializedItems.cbegin();
    }

    const_iterator end() const
    {
        ensureMaterialized();
        return materializedItems.cend();
    }

    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    EditorAnchoredRangeRemapReport remap(
        const DocumentChange& change)
    {
        EditorAnchoredRangeRemapReport report;
        if (!root) {
            resetLastRemapStats();
            return report;
        }

        Node* before = nullptr;
        Node* suffix = nullptr;
        splitByStart(root,
                     change.oldEnd(),
                     &before,
                     &suffix,
                     &report.visitedNodes);
        before = removeEndingAfter(before,
                                   change.position,
                                   &report);

        if (change.characterDelta() != 0
            || change.lineDelta != 0) {
            report.shiftedItems = nodeSize(suffix);
            applyShift(suffix,
                       change.characterDelta(),
                       change.lineDelta);
        }
        root = mergeTrees(before, suffix, &report.visitedNodes);
        activeItemCount -= report.removedItems;
        if (report.changed())
            invalidateMaterialized();

        lastRemapVisitedNodes = report.visitedNodes;
        lastRemapRemovedItems = report.removedItems;
        lastRemapShiftedItems = report.shiftedItems;
        return report;
    }

    QList<Item> overlapping(int startPosition,
                            int endPosition,
                            qsizetype* visitedNodes = nullptr) const
    {
        QList<Item> result;
        if (endPosition <= startPosition || !root) {
            if (visitedNodes)
                *visitedNodes = 0;
            return result;
        }
        qsizetype visited = 0;
        collectOverlapping(root,
                           startPosition,
                           endPosition,
                           &result,
                           &visited);
        if (visitedNodes)
            *visitedNodes = visited;
        return result;
    }

    QList<Item> overlappingLines(int firstLine,
                                 int lastLine,
                                 qsizetype* visitedNodes = nullptr) const
    {
        QList<Item> result;
        if (lastLine < firstLine || !root) {
            if (visitedNodes)
                *visitedNodes = 0;
            return result;
        }
        qsizetype visited = 0;
        collectOverlappingLines(root,
                                firstLine,
                                lastLine,
                                &result,
                                &visited);
        if (visitedNodes)
            *visitedNodes = visited;
        return result;
    }

    EditorAnchoredRangeIndexStats stats() const
    {
        EditorAnchoredRangeIndexStats result;
        result.itemCount = activeItemCount;
        result.allocatedNodeCount =
            static_cast<qsizetype>(nodes.size());
        result.lastRemapVisitedNodes = lastRemapVisitedNodes;
        result.lastRemapRemovedItems = lastRemapRemovedItems;
        result.lastRemapShiftedItems = lastRemapShiftedItems;
        result.materializationCount = materializationCount;
        return result;
    }

private:
    struct Node {
        Item item;
        std::uint64_t sequence = 0;
        std::uint32_t priority = 0;
        Node* left = nullptr;
        Node* right = nullptr;
        int lazyCharacterShift = 0;
        int lazyLineShift = 0;
        int maximumEnd = std::numeric_limits<int>::min();
        int minimumFirstLine = std::numeric_limits<int>::max();
        int maximumLastLine = std::numeric_limits<int>::min();
        qsizetype subtreeSize = 1;
        bool active = true;
    };

    static qsizetype nodeSize(const Node* node)
    {
        return node ? node->subtreeSize : 0;
    }

    static int nodeMaximumEnd(const Node* node)
    {
        return node
            ? node->maximumEnd
            : std::numeric_limits<int>::min();
    }

    static int nodeMinimumFirstLine(const Node* node)
    {
        return node
            ? node->minimumFirstLine
            : std::numeric_limits<int>::max();
    }

    static int nodeMaximumLastLine(const Node* node)
    {
        return node
            ? node->maximumLastLine
            : std::numeric_limits<int>::min();
    }

    static bool keyLess(int leftStart,
                        std::uint64_t leftSequence,
                        int rightStart,
                        std::uint64_t rightSequence)
    {
        return leftStart < rightStart
            || (leftStart == rightStart
                && leftSequence < rightSequence);
    }

    static void initializeAggregate(Node* node)
    {
        if (!node)
            return;
        node->maximumEnd = Traits::effectiveEnd(node->item);
        node->minimumFirstLine = Traits::firstLine(node->item);
        node->maximumLastLine = Traits::lastLine(node->item);
        node->subtreeSize = 1;
    }

    static void pull(Node* node)
    {
        if (!node)
            return;
        node->subtreeSize =
            1 + nodeSize(node->left) + nodeSize(node->right);
        node->maximumEnd = std::max(
            Traits::effectiveEnd(node->item),
            std::max(nodeMaximumEnd(node->left),
                     nodeMaximumEnd(node->right)));
        node->minimumFirstLine = std::min(
            Traits::firstLine(node->item),
            std::min(nodeMinimumFirstLine(node->left),
                     nodeMinimumFirstLine(node->right)));
        node->maximumLastLine = std::max(
            Traits::lastLine(node->item),
            std::max(nodeMaximumLastLine(node->left),
                     nodeMaximumLastLine(node->right)));
    }

    static void applyShift(Node* node,
                           int characterDelta,
                           int lineDelta)
    {
        if (!node || (characterDelta == 0 && lineDelta == 0))
            return;
        Traits::shift(node->item, characterDelta, lineDelta);
        node->lazyCharacterShift += characterDelta;
        node->lazyLineShift += lineDelta;
        if (characterDelta != 0)
            node->maximumEnd += characterDelta;
        if (lineDelta != 0) {
            if (node->minimumFirstLine
                != std::numeric_limits<int>::max()) {
                node->minimumFirstLine += lineDelta;
            }
            if (node->maximumLastLine
                != std::numeric_limits<int>::min()) {
                node->maximumLastLine += lineDelta;
            }
        }
    }

    static void push(Node* node)
    {
        if (!node
            || (node->lazyCharacterShift == 0
                && node->lazyLineShift == 0)) {
            return;
        }
        applyShift(node->left,
                   node->lazyCharacterShift,
                   node->lazyLineShift);
        applyShift(node->right,
                   node->lazyCharacterShift,
                   node->lazyLineShift);
        node->lazyCharacterShift = 0;
        node->lazyLineShift = 0;
    }

    static Node* mergeTrees(Node* left,
                            Node* right,
                            qsizetype* visitedNodes = nullptr)
    {
        if (!left)
            return right;
        if (!right)
            return left;
        if (visitedNodes)
            ++(*visitedNodes);

        if (left->priority < right->priority) {
            push(left);
            left->right =
                mergeTrees(left->right, right, visitedNodes);
            pull(left);
            return left;
        }
        push(right);
        right->left =
            mergeTrees(left, right->left, visitedNodes);
        pull(right);
        return right;
    }

    static void splitByKey(Node* tree,
                           int start,
                           std::uint64_t sequence,
                           Node** left,
                           Node** right)
    {
        if (!tree) {
            *left = nullptr;
            *right = nullptr;
            return;
        }
        push(tree);
        if (keyLess(Traits::start(tree->item),
                    tree->sequence,
                    start,
                    sequence)) {
            splitByKey(tree->right,
                       start,
                       sequence,
                       &tree->right,
                       right);
            pull(tree);
            *left = tree;
            return;
        }
        splitByKey(tree->left,
                   start,
                   sequence,
                   left,
                   &tree->left);
        pull(tree);
        *right = tree;
    }

    static Node* insertNode(Node* tree, Node* node)
    {
        if (!tree)
            return node;
        push(tree);
        if (node->priority < tree->priority) {
            splitByKey(tree,
                       Traits::start(node->item),
                       node->sequence,
                       &node->left,
                       &node->right);
            pull(node);
            return node;
        }
        if (keyLess(Traits::start(node->item),
                    node->sequence,
                    Traits::start(tree->item),
                    tree->sequence)) {
            tree->left = insertNode(tree->left, node);
        } else {
            tree->right = insertNode(tree->right, node);
        }
        pull(tree);
        return tree;
    }

    static void splitByStart(Node* tree,
                             int start,
                             Node** left,
                             Node** right,
                             qsizetype* visitedNodes)
    {
        if (!tree) {
            *left = nullptr;
            *right = nullptr;
            return;
        }
        if (visitedNodes)
            ++(*visitedNodes);
        push(tree);
        if (Traits::start(tree->item) < start) {
            splitByStart(tree->right,
                         start,
                         &tree->right,
                         right,
                         visitedNodes);
            pull(tree);
            *left = tree;
            return;
        }
        splitByStart(tree->left,
                     start,
                     left,
                     &tree->left,
                     visitedNodes);
        pull(tree);
        *right = tree;
    }

    static Node* removeEndingAfter(
        Node* tree,
        int position,
        EditorAnchoredRangeRemapReport* report)
    {
        if (!tree || nodeMaximumEnd(tree) <= position)
            return tree;
        ++report->visitedNodes;
        push(tree);
        tree->left = removeEndingAfter(tree->left,
                                       position,
                                       report);
        tree->right = removeEndingAfter(tree->right,
                                        position,
                                        report);
        if (Traits::effectiveEnd(tree->item) > position) {
            Node* const left = tree->left;
            Node* const right = tree->right;
            tree->left = nullptr;
            tree->right = nullptr;
            tree->active = false;
            tree->subtreeSize = 1;
            ++report->removedItems;
            return mergeTrees(left,
                              right,
                              &report->visitedNodes);
        }
        pull(tree);
        return tree;
    }

    static void collectOverlapping(Node* tree,
                                   int startPosition,
                                   int endPosition,
                                   QList<Item>* result,
                                   qsizetype* visitedNodes)
    {
        if (!tree || nodeMaximumEnd(tree) <= startPosition)
            return;
        ++(*visitedNodes);
        push(tree);
        collectOverlapping(tree->left,
                           startPosition,
                           endPosition,
                           result,
                           visitedNodes);
        const int start = Traits::start(tree->item);
        if (start < endPosition
            && Traits::effectiveEnd(tree->item) > startPosition) {
            result->append(tree->item);
        }
        if (start < endPosition) {
            collectOverlapping(tree->right,
                               startPosition,
                               endPosition,
                               result,
                               visitedNodes);
        }
    }

    static void collectOverlappingLines(Node* tree,
                                        int firstLine,
                                        int lastLine,
                                        QList<Item>* result,
                                        qsizetype* visitedNodes)
    {
        if (!tree
            || nodeMaximumLastLine(tree) < firstLine
            || nodeMinimumFirstLine(tree) > lastLine) {
            return;
        }
        ++(*visitedNodes);
        push(tree);
        collectOverlappingLines(tree->left,
                                firstLine,
                                lastLine,
                                result,
                                visitedNodes);
        if (Traits::firstLine(tree->item) <= lastLine
            && Traits::lastLine(tree->item) >= firstLine) {
            result->append(tree->item);
        }
        collectOverlappingLines(tree->right,
                                firstLine,
                                lastLine,
                                result,
                                visitedNodes);
    }

    static void flushLazy(Node* tree)
    {
        if (!tree)
            return;
        push(tree);
        flushLazy(tree->left);
        flushLazy(tree->right);
    }

    void ensureMaterialized() const
    {
        if (materializedValid)
            return;
        flushLazy(root);
        materializedItems.clear();
        materializedItems.reserve(activeItemCount);
        for (const std::unique_ptr<Node>& node : nodes) {
            if (node->active)
                materializedItems.append(node->item);
        }
        materializedValid = true;
        ++materializationCount;
    }

    void invalidateMaterialized() const
    {
        materializedItems.clear();
        materializedValid = false;
    }

    void resetLastRemapStats()
    {
        lastRemapVisitedNodes = 0;
        lastRemapRemovedItems = 0;
        lastRemapShiftedItems = 0;
    }

    std::uint32_t nextPriority()
    {
        std::uint32_t value = (prioritySeed += 0x9e3779b9u);
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        value ^= value >> 16;
        return value;
    }

    mutable Node* root = nullptr;
    std::vector<std::unique_ptr<Node>> nodes;
    qsizetype activeItemCount = 0;
    std::uint64_t nextSequence = 0;
    std::uint32_t prioritySeed = 0x9e3779b9u;
    mutable QList<Item> materializedItems;
    mutable bool materializedValid = false;
    mutable std::uint64_t materializationCount = 0;
    qsizetype lastRemapVisitedNodes = 0;
    qsizetype lastRemapRemovedItems = 0;
    qsizetype lastRemapShiftedItems = 0;
};

struct EditorAnnotationAnchorTraits
{
    static int start(const EditorAnnotation& annotation)
    {
        return annotation.range.startPosition;
    }

    static int effectiveEnd(const EditorAnnotation& annotation)
    {
        return std::max(annotation.range.startPosition + 1,
                        annotation.range.endPosition);
    }

    static int firstLine(const EditorAnnotation& annotation)
    {
        return annotation.range.firstLine;
    }

    static int lastLine(const EditorAnnotation& annotation)
    {
        return annotation.range.lastLine;
    }

    static void shift(EditorAnnotation& annotation,
                      int characterDelta,
                      int lineDelta)
    {
        annotation.range.startPosition += characterDelta;
        annotation.range.endPosition += characterDelta;
        annotation.range.firstLine =
            std::max(0, annotation.range.firstLine + lineDelta);
        annotation.range.lastLine =
            std::max(annotation.range.firstLine,
                     annotation.range.lastLine + lineDelta);
        const QChar separator = QLatin1Char('\x1f');
        const int firstSeparator =
            annotation.semanticKey.indexOf(separator);
        const int secondSeparator =
            firstSeparator < 0
            ? -1
            : annotation.semanticKey.indexOf(
                  separator, firstSeparator + 1);
        if (firstSeparator >= 0
            && secondSeparator > firstSeparator) {
            annotation.semanticKey.replace(
                firstSeparator + 1,
                secondSeparator - firstSeparator - 1,
                QString::number(annotation.range.startPosition));
        }
    }
};

// AnnotationLayer remains the public coordinator for all annotation kinds.
// Its semantic-ghost source is indexed separately so paint-time publication
// materializes only the queried visible lines.
class EditorRuntimeAnnotationLayer : public AnnotationLayer
{
public:
    void setSourceAnnotations(
        const QString& sourceId,
        const QList<EditorAnnotation>& annotations)
    {
        if (isGhostSource(sourceId)) {
            ghostAnnotations = annotations;
            ghostSourceGeneration = annotations.isEmpty()
                ? 0
                : annotations.first().sourceGeneration;
            AnnotationLayer::removeSource(sourceId);
            return;
        }
        AnnotationLayer::setSourceAnnotations(sourceId, annotations);
    }

    void removeSource(const QString& sourceId)
    {
        if (isGhostSource(sourceId)) {
            ghostAnnotations.clear();
            ghostSourceGeneration = 0;
        }
        AnnotationLayer::removeSource(sourceId);
    }

    void clear()
    {
        ghostAnnotations.clear();
        ghostSourceGeneration = 0;
        AnnotationLayer::clear();
    }

    QStringList sourceIds() const
    {
        QStringList result = AnnotationLayer::sourceIds();
        if (!ghostAnnotations.isEmpty()
            && !result.contains(ghostSourceId())) {
            result.append(ghostSourceId());
            std::sort(result.begin(), result.end());
        }
        return result;
    }

    AnnotationLayerReport resolve(
        const AnnotationLayerQuery& query = {}) const
    {
        const int firstVisibleLine =
            std::max(0, query.firstVisibleLine);
        const int lastVisibleLine =
            std::max(firstVisibleLine, query.lastVisibleLine);
        qsizetype visitedNodes = 0;
        QList<EditorAnnotation> visible =
            ghostAnnotations.overlappingLines(
                firstVisibleLine,
                lastVisibleLine,
                &visitedNodes);
        for (EditorAnnotation& annotation : visible)
            annotation.sourceGeneration = ghostSourceGeneration;
        auto* const self =
            const_cast<EditorRuntimeAnnotationLayer*>(this);
        if (visible.isEmpty()) {
            self->AnnotationLayer::removeSource(ghostSourceId());
        } else {
            self->AnnotationLayer::setSourceAnnotations(
                ghostSourceId(), visible);
        }

        AnnotationLayerReport report = AnnotationLayer::resolve(query);
        const qsizetype offscreenGhosts =
            ghostAnnotations.size() - visible.size();
        report.inputCount += static_cast<int>(offscreenGhosts);
        report.offscreenCount += static_cast<int>(offscreenGhosts);
        lastVisibleQueryVisitedNodes = visitedNodes;
        return report;
    }

    EditorAnchoredRangeRemapReport remapGhostSource(
        const DocumentChange& change,
        quint64 sourceGeneration)
    {
        AnnotationLayer::removeSource(ghostSourceId());
        ghostSourceGeneration = sourceGeneration;
        return ghostAnnotations.remap(change);
    }

    EditorAnchoredRangeIndexStats ghostIndexStats() const
    {
        return ghostAnnotations.stats();
    }

    qsizetype lastGhostVisibleQueryVisitedNodes() const
    {
        return lastVisibleQueryVisitedNodes;
    }

private:
    static QString ghostSourceId()
    {
        return QStringLiteral("semantic-ghosts");
    }

    static bool isGhostSource(const QString& sourceId)
    {
        return sourceId == ghostSourceId();
    }

    EditorAnchoredRangeIndex<
        EditorAnnotation,
        EditorAnnotationAnchorTraits> ghostAnnotations;
    quint64 ghostSourceGeneration = 0;
    mutable qsizetype lastVisibleQueryVisitedNodes = 0;
};

#endif // EDITORANCHOREDRANGEINDEX_H
