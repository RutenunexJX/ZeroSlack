#pragma once
#include <QByteArray>
#include <QtCore/qglobal.h>

// Host copy of the native contract. Kept independent of the optional SuiteApp SDK.
// Matches include/simdock/WorkbenchApi.h; private Ela does not replace host Ela.
inline QByteArray simdockExpectedWorkbenchAbi()
{
    QByteArray result = "simdock-workbench/v1;qt=" QT_VERSION_STR ";bits=";
    result += QByteArray::number(sizeof(void *) * 8);
#if defined(__GNUC__)
    result += ";gcc=" + QByteArray::number(__GNUC__) + '.' + QByteArray::number(__GNUC_MINOR__)
        + '.' + QByteArray::number(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    result += ";msvc=" + QByteArray::number(_MSC_VER);
#else
    result += ";compiler=unsupported";
#endif
#ifdef QT_DEBUG
    result += ";debug=1";
#else
    result += ";debug=0";
#endif
    return result + ";ela=private-SimDockEla/v1";
}
