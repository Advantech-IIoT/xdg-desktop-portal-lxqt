cmake_minimum_required(VERSION 3.18.0)

set(PROJECT_VERSION "1.0.3")

project(xdg-desktop-portal-lxqt VERSION ${PROJECT_VERSION})

set(QT_MIN_VERSION "6.4.0")

set(CMAKE_AUTOMOC on)

include(FeatureSummary)
include(GNUInstallDirs)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    Core
    DBus
    Widgets
)

add_subdirectory(data)
add_subdirectory(src)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
