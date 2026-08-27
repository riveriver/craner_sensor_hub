add_library(modbus_tcp_server_service INTERFACE)

target_include_directories(modbus_tcp_server_service INTERFACE .)
target_link_libraries(modbus_tcp_server_service INTERFACE
	zephyr_interface
	modbus_register_service
)

if(CONFIG_MODBUS_TCP_SERVER_SERVICE)
	add_library(modbus_tcp_server_service_impl STATIC)
	target_include_directories(modbus_tcp_server_service_impl PUBLIC .)
	target_link_libraries(modbus_tcp_server_service_impl PUBLIC
		zephyr_interface
		modbus_register_service
	)
	target_sources(modbus_tcp_server_service_impl PRIVATE
		modbus_tcp_server_service.c
	)
	target_link_libraries(modbus_tcp_server_service INTERFACE
		modbus_tcp_server_service_impl
	)
endif()
