cmake_minimum_required(VERSION 3.10)

project(FaceDoorlockGUI)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/ui)

find_package(Qt5 COMPONENTS Widgets REQUIRED)
find_package(OpenCV REQUIRED)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/services
    ${CMAKE_CURRENT_SOURCE_DIR}/src/presenter
)


set(SOURCES
    src/main.cpp
    src/gui/MainWindow.cpp

		src/presenter/MainPresenter.cpp
		src/presenter/UserImagePresenter.cpp
		src/presenter/DoorSensorPresenter.cpp
		src/presenter/FaceSensorPresenter.cpp
		src/presenter/FaceRecognitionPresenter.cpp
		src/presenter/FaceRegisterPresenter.cpp

		src/services/UserImageService.cpp
		src/services/DoorSensorService.cpp
		src/services/FaceSensorService.cpp
		src/services/FaceRecognitionService.cpp

		src/services/AuthManager.cpp

		src/logger.cpp
		src/util.cpp
)

qt5_wrap_ui(UISrcs ${UI_FILES})

include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(face_doorlock	${SOURCES})

target_link_libraries(face_doorlock PRIVATE 
			Qt5::Widgets 
			${OpenCV_LIBS} 
			wiringPi
)

