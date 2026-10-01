cmake_minimum_required(VERSION 3.22)

include(cmake/KrisCCVersion.cmake)
project(krisCC VERSION ${KRISCC_VERSION} DESCRIPTION "Personal Control Center for KrisOS and Fedora bootc" LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Qml Quick DBus Network Test)
find_package(KF6Kirigami REQUIRED)
qt_standard_project_setup(REQUIRES 6.7)

qt_add_executable(krisCC
    src/main.cpp
    src/BootcBackend.cpp src/BootcBackend.h
    src/CustomActionsBackend.cpp src/CustomActionsBackend.h
    src/CronBackend.cpp src/CronBackend.h src/CronParser.cpp src/CronParser.h
    src/ServiceManagerBackend.cpp src/ServiceManagerBackend.h src/ServiceJson.cpp src/ServiceJson.h
    src/SystemdJobCoordinator.cpp src/SystemdJobCoordinator.h
    src/SystemBackend.cpp src/SystemBackend.h
    src/SystemBackendRuntime.cpp src/SystemBackendRuntime.h
    src/SystemBackendRuntimeArchive.cpp src/SystemBackendRuntimeLifecycle.cpp
    src/SystemBackendRuntimeServices.cpp src/SystemBackendRuntimeBoot.cpp
    src/BackupSafety.cpp src/BackupSafety.h
    src/SoftwareBackend.cpp src/SoftwareBackend.h
    src/PackageSearch.cpp src/PackageSearch.h
    src/PackageInventoryCache.cpp src/PackageInventoryCache.h
    src/PolkitHelper.cpp src/PolkitHelper.h src/RkBackend.cpp src/RkBackend.h
    src/MaintenanceBackend.cpp src/MaintenanceBackend.h src/UtilityBackend.cpp src/UtilityBackend.h
    src/RepairBackend.cpp src/RepairBackend.h src/InstanceController.cpp src/InstanceController.h
    src/OperationLog.cpp src/OperationLog.h src/Validators.cpp src/Validators.h
    src/AdminPolicy.cpp src/AdminPolicy.h src/ProcessRunner.cpp src/ProcessRunner.h
    src/ContractParsers.cpp src/ContractParsers.h
)

