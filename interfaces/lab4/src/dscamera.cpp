#include "dscamera.h"

#include "strutil.h"

#include <dshow.h>
#include <oaidl.h>
#include <qedit.h>
#include <strmif.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <cwctype>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#define SAFE_RELEASE(p)      \
    do {                     \
        if (p) {             \
            (p)->Release();  \
            (p) = nullptr;   \
        }                    \
    } while (0)

namespace {

constexpr wchar_t kBackendName[] = L"DirectShow (Smart Tee + Sample Grabber + VMR-7)";

static const GUID kNullRendererGUID = {0xc1f400a4, 0x3f08, 0x11d3,
                                       {0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e,
                                        0x37}};

std::wstring readBagValue(IPropertyBag* bag, const wchar_t* prop)
{
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    std::wstring result;
    if (SUCCEEDED(bag->Read(prop, &v, nullptr)) && v.vt == VT_BSTR && v.bstrVal)
        result = v.bstrVal;
    VariantClear(&v);
    return result;
}

std::wstring deriveVendor(const std::wstring& name)
{
    static const wchar_t* const kStopWords[] = {
        L"Cam", L"Camera", L"Webcam", L"Web cam", L"HD", L"USB", L"USB2",
        L"Integrated", L"FaceTime", L"Front", L"Rear", L"Built", L"Video",
        L"OBS", L"Virtual",
    };

    std::size_t cut = std::wstring::npos;
    const std::wstring lower = [&name]() {
        std::wstring s = name;
        for (wchar_t& ch : s)
            ch = static_cast<wchar_t>(towlower(ch));
        return s;
    }();
    for (const wchar_t* stop : kStopWords) {
        std::wstring word = stop;
        for (wchar_t& ch : word)
            ch = static_cast<wchar_t>(towlower(ch));
        const std::size_t pos = lower.find(word);
        if (pos != std::wstring::npos && pos > 0) {
            cut = pos;
            break;
        }
    }
    if (cut == std::wstring::npos)
        return name;
    std::wstring result = name.substr(0, cut);
    while (!result.empty() && (result.back() == L' ' || result.back() == L'-'))
        result.pop_back();
    return result.empty() ? L"—" : result;
}

void freeMediaType(AM_MEDIA_TYPE& mt)
{
    if (mt.cbFormat && mt.pbFormat)
        CoTaskMemFree(mt.pbFormat);
    if (mt.pUnk)
        mt.pUnk->Release();
    ZeroMemory(&mt, sizeof(mt));
}

std::wstring formatFourcc(unsigned long fourcc)
{
    if (fourcc == 0)
        return L"";
    wchar_t buf[8];
    buf[0] = static_cast<wchar_t>(fourcc & 0xFF);
    buf[1] = static_cast<wchar_t>((fourcc >> 8) & 0xFF);
    buf[2] = static_cast<wchar_t>((fourcc >> 16) & 0xFF);
    buf[3] = static_cast<wchar_t>((fourcc >> 24) & 0xFF);
    buf[4] = 0;
    return buf;
}

std::wstring formatName(unsigned long fourcc, unsigned short bitcount)
{
    if (fourcc != 0)
        return formatFourcc(fourcc);
    if (bitcount == 32)
        return L"RGB32";
    if (bitcount == 24)
        return L"RGB24";
    if (bitcount == 16)
        return L"RGB565";
    if (bitcount == 8)
        return L"RGB8";
    return L"RGB";
}

bool getPin(IBaseFilter* filter, PIN_DIRECTION dir, int index, IPin** out)
{
    *out = nullptr;
    IEnumPins* e = nullptr;
    if (FAILED(filter->EnumPins(&e)))
        return false;
    int found = -1;
    IPin* pin = nullptr;
    while (e->Next(1, &pin, nullptr) == S_OK) {
        PIN_INFO info = {};
        if (SUCCEEDED(pin->QueryPinInfo(&info))) {
            const BOOL match = (info.dir == dir);
            if (info.pFilter)
                info.pFilter->Release();
            if (match && ++found == index) {
                *out = pin;
                e->Release();
                return true;
            }
        }
        pin->Release();
    }
    e->Release();
    return false;
}

} // namespace

