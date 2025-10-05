#include "match/SimilarityDecision.hpp"
#include <QtCore/QDebug>
#include <cmath>

namespace {
inline bool validSim(float s) {
    return std::isfinite(s) && s >= -1.0f && s <= 1.0f;
}
} // namespace

Decision SimilarityDecision::decide(const MatchTop2& m) const {
    const float best   = m.bestSim;
    const float second = m.secondSim;

    if (!validSim(best)) {
        qDebug() << "[Decision] Reject: invalid best";
        return Decision::Reject;
    }

    const bool hasSecond = validSim(second) && (m.secondIdx >= 0);
    const float gap = hasSecond ? (best - second) : (best - (-1.0f)); // second 없으면 gap 크게

    // 강한 승인
    if (best >= p_.strongAcceptSim && (!hasSecond || gap >= p_.minTop2Gap)) {
        qDebug() << "[Decision] StrongAccept"
                 << "best=" << best << "second=" << second << "gap=" << gap;
        return Decision::StrongAccept;
    }
    // 일반 승인
    if (best >= p_.acceptSim && (!hasSecond || gap >= p_.minTop2Gap)) {
        qDebug() << "[Decision] Accept"
                 << "best=" << best << "second=" << second << "gap=" << gap;
        return Decision::Accept;
    }
    // 후보가 1개만 있을 때의 예외적 허용(갤러리 작을 때 대비)
    if (!hasSecond && best >= p_.minBestOnly) {
        qDebug() << "[Decision] Tentative(bestOnly)"
                 << "best=" << best;
        return Decision::Tentative;
    }

    qDebug() << "[Decision] Reject"
             << "best=" << best << "second=" << second << "gap=" << gap;
    return Decision::Reject;
}

bool SimilarityDecision::isUnknownLikely(const MatchTop2& m) const {
    if (!validSim(m.bestSim)) return true;
    const bool weakBest = (m.bestSim < p_.minBestOnly);
    const bool smallGap = (validSim(m.secondSim) &&
                           (m.bestSim - m.secondSim) < (p_.minTop2Gap * 0.6f));
    return weakBest || smallGap;
}

