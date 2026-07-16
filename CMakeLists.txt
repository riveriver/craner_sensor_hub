cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(craner_encoder_hub)

target_include_directories(app PRIVATE include)

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

target_sources_ifdef(CONFIG_CRANER_ENABLE_LOG_SENSOR_THREAD app PRIVATE
	src/log_app_sensor.c
)

target_sources_ifdef(CONFIG_CRANER_ENABLE_LOG_COMM_THREAD app PRIVATE
	src/log_app_comm.c
)

target_sources_ifdef(CONFIG_CRANER_ENABLE_SYSTEM_HEALTH_THREAD app PRIVATE
	src/system_health_app.c
	src/system_health_event_table.c
	src/stack_monitor_service.c
)

if(CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD OR
   CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD OR
   CONFIG_CRANER_ENABLE_READ_HOISTING_ENCODER_THREAD)
	target_sources(app PRIVATE src/modbus_rtu_client_app.c)
endif()

target_sources_ifdef(CONFIG_CRANER_ENABLE_MODBUS_TCP_SERVER_THREAD app PRIVATE
	src/modbus_tcp_server_app.c
)

target_sources_ifdef(CONFIG_CRANER_ENABLE_OTA_CONTROL app PRIVATE
	src/ota_control_app.c
)

target_sources_ifdef(CONFIG_CRANER_ENABLE_STORAGE_SERVICE app PRIVATE
	src/storage_service.c
)

target_sources_ifdef(CONFIG_CRANER_ENABLE_DEVICE_PARAM_STORE app PRIVATE
	src/device_param_store.c
)

target_sources_ifdef(CONFIG_CRANER_ENABLE_MODBUS_REGISTER_STORE app PRIVATE
	src/modbus_register_store.c
)

target_sources_ifdef(CONFIG_CRANER_ENABLE_COREDUMP_SERVICE app PRIVATE
	src/coredump_service.c
)

target_sources_ifdef(CONFIG_CRANER_ENABLE_MQTT_SERVICE_MANAGER app PRIVATE
	src/mqtt_service_manager.c
)

target_sources_ifdef(CONFIG_CRANER_ENABLE_MQTT_LOG_SERVICE app PRIVATE
	src/mqtt_log_service.c
)

target_sources_ifdef(CONFIG_CRANER_ENABLE_MQTT_SHELL_SERVICE app PRIVATE
	src/mqtt_shell_service.c
)
