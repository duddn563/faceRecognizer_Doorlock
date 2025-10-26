#include "match/FaceMatcher.hpp"

#include <QtCore/QDebug>
#include <opencv2/opencv.hpp>
#include <cmath>
#include <limits>

MatchResult FaceMatcher::bestMatch(const cv::Mat&  alignedFaceBGR, 
								   const std::vector<UserEmbedding>& gallery)
{
	MatchResult r;
	r.sim = -1.0f;
	r.id  = -1;

	if (!m_embedder) {
		qWarning() << "[FaceMatcher] m_embedder is null";
		return r;
	}
	if (gallery.empty()) {
		qWarning() << "[FaceMatcher] gallery is empty";
		return r;
	}

	std::vector<float> emb;
	if (!m_embedder->extract(alignedFaceBGR, emb) || emb.empty()) {
		qWarning() << "[FaceMatcher] extract() failed or empty embedding";
		return r;
	}

	const int d_in = (int)emb.size();

	for (const auto& u : gallery) {
		if (u.embedding.empty()) continue;
		float sim = m_embedder->cosine(emb, u.embedding);
		if (sim > r.sim) {
			r.sim		= sim;
			r.id		= u.id;
			r.name	= u.name; 
		}
	}

	qDebug() << "[FaceMatcher] bestMatch result:" << r.name << "sim=" << r.sim;
	return r;
}

MatchTop2 FaceMatcher::bestMatchTop2(const std::vector<float>& emb, const std::vector<UserEmbedding>& gallery, bool debugAngles)
{
	MatchTop2 r;
	 if (gallery.empty()) {
    qWarning() << "[FaceMatcher] gallery is empty";
    return r;
  }
  if (emb.empty()) {
    qWarning() << "[FaceMatcher] input embedding is empty";
    return r;
  }

	const int dim = (int)emb.size();
	cv::Mat q(1, dim, CV_32F, const_cast<float*>(emb.data()));
	q = q.clone();
	double nq = cv::norm(q, cv::NORM_L2);
	if (nq <= 0.0) return r;
	q /= (nq + 1e-9);

	for (int i = 0; i < (int)gallery.size(); ++i) {
		const auto& u = gallery[i];
		if ((int)u.embedding.size() != dim) {
			qWarning() << "[FaceMatcher] dim mismatch i=" << i;
			continue;
		}
		if (u.proto.empty() || u.proto.type() != CV_32F || u.proto.total() != (size_t)dim) {
			qWarning() << "[FaceMatcher] invalid proto at i=" << i;
			continue;
		}

		double np = cv::norm(u.proto, cv::NORM_L2);
		if (np < 0.0) {
			qWarning() << "[FaceMatcher] proto L2==0 at i=" << i;
			continue;
		}

		float sim = q.dot(u.proto / (float)(np + 1e-9));
		if (debugAngles) {
			double cosn = std::max(-1.0, std::min(1.0, (double)sim));
			double deg  = std::acos(cosn) * 180.0 / M_PI;
			qDebug() << "[FaceMatcher] i=" << i << "cos=" << cosn << "deg=" << deg;
		}

		if (sim > r.bestSim) {
			r.secondIdx = r.bestIdx;
			r.secondSim = r.bestSim;
			r.bestIdx   = i;
			r.bestSim	  = sim;
		}
		else if(sim > r.secondSim) {
			r.secondIdx = i;
			r.secondSim = sim;
		}
	}
	
	return r;
}

