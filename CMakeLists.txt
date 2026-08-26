cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(craner_encoder_hub)

add_subdirectory(driver/wit_imu_modbus)
add_subdirectory(driver/idecoder_encoder_modbus)
add_subdirectory(driver/anemometer_modbus)
add_subdirectory(module/system_health_service)
add_subdirectory(module/imu_sample_service)
add_subdirectory(module/encoder_sample_service)
add_subdirectory(module/anemometer_sample_service)
add_subdirectory(app/luffing_imu_app)
add_subdirectory(app/encoder_app)
add_subdirectory(app/anemometer_app)

target_include_directories(app PRIVATE include src)

target_sources(app PRIVATE
	src/main.c
	src/device_identity_service.c
	src/network_service.c
	src/time_service.c
	src/rtc_trust_store.c
	src/rtc_time_provider.c
	src/power_control_app.c
	src/shell_app.c
	src/modbus_register_map.c
	src/modbus_register_service.c
)

target_sources_ifdef(CONFIG_ENABLE_LOG_SENSOR_THREAD app PRIVATE
	src/log_app_sensor.c
)

target_sources_ifdef(CONFIG_ENABLE_LOG_COMM_THREAD app PRIVATE
	src/log_app_comm.c
)

target_sources_ifdef(CONFIG_SYS_HEALTH_SERVICE app PRIVATE
	src/system_health_event_table.c
)

target_sources_ifdef(CONFIG_ENABLE_MODBUS_TCP_SERVER_THREAD app PRIVATE
	src/modbus_tcp_server_app.c
)

target_sources_ifdef(CONFIG_ENABLE_OTA_CONTROL app PRIVATE
	src/ota_control_app.c
)

target_sources_ifdef(CONFIG_ENABLE_STORAGE_SERVICE app PRIVATE
	src/storage_service.c
)

target_sources_ifdef(CONFIG_ENABLE_DEVICE_PARAM_STORE app PRIVATE
	src/device_param_store.c
)

target_sources_ifdef(CONFIG_ENABLE_MODBUS_REGISTER_STORE app PRIVATE
	src/modbus_register_store.c
)

target_sources_ifdef(CONFIG_ENABLE_COREDUMP_SERVICE app PRIVATE
	src/coredump_service.c
)

target_link_libraries(app PRIVATE
	wit_imu_modbus
	idecoder_encoder_modbus
	anemometer_modbus
	imu_sample_service
	encoder_sample_service
	anemometer_sample_service
)
