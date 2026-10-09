#pragma once
#include "zeroslackexport.h"
#include <QVariantMap>
class QSettings;

namespace SimDockState {
ZEROSLACK_API QVariantMap encode(const QVariantMap& state);
ZEROSLACK_API QVariantMap decode(const QVariantMap& state);
ZEROSLACK_API bool migrateLegacy(QSettings& legacy, QSettings& host, QString* error = nullptr);
ZEROSLACK_API QVariantMap preferences(QSettings& host);
ZEROSLACK_API bool savePreferences(QSettings& host, const QVariantMap& values, QString* error = nullptr);
}
