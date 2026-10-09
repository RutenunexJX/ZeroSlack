#include "simdockstate.h"
#include <QSettings>

namespace {
const auto prefix = QStringLiteral("integrations/simdock/");
const QStringList preferenceKeys{QStringLiteral("simulator/path"), QStringLiteral("waveform/theme")};
const QStringList layoutKeys{QStringLiteral("layout/columns"), QStringLiteral("layout/workbench")};
bool sync(QSettings& settings, QString* error)
{
    settings.sync();
    if (settings.status() == QSettings::NoError) return true;
    if (error) *error = QStringLiteral("Simulation settings could not be saved. Migration will be retried.");
    return false;
}
}

QVariantMap SimDockState::encode(const QVariantMap& state)
{
    auto result = state;
    for (const auto& key : layoutKeys) {
        if (result.value(key).metaType().id() == QMetaType::QByteArray)
            result.insert(key, QVariantMap{{QStringLiteral("encoding"), QStringLiteral("base64")},
                {QStringLiteral("data"), QString::fromLatin1(result.value(key).toByteArray().toBase64())}});
    }
    return result;
}

QVariantMap SimDockState::decode(const QVariantMap& state)
{
    auto result = state;
    auto lost = result.value(QStringLiteral("legacyLayout")).toMap();
    for (const auto& key : layoutKeys) {
        const auto value = result.value(key);
        if (value.metaType().id() == QMetaType::QVariantMap) {
            const auto object = value.toMap();
            const auto decoded = QByteArray::fromBase64Encoding(object.value(QStringLiteral("data")).toString().toLatin1(),
                QByteArray::AbortOnBase64DecodingErrors);
            if (object.value(QStringLiteral("encoding")).toString() == QStringLiteral("base64") && decoded) {
                result.insert(key, decoded.decoded);
                continue;
            }
        } else if (value.metaType().id() == QMetaType::QByteArray || !value.isValid()) {
            continue;
        } else if (value.metaType().id() == QMetaType::QString
                   && !value.toString().contains(QChar::ReplacementCharacter)) {
            result.insert(key, value.toString().toUtf8());
            continue;
        }
        // Old QJsonObject::fromVariantMap replaced non-UTF-8 splitter bytes.
        // Preserve the original value without inventing missing bytes.
        lost.insert(key, value);
        result.remove(key);
    }
    if (!lost.isEmpty()) result.insert(QStringLiteral("legacyLayout"), lost);
    return result;
}

bool SimDockState::migrateLegacy(QSettings& legacy, QSettings& host, QString* error)
{
    // A failed marker flush remains in QSettings' shared in-memory cache.
    // Retry that flush before reporting migration complete.
    if (host.value(prefix + QStringLiteral("legacyMigration/v1")).toBool()) return sync(host, error);
    legacy.sync();
    if (legacy.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("Legacy simulation settings could not be read. Migration will be retried.");
        return false;
    }
    for (const auto& key : preferenceKeys) {
        const auto destination = prefix + QStringLiteral("preferences/") + key;
        if (!host.contains(destination) && legacy.contains(key)) host.setValue(destination, legacy.value(key));
    }
    // These values belong to the retired standalone window and never change
    // the host's theme, workspace, or window geometry.
    for (const auto& key : legacy.allKeys()) {
        const auto destination = prefix + QStringLiteral("legacySnapshot/") + key;
        if (!host.contains(destination)) host.setValue(destination, legacy.value(key));
    }
    if (!sync(host, error)) return false;
    host.setValue(prefix + QStringLiteral("legacyMigration/v1"), true);
    return sync(host, error);
}

QVariantMap SimDockState::preferences(QSettings& host)
{
    QVariantMap values;
    for (const auto& key : preferenceKeys) {
        const auto name = prefix + QStringLiteral("preferences/") + key;
        if (host.contains(name)) values.insert(key, host.value(name));
    }
    return values;
}

bool SimDockState::savePreferences(QSettings& host, const QVariantMap& values, QString* error)
{
    for (const auto& key : preferenceKeys)
        if (values.contains(key)) host.setValue(prefix + QStringLiteral("preferences/") + key, values.value(key));
    return sync(host, error);
}
