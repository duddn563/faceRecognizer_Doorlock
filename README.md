# GARDIX – AI Face Recognition Door Lock System

> Qt + OpenCV + BLE 기반 얼굴인식 스마트 도어락 시스템

##  Project Overview

GARDIX는 **AI 얼굴 인식, BLE 통신, 센서 제어**를 결합한 스마트 도어락 시스템입니다.

지산 근무 중 공용공간에서 반복적으로 카드키와 비밀번호를 사용하는 불편함에서 아이디어를 얻어 개발했습니다.

단순 얼굴 인식 데모가 아닌,

**실시간 인증 + GUI 상태 관리 + BLE 연동 + 센서 기반 도어 제어**

까지 포함한 통합 시스템 구현을 목표로 제작했습니다.

---

##  Features

### Face Recognition
- OpenCV **YuNet** 기반 얼굴 탐지
- **MobileFaceNet** 임베딩 추출
- Cosine Similarity 기반 인증
- 다중 각도 얼굴 등록 지원

###  Door Lock Control
- HC-SR04 초음파 센서 접근 감지
- 리드 스위치 문 열림/닫힘 감시
- 인증 성공 시 도어 상태 자동 변경
- LED 상태 피드백 제공

###  BLE Communication
- Qt6 **QLowEnergyController (Peripheral)** 사용
- Android App 연동
- BLE Notify 기반 실시간 인증 로그 전달
- BLE Write 기반 원격 도어 제어

###  GUI & System Architecture
- Qt GUI 기반 상태 관리(FSM)
- 멀티스레드 구조 적용
- Camera / Recognition / UI 분리 설계
- 실시간 HUD 및 상태 표시

---

##  Tech Stack

| Category | Technology |
|----------|------------|
| Language | C++ |
| Framework | Qt 6 |
| AI / CV | OpenCV, YuNet, MobileFaceNet |
| Database | SQLite |
| Communication | BLE (Bluetooth Low Energy) |
| Hardware | Raspberry Pi 4B, HC-SR04, Reed Switch |

---

##  System Flow

```text
User 접근
   ↓
초음파 센서 감지
   ↓
카메라 얼굴 인식
   ↓
Feature Embedding 생성
   ↓
DB 임베딩 비교
   ↓
인증 성공
   ↓
Door Unlock + BLE Log 전송
   ↓
문 닫힘 감지 → Auto Lock
```

---

##  Architecture

```text
GUI Thread
 ├── MainWindow
 ├── FSM State Management
 └── BLE Status UI

Recognition Thread
 ├── Camera Capture
 ├── YuNet Detection
 ├── MobileFaceNet Embedding
 └── Matching Engine

Hardware Layer
 ├── Ultrasonic Sensor
 ├── Reed Switch
 └── LED Control
```

---

##  Future Improvements

- Fingerprint / NFC 인증 통합
- Wi-Fi / Cloud 연동
- Remote Access Dashboard
- Security Log Backup
- Cross-Platform Mobile App 확장

---

##  Motivation

> “문이 사용자를 먼저 알아보면 어떨까?”

공용 공간의 반복적인 인증 과정에서 얻은 아이디어를 바탕으로,

AI 기반의 **직관적이고 확장 가능한 스마트 출입 시스템**을 구현했습니다.

---

##  Project Structure

```text
faceRecognizer_Doorlock/
│
├── gui/
│   ├── MainWindow.cpp
│   ├── MainWindow.hpp
│   └── styleConstants.hpp
│
├── worker/
│   ├── faceRecognizerWorker.cpp
│   └── faceRecognizerWorker.hpp
│
├── sensor/
│   ├── Dd_ultrasonic.cpp
│   ├── Dd_ultrasonic.hpp
│   ├── Fd_ultrasonic.cpp
│   └── Fd_ultrasonic.hpp
│
├── utils/
│   ├── logger.cpp
│   ├── logger.hpp
│   ├── util.cpp
│   └── util.hpp
│
├── state/
│   └── faceRecognitionState.hpp
│
└── main.cpp
```

---

# Why Qt?

일반적인 상용 얼굴인식 도어락은 Android SDK 또는 전용 펌웨어 기반으로 개발되는 경우가 많습니다.

GARDIX는 **Qt Framework**를 사용하여 다음과 같은 장점을 확보했습니다.

- GUI / BLE / Camera / Database 통합 개발
- 멀티스레드 기반 안정적인 실시간 처리
- Cross-Platform 확장성
- FSM 기반 UI 상태 관리
- 산업용 수준의 유지보수성 및 구조화

---

## Developer

**Lee Youngwoo**

ICT폴리텍 AI네트워크학과

System Development / Computer Vision / Embedded Software / Automation
