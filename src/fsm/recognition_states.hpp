// recognition_states.hpp
#pragma once
#include "recognition_fsm.hpp"
#include <QDebug>

struct IdleState : public IFsmState {
		void onEnter(const FsmContext&) override {
				qDebug() << "[FSM] Enter IDLE";
		}
		void onUpdate(const FsmContext&) override {}
		void onExit(const FsmContext&) override {}
};

struct DetectingState : public IFsmState {
		HysteresisGate gate;		// 검출 히스테리시스
		explicit DetectingState(double enterThresh, double exitThresh)
				: gate(enterThresh, exitThresh, /*need=*/3, /*window=*/5) {}

		void onEnter(const FsmContext&) override { 
				qDebug() << "[FSM] Enter DETECT";
				gate.reset(false); }
		void onUpdate(const FsmContext& c) override {
				qDebug() << "[FSM] Enter DETECT";
				gate.feed(c.detectScore);
		}
		void onExit(const FsmContext& c) override {
				qDebug() << "[FSM] Exit DETECT";
		}
};

struct RecognizingState : public IFsmState {
		HysteresisGate gate;	// 인식 신뢰도 히스테리시스 
		explicit RecognizingState(double enterThresh, double exitThresh)
				: gate(enterThresh, exitThresh, 2, 4) {}

		void onEnter(const FsmContext&) override { 
				qDebug() << "[FSM] Enter RECOGNIZING";
				gate.reset(false); 
		}
		void onUpdate(const FsmContext& c) override { 
				qDebug() << "[FSM] Update RECOGNIZING";
				gate.feed(c.recogConfidence);
		}
		void onExit(const FsmContext&) override {
				qDebug() << "[FSM] Exit RECOGNIZING";
		}
};

struct AuthSuccessState : public IFsmState {
		void onEnter(const FsmContext&) override { 
				qDebug() << "[FSM] Enter AUTH_SUCCESS";
		}
		void onUpdate(const FsmContext&) override {
				qDebug() << "[FSM] Update AUTH_SUCCESS";
		}
		void onExit(const FsmContext&) override {
				qDebug() << "[FSM] Exit AUTH_SUCCESS";
		}
};
struct DoorOpenState : public IFsmState { 
		void onEnter(const FsmContext&) override { 
				qDebug() << "[FSM] Enter DOOR_OPEN";
		}
		void onUpdate(const FsmContext&) override {
				qDebug() << "[FSM] Update DOOR_OPEN";
		}
		void onExit(const FsmContext&) override {
				qDebug() << "[FSM] Exit DOOR_OPEN";
		}
};

struct RegisteringState : public IFsmState {
		void onEnter(const FsmContext&) override { 
				qDebug() << "[FSM] Enter REGISTERING";
		}
		void onUpdate(const FsmContext&) override {
				qDebug() << "[FSM] Update REGISTERING";
		}
		void onExit(const FsmContext&) override {
				qDebug() << "[FSM] Exit REGISTERING";
		}

};
struct DuplicateFaceState : public IFsmState {
		void onEnter(const FsmContext&) override { 
				qDebug() << "[FSM] Enter DUPLICATE_FACE";
		}
		void onUpdate(const FsmContext&) override {
				qDebug() << "[FSM] Update DUPLICATE_FACE";
		}
		void onExit(const FsmContext&) override {
				qDebug() << "[FSM] Exit DUPLICATE_FACE";
		}

};
struct AuthFailState : public IFsmState {
		void onEnter(const FsmContext&) override { 
				qDebug() << "[FSM] Enter AUTH_FAIL";
		}
		void onUpdate(const FsmContext&) override {
				qDebug() << "[FSM] Update AUTH_FAIL";
		}
		void onExit(const FsmContext&) override {
				qDebug() << "[FSM] Exit AUTH_FAIL";
		}

};
struct LockedOutState : public IFsmState {
		void onEnter(const FsmContext&) override { 
				qDebug() << "[FSM] Enter LOCKED_OUT";
		}
		void onUpdate(const FsmContext&) override {
				qDebug() << "[FSM] Update LOCKED_OUT";
		}
		void onExit(const FsmContext&) override {
				qDebug() << "[FSM] Exit LOCKED_OUT";
		}

};


