# Anemometer Modbus Helper

This helper wraps one anemometer Modbus RTU read.

It owns:

- Modbus RTU client initialization.
- Reading the configured holding-register block.
- Decoding raw registers into temperature, humidity, pressure, wind speed,
  and wind direction fields.

It deliberately does not own product register names, health events, channel
selection, or sampling threads.
