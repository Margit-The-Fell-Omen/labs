#include "snapframe.h"

#include "strutil.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <string>
#include <vector>
#include <windows.h>

namespace {

inline unsigned long mcc(char a, char b, char c, char d)
{
    return static_cast<unsigned long>(static_cast<unsigned char>(a))
           | (static_cast<unsigned long>(static_cast<unsigned char>(b)) << 8)
           | (static_cast<unsigned long>(static_cast<unsigned char>(c)) << 16)
           | (static_cast<unsigned long>(static_cast<unsigned char>(d)) << 24);
}

constexpr unsigned long kFccYuy2 = 0x32595559; // 'YUY2'
constexpr unsigned long kFccUyvy = 0x56595559; // 'UYVY'
constexpr unsigned long kFccMjpg = 0x47504A4D; // 'MJPG'
constexpr unsigned long kFccNv12 = 0x3231564E; // 'NV12'
constexpr unsigned long kFccIyuv = 0x56555949; // 'IYUV'
constexpr unsigned long kFccI420 = 0x30323449; // 'I420'

bool writeBytes(const std::wstring& path, const void* data, size_t size)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr);
    CloseHandle(h);
    return ok && written == size;
}

} // namespace

bool saveSnapshot(const std::wstring& path, const RawFrame& frame, SnapFormat format)
{
    if (!frame.data || frame.width <= 0 || frame.height <= 0 || frame.size == 0)
        return false;

    try {
        cv::Mat bgr;
        const unsigned long cc = frame.fourcc;
        const int w = frame.width;
        const int h = frame.height;

        if (cc == kFccMjpg) {
            cv::Mat jpeg(1, static_cast<int>(frame.size), CV_8UC1,
                         const_cast<void*>(frame.data));
            bgr = cv::imdecode(jpeg, cv::IMREAD_COLOR);
        } else if (cc == 0 && frame.channels == 3) {
            cv::Mat img(h, w, CV_8UC3, const_cast<void*>(frame.data),
                        static_cast<std::size_t>(frame.stride));
            bgr = img;
        } else if (cc == 0 && frame.channels == 4) {
            cv::Mat img(h, w, CV_8UC4, const_cast<void*>(frame.data),
                        static_cast<std::size_t>(frame.stride));
            cv::cvtColor(img, bgr, cv::COLOR_BGRA2BGR);
        } else if (cc == kFccYuy2) {
            cv::Mat img(h, w, CV_8UC2, const_cast<void*>(frame.data),
                        static_cast<std::size_t>(frame.stride));
            cv::cvtColor(img, bgr, cv::COLOR_YUV2BGR_YUY2);
        } else if (cc == kFccUyvy) {
            cv::Mat img(h, w, CV_8UC2, const_cast<void*>(frame.data),
                        static_cast<std::size_t>(frame.stride));
            cv::cvtColor(img, bgr, cv::COLOR_YUV2BGR_UYVY);
        } else if (cc == kFccNv12) {
            cv::Mat img(h * 3 / 2, w, CV_8UC1, const_cast<void*>(frame.data),
                        static_cast<std::size_t>(w));
            cv::cvtColor(img, bgr, cv::COLOR_YUV2BGR_NV12);
        } else if (cc == kFccIyuv || cc == kFccI420) {
            cv::Mat img(h * 3 / 2, w, CV_8UC1, const_cast<void*>(frame.data),
                        static_cast<std::size_t>(w));
            cv::cvtColor(img, bgr, cv::COLOR_YUV2BGR_I420);
        } else {
            return false;
        }

        if (bgr.empty())
            return false;

        if (format == SnapFormat::Bmp) {
            cv::Mat flat = bgr.isContinuous() ? bgr : bgr.clone();
            RawFrame bmp;
            bmp.data = flat.data;
            bmp.size = static_cast<std::size_t>(flat.total()) * flat.elemSize();
            bmp.width = flat.cols;
            bmp.height = flat.rows;
            bmp.stride = static_cast<int>(flat.step);
            bmp.channels = 3;
            bmp.fourcc = 0;
            return writeBmpFile(path, bmp);
        }

        std::vector<unsigned char> encoded;
        if (!cv::imencode(".jpg", bgr, encoded))
            return false;
        return writeBytes(path, encoded.data(), encoded.size());
    } catch (...) {
        return false;
    }
}