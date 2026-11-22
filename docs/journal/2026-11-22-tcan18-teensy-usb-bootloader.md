# TCAN-18: software-triggered Teensy 4.1 bootloader (2026-11-22)

## What PJRC does (read in the PJRC `teensy4` core, `~/Documents/teensy/teensy4/usb.c`)  [verified: source read]
- Request: the host sets the USB CDC serial port to **134 baud** (`usb.c:843`, `if (usb_cdc_line_coding[0] == 134)`). A second path, a SEREMU HID feature report `A9 45 C2 6B` (`usb.c:874`), is for the HID serial emulation and is not used here.
- Then 80 SOF interrupts (~10 ms) later the core calls `_reboot_Teensyduino_()` (`usb.c:241`), which is `asm("bkpt #251")` when OCOTP CFG5 bit 1 is clear (`usb.c:243-244`). The countdown is never cancelled, so a host that closes the port right after setting 134 baud still gets the reboot.
- If CFG5 bit 1 is set (secure-locked chip) PJRC takes a different path with hard-coded RAM and ROM addresses (`usb.c:246-254`). **Not implemented here**: the firmware refuses and logs.
- `teensy_loader_cli -s` ("soft reboot") does NOT help on macOS: `soft_reboot()` prints "Soft reboot is not implemented for OSX" (`teensy_loader_cli.c:716`). So the host side must set 134 baud itself (`stty -f <port> 134`).

## Hardware result (2026-11-22, owner's Teensy 4.1)  [verified: observed]
- Flashed with the PROGRAM button, the app enumerated as "TrackCAN DataLogger" and `scripts/teensy_usb_flash.sh` took it into HalfKay (16C0:0478), flashed and rebooted with **no button press**. Repeated twice. So `bkpt #251` under Zephyr 4.4 does reach the bootloader; the Zephyr HardFault handler did not get in the way.
- First attempt failed for a different reason: with default stack sizes the USB stack hit an MPU stack overflow ~10 ms after boot (no USB port, console showed the fault). Fixed in prj.conf (UDC workqueue, USBD thread, system workqueue, main, ISR stacks).
- Not yet checked: that opening the port at 115200 does not reboot; the PROGRAM button after this firmware; CAN traffic during the request.

## What was NOT verified before the test (kept for the record)  [now resolved above for the main question; the reason it works is still not documented by PJRC]
- **Why `bkpt #251` ends in HalfKay.** With no debugger attached a `bkpt` escalates to a HardFault. The core source does not show what catches it (presumably the Teensy's separate bootloader chip), and I found no PJRC text stating it. 
- **Whether it works under Zephyr.** Zephyr installs its own HardFault handler (`arch/arm/core/cortex_m/fault.c`, DEBUGEVT case at line 731), which may handle the trap itself and reset or halt instead of leaving it to whatever PJRC relies on. Only the physical Teensy can tell.
- Console: `bootloader_control.c` locks interrupts before the trap, as PJRC's path effectively does.

## Implementation
- `DataLogger_Node/src/bootloader/bootloader_request.[ch]`: pure logic, host-tested (5026 checks): 134 baud, 50 ms delay, fires once, not cancelled, wrap-safe.
- `bootloader_control.[ch]`: 10 ms delayed work polling `uart_line_ctrl_get(BAUD_RATE)` of the new USB CDC ACM port; at the request, `LOG_PANIC()`, `irq_lock()`, `bkpt #251`.
- `prj.conf`: USB device_next stack, CDC ACM at boot (product string "TrackCAN DataLogger"), UART line control. `boards/teensy41.overlay`: `usb_serial` node under `zephyr_udc0`. The console stays on LPUART6.
- `scripts/teensy_usb_flash.sh`: finds the port, sends 134 baud, `teensy_loader_cli --mcu=TEENSY41 -w`.

## Hardware validation (needs the Teensy)
1. Press PROGRAM once and flash `build/datalogger_bl/zephyr/zephyr.hex` (this image has the feature; older images do not).
2. Confirm the port: `ioreg -r -c IOUSBHostDevice -l -w0 | grep -B2 -A12 "TrackCAN DataLogger"`; expect an `IOCalloutDevice`.
3. Run `scripts/teensy_usb_flash.sh`. Pass: the Teensy leaves the app (USB device 16C0:0478 appears) and flashing completes with no button.
4. Fail modes to look for: nothing happens (trap swallowed by Zephyr's fault handler: the board resets and the app restarts); the Teensy hangs (press PROGRAM, still works); the port never appears (USB not enumerating: check `dmesg`-style via `ioreg`).
5. Confirm normal operation is unaffected: CAN logger, SPI sensor prints on the UART console, and that opening the port at 115200 does not reboot.
6. Regression check: the PROGRAM button still enters the bootloader.

If step 3 fails, the fallback is not a different trap but a decision for the owner: PJRC publishes no other software entry, so the options are the button, or a custom hardware line.
