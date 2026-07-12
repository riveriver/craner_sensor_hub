cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(craner_encoder_hub)

target_sources(app PRIVATE
	src/main.c
	src/log_app_sensor.c
	src/log_app_comm.c
	src/power_control_app.c
	src/heartbeat_led_app.c
	src/shell_app.c
	src/modbus_rtu_client_app.c
)
