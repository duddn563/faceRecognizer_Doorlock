#pragma once
#include <atomic>
#include <opencv2/opencv.hpp>

class LatestFrameMailbox {
public:
    // 캡처 스레드: 최신 프레임 게시
    void publish(const cv::Mat& bgr) {
        const int w = 1 - idx_.load(std::memory_order_relaxed);
        // 안전을 위해 고유 버퍼 보장 (읽는 쪽과 버퍼 충돌 방지)
        buffers_[w] = bgr.clone();             // deep copy 1회
        idx_.store(w, std::memory_order_release);
        seq_.fetch_add(1, std::memory_order_release);
    }

    // 소비자 스레드: 최신 프레임 가져오기 (없으면 false)
    bool tryConsume(cv::Mat& out, uint64_t& lastSeq) {
        const uint64_t s = seq_.load(std::memory_order_acquire);
        if (s == lastSeq) return false;       // 새 프레임 없음
        // 최신 인덱스 읽기
        const int r = idx_.load(std::memory_order_acquire);
        out = buffers_[r];                    // shallow (ref-count 증가, 추가 복사 없음)
        lastSeq = s;
        return !out.empty();
    }

private:
    cv::Mat buffers_[2];
    std::atomic<int> idx_{0};
    std::atomic<uint64_t> seq_{0};
};