class GrabberCallback : public ISampleGrabberCB
{
public:
    explicit GrabberCallback(DsCamera* owner)
        : m_owner(owner), m_ref(1)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_ISampleGrabberCB) {
            *ppv = static_cast<ISampleGrabberCB*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG r = --m_ref;
        if (r == 0)
            delete this;
        return r;
    }

    STDMETHODIMP SampleCB(double, IMediaSample* sample) override
    {
        BYTE* data = nullptr;
        long len = 0;
        if (SUCCEEDED(sample->GetPointer(&data)))
            len = sample->GetActualDataLength();
        if (data && len > 0)
            m_owner->onSample(data, len);
        return S_OK;
    }

    STDMETHODIMP BufferCB(double, BYTE* data, long len) override
    {
        if (data && len > 0)
            m_owner->onSample(data, len);
        return S_OK;
    }

private:
    DsCamera* m_owner;
    ULONG m_ref;
};

class RecCallback : public ISampleGrabberCB
{
public:
    explicit RecCallback(DsCamera* owner)
        : m_owner(owner), m_ref(1)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_ISampleGrabberCB) {
            *ppv = static_cast<ISampleGrabberCB*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG r = --m_ref;
        if (r == 0)
            delete this;
        return r;
    }

    STDMETHODIMP SampleCB(double, IMediaSample* sample) override
    {
        BYTE* data = nullptr;
        long len = 0;
        if (SUCCEEDED(sample->GetPointer(&data)))
            len = sample->GetActualDataLength();
        if (data && len > 0)
            m_owner->onRecordSample(data, len);
        return S_OK;
    }

    STDMETHODIMP BufferCB(double, BYTE* data, long len) override
    {
        if (data && len > 0)
            m_owner->onRecordSample(data, len);
        return S_OK;
    }

private:
    DsCamera* m_owner;
    ULONG m_ref;
};

DsCamera::DsCamera() = default;

DsCamera::~DsCamera()
{
    disconnect();
}

std::vector<CameraDevice> DsCamera::enumerate()
{
    std::vector<CameraDevice> out;
    ICreateDevEnum* devEnum = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ICreateDevEnum, reinterpret_cast<void**>(&devEnum))))
        return out;

    IEnumMoniker* enumerator = nullptr;
    HRESULT hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumerator, 0);
    if (SUCCEEDED(hr) && enumerator) {
        IMoniker* moniker = nullptr;
        while (enumerator->Next(1, &moniker, nullptr) == S_OK) {
            CameraDevice device;
            IPropertyBag* bag = nullptr;
            if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag,
                                                 reinterpret_cast<void**>(&bag)))) {
                device.name = readBagValue(bag, L"FriendlyName");
                device.path = readBagValue(bag, L"DevicePath");
                bag->Release();
            }
            if (device.name.empty()) {
                LPOLESTR display = nullptr;
                if (SUCCEEDED(moniker->GetDisplayName(nullptr, nullptr, &display)))
                    device.name = display;
                CoTaskMemFree(display);
            }
            device.vendor = deriveVendor(device.name);
            out.push_back(std::move(device));
            moniker->Release();
        }
        enumerator->Release();
    }
    devEnum->Release();
    return out;
}

bool DsCamera::connectByIndex(int index, HWND hwndPreview, const RECT& previewRect)
{
    if (connected())
        return false;

    const std::vector<CameraDevice> devices = enumerate();
    if (index < 0 || static_cast<std::size_t>(index) >= devices.size())
        return false;
    m_name = devices[static_cast<std::size_t>(index)].name;
    m_vendor = devices[static_cast<std::size_t>(index)].vendor;
    m_previewRect = previewRect;

    if (!buildGraph(index, hwndPreview)) {
        teardownGraph();
        return false;
    }
    return true;
}