set_source_files_properties(qml/components/UiMetrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set(KRISCC_QML_FILES
    qml/components/UiMetrics.qml qml/components/OutputCard.qml qml/Main.qml
    qml/modules/DashboardModule.qml qml/modules/SoftwareModule.qml qml/modules/ToolsModule.qml
    qml/modules/ServicesNetworkModule.qml qml/modules/SystemModule.qml qml/modules/CommandsModule.qml
    qml/modules/RecoveryModule.qml)
foreach(qml_file IN LISTS KRISCC_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()
qt_add_qml_module(krisCC URI org.kriscc VERSION 1.0 RESOURCE_PREFIX /qt/qml QML_FILES ${KRISCC_QML_FILES})
target_link_libraries(krisCC PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::DBus Qt6::Network KF6::Kirigami)
target_compile_definitions(krisCC PRIVATE KRISCC_VERSION="${PROJECT_VERSION}")

add_executable(kriscc-admin src/AdminHelper.cpp src/AdminPolicy.cpp src/AdminPolicy.h src/Validators.cpp src/Validators.h)
set_target_properties(kriscc-admin PROPERTIES OUTPUT_NAME admin)
target_link_libraries(kriscc-admin PRIVATE Qt6::Core)
add_executable(kriscc-archive src/ArchiveTool.cpp src/ArchiveRestoreEngine.cpp src/ArchiveRestoreEngine.h src/BackupSafety.cpp src/BackupSafety.h)
set_target_properties(kriscc-archive PROPERTIES OUTPUT_NAME archive)
target_link_libraries(kriscc-archive PRIVATE Qt6::Core)

include(CTest)
if(BUILD_TESTING)
    add_executable(kriscc-test-validators tests/test_validators.cpp src/Validators.cpp src/Validators.h)
    target_include_directories(kriscc-test-validators PRIVATE src)
    target_link_libraries(kriscc-test-validators PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME validators COMMAND kriscc-test-validators)

    add_executable(kriscc-test-admin-policy tests/test_admin_policy.cpp src/AdminPolicy.cpp src/AdminPolicy.h src/Validators.cpp src/Validators.h)
    target_include_directories(kriscc-test-admin-policy PRIVATE src)
    target_link_libraries(kriscc-test-admin-policy PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME admin-policy COMMAND kriscc-test-admin-policy)

    add_executable(kriscc-test-process-runner tests/test_process_runner.cpp src/ProcessRunner.cpp src/ProcessRunner.h)
    target_include_directories(kriscc-test-process-runner PRIVATE src)
    target_link_libraries(kriscc-test-process-runner PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME process-runner COMMAND kriscc-test-process-runner)
    set_tests_properties(process-runner PROPERTIES TIMEOUT 15)

    add_executable(kriscc-test-backup-safety tests/test_backup_safety.cpp src/BackupSafety.cpp src/BackupSafety.h)
    target_include_directories(kriscc-test-backup-safety PRIVATE src)
    target_link_libraries(kriscc-test-backup-safety PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME backup-safety COMMAND kriscc-test-backup-safety)

    add_executable(kriscc-test-archive-restore tests/test_archive_restore.cpp src/ArchiveRestoreEngine.cpp src/ArchiveRestoreEngine.h)
    target_include_directories(kriscc-test-archive-restore PRIVATE src)
    target_link_libraries(kriscc-test-archive-restore PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME archive-restore COMMAND kriscc-test-archive-restore)
    set_tests_properties(archive-restore PROPERTIES TIMEOUT 30)

    add_executable(kriscc-test-service-json tests/test_service_json.cpp src/ServiceJson.cpp src/ServiceJson.h)
    target_include_directories(kriscc-test-service-json PRIVATE src)
    target_link_libraries(kriscc-test-service-json PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME service-json COMMAND kriscc-test-service-json)

    add_executable(kriscc-test-service-manager-reentrancy
        tests/test_service_manager_reentrancy.cpp
        src/ServiceManagerBackend.cpp src/ServiceManagerBackend.h
        src/ServiceJson.cpp src/ServiceJson.h
        src/ProcessRunner.cpp src/ProcessRunner.h
        src/OperationLog.cpp src/OperationLog.h)
    target_include_directories(kriscc-test-service-manager-reentrancy PRIVATE src)
    target_link_libraries(kriscc-test-service-manager-reentrancy PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME service-manager-reentrancy COMMAND kriscc-test-service-manager-reentrancy)

    add_executable(kriscc-test-systemd-job-coordinator
        tests/test_systemd_job_coordinator.cpp
        src/SystemdJobCoordinator.cpp src/SystemdJobCoordinator.h)
    target_include_directories(kriscc-test-systemd-job-coordinator PRIVATE src)
    target_link_libraries(kriscc-test-systemd-job-coordinator PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
    add_test(NAME systemd-job-coordinator COMMAND kriscc-test-systemd-job-coordinator)
    set_tests_properties(systemd-job-coordinator PROPERTIES TIMEOUT 10)

    add_executable(kriscc-test-maintenance-trash tests/test_maintenance_trash.cpp src/MaintenanceTrash.cpp src/MaintenanceTrash.h)
    target_include_directories(kriscc-test-maintenance-trash PRIVATE src)
    target_link_libraries(kriscc-test-maintenance-trash PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME maintenance-trash COMMAND kriscc-test-maintenance-trash)

    add_test(NAME cleanup-estimate COMMAND /usr/bin/bash ${CMAKE_SOURCE_DIR}/tests/test_cleanup_estimate.sh)

    add_executable(kriscc-test-package-inventory tests/test_package_inventory.cpp src/PackageInventoryCache.cpp src/PackageInventoryCache.h)
    target_include_directories(kriscc-test-package-inventory PRIVATE src)
    target_link_libraries(kriscc-test-package-inventory PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME package-inventory COMMAND kriscc-test-package-inventory)
    set_tests_properties(package-inventory PROPERTIES TIMEOUT 15)

    add_executable(kriscc-test-parsers tests/test_contract_parsers.cpp src/ContractParsers.cpp src/ContractParsers.h src/Validators.cpp src/Validators.h)
    target_include_directories(kriscc-test-parsers PRIVATE src)
    target_link_libraries(kriscc-test-parsers PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME parsers COMMAND kriscc-test-parsers)

    add_executable(kriscc-test-cron-parser tests/test_cron_parser.cpp src/CronParser.cpp src/CronParser.h)
    target_include_directories(kriscc-test-cron-parser PRIVATE src)
    target_link_libraries(kriscc-test-cron-parser PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME cron-parser COMMAND kriscc-test-cron-parser)
endif()

add_executable(kriscc-maintenance src/MaintenanceHelper.cpp src/MaintenanceTrash.cpp src/MaintenanceTrash.h)
set_target_properties(kriscc-maintenance PROPERTIES OUTPUT_NAME maintenance)
target_link_libraries(kriscc-maintenance PRIVATE Qt6::Core)
install(TARGETS krisCC RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS kriscc-admin kriscc-archive kriscc-maintenance RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/kriscc)
install(PROGRAMS src/bootc-status.sh DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/kriscc RENAME bootc-status)
install(PROGRAMS src/cleanup-estimate.sh DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/kriscc RENAME cleanup-estimate)
install(FILES data/krisCC.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.kriscc.controlcenter.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES data/org.kriscc.KrisCC.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/icons/hicolor/scalable/apps/krisCC.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
