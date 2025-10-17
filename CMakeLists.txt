cmake_minimum_required(VERSION 3.10)

project(FaceDoorlockGUI)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/ui)

find_package(Qt6 COMPONENTS Widgets REQUIRED)
find_package(Qt6 COMPONENTS Sql REQUIRED)
find_package(Qt6 COMPONENTS Core Bluetooth)
find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBGPIOD REQUIRED IMPORTED_TARGET libgpiod)

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/src/gui
	${CMAKE_CURRENT_SOURCE_DIR}/src/services
	${CMAKE_CURRENT_SOURCE_DIR}/src/presenter
)


set(SOURCES
	src/main.cpp
	src/fsm/recognition_fsm.cpp
	src/fsm/fsm_logging.cpp
	src/log/SystemLogger.cpp
	src/log/logger.cpp
	src/gui/MainWindow.cpp
	src/gui/DevInfoTab.cpp
	src/gui/LogTab.cpp
	src/gui/DevInfoDialog.cpp
	src/gui/SingleLogDialog.cpp
	src/gui/BasicInfoWidget.cpp
	src/gui/NetworkInfoWidget.cpp
	src/gui/CpuInfoWidget.cpp
	src/gui/MemInfoWidget.cpp
	src/gui/StyledMsgBox.cpp
	src/gui/LedWidget.cpp
	src/ble/BleServer.cpp
	src/ble/BleUuid.cpp

	src/hw/UltrasonicSensor.cpp
	src/hw/DoorlockController.cpp
	src/hw/ReedSensor.cpp
	src/hw/UnlockUntilReed.cpp
	src/presenter/MainPresenter.cpp

	src/presenter/UserImagePresenter.cpp
	src/presenter/FaceRecognitionPresenter.cpp
	src/presenter/FaceRegisterPresenter.cpp
	src/services/UserImageService.cpp
	src/services/DoorSensorService.cpp
	src/services/FaceSensorService.cpp
	src/services/FaceRecognitionService.cpp
	src/services/QSqliteService.cpp
	src/services/AuthManager.cpp
	src/ai/Embedder.cpp

	src/liveness/LivenessGate.cpp
	src/detect/LandmarkAligner.cpp
	src/match/FaceMatcher.cpp
	src/match/SimilarityDecision.cpp
	src/detect/FaceDetector.cpp
	src/capture/FrameCapture.cpp
)

qt6_wrap_ui(UISrcs ${UI_FILES})

include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(face_doorlock	${SOURCES})

target_link_libraries(face_doorlock PRIVATE 
	Qt6::Widgets 
	Qt6::Sql
	Qt6::Bluetooth
	${OpenCV_LIBS} 
	wiringPi
	PkgConfig::LIBGPIOD
)