bool DsCamera::buildGraph(int index, HWND hwndPreview)
{
    std::vector<IMoniker*> monikers = [&index]() {
        std::vector<IMoniker*> out;
        ICreateDevEnum* devEnum = nullptr;
        if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_ICreateDevEnum,
                                    reinterpret_cast<void**>(&devEnum))))
            return out;
        IEnumMoniker* enumerator = nullptr;
        if (SUCCEEDED(devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,
                                                     &enumerator, 0))
            && enumerator) {
            IMoniker* moniker = nullptr;
            while (enumerator->Next(1, &moniker, nullptr) == S_OK) {
                out.push_back(moniker);
            }
            enumerator->Release();
        }
        devEnum->Release();
        if (index < 0 || static_cast<std::size_t>(index) >= out.size()) {
            for (IMoniker* m : out)
                m->Release();
            out.clear();
        }
        return out;
    }();

    if (monikers.empty())
        return false;

    HRESULT hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IGraphBuilder,
                                  reinterpret_cast<void**>(&m_graph));
    if (FAILED(hr))
        return false;
    m_graph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&m_control));

    hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ICaptureGraphBuilder2,
                          reinterpret_cast<void**>(&m_build));
    if (FAILED(hr))
        return false;
    m_build->SetFiltergraph(m_graph);

    IFilterGraph2* graph2 = nullptr;
    m_graph->QueryInterface(IID_IFilterGraph2, reinterpret_cast<void**>(&graph2));
    if (!graph2)
        return false;
    hr = graph2->AddSourceFilterForMoniker(monikers[static_cast<std::size_t>(index)], nullptr,
                                           L"CameraSource", &m_source);
    graph2->Release();
    for (IMoniker* m : monikers)
        m->Release();
    if (FAILED(hr))
        return false;

    hr = CoCreateInstance(CLSID_SmartTee, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter,
                          reinterpret_cast<void**>(&m_tee));
    if (FAILED(hr))
        return false;
    m_graph->AddFilter(m_tee, L"SmartTee");

    hr = CoCreateInstance(CLSID_VideoMixingRenderer, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IBaseFilter, reinterpret_cast<void**>(&m_vmr));
    if (FAILED(hr))
        return false;
    m_graph->AddFilter(m_vmr, L"VMR7");
    IVMRFilterConfig* config = nullptr;
    if (SUCCEEDED(m_vmr->QueryInterface(IID_IVMRFilterConfig,
                                        reinterpret_cast<void**>(&config)))) {
        config->SetRenderingMode(VMRMode_Windowless);
        config->SetNumberOfStreams(1);
        config->Release();
    }
    if (FAILED(m_vmr->QueryInterface(IID_IVMRWindowlessControl,
                                     reinterpret_cast<void**>(&m_vmrwc))))
        return false;
    m_vmrwc->SetVideoClippingWindow(hwndPreview);
    m_vmrwc->SetAspectRatioMode(VMR_ARMODE_NONE);

    hr = CoCreateInstance(CLSID_SampleGrabber, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter,
                          reinterpret_cast<void**>(&m_grabFilter));
    if (FAILED(hr))
        return false;
    m_graph->AddFilter(m_grabFilter, L"SampleGrabber");
    m_grabFilter->QueryInterface(IID_ISampleGrabber, reinterpret_cast<void**>(&m_grab));
    m_cb = new GrabberCallback(this);
    m_grab->SetBufferSamples(FALSE);
    m_grab->SetCallback(m_cb, 0);

    IPin* srcPin = nullptr;
    m_build->FindPin(m_source, PINDIR_OUTPUT, &PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, FALSE, 0,
                     &srcPin);
    if (!srcPin)
        getPin(m_source, PINDIR_OUTPUT, 0, &srcPin);
    IPin* teeIn = nullptr;
    getPin(m_tee, PINDIR_INPUT, 0, &teeIn);
    if (!srcPin || !teeIn)
        return false;
    hr = m_graph->Connect(srcPin, teeIn);
    SAFE_RELEASE(srcPin);
    SAFE_RELEASE(teeIn);
    if (FAILED(hr))
        return false;

    AM_MEDIA_TYPE mt = {};
    mt.majortype = MEDIATYPE_Video;
    mt.subtype = MEDIASUBTYPE_RGB24;
    mt.formattype = FORMAT_VideoInfo;
    mt.cbFormat = sizeof(VIDEOINFOHEADER);
    mt.pbFormat = static_cast<BYTE*>(CoTaskMemAlloc(sizeof(VIDEOINFOHEADER)));
    if (mt.pbFormat)
        ZeroMemory(mt.pbFormat, sizeof(VIDEOINFOHEADER));
    m_grab->SetMediaType(&mt);
    freeMediaType(mt);

    hr = m_build->RenderStream(&PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video, m_tee, m_grabFilter,
                               m_vmr);
    if (FAILED(hr)) {
        m_grab->SetMediaType(nullptr);
        hr = m_build->RenderStream(&PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video, m_tee, m_grabFilter,
                                   m_vmr);
    }
    if (FAILED(hr)) {
        IPin* previewPin = nullptr;
        if (FAILED(m_build->FindPin(m_tee, PINDIR_OUTPUT, &PIN_CATEGORY_PREVIEW, nullptr,
                                    FALSE, 0, &previewPin)))
            getPin(m_tee, PINDIR_OUTPUT, 0, &previewPin);
        IPin* grabIn = nullptr;
        getPin(m_grabFilter, PINDIR_INPUT, 0, &grabIn);
        bool ok1 = false;
        if (previewPin && grabIn) {
            hr = m_graph->Connect(previewPin, grabIn);
            ok1 = SUCCEEDED(hr);
        }
        SAFE_RELEASE(previewPin);
        SAFE_RELEASE(grabIn);
        IPin* grabOut = nullptr;
        IPin* vmrIn = nullptr;
        getPin(m_grabFilter, PINDIR_OUTPUT, 0, &grabOut);
        getPin(m_vmr, PINDIR_INPUT, 0, &vmrIn);
        bool ok2 = false;
        if (grabOut && vmrIn) {
            hr = m_graph->Connect(grabOut, vmrIn);
            ok2 = SUCCEEDED(hr);
        }
        SAFE_RELEASE(grabOut);
        SAFE_RELEASE(vmrIn);
        if (!ok1 || !ok2)
            return false;
    }

    hr = m_control->Run();
    if (FAILED(hr))
        return false;

    readMediaTypeInfo();
    QueryPerformanceCounter(&m_fpsMark);
    m_sampleCount = 0;
    m_sampleCountPrev = 0;
    return true;
}

