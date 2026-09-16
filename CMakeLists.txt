cmake_minimum_required(VERSION 3.16)

project(ClassFlow VERSION 5.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools Svg)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools Svg)

set(TS_FILES ClassFlow_zh_CN.ts)

# ---- core 纯逻辑库：models / store / schedule，可独立单测 ----
set(CORE_SOURCES
        # core/models：数据模型
        src/core/models/models.cpp
        src/core/models/models.h
        src/core/models/term.h
        src/core/models/user.h
        src/core/models/course.h
        src/core/models/classroom.h
        src/core/models/teacher.h
        src/core/models/timetable.h

        # core/filter：筛选逻辑（与排课核心平行，供 UI 调用）
        src/core/filter/schedulefilter.cpp
        src/core/filter/schedulefilter.h

        # core/store：数据存取（CSV 导入/导出 / 工作区快照 / ID 索引查询 / 公共辅助）
        src/core/store/csv.cpp
        src/core/store/csv.h
        src/core/store/datastore.cpp
        src/core/store/datastore.h
        src/core/store/datastore_edit.cpp
        src/core/store/snapshot.cpp
        src/core/store/lookup.cpp
        src/core/store/undobuffer.cpp
        src/core/store/undobuffer.h
        src/core/store/utility.cpp
        src/core/store/utility.h

        # core/schedule：排课核心（冲突检测 + 排课算法）
        src/core/schedule/conflicttable.cpp
        src/core/schedule/conflicttable.h
        src/core/schedule/scheduler.cpp
        src/core/schedule/scheduler.h
        src/core/schedule/strategy.cpp
        src/core/schedule/strategy.h
        src/core/schedule/annealingstrategy.cpp
        src/core/schedule/annealingstrategy.h
        src/core/schedule/annealingcandidate.cpp
        src/core/schedule/annealingcandidate.h
        src/core/schedule/annealingmoves.cpp
        src/core/schedule/annealingcost.cpp
        src/core/schedule/annealingcost.h
        src/core/schedule/manualmove.cpp
        src/core/schedule/manualmove.h
        src/core/schedule/manualadd.cpp
        src/core/schedule/manualadd.h
        src/core/schedule/roomrules.cpp
        src/core/schedule/roomrules.h
)

add_library(classflow_core STATIC ${CORE_SOURCES})

# 以 src 为 include 根目录，跨层 include 使用 core/models/...、core/store/... 等相对路径
target_include_directories(classflow_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(classflow_core PUBLIC Qt${QT_VERSION_MAJOR}::Core)

# ---- 应用：入口 + UI 表现层 ----
set(APP_SOURCES
        src/main.cpp

        # ui：Qt Widgets 表现层（按职责分子目录：dialog 弹窗 / timetable 课表 MVD / schedule 后台排课）
        src/ui/mainwindow.cpp
        src/ui/mainwindow.h
        src/ui/mainwindow.ui
        src/ui/emptystate.cpp
        src/ui/emptystate.h
        src/ui/dialog/importdialog.cpp
        src/ui/dialog/importdialog.h
        src/ui/dialog/importdialog.ui
        src/ui/dialog/snapshotimportdialog.cpp
        src/ui/dialog/snapshotimportdialog.h
        src/ui/dialog/coursedetaildialog.cpp
        src/ui/dialog/coursedetaildialog.h
        src/ui/dialog/editinfodialog.cpp
        src/ui/dialog/editinfodialog.h
        src/ui/dialog/editui.cpp
        src/ui/dialog/editui.h
        src/ui/dialog/classinfoform.cpp
        src/ui/dialog/classinfoform.h
        src/ui/dialog/classeditdialog.cpp
        src/ui/dialog/classeditdialog.h
        src/ui/dialog/timeroomeditor.cpp
        src/ui/dialog/timeroomeditor.h
        src/ui/dialog/roomtimeutil.cpp
        src/ui/dialog/roomtimeutil.h
        src/ui/dialog/fromscratcheditor.cpp
        src/ui/dialog/fromscratcheditor.h
        src/ui/dialog/deletedialog.cpp
        src/ui/dialog/deletedialog.h
        src/ui/dialog/adddialog.cpp
        src/ui/dialog/adddialog.h
        src/ui/dialog/classgrouping.cpp
        src/ui/dialog/classgrouping.h
        src/ui/dialog/lockdialog.cpp
        src/ui/dialog/lockdialog.h
        src/ui/dialog/courselistdialog.cpp
        src/ui/dialog/courselistdialog.h
        src/ui/dialog/scheduleerrordialog.cpp
        src/ui/dialog/scheduleerrordialog.h
        src/ui/dialog/filterdialog.cpp
        src/ui/dialog/filterdialog.h
        src/ui/timetable/timetablemodel.cpp
        src/ui/timetable/timetablemodel.h
        src/ui/timetable/timetabledelegate.cpp
        src/ui/timetable/timetabledelegate.h
        src/ui/timetable/timetableview.cpp
        src/ui/timetable/timetableview.h
        src/ui/timetable/timetablecontroller.cpp
        src/ui/timetable/timetablecontroller.h
        src/ui/schedule/scheduleworker.cpp
        src/ui/schedule/scheduleworker.h
        src/ui/schedule/schedulecontroller.cpp
        src/ui/schedule/schedulecontroller.h

        # ui/theme：主题系统（ThemeManager 单例 + QSS 模板资源 + 主题色图标助手）
        src/ui/theme/theme.cpp
        src/ui/theme/theme.h
        src/ui/theme/themeicons.cpp
        src/ui/theme/themeicons.h
        src/ui/theme/theme.qrc

        # icons：Feather 图标源（根目录独立资源，运行时前缀 /theme/icons，见 icons/icons.qrc）
        icons/icons.qrc

        ${TS_FILES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(ClassFlow
        MANUAL_FINALIZATION
        ${APP_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET ClassFlow APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    if(ANDROID)
        add_library(ClassFlow SHARED
            ${APP_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(ClassFlow
            ${APP_SOURCES}
        )
    endif()

    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

target_link_libraries(ClassFlow PRIVATE classflow_core
    Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Svg)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.ClassFlow)
endif()
set_target_properties(ClassFlow PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# ---- Windows：把 icons/classflow.ico 嵌为 exe 程序图标 ----
# 用 configure_file 生成 app.rc（图标路径注入绝对路径，避免 windres 相对路径歧义）；
# 编译进 ClassFlow.exe 后，资源管理器/任务栏/快捷方式即显示该图标（另见 main 里
# setWindowIcon 负责运行期窗口图标，qrc 内 classflow.png 与之配套）。
if(WIN32)
    enable_language(RC)
    set(CF_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/icons/classflow.ico")
    configure_file(app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    target_sources(ClassFlow PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

include(GNUInstallDirs)
install(TARGETS ClassFlow
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ---- 单元测试（tests/，可 -DBUILD_TESTING=OFF 关闭）----
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(ClassFlow)
endif()
