#include "documenttemplatemodel.h"
#include "editorfileidentity.h"
#include "editorruntime.h"
#include "mycodeeditor.h"
#include "templateinputservice.h"
#include "usertemplateservice.h"
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <algorithm>

bool MyCodeEditor::documentReadOnly() const {
  return authoringLock ? sourceReadOnlyState : QPlainTextEdit::isReadOnly();
}
void MyCodeEditor::clearTemplatePreedit() {
  if (templatePreeditBlock.isValid() && templatePreeditBlock.layout())
    templatePreeditBlock.layout()->setPreeditArea(-1, {});
  templatePreeditBlock = {};
  viewport()->update();
}
void MyCodeEditor::setReadOnly(bool value) {
  if (value && !documentReadOnly())
    emit editingInteractionEnded();
  sourceReadOnlyState = value;
  QPlainTextEdit::setReadOnly(value || authoringLock);
}
bool MyCodeEditor::templateAuthoringLocked() const { return authoringLock; }
bool MyCodeEditor::templateInputEditing() const {
  return templateEditing && !activeTemplate.isEmpty();
}
bool MyCodeEditor::templateSelectionEditable() const {
  return templateInputEditing() && !documentReadOnly() &&
         templateModel()->inputAt(activeTemplate, textCursor().selectionStart(),
                                  textCursor().selectionEnd()) >= 0;
}
bool MyCodeEditor::focusNextPrevChild(bool next) {
  if (authoringLock && templateInputEditing())
    return false;
  return QPlainTextEdit::focusNextPrevChild(next);
}
QString MyCodeEditor::selectedTemplateId() const { return activeTemplate; }
DocumentTemplateModel *MyCodeEditor::templateModel() const {
  return DocumentTemplateModel::get(document());
}
void MyCodeEditor::bindTemplateModel() {
  clearTemplatePreedit();
  QObject::disconnect(templateModelConnection);
  activeTemplate.clear();
  activeTemplateRegion.clear();
  templateTargetKind = TemplateTargetKind::Instance;
  templateEditing = false;
  templateModelConnection =
      connect(templateModel(), &DocumentTemplateModel::changed, this, [this] {
        if (!templateMutation && templateEditing)
          refreshTemplateSlotRanges();
        if (!activeTemplate.isEmpty() &&
            !templateModel()->instance(activeTemplate)) {
          activeTemplate.clear();
          activeTemplateRegion.clear();
          templateTargetKind = TemplateTargetKind::Instance;
          templateEditing = false;
          state->templateSlots.clear(this);
        }
        updateTemplatePresentation();
        viewport()->update();
      });
  updateTemplatePresentation();
}
void MyCodeEditor::updateTemplatePresentation() {
  if (templateInputEditing()) {
    activeTemplateRegion = selectedTemplateRegion();
    const auto* item = templateModel()->instance(activeTemplate);
    const bool container = item && std::any_of(item->inputs.begin(), item->inputs.end(), [](const auto& input) {
      return input.input.value("kind").toString() == "Code";
    });
    templateTargetKind = !activeTemplateRegion.isEmpty() ? TemplateTargetKind::Handwriting
        : container ? TemplateTargetKind::Inputs : TemplateTargetKind::Instance;
  }
  updateTemplateRegionBar();
  if (!authoringLock)
    return;
  if (!state->modes.isActive(EditorModeId::TemplateAuthoring))
    state->modes.enter(EditorModeId::TemplateAuthoring,
                       EditorModeEntryReason::Restore);
  const QString label = templateTargetPath();
  state->modes.updatePresentation(
      EditorModeId::TemplateAuthoring,
      QStringLiteral("Template mode · %1 · %2")
          .arg(label, templateSearching ? QStringLiteral("Search")
                      : templateEditing ? QStringLiteral("Inputs")
                                        : QStringLiteral("Navigate")),
      QStringLiteral(
          "Enter: enter · Up/Down: siblings · Ctrl+Enter: parent · Esc: exit"));
}
void MyCodeEditor::setTemplateAuthoringLocked(bool locked) {
  if (locked == authoringLock)
    return;
  emit editingInteractionEnded();
  if (locked) {
    sourceReadOnlyState = QPlainTextEdit::isReadOnly();
    state->exitInteractionModes(EditorModeExitReason::ExternalControl);
    authoringLock = true;
    templateEditing = false;
    activeTemplateRegion.clear();
    templateTargetKind = TemplateTargetKind::Instance;
    QPlainTextEdit::setReadOnly(true);
    const auto id = templateModel()->containerAt(textCursor().position());
    if (!id.isEmpty())
      activeTemplate = id;
    if (!templateModel()->issue().isEmpty())
      emit editorStatusMessageRequested(templateModel()->issue());
    updateTemplatePresentation();
  } else {
    clearTemplatePreedit();
    const auto issue = state->templateSlots.currentValidationError(this);
    authoringLock = false;
    templateEditing = false;
    templateSearching = false;
    state->templateSlots.clear(this);
    state->modes.exit(EditorModeId::TemplateAuthoring,
                      EditorModeExitReason::Completed);
    QPlainTextEdit::setReadOnly(sourceReadOnlyState);
    if (!issue.isEmpty())
      emit editorStatusMessageRequested(issue);
  }
  updateTemplateRegionBar();
  viewport()->update();
}
void MyCodeEditor::setTemplateSearchActive(bool active) {
  templateSearching = active;
  updateTemplatePresentation();
}
bool MyCodeEditor::editTemplateInstance(const QString &requested,
                                        const QString &inputName,
                                        int position) {
  if (documentReadOnly())
    return false;
  if (!requested.isEmpty())
    activeTemplate = requested;
  activeTemplateRegion.clear();
  const auto *item = templateModel()->instance(activeTemplate);
  if (!item)
    return false;
  if (item->inputs.isEmpty()) {
    templateEditing = false;
    updateTemplatePresentation();
    return true;
  }
  auto inputs = std::make_shared<TemplateInputContext>();
  // Reopened instances use the current semantic service and role configuration.
  // No serialized semantic snapshot or old asynchronous callback is revived.
  const auto location = editorSemanticContextForPosition(-1, false);
  inputs->snapshot = SemanticIndex::getInstance()->snapshot();
  inputs->location.fileName = documentFileName();
  inputs->location.moduleName = location.moduleName;
  inputs->location.packageName = location.packageName;
  inputs->location.cursorLine = location.cursorLine;
  inputs->location.cursorPosition = textCursor().position();
  configureTemplateInputContext(inputs, activeTemplate);
  templateModel()->setInputContext(activeTemplate, inputs);
  templateEditing = true;
  const auto copy = *templateModel()->instance(activeTemplate);
  state->templateSlots.start(this, 0, document()->characterCount() - 1,
                             copy.inputs);
  int group = 0;
  QList<int> stops;
  for (const auto &input : copy.inputs)
    if (!stops.contains(input.tabStop))
      stops << input.tabStop;
  std::sort(stops.begin(), stops.end());
  for (const auto &input : copy.inputs)
    if (input.name == inputName) {
      group = stops.indexOf(input.tabStop);
      break;
    }
  if (!inputName.isEmpty())
    state->templateSlots.selectIndex(this, qMax(0, group));
  if (position >= 0) {
    QTextCursor cursor(document());
    cursor.setPosition(qBound(0, position, document()->characterCount() - 1));
    setTextCursor(cursor);
  }
  setAttribute(Qt::WA_InputMethodEnabled, true);
  updateTemplatePresentation();
  return true;
}
void MyCodeEditor::refreshTemplateSlotRanges() {
  const auto *item = templateModel()->instance(activeTemplate);
  if (!item || !templateEditing)
    return;
  const bool restoreRegion = !state->templateSlots.active() && !activeTemplateRegion.isEmpty();
  state->templateSlots.syncPersistentRanges(this, item->inputs);
  if (restoreRegion) {
    QList<int> stops;
    for (const auto& slot : item->inputs) if (!stops.contains(slot.tabStop)) stops << slot.tabStop;
    std::sort(stops.begin(), stops.end());
    for (const auto& slot : item->inputs) if (slot.name == activeTemplateRegion) {
      state->templateSlots.selectIndex(this, stops.indexOf(slot.tabStop));
      break;
    }
  }
}
void MyCodeEditor::finishTemplateInput() {
  if (!templateInputEditing()) {
    leaveTemplateTarget();
    return;
  }
  emit editingInteractionEnded();
  clearTemplatePreedit();
  const auto *item = templateModel()->instance(activeTemplate);
  const auto issue = item && item->snapshot.value("requireValidCommit").toBool()
      ? state->templateSlots.allValidationError(this) : state->templateSlots.currentValidationError(this);
  if (item && item->snapshot.value("requireValidCommit").toBool() && !issue.isEmpty()) {
    emit editorStatusMessageRequested(issue);
    return;
  }
  const QString region = selectedTemplateRegion();
  const bool container = item && std::any_of(item->inputs.begin(), item->inputs.end(), [](const auto& slot) {
    return slot.input.value("kind").toString() == "Code";
  });
  selectTemplateTarget({activeTemplate, region, !region.isEmpty() ? TemplateTargetKind::Handwriting
      : container ? TemplateTargetKind::Inputs : TemplateTargetKind::Instance});
  if (!issue.isEmpty())
    emit editorStatusMessageRequested(issue);
}
void MyCodeEditor::indentTemplateContinuation(const QString &indent) {
  auto cursor = textCursor();
  auto *model = templateModel();
  const int input = model->inputAt(activeTemplate, cursor.position(), cursor.position());
  const auto *item = model->instance(activeTemplate);
  if (documentReadOnly() || !item || input < 0 ||
      item->inputs.at(input).input.value("kind").toString() != "Code")
    return;
  const auto segment = model->inputSegment(activeTemplate, input, cursor.position());
  const auto block = cursor.block();
  const int column = cursor.positionInBlock();
  const QString line = block.text();
  const bool atIndent = line.left(column).trimmed().isEmpty();
  int end = cursor.position();
  while (end < segment.second && end - block.position() < line.size() &&
         (document()->characterAt(end) == ' ' || document()->characterAt(end) == '\t'))
    ++end;
  if (!line.mid(end - block.position()).isEmpty()) {
    if (atIndent && cursor.position() == item->inputs.at(input).start + item->inputs.at(input).length) {
      const int position = cursor.position();
      if (replaceTemplateInput(cursor, indent + '\n')) {
        cursor.setPosition(position + indent.size());
        setTextCursor(cursor);
      }
      return;
    }
    // Existing handwriting or a following child owns the remainder of this line.
    if (atIndent) {
      cursor.setPosition(end);
      setTextCursor(cursor);
    }
    return;
  }
  if (!atIndent) {
    replaceTemplateInput(cursor, QStringLiteral("\n") + indent);
    return;
  }
  const QString existing = line.left(end - block.position());
  cursor.setPosition(end);
  if (indent.startsWith(existing) && indent.size() > existing.size())
    replaceTemplateInput(cursor, indent.mid(existing.size()));
  else
    setTextCursor(cursor);
}

