add_library(wit_imu_modbus INTERFACE)

target_include_directories(wit_imu_modbus INTERFACE .)
target_link_libraries(wit_imu_modbus INTERFACE zephyr_interface)

if(CONFIG_WIT_IMU_MODBUS)
	add_library(wit_imu_modbus_impl STATIC)
	target_include_directories(wit_imu_modbus_impl PUBLIC
		.
		${CMAKE_CURRENT_SOURCE_DIR}/../../module/imu_sample_service
	)
	target_link_libraries(wit_imu_modbus_impl PUBLIC zephyr_interface)
	target_sources(wit_imu_modbus_impl PRIVATE wit_imu_modbus.c)
	target_link_libraries(wit_imu_modbus INTERFACE wit_imu_modbus_impl)
endif()
