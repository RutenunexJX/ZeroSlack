#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// Derived from one immutable semantic publication and the configured file snapshot.
// Project ownership and persistence remain in Workbench.
struct DesignModuleCandidate {
    QString name;
    bool ambiguous = false;
    bool added = false;
};
struct DesignFileCandidate {
    QString file;
    QList<DesignModuleCandidate> modules;
    bool analyzed = false;
    bool hasDiagnostics = false;
};