void DsCamera::readMediaTypeInfo()
{
    AM_MEDIA_TYPE mt = {};
    if (m_grab && SUCCEEDED(m_grab->GetConnectedMediaType(&mt))) {
        if (mt.formattype == FORMAT_VideoInfo && mt.cbFormat >= sizeof(VIDEOINFOHEADER)
            && mt.pbFormat) {
            const VIDEOINFOHEADER* vih =
                reinterpret_cast<const VIDEOINFOHEADER*>(mt.pbFormat);
            m_width = vih->bmiHeader.biWidth;
            m_height = std::abs(vih->bmiHeader.biHeight);
            if (vih->AvgTimePerFrame > 0)
                m_fps = 10000000.0 / vih->AvgTimePerFrame;
            m_fourcc = vih->bmiHeader.biCompression;
            m_bitcount = vih->bmiHeader.biBitCount;
            if (m_height > 0 && m_bitcount >= 8) {
                const long image = vih->bmiHeader.biSizeImage;
                if (image > 0)
                    m_stride = static_cast<int>(image / m_height);
                else
                    m_stride = ((m_width * m_bitcount + 31) / 32) * 4;
            }
            m_channels = m_bitcount / 8;
            if (m_channels < 3 || m_channels > 4)
                m_channels = 3;
        }
        freeMediaType(mt);
    }
    m_pixelFormat = formatName(m_fourcc, m_bitcount);
}

void DsCamera::disconnect()
{
    teardownGraph();
    m_name.clear();
    m_vendor.clear();
    m_pixelFormat.clear();
}

void DsCamera::teardownGraph()
{
    if (m_recording) {
        m_control->Stop();
        finalizeRecordWriter();
        m_recording = false;
        removeRecordBranch();
    }
    if (m_control)
        m_control->Stop();
    if (m_grab)
        m_grab->SetCallback(nullptr, 0);

    SAFE_RELEASE(m_vmrwc);
    SAFE_RELEASE(m_grab);
    SAFE_RELEASE(m_grabFilter);
    SAFE_RELEASE(m_vmr);
    SAFE_RELEASE(m_tee);
    SAFE_RELEASE(m_source);
    SAFE_RELEASE(m_cb);
    SAFE_RELEASE(m_build);
    SAFE_RELEASE(m_control);
    SAFE_RELEASE(m_graph);

    m_frame.clear();
    m_width = 0;
    m_height = 0;
    m_stride = 0;
    m_channels = 3;
    m_fourcc = 0;
    m_bitcount = 24;
    m_fps = 0.0;
    m_fpsMeasured = 0.0;
    m_sampleCount = 0;
    m_sampleCountPrev = 0;
    m_recording = false;
}

std::wstring DsCamera::deviceName() const
{
    return m_name.empty() ? L"—" : m_name;
}

