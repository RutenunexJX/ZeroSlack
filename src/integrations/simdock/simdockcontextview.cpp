#include "simdockcontextview.h"
#include "simdockstate.h"
#include "../../simulation/simdock/ui/workbench.h"
#include "applicationthememanager.h"
#include "uicontrols.h"
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSettings>
#include <QVBoxLayout>
#include <memory>

SimDockContextView::SimDockContextView(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("simdockHost"));
    setProperty("nativeComponentReady", false);
    setProperty("simdockSourceVersion", QStringLiteral("0.6.1"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    panel = new simdock::Workbench(this);
    layout->addWidget(panel, 1);
    status = UiControls::label(this);
    status->setObjectName(QStringLiteral("nativeComponentStatus"));
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    retryButton = UiControls::pushButton(tr("Reload SimDock"), this);
    retryButton->setObjectName(QStringLiteral("nativeComponentRetry"));
    resetButton = UiControls::pushButton(tr("Reset saved panel state"), this);
    resetButton->setObjectName(QStringLiteral("nativeComponentResetState"));
    layout->addWidget(status);
    layout->addWidget(retryButton);
    layout->addWidget(resetButton);
    connect(retryButton, &QPushButton::clicked, this, &SimDockContextView::retry);
    connect(resetButton, &QPushButton::clicked, this, &SimDockContextView::resetSavedState);
    connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged,
            this, &SimDockContextView::synchronizeTheme);
    connect(panel, &simdock::Workbench::preferencesChanged, this, &SimDockContextView::persistPreferences);
    synchronizeTheme();
}

bool SimDockContextView::activate(const ContextResource& resource)
{
    if (retired || resource.providerId != QStringLiteral("simdock")) return false;
    if (activated && resource.workspaceId == lastActivation.workspaceId
        && resource.stableKey() == lastActivation.stableKey() && resource.state == lastActivation.state) return true;
    activated = true;
    lastActivation = desired = resource;
    statePending = !resource.state.isEmpty();
    retry();
    return true;
}

void SimDockContextView::retry()
{
    if (invoking || retired) return;
    const QScopedValueRollback<bool> guard(invoking, true);
    QString error;
    if (!contextApplied || appliedWorkspace != desired.workspaceId) {
        if (contextApplied && !canClose(&error)) {}
        else error = panel->setContext(desired.workspaceId);
        if (error.isEmpty()) { contextApplied = true; appliedWorkspace = desired.workspaceId; }
    }
    if (error.isEmpty()) {
        QSettings host;
        const auto fixture = qEnvironmentVariable("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH");
        auto legacy = fixture.isEmpty()
            ? std::make_unique<QSettings>(QSettings::NativeFormat, QSettings::UserScope,
                QStringLiteral("SimDock"), QStringLiteral("SimDock"))
            : std::make_unique<QSettings>(fixture, QSettings::IniFormat);
        QString migrationError;
        SimDockState::migrateLegacy(*legacy, host, &migrationError);
        setProperty("simdockMigrationError", migrationError);
        auto state = statePending ? SimDockState::decode(desired.state) : panel->saveState();
        auto preferences = SimDockState::preferences(host);
        const auto saved = state.value(QStringLiteral("preferences")).toMap();
        for (auto it = saved.cbegin(); it != saved.cend(); ++it) preferences.insert(it.key(), it.value());
        state.insert(QStringLiteral("preferences"), preferences);
        state.insert(QStringLiteral("workspace"), desired.workspaceId);
        error = panel->restoreState(state);
        if (error.isEmpty()) {
            legacyLayout = state.value(QStringLiteral("legacyLayout")).toMap();
            statePending = false;
        }
    }
    setProperty("nativeComponentReady", error.isEmpty());
    setProperty("nativeComponentError", error);
    if (error.isEmpty() && !property("simdockPreferencesError").toString().isEmpty()) {
        QSettings host;
        QString saveError;
        SimDockState::savePreferences(host, panel->saveState().value(QStringLiteral("preferences")).toMap(), &saveError);
        setProperty("simdockPreferencesError", saveError);
    }
    updateStatus();
    synchronizeTheme();
}

void SimDockContextView::synchronizeTheme()
{
    if (retired) return;
    const bool dark = isDarkTheme(ApplicationThemeManager::instance().mode());
    panel->setDarkTheme(dark);
    setProperty("nativeComponentDarkTheme", dark);
}

QWidget* SimDockContextView::component() const { return panel; }

void SimDockContextView::persistPreferences()
{
    if (invoking) return;
    QSettings host;
    QString error;
    SimDockState::savePreferences(host, panel->saveState().value(QStringLiteral("preferences")).toMap(), &error);
    setProperty("simdockPreferencesError", error);
    updateStatus();
}

void SimDockContextView::updateStatus()
{
    QStringList errors;
    for (const auto* key : {"nativeComponentError", "simdockMigrationError", "simdockPreferencesError"}) {
        const auto message = property(key).toString();
        if (!message.isEmpty() && !errors.contains(message)) errors.append(message);
    }
    status->setText(errors.join(QLatin1Char('\n')));
    status->setVisible(!errors.isEmpty());
    retryButton->setVisible(!errors.isEmpty());
    resetButton->setVisible(!property("nativeComponentError").toString().isEmpty() && statePending);
}

QVariantMap SimDockContextView::saveState() const
{
    if (statePending || !isReady()) return SimDockState::encode(desired.state);
    auto state = panel->saveState();
    if (!legacyLayout.isEmpty()) state.insert(QStringLiteral("legacyLayout"), legacyLayout);
    return SimDockState::encode(state);
}

void SimDockContextView::restoreState(const QVariantMap& state)
{
    desired.state = lastActivation.state = state;
    statePending = !state.isEmpty();
    retry();
}

void SimDockContextView::resetSavedState()
{
    desired.state.clear(); lastActivation.state.clear(); legacyLayout.clear();
    statePending = false;
    retry();
}

bool SimDockContextView::canClose(QString* error) const
{
    const auto* modal = QApplication::activeModalWidget();
    const bool allowed = !(modal && (modal == this || isAncestorOf(modal))) && panel->canClose();
    if (!allowed && error) *error = tr("SimDock is busy. Finish or stop its operation before closing it or changing workspace.");
    return allowed;
}

void SimDockContextView::retire()
{
    retired = true;
    setProperty("nativeComponentRetired", true);
    setEnabled(false);
}
