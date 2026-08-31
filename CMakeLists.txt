cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(craner_sensor_hub)

add_subdirectory(module/system_health_service)
add_subdirectory(module/network_manager_service)
add_subdirectory(module/time_manager_service)
add_subdirectory(module/ota_manager_service)
add_subdirectory(module/device_param_server)
add_subdirectory(module/device_identity_service)
add_subdirectory(module/fault_dump_service)
add_subdirectory(module/modbus_data_model)
add_subdirectory(module/modbus_tcp_server)
add_subdirectory(module/imu_sample_service)
add_subdirectory(module/encoder_sample_service)
add_subdirectory(module/anemometer_sample_service)
add_subdirectory(app/modbus_tcp)
add_subdirectory(app/power_manager)
add_subdirectory(app/system_health)
add_subdirectory(app/luffing_imu_app)
add_subdirectory(app/encoder_app)
add_subdirectory(app/anemometer_app)

target_include_directories(app PRIVATE include src app/system_health)

target_sources(app PRIVATE
	src/main.c
)

target_link_libraries(app PRIVATE
	system_health_service
	network_manager_service
	time_manager_service
	ota_manager_service
	device_param_server
	device_identity_service
	fault_dump_service
	modbus_data_model
	modbus_tcp_server
	imu_sample_service
	encoder_sample_service
	anemometer_sample_service
)