bool MyCodeEditor::insertTemplateNewline() {
  auto cursor = textCursor();
  auto *model = templateModel();
  const int input = model->inputAt(activeTemplate, cursor.selectionStart(), cursor.selectionEnd());
  const auto *item = model->instance(activeTemplate);
  if (documentReadOnly() || cursor.hasSelection() || !item || input < 0 ||
      item->inputs.at(input).input.value("kind").toString() != "Code")
    return false;
  auto transaction = beginSynchronousEditTransaction();
  templateMutation = true;
  model->beginChange();
  const auto apply = [&](const EditorStructuralInputController::Edit &plan) {
    cursor.setPosition(plan.start);
    cursor.setPosition(plan.end, QTextCursor::KeepAnchor);
    const bool result = model->replaceInput(activeTemplate, input, cursor, plan.text);
    if (result)
      cursor.setPosition(plan.start + plan.caretOffset);
    return result;
  };
  const auto closing = EditorStructuralInputController::closingKeywordEdit(cursor, state->syntax);
  const bool aligned = closing.valid() && model->allows(activeTemplate, input, closing.start, closing.end)
      && apply(closing);
  // Closing alignment finishes its edit block first so syntax sees the aligned
  // text. The newline then joins it, preserving one undo step and model snapshot.
  const auto segment = model->inputSegment(activeTemplate, input, cursor.position());
  const auto newline = EditorStructuralInputController::newlineEdit(cursor, state->syntax, segment.first);
  QTextCursor change(document());
  if (aligned)
    change.joinPreviousEditBlock();
  else
    change.beginEditBlock();
  const bool result = newline.valid() && apply(newline);
  if (result)
    EditorStructuralInputController::markGeneratedClosing(document(), newline);
  change.endEditBlock();
  model->endChange();
  templateMutation = false;
  refreshTemplateSlotRanges();
  setTextCursor(cursor);
  return result;
}
bool MyCodeEditor::replaceTemplateInput(const QTextCursor &cursor,
                                        const QString &value, bool candidateSelection) {
  if (!templateInputEditing() || documentReadOnly())
    return false;
  const int input = templateModel()->inputAt(
      activeTemplate, cursor.selectionStart(), cursor.selectionEnd());
  if (input < 0)
    return false;
  if (templateModel()->instance(activeTemplate)->inputs[input].input.value("closedChoice").toBool() && !candidateSelection) {
    emit editorStatusMessageRequested("Use Up/Down to select an identified signal; text entry is disabled for this input.");
    return true;
  }
  templateMutation = true;
  int position = -1;
  auto edit = beginSynchronousEditTransaction();
  const bool result = templateModel()->replaceInput(activeTemplate, input,
                                                    cursor, value, &position, candidateSelection);
  templateMutation = false;
  if (result) {
    refreshTemplateSlotRanges();
    QTextCursor next(document());
    next.setPosition(position);
    setTextCursor(next);
  }
  return result;
}
bool MyCodeEditor::replaceTemplateSlotValue(int start, int length,
                                            const QString &value) {
  if (!templateInputEditing())
    return false;
  QTextCursor cursor(document());
  cursor.setPosition(start);
  cursor.setPosition(start + length, QTextCursor::KeepAnchor);
  return replaceTemplateInput(cursor, value, true);
}
void MyCodeEditor::templateInsertionContext(QString *parentId,
                                            QString *slotName) const {
  *parentId = templateModel()->containerAt(textCursor().position(), slotName);
  if (templateEditing && *parentId != activeTemplate) {
    // A selected scalar input cannot inherit a more permissive ancestor.
    const int input =
        templateModel()->inputAt(activeTemplate, textCursor().selectionStart(),
                                 textCursor().selectionEnd());
    if (input >= 0 && templateModel()
                              ->instance(activeTemplate)
                              ->inputs.at(input)
                              .input.value("kind")
                              .toString() != "Code") {
      parentId->clear();
      slotName->clear();
    }
  }
}
QVariantMap MyCodeEditor::templateSignalRoles() const {
  return UserTemplateService::getInstance()->signalRoles();
}
void MyCodeEditor::configureTemplateInputContext(const std::shared_ptr<TemplateInputContext>& inputs,
                                                const QString& ownerId, bool prepared) {
  inputs->roles = templateSignalRoles();
  const QPointer<MyCodeEditor> self(this);
  const auto captured = inputs->snapshot;
  const auto outside = [self, ownerId]() -> QString {
    if (!self || ownerId.isEmpty()) return {};
    const auto* owner = self->templateModel()->instance(ownerId);
    if (!owner) return {};
    auto text = self->cachedDocumentText();
    auto fragments = owner->fragments;
    std::sort(fragments.begin(), fragments.end(), [](const auto& a, const auto& b) { return a.start > b.start; });
    for (const auto& fragment : fragments) text.remove(fragment.start, fragment.length);
    return text;
  };
  const auto boundary = outside();
  const auto originalFile = documentFileName();
  const QPointer<QTextDocument> originalDocument(document());
  const auto indexAtCapture = SemanticIndex::getInstance()->snapshot();
  const bool reusable = captured && !ownerId.isEmpty() && (prepared ||
      captured->getCachedFileContent(originalFile) == cachedDocumentText());
  inputs->liveSnapshot = [self, captured, outside, boundary, reusable, originalFile, originalDocument, indexAtCapture, prepared]() -> std::shared_ptr<const SemanticIndexSnapshot> {
    if (!self || !self->tbAssistDependenciesCurrent()) return {};
    if (self->document() != originalDocument || self->documentFileName() != originalFile) return {};
    const auto snapshot = SemanticIndex::getInstance()->snapshot();
    if (snapshot && snapshot->getCachedFileContent(originalFile) == self->cachedDocumentText()) return snapshot;
    // Only the current fixed template and its inputs may differ. The service's
    // existing captured-source/project guard remains authoritative for prepared
    // contexts; a reopened context also retains its shared publication identity.
    if (reusable && outside() == boundary && (prepared || snapshot == indexAtCapture)) return captured;
    return {};
  };
  inputs->liveRoles = [self] { return self ? self->templateSignalRoles() : QVariantMap(); };
}
QString MyCodeEditor::templateContainerContent() const {
  QString parent, name;
  templateInsertionContext(&parent, &name);
  const auto *item = templateModel()->instance(parent);
  if (item)
    for (const auto &slot : item->inputs)
      if (slot.name == name)
        return slot.input.value("content", "statements").toString();
  return {};
}
bool MyCodeEditor::templateCodeSegmentEmpty() const {
  if (!templateInputEditing())
    return false;
  const auto *item = templateModel()->instance(activeTemplate);
  const int input =
      templateModel()->inputAt(activeTemplate, textCursor().selectionStart(),
                               textCursor().selectionEnd());
  if (!item || input < 0 ||
      item->inputs.at(input).input.value("kind").toString() != "Code")
    return false;
  const auto segment = templateModel()->inputSegment(
      activeTemplate, input, textCursor().selectionStart());
  if (segment.first < 0)
    return false;
  QTextCursor prefix(document());
  // Every new statement line in a handwritten segment can begin another input.
  prefix.setPosition(qMax(segment.first, textCursor().block().position()));
  prefix.setPosition(textCursor().selectionStart(), QTextCursor::KeepAnchor);
  return prefix.selectedText().trimmed().isEmpty();
}
bool MyCodeEditor::handleTemplateInputKey(QKeyEvent *event) {
  if (!event || (!authoringLock && !templateInputEditing()))
    return false;
  if (templateInputEditing() && !state->templateSlots.active())
    refreshTemplateSlotRanges();
  if (event->key() == Qt::Key_Escape && authoringLock) {
    setTemplateAuthoringLocked(false);
    return true;
  }
  if (event->matches(QKeySequence::Undo)) {
    undo();
    return true;
  }
  if (event->matches(QKeySequence::Redo)) {
    redo();
    return true;
  }
  if (event->matches(QKeySequence::Copy)) {
    copy();
    return true;
  }
  if (event->matches(QKeySequence::Cut)) {
    cut();
    return true;
  }
  if (event->matches(QKeySequence::Paste)) {
    paste();
    return true;
  }
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    if (event->modifiers() == Qt::ControlModifier && authoringLock) {
      finishTemplateInput();
      return true;
    }
    if (!templateInputEditing() && authoringLock) {
      enterTemplateTarget();
      return true;
    }
  }
  if (!templateInputEditing()) {
    if (authoringLock &&
        (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) &&
        event->modifiers() == Qt::NoModifier) {
      templateNavigate(event->key() == Qt::Key_Up ? -1 : 1);
      return true;
    }
    return false;
  }
  if (state->templateSlots.handleKeyPress(this, event)) {
    if (!state->templateSlots.active() && !authoringLock) {
      templateEditing = false;
      emit editingInteractionEnded();
    }
    updateTemplatePresentation();
    return true;
  }
  QTextCursor edit = textCursor();
  QString inserted;
  bool modifies = false;
  if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
    if (!edit.hasSelection())
      edit.movePosition(event->key() == Qt::Key_Backspace
                            ? QTextCursor::PreviousCharacter
                            : QTextCursor::NextCharacter,
                        QTextCursor::KeepAnchor);
    modifies = true;
  } else if (!(event->modifiers() &
               (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
             !event->text().isEmpty()) {
    inserted = event->text();
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      if (insertTemplateNewline())
        return true;
      inserted = QStringLiteral("\n");
    }
    modifies = true;
  }
  if (!modifies)
    return false;
  if (replaceTemplateInput(edit, inserted))
    return true;
  if (authoringLock) {
    emit editorStatusMessageRequested(
        QStringLiteral("Only the current input or handwritten container "
                       "segment is editable."));
    return true;
  }
  templateEditing = false;
  activeTemplate.clear();
  return false;
}

