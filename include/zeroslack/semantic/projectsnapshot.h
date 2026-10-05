#pragma once
#include <zeroslack/semantic/symboltaxonomy.h>
#include <QHash>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <cstdint>

struct ProjectSnapshot {
    std::uint64_t revision = 0;
    QString workspaceRoot;
    QStringList allFiles;
    QStringList systemVerilogFiles;
    QStringList includeDirs;
    QHash<QString, QString> defines;
    QStringList fileExtensions;
    QHash<QString, SymbolTaxonomy::SourceRole> sourceRoles;
    QString topModule;
    QStringList ignoredPaths;
    // Cached/loading file lists are useful for navigation but are not yet a
    // complete semantic input. A completed empty scan is authoritative.
    bool sourceDiscoveryComplete = true;

    bool isOpen() const { return !workspaceRoot.isEmpty(); }
    // Ordered source/include paths are semantic inputs, not sets. The key is
    // shared by request scheduling, dependency graphs and retained workspaces.
    QString semanticIdentity() const;
    QStringList filesForSourceRole(SymbolTaxonomy::SourceRole role) const;
    QStringList designSourceFiles() const;
    QStringList headerSourceFiles() const;
};


Q_DECLARE_METATYPE(ProjectSnapshot)
