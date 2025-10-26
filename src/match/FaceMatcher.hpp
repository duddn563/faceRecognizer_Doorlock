#pragma once
#include <vector>
#include "include/types.hpp"
#include "ai/Embedder.hpp"

// 단순 코사인 매칭기. 갤러리와 unknown 풀은 호출 시점에 받아온다(소유권 없음)
class FaceMatcher {
	public:
		explicit FaceMatcher(std::shared_ptr<Embedder> embedder)
			: m_embedder(std::move(embedder)) {}

		// 갤러리에서 best 1개 찾기
		MatchResult bestMatch(const cv::Mat& alignedFaceBGR,
							  const std::vector<UserEmbedding>& gallery);
		// 갤러리에서 top-2 찾기
		static MatchTop2 bestMatchTop2(const std::vector<float>& emb, const std::vector<UserEmbedding>& gallery, bool debugAngles = false);
	private:
		std::shared_ptr<Embedder> m_embedder;
};
