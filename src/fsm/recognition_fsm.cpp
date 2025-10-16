#include "recognition_fsm.hpp"
#include "fsm_logging.hpp"

RecognitionFsm::RecognitionFsm(QObject* parent) : QObject(parent)
{
		timer_.setInterval(33); // ~30Hz
		connect(&timer_, &QTimer::timeout, this, &RecognitionFsm::tick);
}

void RecognitionFsm::addState(RecognitionState s, std::unique_ptr<IFsmState> st) 
{
    states_[s] = std::move(st);
}

void RecognitionFsm::addTransition(const Transition& t) 
{ 
		trans_.push_back(t); 
}

void RecognitionFsm::start(RecognitionState initial) 
{
		current_ = initial;
		enterTime_.start();
		if (states_.count(current_)) states_[current_]->onEnter(ctx_);
		emit stateChanged(current_);
		timer_.start();
}

void RecognitionFsm::stop() 
{ 
		timer_.stop(); 
}

void RecognitionFsm::updateContext(const FsmContext& c) 
{ 
		ctx_ = c; 
}

void RecognitionFsm::tick() 
{
	// qCDebug(LC_FSM_STATE) << "[FSM] tick()";
	if (!states_.count(current_)) return;

	// 1) onUpdate
	states_[current_]->onUpdate(ctx_);
	if (current_ == RecognitionState::RECOGNIZING) {
		ctx_.recogPass = states_[current_]->recogPass();
	}
	else {
		ctx_.recogPass = false;
	}


	// min dwell 체크 후 전환 검사
	const qint64 dwell = enterTime_.elapsed();
	qCDebug(LC_FSM_STATE) << "onUPdate"
		<< "state=" << static_cast<int>(current_)
		<< "dwell(ms)=" << dwell;

	// 2) 현 상태에서 가능한 전환들을 "순서대로" 검사 + 로그
	for (const auto& t : trans_) {
		if (t.from != current_) continue;

		if (dwell < t.minDwellMs) {
			qCDebug(LC_FSM_GUARD) << "[SKIP DWELL]" << t.name
				<< "need>=" << t.minDwellMs << "ms, "
				<< "now=" << dwell << "ms";
			continue;
		}

		const bool pass = (t.guard && t.guard(ctx_));
		const std::string key = t.name ? t.name : (std::to_string((int)t.from) + "->" +std::to_string((int)t.to));
		static int s_evalTick = 0;

		bool changed = (lastEval_.find(key) == lastEval_.end()) || (lastEval_[key] != pass);
		if (changed || ((++s_evalTick % evalEvery_) == 0)) {
			qCDebug(LC_FSM_GUARD) << "[EVAL]" << t.name
				<< "from" << static_cast<int>(t.from)
				<< "to"		<< static_cast<int>(t.to)
				<< "result=" << pass;
		}



		if (pass) {
			// 3) exit -> enter
			qCDebug(LC_FSM_STATE) << "[EXIT]" << static_cast<int>(current_)
				<< "after dwell=" << dwell << "ms";
			states_[current_]->onExit(ctx_);

			current_ = t.to;
			enterTime_.restart();

			qCDebug(LC_FSM_STATE) << "[ENTER]" << static_cast<int>(current_);

			// --- add: 전환 직전 공통 리셋 ---
			auto resetAuthAttempt = [](FsmContext& c){
				c.authStreak = 0;
				c.allowEntry = false;
				c.timeout    = false;
				// 필요하면
				// c.recogStartedTs = c.nowMs;
				// c.failCount = 0; // 실패 누적은 정책에 따라 리셋/유지 결정
			};

			switch (t.to) {
				case RecognitionState::DETECTING:
				case RecognitionState::RECOGNIZING:
				//case RecognitionState::AUTH_FAIL:
				//case RecognitionState::IDLE: // IDLE 진입 시도 때도 초기화 원하면 주석 해제
					resetAuthAttempt(ctx_);
					break;
				default:
					break;
			}


			if (states_.count(current_)) states_[current_]->onEnter(ctx_);

			emit stateChanged(current_);
			break;  // 한 번에 하나만 전환
		}
	}
}

