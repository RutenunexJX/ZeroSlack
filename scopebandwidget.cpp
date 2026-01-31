#include "scopebandwidget.h"
#include "mycodeeditor.h"
#include "syminfo.h"
#include <QScrollBar>
#include <QResizeEvent>
#include <QTimer>

ScopeBandWidget::ScopeBandWidget(QWidget* parent)
    : QWidget(parent)
{
    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene, this);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setRenderHint(QPainter::Antialiasing, false);
    m_view->setRenderHint(QPainter::SmoothPixmapTransform, false);
    m_view->setBackgroundBrush(QColor(245, 245, 248));
    m_view->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_view->setFixedWidth(kBandWidth);
    setFixedWidth(kBandWidth);
}

ScopeBandWidget::~ScopeBandWidget()
{
    disconnectEditor();
}

void ScopeBandWidget::setEditor(MyCodeEditor* editor)
{
    if (m_editor == editor) return;
    disconnectEditor();
    m_editor = editor;
    connectEditor();
    refresh();
}

void ScopeBandWidget::connectEditor()
{
    if (!m_editor) return;
    connect(m_editor, &QPlainTextEdit::updateRequest,
            this, &ScopeBandWidget::onEditorUpdateRequest, Qt::UniqueConnection);
    connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &ScopeBandWidget::onEditorScrollValueChanged, Qt::UniqueConnection);
    // 行数变化时立即刷新条带，使 logic 绿块马上跟上；卡顿由 updateRequest 合并解决
    m_blockCountConnection = connect(m_editor, &QPlainTextEdit::blockCountChanged,
                                     this, [this](int) { refresh(); });
}

void ScopeBandWidget::disconnectEditor()
{
    if (!m_editor) return;
    disconnect(m_editor, &QPlainTextEdit::updateRequest,
               this, &ScopeBandWidget::onEditorUpdateRequest);
    disconnect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged,
               this, &ScopeBandWidget::onEditorScrollValueChanged);
    disconnect(m_blockCountConnection);
}

void ScopeBandWidget::onEditorUpdateRequest(const QRect& rect, int dy)
{
    Q_UNUSED(rect);
    if (dy != 0) {
        // 仅滚动：只同步滚动条，不重建场景，避免频繁 refresh() 导致卡顿
        syncScrollFromEditor();
        return;
    }
    // 内容/布局变化：合并多次 updateRequest 为一次 refresh，避免卡顿（行数变化由 blockCountChanged 立即刷新）
    if (!m_refreshScheduled) {
        m_refreshScheduled = true;
        QTimer::singleShot(0, this, [this]() {
            m_refreshScheduled = false;
            refresh();
        });
    }
}

void ScopeBandWidget::onEditorScrollValueChanged(int value)
{
    syncScrollFromEditor();
    Q_UNUSED(value);
}

void ScopeBandWidget::syncScrollFromEditor()
{
    if (!m_editor || !m_view) return;
    QScrollBar* editorBar = m_editor->verticalScrollBar();
    m_view->verticalScrollBar()->setRange(editorBar->minimum(), editorBar->maximum());
    m_view->verticalScrollBar()->setValue(editorBar->value());
}

void ScopeBandWidget::refresh()
{
    if (!m_editor || !m_scene || !m_view) return;

    const QString fileName = m_editor->getFileName();
    const qreal docHeight = m_editor->getDocumentHeightPx();
    if (docHeight <= 0) {
        m_scene->clear();
        m_scene->setSceneRect(0, 0, kBandWidth, 1);
        syncScrollFromEditor();
        return;
    }

    m_scene->clear();
    m_scene->setSceneRect(0, 0, kBandWidth, docHeight);

    sym_list* sym = sym_list::getInstance();
    if (!sym) {
        syncScrollFromEditor();
        return;
    }

    QList<sym_list::SymbolInfo> allSymbols = sym->findSymbolsByFileName(fileName);
    QList<sym_list::SymbolInfo> modules;
    QList<sym_list::SymbolInfo> logics;
    for (const auto& s : allSymbols) {
        if (s.symbolType == sym_list::sym_module) modules.append(s);
        else if (s.symbolType == sym_list::sym_logic) logics.append(s);
    }

    struct ModuleItemInfo { ModuleScopeItem* item; int startLine; int endLine; };
    QVector<ModuleItemInfo> moduleInfos;

    for (const sym_list::SymbolInfo& mod : modules) {
        if (!sym->isValidModuleName(mod.symbolName)) continue;
        int endLine = sym->findEndModuleLine(fileName, mod);
        if (endLine < 0) continue;

        qreal top = m_editor->getBlockTopY(mod.startLine);
        qreal bottom = m_editor->getBlockTopY(endLine) + m_editor->getBlockHeight(endLine);
        qreal h = qMax(qreal(1), bottom - top);

        ModuleScopeItem* item = new ModuleScopeItem();
        item->setRect(QRectF(0, 0, kBandWidth, h));
        item->setPos(0, top);
        m_scene->addItem(item);
        moduleInfos.append({ item, mod.startLine, endLine });
    }

    for (const sym_list::SymbolInfo& logic : logics) {
        int startLine = logic.startLine;
        int endLine = logic.endLine;
        if (endLine < startLine) endLine = startLine;

        ModuleScopeItem* parentModule = nullptr;
        qreal parentTop = 0;
        for (const ModuleItemInfo& mi : moduleInfos) {
            if (mi.startLine <= startLine && endLine <= mi.endLine) {
                parentModule = mi.item;
                parentTop = m_editor->getBlockTopY(mi.startLine);
                break;
            }
        }

        qreal logicTop = m_editor->getBlockTopY(startLine);
        qreal logicBottom = m_editor->getBlockTopY(endLine) + m_editor->getBlockHeight(endLine);
        qreal logicH = qMax(qreal(1), logicBottom - logicTop);

        if (parentModule) {
            LogicScopeItem* li = new LogicScopeItem(parentModule);
            li->setPos(0, logicTop - parentTop);
            li->setRect(QRectF(0, 0, kBandWidth, logicH));
        } else {
            LogicScopeItem* li = new LogicScopeItem();
            li->setRect(QRectF(0, 0, kBandWidth, logicH));
            li->setPos(0, logicTop);
            m_scene->addItem(li);
        }
    }

    syncScrollFromEditor();
}

void ScopeBandWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_view)
        m_view->setGeometry(0, 0, event->size().width(), event->size().height());
}