QString MyCodeEditor::simulationTemplateInsertionError(
    int start, int removed, const TemplateInsertionSpec &spec,
    const QString &parent, const QString &parentSlot) const {
  if (documentReadOnly())
    return QStringLiteral("This document is read-only.");
  if (authoringLock && !templateInputEditing() && !activeTemplateRegion.isEmpty())
    return QStringLiteral("Press Enter to enter the selected region's handwritten code before inserting a template.");
  const int preserved = spec.preservedStart >= 0 ? spec.preservedLength : 0;
  if (authoringLock && parent.isEmpty() && removed != preserved)
    return QStringLiteral("Source outside a container is read-only in template "
                          "mode; use an empty insertion position.");
  if (!templateModel()->canInsert(start + preserved, removed - preserved,
                                  parent, parentSlot))
    return QStringLiteral(
        "Insert templates only in a code container or an unowned source gap.");
  if (preserved > 0 && !templateModel()->canInsert(start, 0, {}, {}))
    return QStringLiteral(
        "The shared declaration position is protected by another template.");
  return {};
}

bool MyCodeEditor::insertSimulationTemplate(
    const QString &text, const CodeTemplateSlotList &slotMetadata,
    const TemplateInsertionSpec &spec, const QString &parent,
    const QString &parentSlot, QString *error) {
  const auto reject = [&](const QString &message) {
    if (error)
      *error = message;
    return false;
  };
  const int start = textCursor().selectionStart(),
            removed = textCursor().selectionEnd() - start;
  const auto issue = simulationTemplateInsertionError(start, removed, spec,
                                                      parent, parentSlot);
  if (!issue.isEmpty())
    return reject(issue);
  const int preserved = spec.preservedStart >= 0 ? spec.preservedLength : 0;
  const int insertion = start + preserved;
  auto *model = templateModel();
  const auto cursor = textCursor();
  QTextCursor indentationCursor(document());
  indentationCursor.setPosition(start);
  const QString prefix = indentationCursor.block().text().left(
      start - indentationCursor.block().position());
  const QString indentation = prefix.trimmed().isEmpty() ? prefix : QString();
  QString adjusted;
  QList<int> map;
  map.reserve(text.size() + 1);
  for (int i = 0; i < text.size(); ++i) {
    map << int(adjusted.size());
    adjusted += text.at(i);
    if (text.at(i) == '\n' && i + 1 < text.size())
      adjusted += indentation;
  }
  map << int(adjusted.size());
  TemplateInstance instance;
  instance.parentId = parent;
  instance.parentSlot = parentSlot;
  instance.label = spec.label;
  instance.snapshot = spec.snapshot;
  for (const auto &fragment : spec.fragments) {
    if (fragment.start < 0 || fragment.end() > text.size())
      return reject(QStringLiteral("Invalid template fragment."));
    instance.fragments << TemplateFragment{
        start + map.at(fragment.start),
        map.at(fragment.end()) - map.at(fragment.start), fragment.auxiliary};
  }
  instance.inputs = slotMetadata;
  for (auto &slot : instance.inputs) {
    const int end = slot.start + slot.length;
    if (slot.start < 0 || end > text.size())
      return reject(QStringLiteral("Invalid template input."));
    slot.start = start + map.at(slot.start);
    slot.length = start + map.at(end) - slot.start;
  }
  int parentInput = -1;
  if (const auto *value = model->instance(parent))
    for (int i = 0; i < value->inputs.size(); ++i)
      if (value->inputs.at(i).name == parentSlot)
        parentInput = i;
  state->templateSlots.clear(this);
  templateMutation = true;
  auto transaction = beginSynchronousEditTransaction();
  model->beginChange();
  QTextCursor change(document());
  change.beginEditBlock();
  if (spec.preservedStart >= 0 && preserved > 0) {
    const int call = spec.preservedStart + preserved;
    change.setPosition(insertion);
    change.setPosition(start + removed, QTextCursor::KeepAnchor);
    const auto callText = adjusted.mid(map.at(call));
    change.insertText(callText);
    model->applyChange(insertion, removed - preserved, callText.size(), parent,
                       parentInput);
    change.setPosition(start);
    const auto declarations = adjusted.left(map.at(spec.preservedStart));
    change.insertText(declarations);
    model->applyChange(start, 0, declarations.size());
  } else {
    change.setPosition(start);
    change.setPosition(start + removed, QTextCursor::KeepAnchor);
    change.insertText(adjusted);
    model->applyChange(start, removed, adjusted.size(), parent, parentInput);
  }
  activeTemplate = model->add(instance);
  activeTemplateRegion.clear();
  templateTargetKind = TemplateTargetKind::Instance;
  change.endEditBlock();
  model->endChange();
  templateMutation = false;
  templateEditing = !instance.inputs.isEmpty();
  state->templateSlots.start(this, 0, document()->characterCount() - 1,
                             instance.inputs);
  updateTemplatePresentation();
  viewport()->update();
  if (error)
    error->clear();
  return true;
}

