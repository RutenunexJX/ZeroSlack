#include "scopebandwidget.h"
#include "mycodeeditor.h"
#include "scopebandservice.h"
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

void ScopeBandWidget::setDocumentFileName(const QString& fileName)
{
    if (m_fileName == fileName)
        return;

    m_fileName = fileName;
    refresh();
}

void ScopeBandWidget::connectEditor()
{
    if (!m_editor) return;
    connect(m_editor, &QPlainTextEdit::updateRequest,
            this, &ScopeBandWidget::onEditorUpdateRequest, Qt::UniqueConnection);
    connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &ScopeBandWidget::onEditorScrollValueChanged, Qt::UniqueConnection);
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
        syncScrollFromEditor();
        return;
    }
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

    const qreal docHeight = m_editor->getDocumentHeightPx();
    if (docHeight <= 0) {
        m_scene->clear();
        m_scene->setSceneRect(0, 0, kBandWidth, 1);
        syncScrollFromEditor();
        return;
    }

    m_scene->clear();
    m_scene->setSceneRect(0, 0, kBandWidth, docHeight);

    ScopeBandQuery query;
    query.fileName = m_fileName;
    const ScopeBandReport report =
        ScopeBandService::getInstance()->scopeBands(query);

    struct ModuleItemInfo { ModuleScopeItem* item; int startLine; int endLine; };
    QVector<ModuleItemInfo> moduleInfos;

    for (const ScopeBandSymbolRange& module : report.modules) {
        const sym_list::SymbolInfo& mod = module.symbol;
        const int endLine = module.endLine;

        qreal top = m_editor->getBlockTopY(mod.startLine);
        qreal bottom = m_editor->getBlockTopY(endLine) + m_editor->getBlockHeight(endLine);
        qreal h = qMax(qreal(1), bottom - top);

        ModuleScopeItem* item = new ModuleScopeItem();
        item->setRect(QRectF(0, 0, kBandWidth, h));
        item->setPos(0, top);
        m_scene->addItem(item);
        moduleInfos.append({ item, mod.startLine, endLine });
    }

    for (const ScopeBandSymbolRange& logicRange : report.logics) {
        const sym_list::SymbolInfo& logic = logicRange.symbol;
        int startLine = logic.startLine;
        int endLine = logicRange.endLine;

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
