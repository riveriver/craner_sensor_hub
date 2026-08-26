# IDECODER Encoder Modbus Helper

This helper wraps the Modbus RTU access pattern for one IDECODER encoder.

It owns:

- Modbus client initialization on one configured RS485/Modbus interface.
- Reading the encoder holding registers.
- Returning the raw turn count and single-turn value.

It deliberately does not own:

- Product channel names such as slewing, luffing, or hoisting.
- Modbus TCP register mapping.
- System health events.
- Sampling threads or retry policy.

Enable it with `CONFIG_IDECODER_ENCODER_MODBUS`.
