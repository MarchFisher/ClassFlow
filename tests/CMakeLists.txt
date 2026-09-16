cmake_minimum_required(VERSION 3.16)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

# 每个 tst_*.cpp 生成一个独立测试可执行文件
set(TEST_SOURCES
        tst_datastore.cpp
        tst_snapshot.cpp
        tst_conflicttable.cpp
        tst_scheduler.cpp
        tst_filter.cpp
        tst_annealing.cpp
        tst_movable.cpp
        tst_manualmove.cpp
        tst_manualadd.cpp
)

foreach(src ${TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE classflow_core Qt${QT_VERSION_MAJOR}::Test)

    # 注入示例数据目录：DATA_DIR = small/classic 经典样例（含教学班/教室/作息/教师四件套），
    # DATA_LARGE_DIR = large/eighteen 中量级样例（大数据测试用）。避免依赖运行目录。
    target_compile_definitions(${name} PRIVATE
            DATA_DIR="${CMAKE_SOURCE_DIR}/data/small/classic"
            DATA_LARGE_DIR="${CMAKE_SOURCE_DIR}/data/large/eighteen")

    add_test(NAME ${name} COMMAND ${name})
endforeach()

# —— 课表点击命中回归（无头 GUI：含 UI 源文件；覆盖坐标帧/几何命中）——
add_executable(tst_hittest
        tst_hittest.cpp
        ../src/ui/timetable/timetablemodel.cpp
        ../src/ui/timetable/timetabledelegate.cpp
        ../src/ui/timetable/timetableview.cpp
        ../src/ui/theme/theme.cpp)
target_include_directories(tst_hittest PRIVATE ../src)
target_link_libraries(tst_hittest PRIVATE classflow_core
        Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
target_compile_definitions(tst_hittest PRIVATE
        DATA_DIR="${CMAKE_SOURCE_DIR}/data/small/classic"
        DATA_LARGE_DIR="${CMAKE_SOURCE_DIR}/data/large/eighteen")
add_test(NAME tst_hittest COMMAND tst_hittest)
