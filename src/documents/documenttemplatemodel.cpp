#include "documenttemplatemodel.h"
#include "editorfileidentity.h"
#include "templateinputservice.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTextDocument>
#include <QUuid>
#include <algorithm>

namespace {
QByteArray digest(QTextDocument *text) {
  return QCryptographicHash::hash(text->toPlainText().toUtf8(),
                                  QCryptographicHash::Sha256)
      .toHex();
}
bool intersects(int start, int end, int otherStart, int otherEnd) {
  return start == end ? start > otherStart && start < otherEnd
                      : start < otherEnd && end > otherStart;
}
void shift(int &start, int &length, int position, int removed, int added,
           bool grows) {
  const int end = position + removed, delta = added - removed;
  if (grows && position >= start && end <= start + length)
    length += delta;
  else if (start >= end)
    start += delta;
}
} // namespace

DocumentTemplateModel *DocumentTemplateModel::get(QTextDocument *document) {
  if (!document)
    return nullptr;
  if (auto *existing = document->findChild<DocumentTemplateModel *>(
          QString(), Qt::FindDirectChildrenOnly))
    return existing;
  return new DocumentTemplateModel(document);
}
DocumentTemplateModel::DocumentTemplateModel(QTextDocument *document)
    : QObject(document), text(document) {
  remember();
  connect(text, &QTextDocument::contentsChange, this,
          [this](int position, int removed, int added) {
            if (changing() || (!removed && !added))
              return;
            pendingRecord = true;
            QString owner;
            int input = -1, best = -1;
            for (const auto &item : std::as_const(items)) {
              const int candidate =
                  inputAt(item.id, position, position + removed);
              if (candidate >= 0 && depth(item.id) > best) {
                owner = item.id;
                input = candidate;
                best = depth(item.id);
              }
            }
            applyChange(position, removed, added, owner, input);
          });
  connect(text, &QTextDocument::contentsChanged, this, [this] {
    if (!changing() && pendingRecord) {
      pendingRecord = false;
      remember();
    }
  });
}
const TemplateInstance *
DocumentTemplateModel::instance(const QString &id) const {
  for (const auto &item : items)
    if (item.id == id)
      return &item;
  return nullptr;
}
bool DocumentTemplateModel::ancestor(const QString &parent,
                                     const QString &child) const {
  auto *value = instance(child);
  for (int i = 0; value && i < items.size(); ++i) {
    if (value->parentId == parent)
      return true;
    value = instance(value->parentId);
  }
  return false;
}
int DocumentTemplateModel::depth(const QString &id) const {
  int result = 0;
  auto *value = instance(id);
  while (value && !value->parentId.isEmpty() && result < items.size()) {
    ++result;
    value = instance(value->parentId);
  }
  return result;
}
bool DocumentTemplateModel::allows(const QString &id, int input, int start,
                                   int end) const {
  const auto *item = instance(id);
  if (!item || input < 0 || input >= item->inputs.size() || start > end)
    return false;
  const auto &slot = item->inputs.at(input);
  if (start < slot.start || end > slot.start + slot.length)
    return false;
  for (const auto &child : items) {
    if (child.id == id)
      continue;
    for (const auto &fragment : child.fragments) {
      if (!intersects(start, end, fragment.start, fragment.end()))
        continue;
      if (ancestor(child.id, id))
        continue;
      return false;
    }
  }
  return true;
}
int DocumentTemplateModel::inputAt(const QString &id, int start,
                                   int end) const {
  const auto *value = instance(id);
  if (value)
    for (int i = 0; i < value->inputs.size(); ++i)
      if (allows(id, i, start, end))
        return i;
  return -1;
}
QPair<int, int> DocumentTemplateModel::inputSegment(const QString &id,
                                                    int input,
                                                    int position) const {
  if (!allows(id, input, position, position))
    return {-1, -1};
  const auto &slot = instance(id)->inputs.at(input);
  int start = slot.start, end = start + slot.length;
  for (const auto &child : items)
    if (child.parentId == id && child.parentSlot == slot.name) {
      for (const auto &fragment : child.fragments) {
        if (fragment.auxiliary)
          continue;
        if (fragment.end() <= position)
          start = qMax(start, fragment.end());
        else if (fragment.start >= position)
          end = qMin(end, fragment.start);
      }
    }
  return {start, end};
}
QString DocumentTemplateModel::containerAt(int position, QString *name) const {
  QString result;
  int best = -1;
  for (const auto &item : items) {
    for (int i = 0; i < item.inputs.size(); ++i) {
      const auto &slot = item.inputs.at(i);
      if (slot.input.value("kind").toString() == "Code" &&
          allows(item.id, i, position, position) && depth(item.id) > best) {
        result = item.id;
        best = depth(item.id);
        if (name)
          *name = slot.name;
      }
    }
  }
  return result;
}
bool DocumentTemplateModel::canInsert(int position, int removed,
                                      const QString &parentId,
                                      const QString &parentSlot) const {
  if (position < 0 || removed < 0 ||
      position + removed >= text->characterCount())
    return false;
  if (!parentId.isEmpty()) {
    const auto *parent = instance(parentId);
    if (!parent)
      return false;
    for (int i = 0; i < parent->inputs.size(); ++i) {
      const auto &slot = parent->inputs.at(i);
      if (slot.name == parentSlot &&
          slot.input.value("kind").toString() == "Code")
        return allows(parentId, i, position, position + removed);
    }
    return false;
  }
  for (const auto &item : items)
    for (const auto &fragment : item.fragments)
      if (intersects(position, position + removed, fragment.start,
                     fragment.end()))
        return false;
  return true;
}
void DocumentTemplateModel::removeTree(const QString &id) {
  QSet<QString> removed{id};
  bool more = true;
  while (more) {
    more = false;
    for (const auto &item : std::as_const(items))
      if (removed.contains(item.parentId) && !removed.contains(item.id)) {
        removed.insert(item.id);
        more = true;
      }
  }
  items.removeIf([&](const auto &item) { return removed.contains(item.id); });
}
void DocumentTemplateModel::applyChange(int position, int removed, int added,
                                        const QString &owner, int input) {
  const auto *owning = instance(owner);
  const QString inputName =
      owning && input >= 0 && input < owning->inputs.size()
          ? owning->inputs.at(input).name
          : QString();
  QStringList affected;
  for (const auto &item : std::as_const(items)) {
    const bool grows = item.id == owner || ancestor(item.id, owner);
    if (grows)
      continue;
    for (const auto &fragment : item.fragments)
      if (intersects(position, position + removed, fragment.start,
                     fragment.end())) {
        affected << item.id;
        break;
      }
  }
  for (const auto &id : affected)
    removeTree(id);
  if (!affected.isEmpty())
    problem =
        QStringLiteral("Template boundaries changed; affected instances are "
                       "read-only in template mode until inserted again.");
  for (auto &item : items) {
    const bool grows = item.id == owner || ancestor(item.id, owner);
    for (auto &fragment : item.fragments)
      shift(fragment.start, fragment.length, position, removed, added, grows);
    for (auto &slot : item.inputs)
      shift(slot.start, slot.length, position, removed, added,
            grows && (item.id != owner || slot.name == inputName));
  }
  if (!manual)
    emit changed();
}
void DocumentTemplateModel::remember() {
  const int step = text->availableUndoSteps();
  auto it = history.upperBound(step);
  while (it != history.end())
    it = history.erase(it);
  history[step] = items;
}
void DocumentTemplateModel::beginChange() {
  if (!manual)
    remember();
  ++manual;
}
QString DocumentTemplateModel::add(TemplateInstance value) {
  if (value.id.isEmpty())
    value.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  value.fixedHash = fixedDigest(value);
  const auto id = value.id;
  items << std::move(value);
  problem.clear();
  return id;
}
void DocumentTemplateModel::endChange() {
  if (manual > 0 && --manual == 0) {
    remember();
    emit changed();
  }
}
bool DocumentTemplateModel::replaceInput(const QString &id, int input,
                                         const QTextCursor &cursor,
                                         const QString &value,
                                         int *nextPosition, bool candidateSelection) {
  if (!allows(id, input, cursor.selectionStart(), cursor.selectionEnd()))
    return false;
  const auto original = *instance(id);
  const auto slot = original.inputs.at(input);
  if (slot.input.value("closedChoice").toBool()) {
    if (!candidateSelection || cursor.selectionStart() != slot.start ||
        cursor.selectionEnd() != slot.start + slot.length) return false;
    QHash<QString, QString> bindings;
    for (const auto &s : original.inputs)
      bindings[s.name] = text->toPlainText().mid(s.start, s.length);
    const TemplateInputContext empty;
    if (!TemplateInputService::validate(slot.input, value,
          slot.inputContext ? *slot.inputContext : empty, bindings).isEmpty()) return false;
  }
  const bool code = slot.input.value("kind").toString() == "Code";
  const int relative = cursor.selectionStart() - slot.start,
            removed = cursor.selectionEnd() - cursor.selectionStart();
  QList<int> targets;
  for (int i = 0; i < original.inputs.size(); ++i) {
    const auto &linked = original.inputs.at(i);
    if (i == input ||
        (!code && slot.tabStop >= 0 && linked.tabStop == slot.tabStop)) {
      if (!allows(id, i, linked.start + relative,
                  linked.start + relative + removed))
        return false;
      targets << i;
    }
  }
  std::sort(targets.begin(), targets.end(), [&](int a, int b) {
    return original.inputs.at(a).start > original.inputs.at(b).start;
  });
  beginChange();
  QTextCursor transaction(text);
  transaction.beginEditBlock();
  for (int target : targets) {
    const auto current = instance(id)->inputs.at(target);
    QTextCursor edit(text);
    edit.setPosition(current.start + relative);
    edit.setPosition(current.start + relative + removed,
                     QTextCursor::KeepAnchor);
    edit.insertText(value);
    applyChange(current.start + relative, removed, value.size(), id, target);
  }
  transaction.endEditBlock();
  if (nextPosition)
    *nextPosition =
        instance(id)->inputs.at(input).start + relative + value.size();
  endChange();
  return true;
}
bool DocumentTemplateModel::applyEdits(QList<TemplateDocumentEdit> edits,
                                       QString *selectedId, QString *error) {
  const auto fail = [&](const QString &reason) {
    if (error) *error = reason;
    return false;
  };
  if (edits.isEmpty()) return fail("No template changes were prepared.");
  QSet<QString> replaced;
  for (const auto &edit : edits) {
    if (!edit.replaceId.isEmpty()) {
      const auto *old = instance(edit.replaceId);
      if (!old || replaced.contains(old->id) || old->fragments.size() != 1 ||
          old->fragments.first().start != edit.start ||
          old->fragments.first().length != edit.length ||
          old->parentId != edit.parentId || old->parentSlot != edit.parentSlot ||
          old->fixedHash != fixedDigest(*old))
        return fail("The instance changed; reopen its inputs.");
      for (const auto &child : items)
        if (child.parentId == old->id)
          return fail("An instance containing child templates cannot be replaced.");
      replaced.insert(old->id);
    }
  }
  // Check against a copy without the explicitly replaced leaves. Text and undo
  // history stay untouched until every target and relative range is validated.
  const auto saved = items;
  items.removeIf([&](const auto &item) { return replaced.contains(item.id); });
  bool allowed = true;
  for (int i = 0; i < edits.size(); ++i) {
    const auto &edit = edits[i];
    allowed &= canInsert(edit.start, edit.length, edit.parentId, edit.parentSlot);
    for (int j = 0; j < i; ++j)
      allowed &= !intersects(edit.start, edit.start + edit.length,
                             edits[j].start, edits[j].start + edits[j].length)
                 && edit.start != edits[j].start;
    for (const auto &slot : edit.inputs)
      allowed &= slot.start >= 0 && slot.length >= 0 &&
                 slot.start + slot.length <= edit.text.size();
    allowed &= TemplateInputService::dependencyError(edit.inputs).isEmpty();
    allowed &= edit.text.isEmpty() || !edit.instance.snapshot.isEmpty();
  }
  items = saved;
  if (!allowed) return fail("A prepared template target is no longer editable.");
  const int selectedStart = edits.first().start;
  std::stable_sort(edits.begin(), edits.end(), [](const auto &a, const auto &b) {
    return a.start > b.start;
  });
  beginChange();
  items.removeIf([&](const auto &item) { return replaced.contains(item.id); });
  QTextCursor transaction(text);
  transaction.beginEditBlock();
  for (const auto &edit : edits) {
    int input = -1;
    if (const auto *parent = instance(edit.parentId))
      for (int i = 0; i < parent->inputs.size(); ++i)
        if (parent->inputs[i].name == edit.parentSlot) input = i;
    QTextCursor cursor(text);
    cursor.setPosition(edit.start);
    cursor.setPosition(edit.start + edit.length, QTextCursor::KeepAnchor);
    cursor.insertText(edit.text);
    applyChange(edit.start, edit.length, edit.text.size(), edit.parentId, input);
    if (!edit.text.isEmpty()) {
      TemplateInstance value;
      value.id = edit.replaceId;
      value.parentId = edit.parentId;
      value.parentSlot = edit.parentSlot;
      value.label = edit.instance.label;
      value.snapshot = edit.instance.snapshot;
      value.fragments = {{edit.start, int(edit.text.size()), false}};
      value.inputs = edit.inputs;
      for (auto &slot : value.inputs) slot.start += edit.start;
      const auto id = add(value);
      if (selectedId && edit.start == selectedStart) *selectedId = id;
    }
  }
  transaction.endEditBlock();
  endChange();
  if (error) error->clear();
  return true;
}