std::wstring DsCamera::vendor() const
{
    return m_vendor.empty() ? L"—" : m_vendor;
}

std::wstring DsCamera::pixelFormat() const
{
    return m_pixelFormat.empty() ? L"—" : m_pixelFormat;
}

std::wstring DsCamera::backendInfo() const
{
    return kBackendName;
}

int DsCamera::width() const
{
    return m_width;
}

int DsCamera::height() const
{
    return m_height;
}

double DsCamera::fps() const
{
    return m_fps;
}

double DsCamera::fpsMeasured() const
{
    return m_fpsMeasured;
}

void DsCamera::onSample(const unsigned char* data, long length)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!data || length <= 0)
        return;
    if (m_frame.size() != static_cast<std::size_t>(length))
        m_frame.resize(static_cast<std::size_t>(length));
    std::memcpy(m_frame.data(), data, static_cast<std::size_t>(length));
    ++m_sampleCount;
}

bool DsCamera::grabFrame(RawFrame& out, std::vector<unsigned char>& buffer)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_frame.empty() || m_width <= 0 || m_height <= 0)
        return false;

    buffer = m_frame;
    const std::size_t length = buffer.size();
    const std::size_t area =
        static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);

    int channels = m_channels;
    if (m_fourcc == 0) {
        if (channels > 4)
            channels = 4;
        if (channels < 3)
            channels = 3;
    } else {
        channels = 2;
    }
    if (channels == 3 && length >= area * 4)
        channels = 4;
    if (length < area * static_cast<std::size_t>(channels))
        channels = 2;

    int stride = 0;
    if (m_height > 0)
        stride = static_cast<int>(length / static_cast<std::size_t>(m_height));
    const int minStride = m_width * channels;
    if (stride < minStride && channels == 2)
        stride = minStride;

    out.data = buffer.data();
    out.size = length;
    out.width = m_width;
    out.height = m_height;
    out.stride = stride;
    out.channels = channels;
    out.fourcc = m_fourcc;
    return true;
}

void DsCamera::repaintVideo(HWND hwnd, HDC hdc)
{
    if (!m_vmrwc)
        return;

    LONG nativeW = 0, nativeH = 0, arW = 0, arH = 0;
    m_vmrwc->GetNativeVideoSize(&nativeW, &nativeH, &arW, &arH);
    if (nativeW <= 0 || nativeH <= 0) {
        nativeW = m_width;
        nativeH = m_height;
    }
    if (nativeW <= 0 || nativeH <= 0)
        return;

    const int roomW = m_previewRect.right - m_previewRect.left;
    const int roomH = m_previewRect.bottom - m_previewRect.top;
    const double scale = std::min(static_cast<double>(roomW) / nativeW,
                                  static_cast<double>(roomH) / nativeH);
    const LONG drawW = static_cast<LONG>(nativeW * scale);
    const LONG drawH = static_cast<LONG>(nativeH * scale);
    RECT dest;
    dest.left = m_previewRect.left + (roomW - drawW) / 2;
    dest.top = m_previewRect.top + (roomH - drawH) / 2;
    dest.right = dest.left + drawW;
    dest.bottom = dest.top + drawH;
    m_vmrwc->SetVideoPosition(nullptr, &dest);
    m_vmrwc->RepaintVideo(hwnd, hdc);
}

void DsCamera::updateMeasuredFps()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    static LARGE_INTEGER freq;
    static bool haveFreq = false;
    if (!haveFreq) {
        QueryPerformanceFrequency(&freq);
        haveFreq = true;
    }
    const double dt =
        static_cast<double>(now.QuadPart - m_fpsMark.QuadPart) / freq.QuadPart;
    if (dt >= 1.0 && m_sampleCount >= m_sampleCountPrev) {
        m_fpsMeasured = (m_sampleCount - m_sampleCountPrev) / dt;
        m_sampleCountPrev = m_sampleCount;
        m_fpsMark = now;
    }
}

