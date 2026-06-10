#ifndef ALTERNATECOMMANDSERVICE_H
#define ALTERNATECOMMANDSERVICE_H

#include <QString>
#include <QStringList>
#include <memory>

enum class AlternateCommandAction {
    None,
    Save,
    SaveAs,
    Open,
    NewFile,
    Close,
    Copy,
    Paste,
    Cut,
    Undo,
    Redo,
    Find,
    Replace,
    GotoLine,
    SelectAll,
    Comment,
    Uncomment,
    Indent,
    Unindent
};

struct AlternateCommandCompletionState {
    QString normalizedInput;
    QStringList matches;
    bool showCompletions = false;
};

class AlternateCommandService
{
public:
    static AlternateCommandService* getInstance();

    AlternateCommandService();
    ~AlternateCommandService();

    QString normalizeCommandInput(const QString& input) const;
    QStringList commands() const;
    QStringList matchingCommands(const QString& filter) const;
    AlternateCommandCompletionState completionState(
        const QString& input) const;
    bool isKnownCommand(const QString& command) const;
    AlternateCommandAction commandAction(const QString& command) const;

private:
    QStringList commandCatalog;
    static std::unique_ptr<AlternateCommandService> instance;
};

#endif // ALTERNATECOMMANDSERVICE_H
