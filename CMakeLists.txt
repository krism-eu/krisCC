cmake_minimum_required(VERSION 3.22)

project(krisCC
    VERSION 0.7.0
    DESCRIPTION "Personal Control Center for KrisOS and Fedora bootc"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Qml Quick DBus Test)
find_package(KF6Kirigami REQUIRED)
qt_standard_project_setup(REQUIRES 6.7)

qt_add_executable(krisCC
    src/main.cpp
    src/BootcBackend.cpp src/BootcBackend.h
    src/CustomActionsBackend.cpp src/CustomActionsBackend.h
    src/SystemBackend.cpp src/SystemBackend.h
    src/SoftwareBackend.cpp src/SoftwareBackend.h
    src/PackageSearch.cpp src/PackageSearch.h
    src/PolkitHelper.cpp src/PolkitHelper.h
    src/RkBackend.cpp src/RkBackend.h
    src/MaintenanceBackend.cpp src/MaintenanceBackend.h
    src/UtilityBackend.cpp src/UtilityBackend.h
    src/InstanceController.cpp src/InstanceController.h
    src/OperationLog.cpp src/OperationLog.h
    src/Validators.cpp src/Validators.h
)

set_source_files_properties(qml/components/UiMetrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set(KRISCC_QML_FILES
    qml/components/UiMetrics.qml
    qml/components/PageIntro.qml
    qml/components/OutputCard.qml
    qml/Main.qml
    qml/modules/DashboardModule.qml
    qml/modules/SoftwareModule.qml
    qml/modules/FlatpakModule.qml
    qml/modules/PodmanModule.qml
    qml/modules/SystemModule.qml
    qml/modules/CommandsModule.qml
    qml/modules/RecoveryModule.qml
)

foreach(qml_file IN LISTS KRISCC_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()


qt_add_qml_module(krisCC
    URI org.kriscc
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${KRISCC_QML_FILES}
)

target_link_libraries(krisCC PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::DBus
    KF6::Kirigami
)
set(KRISCC_RELEASE 7)
target_compile_definitions(krisCC PRIVATE KRISCC_VERSION="${PROJECT_VERSION}-${KRISCC_RELEASE}")

add_executable(kriscc-admin
    src/AdminHelper.cpp
    src/Validators.cpp src/Validators.h
)
set_target_properties(kriscc-admin PROPERTIES OUTPUT_NAME admin)
target_link_libraries(kriscc-admin PRIVATE Qt6::Core)

enable_testing()
add_executable(kriscc-test-validators
    tests/test_validators.cpp
    src/Validators.cpp src/Validators.h
)
target_link_libraries(kriscc-test-validators PRIVATE Qt6::Core Qt6::Test)
add_test(NAME validators COMMAND kriscc-test-validators)

add_executable(kriscc-maintenance
    src/MaintenanceHelper.cpp
    src/MaintenanceTrash.cpp src/MaintenanceTrash.h
)
set_target_properties(kriscc-maintenance PROPERTIES OUTPUT_NAME maintenance)
target_link_libraries(kriscc-maintenance PRIVATE Qt6::Core)

install(TARGETS krisCC RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS kriscc-admin kriscc-maintenance RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/kriscc)
install(PROGRAMS src/bootc-status.sh DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/kriscc RENAME bootc-status)
install(FILES data/krisCC.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.kriscc.controlcenter.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES data/org.kriscc.KrisCC.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/icons/hicolor/scalable/apps/krisCC.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)