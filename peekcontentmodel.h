#ifndef PEEKCONTENTMODEL_H
#define PEEKCONTENTMODEL_H

#include <QSize>
#include <QString>
#include <QVector>

enum class PeekContentKind {
    DefinitionPreview,
    EffectiveValue,
    NumericRadix,
    DiagnosticDetail,
    DeclarationPreview,
    CodePreview,
    FoldShelfPreview,
    ActionPlanPreview
};

enum class PeekContentRowRole {
    Body,
    Muted,
    Code,
    HighlightedCode,
    Caret,
    Warning
};

struct PeekContentRow {
    QString text;
    PeekContentRowRole role = PeekContentRowRole::Body;
    bool wordWrap = false;
    bool detail = false;
    QString fieldLabel;
    QString fieldValue;
    bool prominent = false;
};

struct PeekNavigationTarget {
    QString fileName;
    int line = -1;
    int column = -1;

    bool isValid() const
    {
        return !fileName.isEmpty() && line > 0;
    }
};

struct PeekEditableTextModel {
    bool enabled = false;
    bool readOnly = false;
    QString text;
    QString placeholderText;
    QString objectName;
    int minimumWidth = 0;
};

struct PeekReadOnlyTextModel {
    bool enabled = false;
    QString text;
    QString objectName;
    QSize minimumSize = QSize(320, 160);
    bool wordWrap = false;
};

enum class PeekContentActionRole {
    Primary,
    Secondary,
    Destructive
};

struct PeekContentAction {
    QString id;
    QString label;
    PeekContentActionRole role =
        PeekContentActionRole::Secondary;
    bool enabled = true;
    bool defaultAction = false;
};

struct PeekContentModel {
    PeekContentKind kind = PeekContentKind::DeclarationPreview;
    QString title;
    bool symbolCard = false;
    QString category;
    bool portAccent = false;
    QVector<PeekContentRow> rows;
    PeekNavigationTarget navigationTarget;
    PeekEditableTextModel editor;
    PeekReadOnlyTextModel readOnlyText;
    QVector<PeekContentAction> actions;
    QSize maximumSize = QSize(760, 480);

    bool isEmpty() const
    {
        return title.isEmpty()
            && rows.isEmpty()
            && !editor.enabled
            && !readOnlyText.enabled
            && actions.isEmpty();
    }
};

#endif // PEEKCONTENTMODEL_H
