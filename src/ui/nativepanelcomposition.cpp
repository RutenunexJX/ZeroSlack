#include "nativepanelcomposition.h"
#include <QGuiApplication>
#include <QWidget>
#include <QtConcurrentRun>
#include <vector>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class NativePanelComposition::State
{
public:
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDCompositionDevice> device;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> root;
    struct Layer {
        ComPtr<IDCompositionVisual> visual;
        ComPtr<IDCompositionRectangleClip> clip;
    };
    std::vector<Layer> layers;
    qreal scale = 1;

    bool initialize()
    {
        if (device) return true;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                &d3d, nullptr, &context))) return false;
        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(d3d.As(&dxgi))) return false;
        return SUCCEEDED(DCompositionCreateDevice(dxgi.Get(), __uuidof(IDCompositionDevice),
                                                  reinterpret_cast<void**>(device.GetAddressOf())));
    }

    bool imageVisual(const QImage& image, ComPtr<IDCompositionVisual>& visual)
    {
        const auto pixels = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        ComPtr<IDCompositionSurface> surface;
        if (FAILED(device->CreateSurface(image.width(), image.height(), DXGI_FORMAT_B8G8R8A8_UNORM,
                                         DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return false;
        POINT offset{};
        ComPtr<IDXGISurface> drawing;
        if (FAILED(surface->BeginDraw(nullptr, __uuidof(IDXGISurface),
                reinterpret_cast<void**>(drawing.GetAddressOf()), &offset))) return false;
        ComPtr<ID3D11Texture2D> texture;
        const HRESULT query = drawing.As(&texture);
        if (SUCCEEDED(query)) {
            const D3D11_BOX box{UINT(offset.x), UINT(offset.y), 0,
                UINT(offset.x + image.width()), UINT(offset.y + image.height()), 1};
            context->UpdateSubresource(texture.Get(), 0, &box, pixels.constBits(), pixels.bytesPerLine(), 0);
        }
        const HRESULT end = surface->EndDraw();
        if (FAILED(query) || FAILED(end) || FAILED(d3d->GetDeviceRemovedReason())) return false;
        if (FAILED(device->CreateVisual(&visual))) return false;
        return SUCCEEDED(visual->SetContent(surface.Get()));
    }

    ComPtr<IDCompositionAnimation> animation(float from, float to, int durationMs)
    {
        const float seconds = durationMs / 1000.0f;
        const float distance = to - from;
        ComPtr<IDCompositionAnimation> animation;
        if (FAILED(device->CreateAnimation(&animation))) return {};
        // Ela's OutCubic curve: from + distance * (3t - 3t^2 + t^3).
        if (FAILED(animation->AddCubic(0, from, 3 * distance / seconds,
                -3 * distance / (seconds * seconds), distance / (seconds * seconds * seconds)))) return {};
        if (FAILED(animation->End(seconds, to))) return {};
        return animation;
    }

    void clear()
    {
        if (target) {
            // Complete Qt's raster blits before exposing that surface.
            GdiFlush();
            target->SetRoot(nullptr);
            device->Commit();
        }
        layers.clear(); root.Reset(); target.Reset();
    }
    ~State() { clear(); }
};
#else
class NativePanelComposition::State {};
#endif

NativePanelComposition::NativePanelComposition() : state(std::make_shared<State>()) {}
NativePanelComposition::~NativePanelComposition() = default;

void NativePanelComposition::warmUp()
{
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() != QStringLiteral("windows") || initialization.isValid()) return;
    // Device creation can take hundreds of milliseconds on the first use.
    // The worker owns only COM resources, never HWNDs or Qt widgets.
    initialization = QtConcurrent::run([resources = state] { return resources->initialize(); });
#endif
}

bool NativePanelComposition::prepare(QWidget* parent, const QRect& geometry,
                                     const QList<PanelMotionLayer>& layers)
{
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() != QStringLiteral("windows")) return false;
    if (!initialization.isValid() || !initialization.isFinished() || !initialization.result()) return false;
    state->clear();
    state->scale = parent->devicePixelRatioF();
    // A child HWND clips Qt's backing-store flush. Attach above the host's raster
    // surface instead, so the live endpoint can be painted beneath the animation.
    const auto hwnd = reinterpret_cast<HWND>(parent->winId());
    const D2D_RECT_F clip{0, 0, float(geometry.width() * state->scale), float(geometry.height() * state->scale)};
    if (FAILED(state->device->CreateTargetForHwnd(hwnd, TRUE, &state->target))
        || FAILED(state->device->CreateVisual(&state->root))
        || FAILED(state->root->SetOffsetX(float(qRound(geometry.x() * state->scale))))
        || FAILED(state->root->SetOffsetY(float(qRound(geometry.y() * state->scale))))
        || FAILED(state->root->SetClip(clip))
        || FAILED(state->target->SetRoot(state->root.Get()))) {
        state->clear();
        return false;
    }
    for (const auto& layer : layers) {
        State::Layer nativeLayer;
        if (!state->imageVisual(layer.image, nativeLayer.visual)
            || FAILED(state->device->CreateRectangleClip(&nativeLayer.clip))
            || FAILED(nativeLayer.clip->SetLeft(0.0f))
            || FAILED(nativeLayer.clip->SetTop(0.0f))
            || FAILED(nativeLayer.visual->SetClip(nativeLayer.clip.Get()))
            // With no reference visual, FALSE inserts above all existing siblings.
            || FAILED(state->root->AddVisual(nativeLayer.visual.Get(), FALSE, nullptr))) {
            state->clear(); return false;
        }
        state->layers.push_back(std::move(nativeLayer));
    }
    return true;
#else
    Q_UNUSED(parent); Q_UNUSED(geometry); Q_UNUSED(layers);
    return false;
#endif
}

