#pragma once

#include "aviwriter.h"
#include "bmpwrite.h"

#include <dshow.h>
#include <oaidl.h>
#include <qedit.h>
#include <windows.h>

#include <mutex>
#include <string>
#include <vector>

struct CameraDevice
{
    std::wstring name;
    std::wstring vendor;
    std::wstring path;
};

class GrabberCallback;
class RecCallback;

class DsCamera
{
public:
    DsCamera();
    ~DsCamera();

    static std::vector<CameraDevice> enumerate();

    bool connectByIndex(int index, HWND hwndPreview, const RECT& previewRect);
    void disconnect();

    bool connected() const { return m_graph != nullptr; }
    bool recording() const { return m_recording; }

    std::wstring deviceName() const;
    std::wstring vendor() const;
    std::wstring pixelFormat() const;
    std::wstring backendInfo() const;
    int width() const;
    int height() const;
    double fps() const;
    double fpsMeasured() const;

    bool grabFrame(RawFrame& out, std::vector<unsigned char>& buffer);
    void repaintVideo(HWND hwnd, HDC hdc);

    bool startRecording(const std::wstring& path, std::wstring& error);
    bool stopRecording();

    void updateMeasuredFps();
    void onSample(const unsigned char* data, long length);
    void onRecordSample(const unsigned char* data, long length);

private:
    bool buildGraph(int index, HWND hwndPreview);
    void teardownGraph();
    void readMediaTypeInfo();
    void finalizeRecordWriter();
    void removeRecordBranch();

    IGraphBuilder* m_graph = nullptr;
    IMediaControl* m_control = nullptr;
    ICaptureGraphBuilder2* m_build = nullptr;
    IBaseFilter* m_source = nullptr;
    IBaseFilter* m_tee = nullptr;
    IBaseFilter* m_vmr = nullptr;
    IVMRWindowlessControl* m_vmrwc = nullptr;
    IBaseFilter* m_grabFilter = nullptr;
    ISampleGrabber* m_grab = nullptr;
    GrabberCallback* m_cb = nullptr;

    std::mutex m_mutex;
    std::vector<unsigned char> m_frame;
    RECT m_previewRect = {0, 0, 640, 480};
    int m_width = 0;
    int m_height = 0;
    int m_stride = 0;
    int m_channels = 3;
    unsigned long m_fourcc = 0;
    unsigned short m_bitcount = 24;

    std::wstring m_name;
    std::wstring m_vendor;
    std::wstring m_pixelFormat;
    double m_fps = 0.0;
    double m_fpsMeasured = 0.0;
    unsigned long long m_sampleCount = 0;
    unsigned long long m_sampleCountPrev = 0;
    LARGE_INTEGER m_fpsMark = {};

    bool m_recording = false;
    IBaseFilter* m_recGrabFilter = nullptr;
    ISampleGrabber* m_recGrab = nullptr;
    IBaseFilter* m_nullRend = nullptr;
    RecCallback* m_recCb = nullptr;
    AviWriter* m_writer = nullptr;
    std::mutex m_recMutex;
    int m_recW = 0;
    int m_recH = 0;
    int m_recStride = 0;
    int m_recChannels = 3;
    unsigned long m_recFourcc = 0;
};