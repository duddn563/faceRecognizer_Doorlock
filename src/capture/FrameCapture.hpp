// capture/FrameCapture.hpp
#pragma once
#include <QObject>
#include <QThread>
#include <QElapsedTimer>
#include <opencv2/opencv.hpp>

class FrameCapture : public QObject {
	Q_OBJECT
public:
    explicit FrameCapture();
    ~FrameCapture();

    void setDevice(const QString& devPath) { devPath_ = devPath; useIndex_ = -1; }
    void setCameraIndex(int idx) { useIndex_ = idx; devPath_.clear(); }
    void setResolution(int w, int h) { w_ = w; h_ = h; }
    void setFps(double fps) { fpsReq_ = fps; }
    void setFourcc(int fourcc) { fourcc_ = fourcc; }
    void setUseV4L2(bool v) { useV4L2_ = v; }
    void setUseGst(bool v) { useGst_ = v; }
    void setGstPipeline(const QString& p) { gstPipeline_ = p; }

public slots:
    void start();
    void stop();

signals:
    void frameReady(const cv::Mat& bgr);
    void cameraError(const QString& msg);

private slots:
    void loop();

private:
    bool openCamera();
    void closeCamera();
    bool readOne(cv::Mat& out);

    cv::VideoCapture cap_;
    QString devPath_;
    QString gstPipeline_;
    int useIndex_{0};
    int w_{640}, h_{480};
    double fpsReq_{30.0};
    int fourcc_{0};
    bool useV4L2_{true};
    bool useGst_{false};

    QThread worker_;
    std::atomic_bool running_{false};
    int failCount_{0};
    const int maxFailBeforeReopen_{10};
    const int reopenSleepMs_{300};
};

