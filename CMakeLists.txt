cmake_minimum_required(VERSION 3.16)
project(AOWIS-SERVER-GUI LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)

find_package(Qt6 REQUIRED COMPONENTS Quick)

#qt_policy(SET QTP0001 NEW)
#qt_policy(SET QTP0004 NEW)

qt_add_executable(aowis-server-gui
    src/main.cpp
)

qt_add_qml_module(aowis-server-gui
    URI AOWIS.ServerGui
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_link_libraries(aowis-server-gui PRIVATE Qt6::Quick)
