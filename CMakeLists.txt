cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(craner_encoder_hub)

add_subdirectory(module/system_health_service)
add_subdirectory(module/network_manager_service)
add_subdirectory(module/time_manager_service)
add_subdirectory(module/ota_manager_service)
add_subdirectory(module/device_param_server)
add_subdirectory(module/device_identity_service)
add_subdirectory(module/modbus_register_service)
add_subdirectory(module/modbus_tcp_server)
add_subdirectory(module/imu_sample_service)
add_subdirectory(module/encoder_sample_service)
add_subdirectory(module/anemometer_sample_service)
add_subdirectory(app/luffing_imu_app)
add_subdirectory(app/encoder_app)
add_subdirectory(app/anemometer_app)

target_include_directories(app PRIVATE include src)

target_sources(app PRIVATE
	src/main.c
	src/power_control_app.c
	src/shell_app.c
	src/modbus_register_map.c
)

target_sources_ifdef(CONFIG_MODBUS_TCP_SERVER app PRIVATE
	src/modbus_tcp_register_adapter.c
)

target_sources_ifdef(CONFIG_ENABLE_LOG_SENSOR_THREAD app PRIVATE
	src/log_app_sensor.c
)

target_sources_ifdef(CONFIG_ENABLE_LOG_COMM_THREAD app PRIVATE
	src/log_app_comm.c
)

target_sources_ifdef(CONFIG_SYS_HEALTH_SERVICE app PRIVATE
	src/system_health_app.c
)

target_sources_ifdef(CONFIG_ENABLE_STORAGE_SERVICE app PRIVATE
	src/storage_service.c
)

target_sources_ifdef(CONFIG_ENABLE_COREDUMP_SERVICE app PRIVATE
	src/coredump_service.c
)

target_link_libraries(app PRIVATE
	system_health_service
	network_manager_service
	time_manager_service
	ota_manager_service
	device_param_server
	device_identity_service
	modbus_register_service
	modbus_tcp_server
	imu_sample_service
	encoder_sample_service
	anemometer_sample_service
)
