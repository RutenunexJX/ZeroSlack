#pragma once

#include "applicationthememanager.h"

// Existing workflow tests can run against the real Ela controls as well as Qt.
inline bool initializeUiStyleForTest()
{
    if (qEnvironmentVariable("ZEROSLACK_TEST_UI_STYLE") != QStringLiteral("ela"))
        return true;
    auto& manager = ApplicationThemeManager::instance();
    if (!manager.selectBackend(UiStyleBackend::Ela))
        return false;
    manager.applyToApplication();
    return true;
}
