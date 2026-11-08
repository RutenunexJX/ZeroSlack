#include "documentblockmodel.h"
#include "formattercursoranchor.h"
#include <QJsonArray>
#include <QTextDocument>
#include <QTextBlock>
#include <QUuid>
#include <QSet>
#include <algorithm>

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
void anchor(EditorAssociatedBlock& block, const QTextCursor& cursor) {
    block.returnPosition = cursor.position(); block.returnAnchor = cursor.anchor();
    if (cursor.hasSelection()) { block.start = cursor.selectionStart(); block.end = cursor.selectionEnd(); }
    else { block.start = cursor.block().position(); block.end = block.start + cursor.block().text().size(); }
    block.missing = false;
}
}
DocumentBlockModel* DocumentBlockModel::get(QTextDocument* document) {
    if (!document) return nullptr;
    auto* model = document->findChild<DocumentBlockModel*>("editorAssociations", Qt::FindDirectChildrenOnly);
    return model ? model : new DocumentBlockModel(document);
}
DocumentBlockModel::DocumentBlockModel(QTextDocument* value)
    : QObject(value), document(value), identity(newId()) {
    setObjectName("editorAssociations");
    connect(document, &QTextDocument::contentsChange, this, &DocumentBlockModel::sourceChanged);
    connect(document, &QTextDocument::contentsChanged, this, [this] {
        if (!replaying && !formatting && (!blocks.isEmpty() || !commands.isEmpty())) captureAnchors();
    });
}
const EditorAssociatedBlock* DocumentBlockModel::find(const QString& id) const {
    const auto it = std::find_if(blocks.cbegin(), blocks.cend(), [&](const auto& b) { return b.id == id; });
    return it == blocks.cend() ? nullptr : &*it;
}
void DocumentBlockModel::publish(bool previouslyDirty) {
    ++generation; emit changed(); emit historyChanged();
    if (previouslyDirty != dirty()) emit dirtyChanged(dirty());
}
void DocumentBlockModel::captureAnchors() {
    AnchorState state; state.stamp = stamp;
    for (const auto& block : blocks) state.entries.insert(block.id, block);
    textStates[document->availableUndoSteps()] = state;
}
void DocumentBlockModel::mutate(const QList<EditorAssociatedBlock>& next) {
    const bool wasDirty = dirty();
    commands.resize(applied);
    document->clearUndoRedoStacks(QTextDocument::RedoStack);
    commands.append({blocks, next, document->availableUndoSteps(), stamp, ++nextStamp});
    blocks = next; stamp = nextStamp; ++applied;
    // The next actual editor transaction must begin an explicit Qt group;
    // an empty begin/end here does not break Qt's implicit typing merge.
    textBarrier = true;
    captureAnchors(); publish(wasDirty);
}
QString DocumentBlockModel::insert(const QString& type, const QTextCursor& source, const QString& text) {
    EditorAssociatedBlock block; block.type = type; block.text = text; return insert(block, source);
}
QString DocumentBlockModel::insert(EditorAssociatedBlock block, const QTextCursor& source) {
    if (source.document() != document || !QStringList{"text", "image", "reference"}.contains(block.type)) return {};
    block.id = newId();
    anchor(block, source);
    for (const auto& other : blocks) if (other.end == block.end) block.order = qMax(block.order, other.order + 1);
    auto next = blocks; next.append(block); mutate(next); return block.id;
}
bool DocumentBlockModel::update(EditorAssociatedBlock value) {
    auto next = blocks;
    for (auto& block : next) if (block.id == value.id) {
        value.revision = block.revision + 1; block = value; mutate(next); return true;
    }
    return false;
}
bool DocumentBlockModel::remove(const QString& id) {
    auto next = blocks;
    const auto it = std::find_if(next.begin(), next.end(), [&](const auto& b) { return b.id == id; });
    if (it == next.end()) return false;
    next.erase(it); mutate(next); return true;
}
bool DocumentBlockModel::reorder(const QString& id, int direction) {
    auto next = blocks;
    std::stable_sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.end != b.end ? a.end < b.end : a.order < b.order; });
    for (int i = 0; i < next.size(); ++i) if (next[i].id == id) {
        const int j = i + (direction < 0 ? -1 : 1);
        if (j < 0 || j >= next.size() || next[i].end != next[j].end) return false;
        std::swap(next[i], next[j]);
        for (int n = 0; n < next.size(); ++n) next[n].order = n;
        mutate(next); return true;
    }
    return false;
}
bool DocumentBlockModel::reattach(const QString& id, const QTextCursor& source) {
    const auto* current = find(id); if (!current || source.document() != document) return false;
    auto value = *current; anchor(value, source); return update(value);
}
bool DocumentBlockModel::undoAssociation(bool redo) {
    endTextSession();
    const int steps = document->availableUndoSteps();
    if ((!redo && applied == 0) || (redo && applied == commands.size())) return false;
    const auto& command = commands[redo ? applied : applied - 1];
    if ((!redo && steps > command.textStep) || (redo && steps < command.textStep)) return false;
    if (steps != command.textStep) { setError("The text undo boundary changed. Association history cannot skip it."); return true; }
    const bool wasDirty = dirty(); blocks = redo ? command.after : command.before;
    stamp = redo ? command.afterStamp : command.beforeStamp; applied += redo ? 1 : -1;
    textBarrier = true;
    captureAnchors(); publish(wasDirty); return true;
}
bool DocumentBlockModel::canUndo() const { return applied > 0 || document->isUndoAvailable(); }
void DocumentBlockModel::beginTextSession(const QString& id) { if (editing != id) { editing = id; editingCommand = -1; } }
void DocumentBlockModel::endTextSession() { editing.clear(); editingCommand = -1; }
void DocumentBlockModel::editText(const QString& id, const QString& text) {
    const auto* existing = find(id); if (!existing || existing->text == text) return;
    beginTextSession(id); auto value = *existing; value.text = text;
    if (editingCommand >= 0 && editingCommand == applied - 1 && applied == commands.size()
        && commands[editingCommand].textStep == document->availableUndoSteps()) {
        const bool wasDirty = dirty();
        for (auto& block : blocks) if (block.id == id) { block.text = text; ++block.revision; }
        stamp = ++nextStamp; commands[editingCommand].after = blocks; commands[editingCommand].afterStamp = stamp;
        captureAnchors(); publish(wasDirty);
    } else {
        update(value); editingCommand = applied - 1;
    }
}
bool DocumentBlockModel::canRedo() const { return applied < commands.size() || document->isRedoAvailable(); }
void DocumentBlockModel::beginTextReplay() { replaying = true; }
void DocumentBlockModel::endTextReplay() {
    const bool wasDirty = dirty();
    const auto it = textStates.constFind(document->availableUndoSteps());
    if (it != textStates.cend()) {
        for (auto& block : blocks) {
            const auto found = it->entries.constFind(block.id);
            if (found == it->entries.cend()) continue;
            block.start = found->start; block.end = found->end;
            block.returnAnchor = found->returnAnchor; block.returnPosition = found->returnPosition; block.missing = found->missing;
            ++block.revision;
        }
        stamp = it->stamp;
    }
    replaying = false; publish(wasDirty);
}
void DocumentBlockModel::sourceChanged(int position, int removed, int added) {
    if (formatting || blocks.isEmpty() || (removed == 0 && added == 0)) return;
    const bool wasDirty = dirty(); bool changed = false;
    const int end = position + removed, delta = added - removed;
    auto remap = [&](int value) { return value >= end ? value + delta : value > position ? position + added : value; };
    if (!replaying) {
        commands.resize(applied);
        auto it = textStates.upperBound(document->availableUndoSteps());
        while (it != textStates.end()) it = textStates.erase(it);
    }
    for (auto& block : blocks) {
        if (block.missing) continue;
        if (removed > 0 && position <= block.start && end >= block.end && (block.start < end)) block.missing = true;
        const int start = remap(block.start), finish = remap(block.end);
        if (block.missing || start != block.start || finish != block.end) ++block.revision;
        changed |= block.missing || start != block.start || finish != block.end;
        block.start = qMax(0, start); block.end = qMax(block.start, finish);
        block.returnPosition = qMax(0, remap(block.returnPosition)); block.returnAnchor = qMax(0, remap(block.returnAnchor));
    }
    if (changed) { stamp = ++nextStamp; publish(wasDirty); }
}
void DocumentBlockModel::beginFormatting() {
    if (blocks.isEmpty()) return;
    endTextSession(); captureAnchors(); formatting = true;
}
void DocumentBlockModel::endFormatting(const FormatterTriviaPositionMapper& mapper, bool succeeded) {
    if (!formatting) return;
    formatting = false;
    if (!succeeded) return;
    const bool wasDirty = dirty();
    commands.resize(applied);
    for (auto& block : blocks) {
        if (block.missing) continue;
        const int start = mapper.mapBoundary(block.start);
        const int end = mapper.mapBoundary(block.end, true);
        const int position = mapper.mapBoundary(block.returnPosition, block.returnPosition > block.returnAnchor);
        const int anchor = mapper.mapBoundary(block.returnAnchor, block.returnAnchor > block.returnPosition);
        if (start < 0 || end < start || position < 0 || anchor < 0) block.missing = true;
        else { block.start = start; block.end = end; block.returnPosition = position; block.returnAnchor = anchor; }
        ++block.revision;
    }
    stamp = ++nextStamp; captureAnchors(); publish(wasDirty);
}
void DocumentBlockModel::markSaved() { const bool wasDirty = dirty(); savedBlocks = blocks; savedStamp = stamp; if (wasDirty) emit dirtyChanged(false); }
void DocumentBlockModel::markUnsaved() { const bool wasDirty = dirty(); stamp = ++nextStamp; captureAnchors(); publish(wasDirty); }
void DocumentBlockModel::discard() { const bool wasDirty = dirty(); blocks = savedBlocks; stamp = savedStamp; commands.clear(); applied = 0; textStates.clear(); captureAnchors(); publish(wasDirty); }
void DocumentBlockModel::resetHistory(const QString& reason) {
    commands.clear(); applied = 0; textStates.clear(); captureAnchors();
    if (!blocks.isEmpty()) setError(reason); emit historyChanged();
}
void DocumentBlockModel::setError(const QString& message) { if (failure == message) return; failure = message; emit errorChanged(message); }
void DocumentBlockModel::cloneIdentity() { identity = newId(); stamp = ++nextStamp; }
QJsonObject DocumentBlockModel::serialize() const {
    QJsonArray array;
    for (const auto& b : blocks) array.append(QJsonObject{{"id", b.id}, {"type", b.type}, {"text", b.text}, {"uri", b.uri}, {"asset", b.asset}, {"caption", b.caption},
        {"start", b.start}, {"end", b.end}, {"returnPosition", b.returnPosition}, {"returnAnchor", b.returnAnchor}, {"order", b.order},
        {"width", b.width}, {"height", b.height}, {"missing", b.missing}, {"collapsed", b.collapsed}});
    return {{"version", 1}, {"documentId", identity}, {"blocks", array}};
}
bool DocumentBlockModel::restore(const QJsonObject& value, QString* error) {
    auto fail = [&](const QString& message) { if (error) *error = message; setError(message); return false; };
    if (value.value("version").toInt() != 1 || QUuid(value.value("documentId").toString()).isNull() || !value.value("blocks").isArray())
        return fail("Unsupported or corrupt editor association data. The original file was preserved.");
    QList<EditorAssociatedBlock> next; QSet<QString> ids;
    for (const auto& entry : value.value("blocks").toArray()) {
        if (!entry.isObject()) return fail("Invalid editor block entry.");
        const auto o = entry.toObject(); EditorAssociatedBlock b;
        b.id = o.value("id").toString(); b.type = o.value("type").toString();
        if (QUuid(b.id).isNull() || ids.contains(b.id) || !QStringList{"text", "image", "reference"}.contains(b.type)) return fail("Invalid block identity or type.");
        for (const auto& key : {"start","end","returnPosition","returnAnchor","order","width","height"})
            if (!o.value(key).isDouble() || o.value(key).toDouble() != o.value(key).toInt(-1) || o.value(key).toInt(-1) < 0) return fail("Invalid numeric block field.");
        for (const auto& key : {"text","uri","asset","caption"}) if (!o.value(key).isString()) return fail("Invalid block content field.");
        for (const auto& key : {"missing","collapsed"}) if (!o.value(key).isBool()) return fail("Invalid block display state.");
        ids.insert(b.id); b.text = o.value("text").toString(); b.uri = o.value("uri").toString(); b.asset = o.value("asset").toString(); b.caption = o.value("caption").toString();
        b.start = o.value("start").toInt(-1); b.end = o.value("end").toInt(-1);
        if (b.start < 0 || b.end < b.start) return fail("Invalid block anchor.");
        b.returnPosition = o.value("returnPosition").toInt(b.start); b.returnAnchor = o.value("returnAnchor").toInt(b.start);
        b.order = o.value("order").toInt(); b.width = qBound(120, o.value("width").toInt(480), 1600); b.height = qBound(60, o.value("height").toInt(160), 600);
        b.missing = o.value("missing").toBool() || b.end >= document->characterCount(); b.collapsed = o.value("collapsed").toBool(); next.append(b);
    }
    const bool wasDirty = dirty(); blocks = next; identity = value.value("documentId").toString(); stamp = ++nextStamp;
    commands.clear(); applied = 0; textStates.clear(); captureAnchors(); markSaved(); setError({}); publish(wasDirty); return true;
}
