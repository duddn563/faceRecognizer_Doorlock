#pragma once
#include "include/types.hpp"		// MatchTop2
#include <cstddef>

// Final decision
enum class Decision {
	Reject = 0,
	Tentative,
	Accept,
	StrongAccept
};

// 판정 파라미터(필요시 setParams로 변경 가능)
struct DecisionParams {
	float acceptSim        = 0.97f; // 이 이상이면 Accept
    float strongAcceptSim  = 0.98f; // 이 이상이면 StrongAccept
    float minTop2Gap       = 0.04f; // best - second 최소 간격
    float minBestOnly      = 0.40f; // second가 없을 때 허용 하한
};

class SimilarityDecision {
public:
    SimilarityDecision() = default;
    explicit SimilarityDecision(const DecisionParams& p): p_(p) {}

    Decision decide(const MatchTop2& m) const;       // 최종 판정
    bool isUnknownLikely(const MatchTop2& m) const;  // 보조 판단(선택)

    const DecisionParams& params() const { return p_; }
    void setParams(const DecisionParams& p) { p_ = p; }

private:
    DecisionParams p_;
};
