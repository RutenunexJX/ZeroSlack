#include "editorruntime.h"

#include "actionregistry.h"
#include "definitionservice.h"
#include "editorhoverpopup.h"
#include "mycodeeditor.h"
#include "semanticrenamesupport.h"

#include <QLineEdit>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QTextCursor>
#include <QVariantMap>

namespace {
bool sameSnapshot(const SemanticSnapshotToken& left,
                  const SemanticSnapshotToken& right)
{
    return left.isValid()
        && right.isValid()
        && left.revision == right.revision
        && left.snapshot == right.snapshot;
}

bool requestRenameAction(MyCodeEditor* editor,
                         const QVariantMap& parameters)
{
    if (!editor)
        return false;
    bool handled = false;
    emit editor->registeredActionRequested(
        QString::fromLatin1(ActionIds::RtlRename),
        parameters,
        &handled);
    return handled;
}
}

void MyCodeEditorState::clearSemanticRenameEditorState()
{
    ++semanticRenameSessionGeneration;
    QObject::disconnect(semanticRenamePeekClosedConnection);
    semanticRenamePeekClosedConnection = {};
    QObject::disconnect(semanticRenameTextConnection);
    semanticRenameTextConnection = {};
    QObject::disconnect(semanticRenameReturnConnection);
    semanticRenameReturnConnection = {};
    semanticRenamePeek.clear();
    semanticRenameEditor.clear();
    semanticRenameDocument.clear();
    semanticRenameDocumentRevision = 0;
    semanticRenameFileName.clear();
    semanticRenameFileIdentityKey.clear();
    semanticRenameToken = {};
    semanticRenameSubject = {};
}

void MyCodeEditorState::cancelSemanticRenameEditor()
{
    const QPointer<EditorHoverPopup> pendingPeek = semanticRenamePeek;
    const bool closePendingPeek =
        pendingPeek
        && sourceNavigation.currentPeek() == pendingPeek.data();
    clearSemanticRenameEditorState();
    if (closePendingPeek)
        sourceNavigation.closeExternalPeek();
}

