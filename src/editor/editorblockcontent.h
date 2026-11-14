#pragma once
#include "documentblockmodel.h"
#include <QWidget>
#include <QPointer>
#include <functional>
class QTextEdit;
class QLabel;
class PinloomPreviewImageLabel;
class PinloomHostClient;
class EditorBlockPreview;
class QMenu;
class WaveBlockContent;

// The same content control is used by inline and detached block hosts.
class ZEROSLACK_API EditorBlockContent final : public QWidget {
public:
    EditorBlockContent(DocumentBlockModel* model, PinloomHostClient* client,
        const EditorAssociatedBlock& block, QWidget* parent);
    void present(const EditorAssociatedBlock& block, bool readOnly, bool compact = false);
    bool setEditing(bool value);
    void rebind(DocumentBlockModel* model);
    bool isEditing() const { return editing; }
    bool nativeWaveEditing() const { return editing && wave; }
    static void refreshTools(QWidget* frame, QWidget* tools);
    QTextEdit* textEditor() const { return text; }
    PinloomPreviewImageLabel* imagePreview() const { return image; }
    static void addAppearanceMenu(QMenu* menu, DocumentBlockModel* model, const QString& id,
        QWidget* host, std::function<bool()> readOnly);
    std::function<void()> openOriginal;
private:
    QPointer<DocumentBlockModel> model;
    QMetaObject::Connection retryConnection;
    QString id, resourceKey;
    QTextEdit* text = nullptr;
    QTextEdit* referenceText = nullptr;
    QLabel* description = nullptr;
    PinloomPreviewImageLabel* image = nullptr;
    QWidget* imageHost = nullptr;
    EditorBlockPreview* preview = nullptr;
    WaveBlockContent* wave = nullptr;
    quint64 revision=0, styleRevision=0;
    bool updating=false, editing=false, locked=false;
};
