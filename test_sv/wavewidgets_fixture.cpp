#include <QByteArray>
#include <QFileInfo>
#include <QStringList>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cstddef>
#include <cstring>

class FixtureWaveWorkspace final : public QWidget
{
    Q_OBJECT

public:
    using QWidget::QWidget;

    Q_INVOKABLE bool canRevealSourceObject(
        const QString& semanticId,
        const QString& sourceFile,
        int sourceLine,
        int sourceColumn,
        const QString& symbolName,
        const QString& accessPath) const
    {
        Q_UNUSED(semanticId)
        Q_UNUSED(symbolName)
        Q_UNUSED(accessPath)
        return sourceFile == QStringLiteral("rtl/test.sv")
            && sourceLine == 7 && sourceColumn == 3;
    }

    Q_INVOKABLE bool revealSourceObject(
        const QString& semanticId,
        const QString& sourceFile,
        int sourceLine,
        int sourceColumn,
        const QString& symbolName,
        const QString& accessPath)
    {
        return canRevealSourceObject(
            semanticId, sourceFile, sourceLine, sourceColumn,
            symbolName, accessPath);
    }

signals:
    void sourceNavigationRequested(
        const QString& sourceFile,
        int sourceLine,
        int sourceColumn,
        const QString& semanticId,
        const QString& kind);
};

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
    auto* page = new FixtureWaveWorkspace(parent);
    page->setObjectName(QStringLiteral("FixtureWaveWorkspace"));
    page->setProperty(
        "wavewidgets.contract",
        QStringLiteral("wave-workbench.simulation-workspace/v1"));
    page->setProperty("wavewidgets.abiVersion", 1);
    page->setProperty(
        "wavewidgets.capabilities",
        QStringList{QStringLiteral("result-source-navigation/v1")});
    page->setProperty(
        "wavewidgets.projectPath", QFileInfo(projectPath).absoluteFilePath());
    *workspace = page;
    return 0;
}

#include "wavewidgets_fixture.moc"
