# Anemometer Sample Service

`anemometer_sample_service` provides a reusable one-channel anemometer
sampling worker.

It owns:

- One sampling thread per service instance.
- Retry and reinitialization of the underlying Modbus helper.
- Latest sample and statistics storage.
- A callback after every successful or failed read.
- `anemometer stats` shell diagnostics.

It deliberately does not own product Modbus TCP register names, health events,
or board channel selection.
