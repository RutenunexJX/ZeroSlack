#pragma once
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QMainWindow>
#include <QPointer>
#include <QToolButton>
#include <QWindow>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

// Keep native resize/maximize capabilities while drawing the themed title bar.
class WindowSnapChrome final : public QObject, public QAbstractNativeEventFilter {
public:
    WindowSnapChrome(QMainWindow* window, QToolButton* maximize)
        : QObject(window), host(window), maximizeButton(maximize) {
        host->installEventFilter(this);
        qApp->installNativeEventFilter(this);
    }
    ~WindowSnapChrome() override { qApp->removeNativeEventFilter(this); }
    bool eventFilter(QObject*, QEvent* event) override {
#ifdef Q_OS_WIN
        if (event->type() == QEvent::Show && QGuiApplication::platformName() == QStringLiteral("windows")) {
            const HWND handle = reinterpret_cast<HWND>(host->internalWinId());
            const LONG_PTR style = GetWindowLongPtr(handle, GWL_STYLE);
            const LONG_PTR required = WS_OVERLAPPEDWINDOW;
            if ((style & required) != required || (style & WS_POPUP)) {
                SetWindowLongPtr(handle, GWL_STYLE, (style | required) & ~WS_POPUP);
                SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            }
        }
#else
        Q_UNUSED(event)
#endif
        return false;
    }
    bool nativeEventFilter(const QByteArray&, void* message, qintptr* result) override {
#ifdef Q_OS_WIN
        auto* msg = static_cast<MSG*>(message);
        if (!host->internalWinId() || !maximizeButton || msg->hwnd != reinterpret_cast<HWND>(host->internalWinId())) return false;
        if (msg->message == WM_NCCALCSIZE && msg->wParam) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
            if (IsZoomed(msg->hwnd)) {
                MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
                if (GetMonitorInfo(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
                    params->rgrc[0] = monitor.rcWork;
            }
            *result = 0; return true;
        }
        if (msg->message == WM_NCHITTEST) {
            POINT physical{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
            ScreenToClient(msg->hwnd, &physical);
            const qreal scale = host->devicePixelRatioF();
            const QPoint at(qRound(physical.x / scale), qRound(physical.y / scale));
            const QRect maximizeRect(maximizeButton->mapTo(host, QPoint()), maximizeButton->size());
            if (maximizeRect.contains(at)) { *result = HTMAXBUTTON; return true; }
            if (!host->isMaximized()) {
                const bool left = at.x() < 4, right = at.x() >= host->width() - 4;
                const bool top = at.y() < 4, bottom = at.y() >= host->height() - 4;
                if (top || bottom || left || right) {
                    *result = top ? (left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP)
                        : bottom ? (left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM)
                        : left ? HTLEFT : HTRIGHT;
                    return true;
                }
            }
        }
        if ((msg->message == WM_NCLBUTTONDOWN || msg->message == WM_NCLBUTTONUP) && msg->wParam == HTMAXBUTTON) {
            if (msg->message == WM_NCLBUTTONUP) maximizeButton->click();
            *result = 0; return true;
        }
#else
        Q_UNUSED(message) Q_UNUSED(result)
#endif
        return false;
    }
private:
    QMainWindow* host;
    QPointer<QToolButton> maximizeButton;
};
