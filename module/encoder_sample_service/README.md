# Encoder Sample Service

`encoder_sample_service` provides a reusable one-channel encoder sampling
worker.

It owns:

- One sampling thread per service instance.
- Retry and reinitialization of the underlying IDECODER Modbus helper.
- Latest sample and statistics storage.
- A callback after every successful or failed read.

It deliberately does not own:

- Product channel names such as slewing, luffing, or hoisting.
- Which RS485 ports a product uses.
- Modbus TCP register writes.
- System health events.

The product app creates one `struct encoder_sample_service` per encoder it
wants to sample and starts each instance with its own
`struct encoder_sample_service_config`.
