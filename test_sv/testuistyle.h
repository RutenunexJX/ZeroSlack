#pragma once

#include "applicationthememanager.h"
#include "uicontrols.h"
#include <QDebug>
#include <QPushButton>
#include <memory>

inline bool initializeUiStyleForTest()
{
    ApplicationThemeManager::instance().applyToApplication();
    const std::unique_ptr<QPushButton> probe(UiControls::pushButton(QString()));
    const bool valid = probe->inherits("ElaPushButton")
        && probe->property("zeroslackElaControl").toBool();
    if (valid) qInfo("Test UI backend: ela");
    else qCritical("The maintained UI factory did not create an Ela control");
    return valid;
}
