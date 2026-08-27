add_library(modbus_register_service INTERFACE)

target_include_directories(modbus_register_service INTERFACE .)
target_link_libraries(modbus_register_service INTERFACE zephyr_interface)

if(CONFIG_MODBUS_REGISTER_SERVICE)
	add_library(modbus_register_service_impl STATIC)
	target_include_directories(modbus_register_service_impl PUBLIC .)
	target_link_libraries(modbus_register_service_impl PUBLIC
		zephyr_interface
	)
	target_sources(modbus_register_service_impl PRIVATE
		modbus_register_service.c
	)
	target_sources_ifdef(CONFIG_MODBUS_REGISTER_STORE
		modbus_register_service_impl PRIVATE
		modbus_register_store.c
	)
	target_link_libraries(modbus_register_service INTERFACE
		modbus_register_service_impl
	)
endif()
