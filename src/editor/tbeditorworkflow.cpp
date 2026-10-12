#include "tbeditorworkflow.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "mycodeeditor.h"
#include "editorfileidentity.h"
#include "semanticindex.h"
#include "uicontrols.h"
#include "../simulation/simdock/core/analyzer.h"
#include "../simulation/simdock/core/testbench.h"
#include "../simulation/simdock/core/workspace.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

TbEditorWorkflow::TbEditorWorkflow(TabManager* t, WorkspaceManager* w, QObject* parent)
    : QObject(parent), tabs(t), workspace(w) {
    connect(tabs, &TabManager::activeTabChanged, this, &TbEditorWorkflow::cancel);
    connect(workspace, &WorkspaceManager::workspaceActivated, this, &TbEditorWorkflow::cancel);
    connect(workspace, &WorkspaceManager::workspaceClosed, this, &TbEditorWorkflow::cancel);
}
void TbEditorWorkflow::cancel() {
    if (popup) { popup->hide(); popup->deleteLater(); popup = nullptr; }
}
void TbEditorWorkflow::create(const QString& requestedFile, const QString& requestedModule) {
    cancel();
    auto* editor = tabs->getCurrentEditor();
    if (!editor) { tabs->createNewTab(); editor = tabs->getCurrentEditor(); }
    if (!editor) return;
    const auto root = workspace->getWorkspacePath();
    const auto file = requestedFile.isEmpty() ? editor->documentFileName() : requestedFile;
    const auto preferredModule = requestedModule.isEmpty() ? editor->currentModuleName() : requestedModule;
    auto* frame = new QFrame(editor); popup = frame;
    frame->setObjectName(QStringLiteral("newTbEditorPopup"));
    frame->setAutoFillBackground(true); frame->setBackgroundRole(QPalette::Base);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet(QStringLiteral("QFrame#newTbEditorPopup { background: palette(base); border: 1px solid palette(mid); border-radius: 5px; }"));
    auto* layout = new QVBoxLayout(frame);
    auto* header = new QHBoxLayout;
    header->addWidget(UiControls::label(QStringLiteral("New editable testbench"), frame), 1);
    auto* cancelButton = UiControls::pushButton(QStringLiteral("Cancel"), frame);
    cancelButton->setObjectName(QStringLiteral("tbCreateCancel"));
    connect(cancelButton, &QPushButton::clicked, this, &TbEditorWorkflow::cancel);
    header->addWidget(cancelButton); layout->addLayout(header);
    auto* scroll = UiControls::scrollArea(frame); scroll->setWidgetResizable(true);
    auto* body = new QWidget(scroll); auto* form = new QFormLayout(body);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto* modules = UiControls::comboBox(body); modules->setObjectName(QStringLiteral("tbDutChoice"));
    auto* name = UiControls::lineEdit(body); name->setObjectName(QStringLiteral("tbNewName"));
    auto* path = UiControls::lineEdit(body); path->setObjectName(QStringLiteral("tbNewPath"));
    auto* clock = UiControls::comboBox(body); clock->setObjectName(QStringLiteral("tbClockChoice"));
    auto* reset = UiControls::comboBox(body); reset->setObjectName(QStringLiteral("tbResetChoice"));
    auto* period = UiControls::spinBox(body); period->setRange(2, 1000000); period->setValue(10);
    auto* activeLow = UiControls::checkBox(QStringLiteral("Active-low reset"), body); activeLow->setChecked(true);
    form->addRow(QStringLiteral("DUT"), modules);
    form->addRow(QStringLiteral("TB module"), name);
    form->addRow(QStringLiteral("Workspace file"), path);
    form->addRow(QStringLiteral("Clock suggestion"), clock);
    form->addRow(QStringLiteral("Clock period (ns)"), period);
    form->addRow(QStringLiteral("Reset suggestion"), reset);
    form->addRow(activeLow);
    auto* hint = UiControls::label(QStringLiteral("Clock/reset choices are suggestions. All generated code remains editable."), body);
    hint->setWordWrap(true); form->addRow(hint);
    scroll->setWidget(body); layout->addWidget(scroll);
    auto* preview = UiControls::readOnlyText(frame); preview->setObjectName(QStringLiteral("tbNewPreview"));
    preview->setFont(editor->font()); preview->setMinimumHeight(90); layout->addWidget(preview, 1);
    auto* issue = UiControls::label(frame); issue->setObjectName(QStringLiteral("tbNewStatus"));
    issue->setWordWrap(true); issue->setTextFormat(Qt::PlainText); layout->addWidget(issue);
    auto* create = UiControls::pushButton(QStringLiteral("Create and open"), frame); create->setObjectName(QStringLiteral("tbCreateAndOpen"));
    layout->addWidget(create);
    struct State { simdock::Module module; QString content, file, generated; SemanticSnapshotToken token; bool choosing = false; };
    const auto state = std::make_shared<State>();
    auto* index = SemanticIndex::getInstance();
    QList<SemanticSymbolRecord> available = index->getSymbolRecordsByDeclarationKind(SymbolTaxonomy::DeclarationKind::Module);
    std::stable_sort(available.begin(), available.end(), [&](const auto& a, const auto& b) {
        const bool aCurrent = EditorFileIdentity::same(a.location.fileName, file), bCurrent = EditorFileIdentity::same(b.location.fileName, file);
        return aCurrent != bCurrent ? aCurrent : a.name < b.name;
    });
    for (const auto& module : available) if (simdock::insideWorkspace(root, module.location.fileName))
        modules->addItem(QStringLiteral("%1 · %2").arg(module.name, QDir(root).relativeFilePath(module.location.fileName)),
            QStringList{module.location.fileName, module.name});
    auto currentText = [this](const QString& file) {
        for (const auto* e : tabs->openEditors()) if (EditorFileIdentity::same(e->documentFileName(), file)) return e->cachedDocumentText();
        return SemanticIndex::getInstance()->getCachedFileContent(file);
    };
    const auto options = [=] { auto o = simdock::suggestedTbOptions(state->module); o.name = name->text().trimmed(); o.clock = clock->currentData().toString(); o.reset = reset->currentData().toString(); o.clockPeriodNs = period->value(); o.resetActiveLow = activeLow->isChecked(); return o; };
    const auto refresh = [=] {
        if (state->choosing) return;
        QString error;
        if (root.isEmpty()) error = QStringLiteral("Open a workspace and its DUT document first.");
        else if (state->module.name.isEmpty()) error = QStringLiteral("No analyzed DUT module is available. Open the DUT and wait for analysis.");
        else if (state->token.snapshot != index->snapshot() || currentText(state->file) != state->content)
            error = QStringLiteral("DUT or semantic facts changed. Reselect the DUT to refresh the preview.");
        else if (!editor->tbAssistDependenciesCurrent())
            error = QStringLiteral("Other open documents have pending semantic changes. Analyze or save them before creating the TB.");
        auto module = state->module;
        if (error.isEmpty()) simdock::completeTestbenchTypes(module, *index, state->file, state->content, &error);
        state->generated = error.isEmpty() ? simdock::generateTestbench(module, options(), 1000, &error) : QString();
        const auto relative = path->text().trimmed();
        const auto target = QDir(root).filePath(relative);
        if (error.isEmpty() && (relative.isEmpty() || !relative.endsWith(QStringLiteral(".sv"), Qt::CaseInsensitive)
            || QDir::isAbsolutePath(relative) || !simdock::insideWorkspace(root, target))) error = QStringLiteral("Choose an .sv file inside this workspace.");
        if (error.isEmpty() && QFileInfo::exists(target)) error = QStringLiteral("This file already exists; choose a different filename.");
        for (const auto* e : tabs->openEditors()) if (EditorFileIdentity::same(e->documentFileName(), target)) error = QStringLiteral("A document is already open at this path; it cannot be overwritten.");
        issue->setText(error); preview->setPlainText(state->generated); create->setEnabled(error.isEmpty());
    };
    const auto select = [=] {
        state->choosing = true;
        const auto identity = modules->currentData().toStringList();
        state->module = {}; state->content.clear(); state->file.clear();
        clock->clear(); reset->clear(); clock->addItem(QStringLiteral("None"), QString()); reset->addItem(QStringLiteral("None"), QString());
        if (identity.size() == 2) {
            state->file = identity[0]; state->content = currentText(state->file); state->token = index->snapshotToken();
            if (state->content.size() <= 4 * 1024 * 1024) {
                const auto source = simdock::analyzeSource(state->content.toUtf8(), QDir(root).relativeFilePath(state->file));
                for (const auto& m : source.modules) if (m.name == identity[1]) state->module = m;
            }
            const auto suggested = simdock::suggestedTbOptions(state->module);
            name->setText(suggested.name); path->setText(QStringLiteral("sim/%1.sv").arg(suggested.name));
            for (const auto& p : state->module.ports) if (simdock::isTimingInput(p)) { clock->addItem(p.name, p.name); reset->addItem(p.name, p.name); }
            clock->setCurrentIndex(qMax(0, clock->findData(suggested.clock))); reset->setCurrentIndex(qMax(0, reset->findData(suggested.reset)));
            activeLow->setChecked(suggested.resetActiveLow);
        }
        state->choosing = false; refresh();
    };
    connect(modules, &QComboBox::currentIndexChanged, frame, select);
    auto* refreshDut = UiControls::pushButton(QStringLiteral("Refresh DUT facts"), body);
    refreshDut->setObjectName(QStringLiteral("tbRefreshDut"));
    form->addRow(refreshDut);
    connect(refreshDut, &QPushButton::clicked, frame, select);
    for (auto* edit : {name, path}) connect(edit, &QLineEdit::textChanged, frame, refresh);
    for (auto* combo : {clock, reset}) connect(combo, &QComboBox::currentIndexChanged, frame, refresh);
    connect(period, &QSpinBox::valueChanged, frame, refresh);
    connect(activeLow, &QCheckBox::toggled, frame, refresh);
    connect(create, &QPushButton::clicked, this, [=, this] {
        refresh(); if (!create->isEnabled()) return;
        if (workspace->getWorkspacePath() != root) { cancel(); return; }
        const auto target = QDir(root).filePath(path->text().trimmed());
        QString error;
        if (!simdock::writeNewTb(root, path->text().trimmed(), state->generated, &error)) { issue->setText(error); return; }
        const auto dutFile = state->file, dutName = state->module.name, tbName = options().name;
        cancel();
        tabs->openFileInTab(target);
        workspace->refreshWorkspaceFiles();
        emit testbenchCreated(dutFile, dutName, target, tbName);
    });
    for (int i = 0; i < modules->count(); ++i) {
        const auto data = modules->itemData(i).toStringList();
        if (data.size() == 2 && data[1] == preferredModule && EditorFileIdentity::same(data[0], file)) { modules->setCurrentIndex(i); break; }
    }
    select();
    const int width = qMin(630, qMax(180, editor->width() - 16));
    frame->resize(width, qMin(680, qMax(220, editor->height() - 16)));
    frame->move(qMax(8, (editor->width() - width) / 2), 8);
    frame->show(); frame->raise();
}
