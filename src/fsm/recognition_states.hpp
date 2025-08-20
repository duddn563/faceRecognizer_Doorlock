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

		void onEnter(const FsmContext&) override { gate.reset(false); }
		void onUpdate(const FsmContext& c) override {
				gate.feed(c.detectScore);
		}
};

struct RecognizingState : public IFsmState {
		HysteresisGate gate;	// 인식 신뢰도 히스테리시스 
		explicit RecognizingState(double enterThresh, double exitThresh)
				: gate(enterThresh, exitThresh, 2, 4) {}

		void onEnter(const FsmContext&) override { gate.reset(false); }
		void onUpdate(const FsmContext& c) override { 
				gate.feed(c.recogConfidence);
		}
};

struct RegisteringState : public IFsmState {};
struct DuplicateFaceState : public IFsmState {};
struct AuthSuccessState : public IFsmState {};
struct AuthFailState : public IFsmState {};
struct DoorOpenState : public IFsmState {};
struct LockedOutState : public IFsmState {};