bool MyCodeEditor::applyTemplateEdits(const QList<TemplateDocumentEdit>& edits, QString* error) {
  if (documentReadOnly()) {
    if (error) *error = "This document is read-only.";
    return false;
  }
  auto transaction = beginSynchronousEditTransaction();
  templateMutation = true;
  QString selected;
  const bool changed = templateModel()->applyEdits(edits, &selected, error);
  templateMutation = false;
  if (changed) {
    clearTemplatePreedit();
    state->templateSlots.clear(this);
    templateEditing = false;
    activeTemplate = selected.isEmpty() ? edits.first().parentId : selected;
    activeTemplateRegion.clear();
    templateTargetKind = TemplateTargetKind::Instance;
    if (const auto* value = templateModel()->instance(activeTemplate)) {
      QTextCursor cursor(document());
      cursor.setPosition(value->fragments.first().start);
      setTextCursor(cursor);
    }
    updateTemplatePresentation();
  }
  return changed;
}

void MyCodeEditor::paintTemplateBackground(QPainter &painter,
                                           QPaintEvent *event) {
  if (templateModel()->instances().isEmpty()) {
    state->annotationLayer.removeSource(QStringLiteral("template-instances"));
    return;
  }
  const int first =
      cursorForPosition(QPoint(0, event->rect().top())).blockNumber();
  const int last =
      cursorForPosition(QPoint(0, event->rect().bottom())).blockNumber();
  auto entries = templateModel()->instances();
  std::stable_sort(
      entries.begin(), entries.end(), [&](const auto &a, const auto &b) {
        return templateModel()->depth(a.id) < templateModel()->depth(b.id);
      });
  QList<EditorAnnotation> annotations;
  const bool dark = palette().color(QPalette::Base).lightnessF() < 0.5;
  const int hues[] = {205, 155, 265, 35, 320, 185};
  painter.save();
  painter.setClipRect(event->rect());
  for (int i = 0; i < entries.size(); ++i) {
    const auto &item = entries.at(i);
    const int depth = templateModel()->depth(item.id);
    const QColor color = QColor::fromHsl(hues[i % 6], dark ? 44 : 80,
                                         dark ? qMin(85, 40 + depth * 8)
                                              : qMax(205, 239 - depth * 7));
    for (const auto &fragment : item.fragments) {
      const auto start = document()->findBlock(fragment.start),
                 end = document()->findBlock(
                     qMax(fragment.start, fragment.end() - 1));
      for (int line = qMax(first, start.blockNumber());
           line <= qMin(last, end.blockNumber()); ++line) {
        if (!sourceLineVisible(line))
          continue;
        const auto geometry = blockGeometry(line);
        QRectF rect(3 + depth * 9, geometry.top,
                    qMax(1, viewport()->width() - 7 - depth * 9),
                    geometry.height);
        const auto block = document()->findBlockByNumber(line);
        if (line == start.blockNumber() && fragment.start > block.position()) {
          QTextCursor edge(document()); edge.setPosition(fragment.start);
          rect.setLeft(qMax(rect.left(), qreal(cursorRect(edge).left())));
        }
        if (line == end.blockNumber() && fragment.end() < block.position() + block.text().size()) {
          QTextCursor edge(document()); edge.setPosition(fragment.end());
          rect.setRight(qMax(rect.left() + 1, qreal(cursorRect(edge).left())));
        }
        painter.fillRect(rect, color);
        const bool selected =
            item.id == activeTemplate && (authoringLock || templateEditing);
        QPen border(selected ? palette().color(QPalette::Highlight)
                             : color.darker(dark ? 70 : 112));
        border.setWidth(selected ? 2 : 1);
        painter.setPen(border);
        painter.drawLine(rect.topLeft(), rect.bottomLeft());
        if (line == start.blockNumber())
          painter.drawLine(rect.topLeft(), rect.topRight());
        if (line == end.blockNumber())
          painter.drawLine(rect.bottomLeft(), rect.bottomRight());
        EditorAnnotation annotation;
        annotation.kind = EditorAnnotationKind::TemplateInstance;
        annotation.placement = EditorAnnotationPlacement::Overlay;
        annotation.range = {
            qMax(fragment.start,
                 document()->findBlockByNumber(line).position()),
            qMin(fragment.end(),
                 document()->findBlockByNumber(line).position() +
                     int(document()->findBlockByNumber(line).text().size())),
            line, line};
        annotation.semanticKey =
            item.id + QLatin1Char(':') + QString::number(line);
        annotation.detail = item.label;
        annotation.active = selected;
        annotation.visualColumn = depth;
        annotations << annotation;
      }
    }
  }
  state->annotationLayer.setSourceAnnotations(
      QStringLiteral("template-instances"), annotations);
  painter.restore();
}