void DocumentTemplateModel::beginReplay() {
  if (!replay) {
    history[text->availableUndoSteps()] = items;
    replay = true;
  }
}
void DocumentTemplateModel::endReplay() {
  if (!replay)
    return;
  replay = false;
  const auto found = history.constFind(text->availableUndoSteps());
  items = found == history.cend() ? QList<TemplateInstance>() : found.value();
  if (!valid(items))
    items.clear();
  emit changed();
}
void DocumentTemplateModel::invalidate(const QString &reason) {
  items.clear();
  history.clear();
  problem = reason;
  remember();
  emit changed();
}
void DocumentTemplateModel::setInputContext(
    const QString &id, std::shared_ptr<const TemplateInputContext> context) {
  for (auto &item : items)
    if (item.id == id)
      for (auto &slot : item.inputs)
        slot.inputContext = context;
}
QJsonObject DocumentTemplateModel::serialize(const QString &fileName) const {
  QJsonArray instances;
  for (const auto &item : items) {
    QJsonArray fragments, inputs;
    for (const auto &f : item.fragments)
      fragments << QJsonObject{
          {"start", f.start}, {"length", f.length}, {"auxiliary", f.auxiliary}};
    for (const auto &s : item.inputs)
      inputs << QJsonObject{{"name", s.name},
                            {"start", s.start},
                            {"length", s.length},
                            {"tabStop", s.tabStop},
                            {"visible", s.visibleWhenEmpty},
                            {"input", QJsonObject::fromVariantMap(s.input)}};
    instances << QJsonObject{
        {"id", item.id},
        {"parent", item.parentId},
        {"parentSlot", item.parentSlot},
        {"label", item.label},
        {"snapshot", QJsonObject::fromVariantMap(item.snapshot)},
        {"fixedHash", item.fixedHash},
        {"fragments", fragments},
        {"inputs", inputs}};
  }
  return {{"version", 1},
          {"file", EditorFileIdentity::lookupKey(fileName)},
          {"textSha256", QString::fromLatin1(digest(text))},
          {"instances", instances}};
}
QString
DocumentTemplateModel::fixedDigest(const TemplateInstance &value) const {
  QJsonArray fixed, declarations;
  auto inputs = value.inputs;
  std::sort(inputs.begin(), inputs.end(),
            [](const auto &a, const auto &b) { return a.start < b.start; });
  const auto read = [this](int start, int end) {
    QTextCursor cursor(text);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    return cursor.selectedText();
  };
  for (const auto &fragment : value.fragments) {
    int position = fragment.start;
    QJsonArray pieces;
    for (const auto &input : inputs)
      if (input.start >= fragment.start &&
          input.start + input.length <= fragment.end()) {
        pieces << read(position, input.start);
        position = input.start + input.length;
      }
    pieces << read(position, fragment.end());
    fixed << QJsonObject{{"auxiliary", fragment.auxiliary}, {"pieces", pieces}};
  }
  for (const auto &input : inputs)
    declarations << QJsonObject{
        {"name", input.name},
        {"stop", input.tabStop},
        {"input", QJsonObject::fromVariantMap(input.input)}};
  const QJsonObject identity{
      {"fixed", fixed},
      {"inputs", declarations},
      {"snapshot", QJsonObject::fromVariantMap(value.snapshot)}};
  return QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument(identity).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha256)
          .toHex());
}
bool DocumentTemplateModel::valid(const QList<TemplateInstance> &values) const {
  const int limit = text->characterCount() - 1;
  QHash<QString, const TemplateInstance *> ids;
  for (const auto &item : values) {
    if (item.id.isEmpty() || ids.contains(item.id) ||
        item.fragments.isEmpty() || item.snapshot.isEmpty())
      return false;
    ids[item.id] = &item;
    for (int i = 0; i < item.fragments.size(); ++i) {
      const auto &f = item.fragments[i];
      if (f.start < 0 || f.length <= 0 || qint64(f.start) + f.length > limit)
        return false;
      for (int j = 0; j < i; ++j)
        if (intersects(f.start, f.end(), item.fragments[j].start,
                       item.fragments[j].end()))
          return false;
    }
    for (const auto &s : item.inputs) {
      bool owned = false;
      if (s.start < 0 || s.length < 0 || qint64(s.start) + s.length > limit ||
          s.name.isEmpty())
        return false;
      for (const auto &f : item.fragments)
        if (s.start >= f.start && s.start + s.length <= f.end())
          owned = true;
      if (!owned)
        return false;
    }
    for (int i = 0; i < item.inputs.size(); ++i)
      for (int j = 0; j < i; ++j) {
        const auto &a = item.inputs[i];
        const auto &b = item.inputs[j];
        if (intersects(a.start, a.start + a.length, b.start,
                       b.start + b.length))
          return false;
      }
    if (!TemplateInputService::dependencyError(item.inputs).isEmpty() ||
        item.fixedHash != fixedDigest(item))
      return false;
  }
  for (const auto &item : values) {
    QSet<QString> visited{item.id};
    auto parentId = item.parentId;
    while (!parentId.isEmpty()) {
      if (!ids.contains(parentId) || visited.contains(parentId))
        return false;
      visited.insert(parentId);
      parentId = ids[parentId]->parentId;
    }
    if (item.parentId.isEmpty())
      continue;
    const auto *parent = ids[item.parentId];
    bool contained = false;
    for (const auto &slot : parent->inputs) {
      if (slot.name != item.parentSlot ||
          slot.input.value("kind").toString() != "Code")
        continue;
      contained = true;
      for (const auto &f : item.fragments)
        if (!f.auxiliary &&
            (f.start < slot.start || f.end() > slot.start + slot.length))
          return false;
    }
    if (!contained)
      return false;
  }
  const auto isAncestor = [&](const QString &ancestor, QString child) {
    while (ids.contains(child)) {
      child = ids[child]->parentId;
      if (child == ancestor)
        return true;
    }
    return false;
  };
  for (int i = 0; i < values.size(); ++i)
    for (int j = 0; j < i; ++j) {
      const auto &a = values[i];
      const auto &b = values[j];
      for (const auto &af : a.fragments)
        for (const auto &bf : b.fragments) {
          if (!intersects(af.start, af.end(), bf.start, bf.end()))
            continue;
          if (isAncestor(a.id, b.id) && af.start <= bf.start &&
              af.end() >= bf.end() && !bf.auxiliary)
            continue;
          if (isAncestor(b.id, a.id) && bf.start <= af.start &&
              bf.end() >= af.end() && !af.auxiliary)
            continue;
          return false;
        }
    }
  return true;
}
bool DocumentTemplateModel::restore(const QJsonObject &object,
                                    const QString &fileName) {
  if (object.isEmpty()) {
    invalidate({});
    return true;
  }
  const auto fail = [this] {
    invalidate(QStringLiteral(
        "Template metadata does not match this document; source text was "
        "preserved and template editing is disabled for unrecognized ranges."));
    return false;
  };
  if (object.value("version").toInt() != 1 ||
      object.value("file").toString() !=
          EditorFileIdentity::lookupKey(fileName) ||
      object.value("textSha256").toString().toLatin1() != digest(text) ||
      !object.value("instances").isArray())
    return fail();
  QList<TemplateInstance> restored;
  for (const auto &entry : object.value("instances").toArray()) {
    const auto o = entry.toObject();
    TemplateInstance item;
    item.id = o.value("id").toString();
    item.parentId = o.value("parent").toString();
    item.parentSlot = o.value("parentSlot").toString();
    item.label = o.value("label").toString();
    item.snapshot = o.value("snapshot").toObject().toVariantMap();
    item.fixedHash = o.value("fixedHash").toString();
    if (!o.value("fragments").isArray() || !o.value("inputs").isArray())
      return fail();
    for (const auto &f : o.value("fragments").toArray())
      item.fragments << TemplateFragment{
          f.toObject().value("start").toInt(-1),
          f.toObject().value("length").toInt(-1),
          f.toObject().value("auxiliary").toBool()};
    for (const auto &s : o.value("inputs").toArray()) {
      const auto so = s.toObject();
      item.inputs << CodeTemplateSlot{
          so.value("name").toString(),
          so.value("start").toInt(-1),
          so.value("length").toInt(-1),
          so.value("tabStop").toInt(-1),
          so.value("visible").toBool(),
          so.value("input").toObject().toVariantMap()};
    }
    restored << item;
  }
  if (!valid(restored))
    return fail();
  items = restored;
  problem.clear();
  history.clear();
  remember();
  emit changed();
  return true;
}
