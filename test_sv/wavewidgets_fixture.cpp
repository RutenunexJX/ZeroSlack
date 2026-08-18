#include <QByteArray>
#include <QFileInfo>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cstddef>
#include <cstring>

extern "C" Q_DECL_EXPORT int wavewidgets_abi_version() noexcept
{
    return 1;
}

extern "C" Q_DECL_EXPORT int wavewidgets_create_simulation_workspace_v1(
    const char* projectPathUtf8,
    QWidget* parent,
    QWidget** workspace,
    char* errorUtf8,
    const std::size_t errorCapacity) noexcept
{
    if (workspace) *workspace = nullptr;
    const QString projectPath = QString::fromUtf8(projectPathUtf8);
    if (!workspace || !QFileInfo::exists(projectPath)) {
        const QByteArray error = QByteArrayLiteral("fixture project is missing");
        if (errorUtf8 && errorCapacity > 0) {
            const auto count = std::min(
                errorCapacity - 1,
                static_cast<std::size_t>(error.size()));
            std::memcpy(errorUtf8, error.constData(), count);
            errorUtf8[count] = '\0';
        }
        return 2;
    }
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("FixtureWaveWorkspace"));
    page->setProperty(
        "wavewidgets.contract",
        QStringLiteral("wave-workbench.simulation-workspace/v1"));
    page->setProperty("wavewidgets.abiVersion", 1);
    page->setProperty(
        "wavewidgets.projectPath", QFileInfo(projectPath).absoluteFilePath());
    *workspace = page;
    return 0;
}
