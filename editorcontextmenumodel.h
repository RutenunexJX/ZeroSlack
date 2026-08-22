#ifndef EDITORCONTEXTMENUMODEL_H
#define EDITORCONTEXTMENUMODEL_H

#include "editoractioncontextservice.h"

#include <QList>
#include <QString>

enum class EditorContextMenuSection {
    Standard,
    Navigate,
    Inspect,
    Refactor
};

struct EditorContextMenuCapability {
    QString actionId;
    bool relevant = true;
    bool executable = true;
    QString unavailableReason;
    bool standard = false;
    bool enterableWhenUnavailable = false;
};

struct EditorContextMenuRequest {
    EditorActionContext actionContext;
    bool symbolAvailable = false;
    QList<EditorContextMenuCapability> capabilities;
};

struct EditorContextMenuItem {
    QString actionId;
    QString text;
    EditorContextMenuSection section = EditorContextMenuSection::Refactor;
    bool executable = false;
    bool enabled = false;
    QString visibleReason;
};

struct EditorContextMenuSectionModel {
    EditorContextMenuSection section = EditorContextMenuSection::Standard;
    QString title;
    QList<EditorContextMenuItem> items;
};

struct EditorContextMenuModel {
    QList<EditorContextMenuSectionModel> sections;
};

EditorContextMenuModel buildEditorContextMenuModel(
    const EditorContextMenuRequest& request);
QString editorContextMenuSectionText(EditorContextMenuSection section);

#endif // EDITORCONTEXTMENUMODEL_H