bool NativePanelComposition::animate(const QList<PanelMotionLayer>& layers, qreal from, qreal to, int durationMs)
{
#ifdef Q_OS_WIN
    if (!state->target || state->layers.size() != size_t(layers.size())) return false;
    for (qsizetype i = 0; i < layers.size(); ++i) {
        const auto& layer = layers[i];
        const auto& native = state->layers[size_t(i)];
        const auto start = layer.position(from) * state->scale, end = layer.position(to) * state->scale;
        const auto startClip = layer.clip(from) * state->scale, endClip = layer.clip(to) * state->scale;
        const auto x = state->animation(start.x(), end.x(), durationMs);
        const auto y = state->animation(start.y(), end.y(), durationMs);
        const auto width = state->animation(startClip.width(), endClip.width(), durationMs);
        const auto height = state->animation(startClip.height(), endClip.height(), durationMs);
        if (!x || !y || !width || !height
            || FAILED(native.visual->SetOffsetX(x.Get())) || FAILED(native.visual->SetOffsetY(y.Get()))
            || FAILED(native.clip->SetRight(width.Get())) || FAILED(native.clip->SetBottom(height.Get()))) return false;
    }
    return SUCCEEDED(state->device->Commit());
#else
    Q_UNUSED(layers); Q_UNUSED(from); Q_UNUSED(to); Q_UNUSED(durationMs);
    return false;
#endif
}

void NativePanelComposition::clear()
{
#ifdef Q_OS_WIN
    if (initialization.isValid() && !initialization.isFinished()) return;
    state->clear();
#endif
}

double NativePanelComposition::refreshRate() const
{
#ifdef Q_OS_WIN
    if (!initialization.isValid() || !initialization.isFinished()) return 0;
    DCOMPOSITION_FRAME_STATISTICS stats{};
    if (state->device && SUCCEEDED(state->device->GetFrameStatistics(&stats))
        && stats.currentCompositionRate.Denominator)
        return double(stats.currentCompositionRate.Numerator) / stats.currentCompositionRate.Denominator;
#endif
    return 0;
}
