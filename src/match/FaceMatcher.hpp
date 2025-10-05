#pragma once
#include <vector>
#include "include/types.hpp"

// 단순 코사인 매칭기. 갤러리와 unknown 풀은 호출 시점에 받아온다(소유권 없음)
class FaceMatcher {
	public:
		// 갤러리에서 best 1개 찾기
		static MatchResult bestMatch(const std::vector<float>& emb, const std::vector<UserEmbedding>& gallery);

		// 갤러리에서 top-2 찾기
		static MatchTop2 bestMatchTop2(const std::vector<float>& emb, const std::vector<UserEmbedding>& gallery, bool debugAngles = false);
};