bool DsCamera::startRecording(const std::wstring& path, std::wstring& error)
{
    error.clear();
    if (m_recording) {
        error = L"запись уже идёт";
        return false;
    }
    if (!m_graph) {
        error = L"камера не подключена";
        return false;
    }

    m_control->Stop();

    auto fail = [&](const std::wstring& text, bool resetWriter) -> bool {
        error = text;
        m_recording = false;
        if (resetWriter)
            finalizeRecordWriter();
        removeRecordBranch();
        m_control->Run();
        return false;
    };

    HRESULT hr = CoCreateInstance(CLSID_SampleGrabber, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IBaseFilter, reinterpret_cast<void**>(&m_recGrabFilter));
    if (FAILED(hr))
        return fail(wformat(L"SampleGrabber: 0x%08X", static_cast<unsigned>(hr)), false);
    m_graph->AddFilter(m_recGrabFilter, L"RecordGrabber");
    m_recGrabFilter->QueryInterface(IID_ISampleGrabber, reinterpret_cast<void**>(&m_recGrab));
    m_recCb = new RecCallback(this);
    m_recGrab->SetBufferSamples(FALSE);
    m_recGrab->SetCallback(m_recCb, 0);

    hr = CoCreateInstance(kNullRendererGUID, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter,
                          reinterpret_cast<void**>(&m_nullRend));
    if (FAILED(hr))
        return fail(wformat(L"NullRenderer: 0x%08X", static_cast<unsigned>(hr)), false);
    m_graph->AddFilter(m_nullRend, L"NullRenderer");

    IPin* recIn = nullptr;
    IPin* recOut = nullptr;
    IPin* nrIn = nullptr;
    getPin(m_recGrabFilter, PINDIR_INPUT, 0, &recIn);
    getPin(m_recGrabFilter, PINDIR_OUTPUT, 0, &recOut);
    getPin(m_nullRend, PINDIR_INPUT, 0, &nrIn);

    auto isConnected = [](IPin* pin) {
        IPin* peer = nullptr;
        const HRESULT r = pin->ConnectedTo(&peer);
        if (peer)
            peer->Release();
        return r == S_OK;
    };

    IPin* cap = nullptr;
    m_build->FindPin(m_tee, PINDIR_OUTPUT, &PIN_CATEGORY_CAPTURE, nullptr, FALSE, 0, &cap);
    if (!cap || isConnected(cap)) {
        SAFE_RELEASE(cap);
        for (int i = 0; i < 8 && !cap; ++i) {
            IPin* p = nullptr;
            if (!getPin(m_tee, PINDIR_OUTPUT, i, &p))
                break;
            if (!isConnected(p))
                cap = p;
            else
                p->Release();
        }
    }

    if (!cap || !recIn || !recOut || !nrIn) {
        SAFE_RELEASE(cap);
        SAFE_RELEASE(recIn);
        SAFE_RELEASE(recOut);
        SAFE_RELEASE(nrIn);
        return fail(L"нет свободного вывода камеры", false);
    }

    hr = m_graph->Connect(cap, recIn);
    SAFE_RELEASE(cap);
    if (FAILED(hr)) {
        SAFE_RELEASE(recIn);
        SAFE_RELEASE(recOut);
        SAFE_RELEASE(nrIn);
        return fail(wformat(L"подключение захвата: 0x%08X", static_cast<unsigned>(hr)),
                    false);
    }
    hr = m_graph->Connect(recOut, nrIn);
    SAFE_RELEASE(recIn);
    SAFE_RELEASE(recOut);
    SAFE_RELEASE(nrIn);
    if (FAILED(hr))
        return fail(wformat(L"подключение сброса: 0x%08X", static_cast<unsigned>(hr)), false);

    int w = 0, h = 0, stride = 0;
    int channels = 3;
    unsigned long fourcc = 0;
    AM_MEDIA_TYPE mt = {};
    if (SUCCEEDED(m_recGrab->GetConnectedMediaType(&mt)) && mt.formattype == FORMAT_VideoInfo
        && mt.cbFormat >= sizeof(VIDEOINFOHEADER) && mt.pbFormat) {
        const VIDEOINFOHEADER* vih = reinterpret_cast<const VIDEOINFOHEADER*>(mt.pbFormat);
        w = vih->bmiHeader.biWidth;
        h = std::abs(vih->bmiHeader.biHeight);
        fourcc = vih->bmiHeader.biCompression;
        const int bitcount = vih->bmiHeader.biBitCount;
        if (bitcount >= 24)
            channels = bitcount / 8;
        else
            channels = 2;
        const long image = vih->bmiHeader.biSizeImage;
        if (h > 0 && channels > 0)
            stride = image > 0 ? static_cast<int>(image / h)
                               : ((w * channels + 3) / 4) * 4;
        freeMediaType(mt);
    }
    if (w <= 0 || h <= 0)
        return fail(L"не удалось определить формат видео", false);

    m_recW = w;
    m_recH = h;
    m_recStride = stride > 0 ? stride : ((w * channels + 3) / 4) * 4;
    m_recChannels = channels;
    m_recFourcc = fourcc;

    const double fps = m_fps > 0.0 ? m_fps : 25.0;

    m_writer = new AviWriter();
    if (!m_writer->open(path, w, h, fps))
        return fail(L"не удалось создать файл записи", true);

    m_recording = true;
    hr = m_control->Run();
    if (FAILED(hr))
        return fail(wformat(L"запуск графа: 0x%08X", static_cast<unsigned>(hr)), true);
    return true;
}

