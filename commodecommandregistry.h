#ifndef COMMODECOMMANDREGISTRY_H
#define COMMODECOMMANDREGISTRY_H

#include <QList>
#include <QString>

enum class ComModeCommandInputKind {
    Fixed,
    ModuleRelativeLine
};

struct ComModeCommandMetadata {
    QString command;
    bool executable = false;
    ComModeCommandInputKind inputKind = ComModeCommandInputKind::Fixed;
    QString family;
    QString title;
    QString description;
};

const QList<ComModeCommandMetadata>& comModeCommandRegistry();
const ComModeCommandMetadata* findComModeCommandMetadata(
    const QString& command);
bool comModeCommandRegistryIsValid(QString* reason = nullptr);
QString comModeCommandHint(const QString& buffer);
QString executableComModeCommand(const QString& buffer);
bool isComModeBufferPrefix(const QString& buffer);
bool isComModeLineBuffer(const QString& buffer);

#endif // COMMODECOMMANDREGISTRY_H
