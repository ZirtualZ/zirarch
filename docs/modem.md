# Modem research

The live On5 exposed no modem device that could be identified safely.
No `/dev/modem*`, `/dev/ttyUSB*`, `rmnet*`, or `ccmni*` node was found,
and `dmesg` showed no IPC/modem startup evidence.

This is a read-only finding. No modem, RADIO, EFS, CPEFS or firmware
partition was accessed or written.

## What an implementation would need

- A discovered Samsung modem control interface (IPC, RIL or ModemManager).
- Existing proprietary blobs and their exact device/ABI compatibility.
- One of libsamsung-ipc, ModemManager, ofono, or a vendor RIL/IPC stack.
- Cellular radio provisioning and APN configuration.
- SMS/call control, SIM state, signal strength and network registration.
- Explicit separation of read-only research from any reset/start action.
