// capture/FrameCapture.cpp
#include "FrameCapture.hpp"
#include <QDebug>
#include <QThread>

FrameCapture::FrameCapture() 
		: QObject(nullptr)
{	
    connect(&worker_, &QThread::started, this, &FrameCapture::loop, Qt::QueuedConnection);
    moveToThread(&worker_);
}
FrameCapture::~FrameCapture(){ 
	stop(); 

	if (worker_.isRunning()) {
		worker_.quit(); 
		worker_.wait(); 
	}
}

void FrameCapture::start(){ 
	if (running_.exchange(true)) return; 
	worker_.start(); 
}
void FrameCapture::stop() { running_ = false; }

bool FrameCapture::openCamera() {
    closeCamera();
    if (useGst_ && !gstPipeline_.isEmpty()) {
        if (!cap_.open(gstPipeline_.toStdString(), cv::CAP_GSTREAMER)) return false;
    } else if (useV4L2_) {
        if (!devPath_.isEmpty()) {
            if (!cap_.open(devPath_.toStdString(), cv::CAP_V4L2)) return false;
        } else {
            if (!cap_.open(useIndex_, cv::CAP_V4L2)) return false;
        }
    } else {
        if (!devPath_.isEmpty()) { if (!cap_.open(devPath_.toStdString())) return false; }
        else { if (!cap_.open(useIndex_)) return false; }
    }
    if (fourcc_) cap_.set(cv::CAP_PROP_FOURCC, fourcc_);
    if (w_>0) cap_.set(cv::CAP_PROP_FRAME_WIDTH,  w_);
    if (h_>0) cap_.set(cv::CAP_PROP_FRAME_HEIGHT, h_);
    if (fpsReq_>0) cap_.set(cv::CAP_PROP_FPS, fpsReq_);
    qInfo() << "[FrameCapture] opened" << (devPath_.isEmpty() ? QString("[index]%1").arg(useIndex_) : "[V4L2] "+devPath_)
            << " -> " << int(cap_.get(cv::CAP_PROP_FRAME_WIDTH)) << "x" << int(cap_.get(cv::CAP_PROP_FRAME_HEIGHT))
            << "@" << cap_.get(cv::CAP_PROP_FPS);
    return true;
}
void FrameCapture::closeCamera(){ if (cap_.isOpened()) cap_.release(); }

bool FrameCapture::readOne(cv::Mat& out) {
    return cap_.read(out) && !out.empty();
}

void FrameCapture::loop() {
    if (!openCamera()) {
        while (running_.load() && !openCamera()) QThread::msleep(reopenSleepMs_);
    }
    QElapsedTimer tick; tick.start();
    const int sleepFloorMs = 1;

    while (running_.load()) {
        if (!cap_.isOpened()) { if (!openCamera()) { QThread::msleep(reopenSleepMs_); continue; } }

        cv::Mat bgr;
        if (!readOne(bgr)) {
            if (++failCount_ >= maxFailBeforeReopen_) {
                emit cameraError("[FrameCapture] read fail threshold, reopening...");
                closeCamera(); QThread::msleep(reopenSleepMs_); failCount_ = 0;
            } else {
                QThread::msleep(5);
            }
            continue;
        }
        failCount_ = 0;
        emit frameReady(bgr);

        if (fpsReq_ > 0) {
            int targetMs = int(1000.0 / fpsReq_);
            int sleepMs = targetMs - int(tick.restart());
            if (sleepMs < sleepFloorMs) sleepMs = sleepFloorMs;
            QThread::msleep(sleepMs);
        } else {
            QThread::msleep(sleepFloorMs);
        }
    }
    closeCamera();
}