bool DsCamera::stopRecording()
{
    if (!m_recording)
        return false;
    m_control->Stop();
    finalizeRecordWriter();
    m_recording = false;
    removeRecordBranch();
    m_control->Run();
    return true;
}

void DsCamera::onRecordSample(const unsigned char* data, long length)
{
    if (!data || length <= 0)
        return;
    std::lock_guard<std::mutex> lock(m_recMutex);
    if (!m_writer || !m_writer->isOpen())
        return;

    std::vector<unsigned char> out;
    if (m_recFourcc == 0x47504A4D) { // 'MJPG'
        out.assign(data, data + length);
    } else {
        try {
            cv::Mat bgr;
            if (m_recFourcc == 0 && m_recChannels == 3) {
                cv::Mat img(m_recH, m_recW, CV_8UC3, const_cast<unsigned char*>(data),
                            static_cast<std::size_t>(m_recStride));
                bgr = img;
            } else if (m_recFourcc == 0 && m_recChannels == 4) {
                cv::Mat img(m_recH, m_recW, CV_8UC4, const_cast<unsigned char*>(data),
                            static_cast<std::size_t>(m_recStride));
                cv::cvtColor(img, bgr, cv::COLOR_BGRA2BGR);
            } else if (m_recFourcc == 0x32595559) { // 'YUY2'
                cv::Mat img(m_recH, m_recW, CV_8UC2, const_cast<unsigned char*>(data),
                            static_cast<std::size_t>(m_recStride));
                cv::cvtColor(img, bgr, cv::COLOR_YUV2BGR_YUY2);
            } else if (m_recFourcc == 0x56595559) { // 'UYVY'
                cv::Mat img(m_recH, m_recW, CV_8UC2, const_cast<unsigned char*>(data),
                            static_cast<std::size_t>(m_recStride));
                cv::cvtColor(img, bgr, cv::COLOR_YUV2BGR_UYVY);
            } else {
                return;
            }
            if (bgr.empty())
                return;
            cv::imencode(".jpg", bgr, out);
        } catch (...) {
            return;
        }
    }

    if (!out.empty())
        m_writer->addFrame(out.data(), out.size());
}

void DsCamera::finalizeRecordWriter()
{
    std::lock_guard<std::mutex> lock(m_recMutex);
    if (m_writer) {
        m_writer->finish();
        delete m_writer;
        m_writer = nullptr;
    }
}

void DsCamera::removeRecordBranch()
{
    auto disconnectAll = [](IBaseFilter* filter) {
        IEnumPins* e = nullptr;
        if (!filter || FAILED(filter->EnumPins(&e)))
            return;
        IPin* p = nullptr;
        while (e->Next(1, &p, nullptr) == S_OK) {
            p->Disconnect();
            p->Release();
        }
        e->Release();
    };

    if (m_recGrab)
        m_recGrab->SetCallback(nullptr, 0);
    disconnectAll(m_recGrabFilter);
    disconnectAll(m_nullRend);
    if (m_recGrabFilter)
        m_graph->RemoveFilter(m_recGrabFilter);
    if (m_nullRend)
        m_graph->RemoveFilter(m_nullRend);

    SAFE_RELEASE(m_recGrab);
    SAFE_RELEASE(m_recGrabFilter);
    SAFE_RELEASE(m_nullRend);
    SAFE_RELEASE(m_recCb);
    m_recW = 0;
    m_recH = 0;
    m_recStride = 0;
    m_recChannels = 3;
    m_recFourcc = 0;
}