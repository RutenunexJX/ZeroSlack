#include "moduletemplatepopup.h"
#include "mycodeeditor.h"
#include "editorsemanticcontextservice.h"
#include "simulationcommandservice.h"
#include "semanticindexsnapshot.h"
#include "usertemplateservice.h"
#include "insightvisualstyle.h"
#include "uicontrols.h"
#include "uitypography.h"
#include "editorfileidentity.h"
#include <QtConcurrent>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

ModuleTemplatePopup::ModuleTemplatePopup(MyCodeEditor* target, const QVariantMap& configuration)
    : QFrame(target, Qt::Popup | Qt::FramelessWindowHint), editor(target), document(target->document()),
      cursor(target->textCursor()), mode(configuration.value("mode").toString()) {
    setObjectName("moduleTemplatePopup");
    setAttribute(Qt::WA_DeleteOnClose);
    InsightVisualStyle::applyGlobalControlPanel(this);
    setFont(UiTypography::font());
    revision = target->semanticDocumentRevision();
    catalogRevision = UserTemplateService::getInstance()->catalogRevision();
    source = capture();
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(12, 12, 12, 12); layout->setSpacing(10);
    auto* title = UiControls::label(mode == "signals" ? "Create signals" : "Create parameters", this);
    UiTypography::apply(title, UiTypography::Role::PanelTitle);
    layout->addWidget(title);
    query = UiControls::lineEdit(this); query->setObjectName("moduleTemplateSearch"); query->setPlaceholderText("Search modules");
    layout->addWidget(query);
    auto* content = new QHBoxLayout; content->setSpacing(12); layout->addLayout(content, 1);
    list = UiControls::listWidget(this); list->setObjectName("moduleTemplateModules"); list->setMinimumWidth(170);
    list->setMaximumWidth(240); content->addWidget(list, 1);
    auto* details = new QVBoxLayout; details->setSpacing(8); content->addLayout(details, 2);
    parameters = UiControls::tableWidget(0, 2, this); parameters->setObjectName("moduleTemplateParameters");
    parameters->setHorizontalHeaderLabels({"Parameter", "Value"}); parameters->verticalHeader()->hide();
    parameters->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    parameters->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    parameters->setFont(UiTypography::font());
    parameters->horizontalHeader()->setFont(UiTypography::font(UiTypography::Role::Metadata));
    parameters->verticalHeader()->setDefaultSectionSize(QFontMetrics(parameters->font()).height() + 10);
    parameters->hide();
    details->addWidget(parameters);
    auto* naming = new QHBoxLayout;
    prefix = UiControls::lineEdit(this); prefix->setObjectName("moduleTemplatePrefix"); prefix->setPlaceholderText("Prefix");
    suffix = UiControls::lineEdit(this); suffix->setObjectName("moduleTemplateSuffix"); suffix->setPlaceholderText("Suffix");
    naming->addWidget(prefix); naming->addWidget(suffix); details->addLayout(naming);
    includeParameters = UiControls::checkBox("Include required parameters", this);
    includeParameters->setObjectName("moduleTemplateIncludeParameters"); includeParameters->setChecked(true);
    includeParameters->setVisible(mode == "signals"); details->addWidget(includeParameters);
    keyword = UiControls::comboBox(this); keyword->setObjectName("moduleTemplateKeyword");
    keyword->addItems({"localparam", "parameter"}); keyword->setVisible(mode == "parameters"); details->addWidget(keyword);
    preview = UiControls::readOnlyText(this); preview->setObjectName("moduleTemplatePreview");
    auto codeFont = target->font(); codeFont.setPixelSize(UiTypography::font().pixelSize());
    preview->setFont(codeFont); preview->setLineWrapMode(QPlainTextEdit::NoWrap); preview->setMinimumHeight(180);
    preview->setPlaceholderText("Select a module to preview declarations");
    details->addWidget(preview, 1);
    feedback = UiControls::label(this); feedback->setObjectName("moduleTemplateFeedback"); feedback->setWordWrap(true);
    layout->addWidget(feedback);
    auto* actions = new QHBoxLayout; actions->addStretch();
    auto* cancel = UiControls::pushButton("Cancel", this); cancel->setObjectName("moduleTemplateCancel");
    insert = UiControls::pushButton("Insert", this); insert->setObjectName("moduleTemplateInsert"); insert->setEnabled(false);
    actions->addWidget(cancel); actions->addWidget(insert); layout->addLayout(actions);
    debounce = new QTimer(this); debounce->setSingleShot(true); debounce->setInterval(120);
    contextTimer = new QTimer(this); contextTimer->setInterval(120);
    watcher = new QFutureWatcher<Job>(this);
    connect(contextTimer, &QTimer::timeout, this, [this] {
        if (!current()) { if (editor) emit editor->editorStatusMessageRequested("Source or template context changed. Reopen module selection."); dismiss(); }
    });
    connect(query, &QLineEdit::textChanged, this, [this] { filter(); });
    connect(list, &QListWidget::currentRowChanged, this, [this] { if (!filling) selectModule(); });
    connect(prefix, &QLineEdit::textChanged, this, [this] { invalidate(); });
    connect(suffix, &QLineEdit::textChanged, this, [this] { invalidate(); });
    connect(parameters, &QTableWidget::itemChanged, this, [this] { if (!filling) invalidate(); });
    connect(includeParameters, &QCheckBox::toggled, this, [this](bool enabled) { parameters->setEnabled(enabled); invalidate(); });
    connect(keyword, &QComboBox::currentTextChanged, this, [this] { invalidate(); });
    connect(cancel, &QPushButton::clicked, this, [this] { dismiss(); });
    connect(insert, &QPushButton::clicked, this, [this] { apply(); });
    connect(debounce, &QTimer::timeout, this, [this] { startJob(); });
    connect(watcher, &QFutureWatcher<Job>::finished, this, [this] {
        if (closing) return;
        if (!current()) { dismiss(); return; }
        if (runningGeneration != generation) { debounce->start(); return; }
        const auto result = watcher->result();
        if (result.inspection) {
            model = result.model;
            if (!model.error.isEmpty()) { feedback->setText(model.error); return; }
            modelReady = true; showParameters(); invalidate();
        } else {
            plan = result.plan;
            preview->setPlainText(plan.text);
            feedback->setText(plan.error.isEmpty() ? QString() : plan.error);
            insert->setEnabled(plan.error.isEmpty() && !plan.text.isEmpty());
        }
    });
    if (const auto snapshot = SemanticIndex::getInstance()->snapshot()) {
        modules = snapshot->getSymbolRecordsByDeclarationKind(SymbolTaxonomy::DeclarationKind::Module);
        std::stable_sort(modules.begin(), modules.end(), [](const auto& a, const auto& b) {
            const auto name = a.name.compare(b.name, Qt::CaseInsensitive);
            return name ? name < 0 : a.location.fileName < b.location.fileName;
        });
    }
    for (auto* widget : findChildren<QWidget*>()) widget->installEventFilter(this);
    installEventFilter(this);
    filter();
}
ModuleTemplatePopup::~ModuleTemplatePopup() { if (cancelled) cancelled->store(true); }
SimulationCommandRequest ModuleTemplatePopup::capture() const {
    SimulationCommandRequest request;
    if (!editor) return request;
    if (auto* service = editor->window()->findChild<SimulationCommandService*>()) request = service->currentRequest();
    else if (const auto snapshot = SemanticIndex::getInstance()->snapshot()) {
        request.contents = snapshot->fileContents(); request.orderedFiles = snapshot->symbolFiles();
    }
    request.context.fileName = editor->documentFileName(); request.context.documentText = editor->toPlainText();
    request.context.cursorPosition = cursor.position(); request.context.readOnly = editor->documentReadOnly();
    request.context.moduleName = editor->editorSemanticContextForPosition(cursor.position()).moduleName;
    request.context.replacementStart = cursor.position(); request.context.replacementLength = 0;
    request.contents.insert(request.context.fileName, request.context.documentText);
    return request;
}
bool ModuleTemplatePopup::current() const {
    if (!editor || !document || editor->document() != document || editor->documentReadOnly()
        || editor->documentFileName() != source.context.fileName || editor->semanticDocumentRevision() != revision
        || editor->textCursor() != cursor || UserTemplateService::getInstance()->catalogRevision() != catalogRevision) return false;
    const auto now = capture();
    return now.contents == source.contents && now.orderedFiles == source.orderedFiles
        && now.includeDirectories == source.includeDirectories && now.defines == source.defines;
}
void ModuleTemplatePopup::openAtCursor() {
    const auto bounds = editor->screen()->availableGeometry();
    resize(qMin(700, bounds.width()), qMin(560, bounds.height()));
    auto point = editor->viewport()->mapToGlobal(editor->cursorRect().bottomLeft());
    point.setX(qBound(bounds.left(), point.x(), bounds.right() - width() + 1));
    point.setY(qBound(bounds.top(), point.y(), bounds.bottom() - height() + 1));
    move(point); show(); query->setFocus(Qt::PopupFocusReason); contextTimer->start();
}
void ModuleTemplatePopup::filter() {
    filling = true;
    const auto previous = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toInt() : -1;
    list->clear();
    for (int i = 0; i < modules.size(); ++i) {
        const auto& module = modules[i];
        if (!(module.name + ' ' + module.location.fileName).contains(query->text().trimmed(), Qt::CaseInsensitive)) continue;
        auto* row = new QListWidgetItem(module.name + '\n' + QFileInfo(module.location.fileName).fileName()
            + ':' + QString::number(module.location.startLine), list);
        row->setData(Qt::UserRole, i); row->setToolTip(module.location.fileName);
        if (i == previous) list->setCurrentItem(row);
    }
    filling = false;
    // Filtering never confirms a sole match; the user chooses a module explicitly.
    if (!list->currentItem()) { modelReady = false; parameters->setRowCount(0); preview->clear(); invalidate(); }
}
void ModuleTemplatePopup::selectModule() {
    modelReady = false; model = {}; parameters->setRowCount(0); preview->clear(); invalidate();
}
ModuleTemplateOptions ModuleTemplatePopup::options() const {
    ModuleTemplateOptions result;
    result.mode = mode; result.prefix = prefix->text(); result.suffix = suffix->text();
    result.parameterKeyword = mode == "parameters" ? keyword->currentText() : QString("localparam");
    result.includeParameters = includeParameters->isChecked();
    for (int row = 0; row < parameters->rowCount(); ++row) {
        const auto& item = model.parameters[row];
        if (parameters->item(row, 0)->checkState() == Qt::Checked) result.selectedParameters.insert(item.name);
        const auto value = parameters->item(row, 1)->text();
        if (!item.local && value != item.value && (mode == "parameters" || result.includeParameters)) result.values.insert(item.name, value);
    }
    return result;
}
void ModuleTemplatePopup::showParameters() {
    filling = true;
    parameters->setRowCount(model.parameters.size());
    for (int row = 0; row < model.parameters.size(); ++row) {
        const auto& item = model.parameters[row];
        auto* name = new QTableWidgetItem(item.name);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (mode == "parameters") { name->setFlags(name->flags() | Qt::ItemIsUserCheckable); name->setCheckState(item.local ? Qt::Unchecked : Qt::Checked); }
        name->setToolTip(item.local ? "Derived localparam; included when required" : item.typeParameter ? "Type parameter" : "Overridable parameter");
        parameters->setItem(row, 0, name);
        auto* value = new QTableWidgetItem(item.value);
        if (item.local) { value->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable); value->setToolTip("Derived localparam"); }
        parameters->setItem(row, 1, value);
    }
    parameters->setFixedHeight(qMin(150, parameters->horizontalHeader()->sizeHint().height()
        + parameters->verticalHeader()->defaultSectionSize() * parameters->rowCount() + 2));
    parameters->setVisible(!model.parameters.isEmpty());
    filling = false;
}
void ModuleTemplatePopup::invalidate() {
    ++generation; insert->setEnabled(false);
    if (cancelled) cancelled->store(true);
    feedback->setText(list->currentItem() ? "Preparing preview…" : modules.isEmpty() ? "No modules available. Wait for analysis or open a source file." : "Choose a module");
    debounce->start();
}
void ModuleTemplatePopup::startJob() {
    if (closing || watcher->isRunning() || !list->currentItem()) return;
    if (!current()) { dismiss(); return; }
    runningGeneration = generation;
    cancelled = std::make_shared<std::atomic_bool>(false);
    auto request = source; request.cancelled = cancelled;
    const auto selected = modules[list->currentItem()->data(Qt::UserRole).toInt()];
    const bool inspect = !modelReady;
    const auto capturedModel = model;
    const auto settings = options();
    QSet<QString> occupied;
    if (const auto snapshot = SemanticIndex::getInstance()->snapshot()) {
        SemanticIndex index; index.setSnapshot(snapshot);
        for (const auto& candidate : snapshot->getSymbolRecordsByName(source.context.moduleName)) {
            if (candidate.declarationKind != SymbolTaxonomy::DeclarationKind::Module
                || !EditorFileIdentity::same(candidate.location.fileName, source.context.fileName)) continue;
            for (const auto& member : index.getDeclarationMemberRecords(candidate)) occupied.insert(member.name);
        }
    }
    watcher->setFuture(QtConcurrent::run([request, selected, inspect, capturedModel, settings, occupied] {
        Job result; result.inspection = inspect;
        if (inspect) result.model = ModuleTemplatePlanner::inspect(request, selected);
        else {
            result.plan = ModuleTemplatePlanner::prepare(capturedModel, settings, occupied);
            if (result.plan.error.isEmpty()) result.plan.error = ModuleTemplatePlanner::validateInsertion(request, capturedModel, result.plan.text);
        }
        return result;
    }));
}
void ModuleTemplatePopup::apply() {
    if (!insert->isEnabled() || !current() || !SimulationCommandPlanner::sourcesCurrent(model.captured)) {
        insert->setEnabled(false); feedback->setText("Sources changed. Reopen module selection."); return;
    }
    const QPointer<MyCodeEditor> target = editor;
    const auto text = plan.text;
    dismiss();
    if (!target) return;
    QString error;
    target->insertOrdinaryTemplate(text, {}, text.size(), &error);
    if (!error.isEmpty()) emit target->editorStatusMessageRequested(error);
}
void ModuleTemplatePopup::dismiss() {
    if (closing) return;
    closing = true;
    if (cancelled) cancelled->store(true);
    debounce->stop(); contextTimer->stop(); close();
    if (editor && editor->isVisible()) { editor->window()->activateWindow(); editor->setFocus(Qt::OtherFocusReason); }
}
void ModuleTemplatePopup::hideEvent(QHideEvent* event) {
    QFrame::hideEvent(event);
    if (!closing) dismiss();
}
bool ModuleTemplatePopup::eventFilter(QObject* object, QEvent* event) {
    if (event->type() != QEvent::KeyPress) return QFrame::eventFilter(object, event);
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) { dismiss(); return true; }
    if (object == query && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)) {
        if (list->count()) list->setCurrentRow(qBound(0, list->currentRow() + (key->key() == Qt::Key_Down ? 1 : -1), list->count() - 1));
        return true;
    }
    if ((object == query || object == list) && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
        if (!list->currentItem() && list->count()) list->setCurrentRow(0);
        else if (modelReady) prefix->setFocus(Qt::TabFocusReason);
        return true;
    }
    return QFrame::eventFilter(object, event);
}
