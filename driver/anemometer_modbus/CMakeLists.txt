add_library(anemometer_modbus INTERFACE)

target_include_directories(anemometer_modbus INTERFACE .)
target_link_libraries(anemometer_modbus INTERFACE zephyr_interface)

if(CONFIG_ANEMOMETER_MODBUS)
	add_library(anemometer_modbus_impl STATIC)
	target_include_directories(anemometer_modbus_impl PUBLIC .)
	target_link_libraries(anemometer_modbus_impl PUBLIC zephyr_interface)
	target_sources(anemometer_modbus_impl PRIVATE anemometer_modbus.c)
	target_link_libraries(anemometer_modbus INTERFACE anemometer_modbus_impl)
endif()