bool MyCodeEditorState::beginSemanticRenameEditor(
    MyCodeEditor* editor,
    QString* failureReason)
{
    const auto fail = [failureReason](const QString& reason) {
        if (failureReason)
            *failureReason = reason;
        return false;
    };
    if (!editor || !editor->document())
        return fail(QStringLiteral("No editor document."));

    const TSIdentifierTarget identifier =
        syntax.identifierAt(editor->textCursor().position());
    if (!identifier.ok()) {
        return fail(QStringLiteral(
            "Place the caret on a renameable SystemVerilog symbol."));
    }

    const EditorSemanticContext context =
        semanticContextForPosition(editor, identifier.startChar, true);
    DefinitionQuery query;
    query.symbolName = identifier.text;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.linePrefixBeforeCursor =
        context.lineText.left(qMax(0, context.column));
    query.cursorLine = context.cursorLine;
    query.cursorColumn = context.column;
    const DefinitionResult definition =
        DefinitionService::getInstance()->resolveDefinition(query);
    if (!definition.found
        || !isSupportedSemanticRenameSubject(definition.symbolRecord)) {
        return fail(QStringLiteral(
            "The selected symbol kind is not supported by semantic rename."));
    }

    SemanticIndex* semantic = SemanticIndex::getInstance();
    const SemanticSnapshotToken token =
        semantic ? semantic->snapshotToken() : SemanticSnapshotToken{};
    if (!token.isValid() || token.revision == 0) {
        return fail(QStringLiteral(
            "A current Slang semantic snapshot is required."));
    }

    QSet<QString> referenceKeys;
    QSet<QString> affectedFiles;
    affectedFiles.insert(EditorFileIdentity::lookupKey(
        definition.symbolRecord.location.fileName));
    const auto collectReferences =
        [&referenceKeys, &affectedFiles](
            const QList<SemanticRelationship>& relationships) {
            for (const SemanticRelationship& relationship : relationships) {
                if (relationship.type != SymbolRelationshipEngine::REFERENCES
                    || !relationship.evidenceRange.isValid()) {
                    continue;
                }
                const SemanticSourceRange& range = relationship.evidenceRange;
                const QString fileKey =
                    EditorFileIdentity::lookupKey(range.fileName);
                referenceKeys.insert(
                    QStringLiteral("%1:%2:%3:%4")
                        .arg(fileKey)
                        .arg(range.position)
                        .arg(range.line)
                        .arg(range.column));
                affectedFiles.insert(fileKey);
            }
        };
    collectReferences(semantic->relationshipsForStableKey(
        definition.symbolRecord.stableKey, true));
    collectReferences(semantic->relationshipsForStableKey(
        definition.symbolRecord.stableKey, false));
    affectedFiles.remove(QString());

    cancelSignalDefinitionEditor();
    cancelSemanticRenameEditor();
    EditorHoverPopup* peek =
        sourceNavigation.beginExternalPeek(editor, true);
    if (!peek)
        return fail(QStringLiteral("Unable to open the rename editor."));

    QTextCursor anchor(editor->document());
    anchor.setPosition(identifier.startChar);
    const QRect localAnchor = editor->cursorRect(anchor);
    const QRect globalAnchor(
        editor->viewport()->mapToGlobal(localAnchor.topLeft()),
        localAnchor.size());

    PeekContentModel content;
    content.kind = PeekContentKind::DeclarationPreview;
    content.title = QStringLiteral("Rename %1")
                        .arg(definition.symbolRecord.name);
    content.rows.append(
        {QStringLiteral("Kind: %1")
             .arg(semanticRenameKindLabel(definition.symbolRecord)),
         PeekContentRowRole::Body,
         false});
    content.rows.append(
        {QStringLiteral("Owner: %1")
             .arg(semanticRenameOwnerLabel(definition.symbolRecord)),
         PeekContentRowRole::Body,
         false});
    content.rows.append(
        {QStringLiteral(
             "Known references: %1 in %2 file(s); exact plan is built on Enter.")
             .arg(referenceKeys.size())
             .arg(affectedFiles.size()),
         PeekContentRowRole::Muted,
         true});
    content.editor.enabled = true;
    content.editor.text = definition.symbolRecord.name;
    content.editor.placeholderText =
        QStringLiteral("new SystemVerilog identifier");
    content.editor.objectName =
        QStringLiteral("semanticRenameInlineEditor");
    content.editor.minimumWidth = 300;
    content.maximumSize = QSize(560, 230);
    peek->showContent(content, globalAnchor, editor->font());

    QLineEdit* lineEdit = peek->editableLineEdit();
    if (!lineEdit) {
        sourceNavigation.closeExternalPeek();
        return fail(QStringLiteral("Unable to create the rename field."));
    }

    semanticRenamePeek = peek;
    semanticRenameEditor = lineEdit;
    semanticRenameDocument = editor->document();
    semanticRenameDocumentRevision = semanticDocumentRevision();
    semanticRenameFileName = identity.current();
    semanticRenameFileIdentityKey =
        EditorFileIdentity::lookupKey(semanticRenameFileName);
    semanticRenameToken = token;
    semanticRenameSubject = definition.symbolRecord;
    const std::uint64_t sessionGeneration =
        ++semanticRenameSessionGeneration;
    const QPointer<EditorHoverPopup> pendingPeek(peek);
    const QPointer<QLineEdit> pendingEditor(lineEdit);

    const auto refreshValidation =
        [this, pendingPeek](const QString& text) {
            if (!pendingPeek)
                return;
            SemanticIndex* currentIndex = SemanticIndex::getInstance();
            QString validation = semanticRenameInlineValidation(
                currentIndex, semanticRenameSubject, text);
            const SemanticSnapshotToken currentToken =
                currentIndex
                ? currentIndex->snapshotToken()
                : SemanticSnapshotToken{};
            if (validation.isEmpty()
                && !sameSnapshot(semanticRenameToken, currentToken)) {
                validation = QStringLiteral(
                    "The semantic generation changed; reopen rename.");
            }
            pendingPeek->setEditableMessage(
                validation.isEmpty()
                    ? QStringLiteral(
                          "Press Enter to build the exact rename plan.")
                    : validation,
                !validation.isEmpty());
        };
    semanticRenameTextConnection = QObject::connect(
        lineEdit,
        &QLineEdit::textChanged,
        editor,
        refreshValidation);
    refreshValidation(lineEdit->text());

    semanticRenamePeekClosedConnection = QObject::connect(
        peek,
        &EditorHoverPopup::closed,
        editor,
        [this, pendingPeek, sessionGeneration]() {
            if (semanticRenameSessionGeneration != sessionGeneration
                || semanticRenamePeek != pendingPeek) {
                return;
            }
            clearSemanticRenameEditorState();
        });
    semanticRenameReturnConnection = QObject::connect(
        lineEdit,
        &QLineEdit::returnPressed,
        editor,
        [this,
         editor,
         pendingPeek,
         pendingEditor,
         sessionGeneration]() {
            if (!pendingPeek || !pendingEditor
                || semanticRenameSessionGeneration != sessionGeneration
                || semanticRenamePeek != pendingPeek
                || semanticRenameEditor != pendingEditor) {
                return;
            }
            SemanticIndex* semantic = SemanticIndex::getInstance();
            const SemanticSnapshotToken currentToken =
                semantic
                ? semantic->snapshotToken()
                : SemanticSnapshotToken{};
            QString validation = semanticRenameInlineValidation(
                semantic,
                semanticRenameSubject,
                pendingEditor->text());
            if (validation.isEmpty()
                && (!sameSnapshot(semanticRenameToken, currentToken)
                    || editor->document() != semanticRenameDocument.data()
                    || semanticDocumentRevision()
                           != semanticRenameDocumentRevision
                    || semanticRenameFileIdentityKey
                           != EditorFileIdentity::lookupKey(
                               identity.current()))) {
                validation = QStringLiteral(
                    "The editor or semantic context changed; reopen rename.");
            }
            if (!validation.isEmpty()) {
                pendingPeek->setEditableMessage(validation, true);
                return;
            }

            QVariantMap parameters;
            parameters.insert(QStringLiteral("newName"),
                              pendingEditor->text().trimmed());
            parameters.insert(
                QStringLiteral("subjectStableKey"),
                semanticRenameSubject.stableKey.toString());
            parameters.insert(QStringLiteral("cursorPosition"),
                              editor->textCursor().position());
            parameters.insert(QStringLiteral("applyOnEnter"), true);
            if (!requestRenameAction(editor, parameters)) {
                pendingPeek->setEditableMessage(
                    QStringLiteral(
                        "The Action Registry rename route is unavailable."),
                    true);
                return;
            }
            if (sourceNavigation.currentPeek() == pendingPeek.data())
                sourceNavigation.closeExternalPeek();
        });
    lineEdit->setFocus(Qt::PopupFocusReason);
    lineEdit->selectAll();
    if (failureReason)
        failureReason->clear();
    return true;
}
