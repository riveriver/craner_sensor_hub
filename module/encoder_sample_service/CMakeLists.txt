add_library(encoder_sample_service INTERFACE)

target_include_directories(encoder_sample_service INTERFACE .)
target_link_libraries(encoder_sample_service INTERFACE
	zephyr_interface
)

if(CONFIG_ENCODER_SAMPLE_SERVICE)
	add_library(encoder_sample_service_impl STATIC)
	target_include_directories(encoder_sample_service_impl PUBLIC .)
	target_link_libraries(encoder_sample_service_impl PUBLIC
		zephyr_interface
	)
	target_sources(encoder_sample_service_impl PRIVATE
		encoder_sample_service.c
		encoder_sample_service_stats.c
	)
	target_sources_ifdef(CONFIG_ENCODER_SAMPLE_SERVICE_SHELL
		encoder_sample_service_impl PRIVATE
		encoder_sample_service_shell.c
	)
	target_link_libraries(encoder_sample_service INTERFACE encoder_sample_service_impl)
endif()
