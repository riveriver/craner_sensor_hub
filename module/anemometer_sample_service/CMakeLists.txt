add_library(anemometer_sample_service INTERFACE)

target_include_directories(anemometer_sample_service INTERFACE .)
target_link_libraries(anemometer_sample_service INTERFACE
	zephyr_interface
)

if(CONFIG_ANEMOMETER_SAMPLE_SERVICE)
	add_library(anemometer_sample_service_impl STATIC)
	target_include_directories(anemometer_sample_service_impl PUBLIC .)
	target_link_libraries(anemometer_sample_service_impl PUBLIC
		zephyr_interface
	)
	target_sources(anemometer_sample_service_impl PRIVATE
		anemometer_sample_service.c
	)
	target_link_libraries(anemometer_sample_service INTERFACE
		anemometer_sample_service_impl
	)
endif()
