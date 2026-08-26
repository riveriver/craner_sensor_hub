add_library(idecoder_encoder_modbus INTERFACE)

target_include_directories(idecoder_encoder_modbus INTERFACE .)
target_link_libraries(idecoder_encoder_modbus INTERFACE zephyr_interface)

if(CONFIG_IDECODER_ENCODER_MODBUS)
	add_library(idecoder_encoder_modbus_impl STATIC)
	target_include_directories(idecoder_encoder_modbus_impl PUBLIC
		.
		${CMAKE_CURRENT_SOURCE_DIR}/../../module/encoder_sample_service
	)
	target_link_libraries(idecoder_encoder_modbus_impl PUBLIC zephyr_interface)
	target_sources(idecoder_encoder_modbus_impl PRIVATE idecoder_encoder_modbus.c)
	target_link_libraries(idecoder_encoder_modbus INTERFACE idecoder_encoder_modbus_impl)
endif()
