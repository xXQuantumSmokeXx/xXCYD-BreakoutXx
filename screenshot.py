r"""
CYD-Breakout screenshot capture.

Double-click: captures the current screen.
Command line:
  python screenshot.py [COM_PORT] [OUTPUT_FILE]

Examples:
  python screenshot.py
  python screenshot.py COM11
  python screenshot.py COM11 ScreenShots\breakout.bmp
  python screenshot.py COM11 gameplay.bmp

Default port: auto-detect (falls back to COM11)
"""

import os
import re
import struct
import sys
import time

try:
    import serial
except ImportError:
    print("Error: pyserial is not installed. Run: python -m pip install pyserial")
    sys.exit(1)

BAUD = 115200
W, H = 320, 240


def find_port():
    """Auto-detect CYD serial port, or fall back to COM11."""
    try:
        import serial.tools.list_ports
        for p in serial.tools.list_ports.comports():
            if any(x in (p.description or "") for x in ["CH340", "CP210", "USB-SERIAL", "USB Serial"]):
                return p.device
    except Exception:
        pass
    return "COM11"


DEFAULT_PORT = find_port()
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
COM_RE = re.compile(r"^COM\d+$", re.IGNORECASE)


def parse_args(argv):
    port = DEFAULT_PORT
    outfile = "screen.bmp"
    args = list(argv)

    if args and COM_RE.match(args[0]):
        port = args.pop(0).upper()

    if args:
        outfile = args.pop(0)

    if args:
        raise ValueError("Too many arguments.")

    return port, outfile


def ask_interactive():
    print("CYD-Breakout Screenshot")
    port = input(f"COM port [{DEFAULT_PORT}]: ").strip() or DEFAULT_PORT
    print("\nNavigate to the screen you want on the device first.")
    print("(Breakout gameplay or splash screen)")
    outfile = input("Output file [screen.bmp]: ").strip() or "screen.bmp"
    return port.upper(), outfile


def read_exact(ser, n, timeout=30):
    data = b""
    deadline = time.time() + timeout
    while len(data) < n and time.time() < deadline:
        chunk = ser.read(min(4096, n - len(data)))
        if chunk:
            data += chunk
            pct = len(data) * 100 // n
            print(f"  {pct}%", end="\r")
    return data


def wait_for_marker(ser, markers, timeout=20):
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        c = ser.read(1)
        if not c:
            continue
        buf += c
        for marker in markers:
            if buf.endswith(marker):
                return marker, buf
        if len(buf) > 512:
            buf = buf[-512:]
    return None, buf


def write_bmp(filename, pixels_bgr24):
    if not os.path.isabs(filename):
        filename = os.path.join(BASE_DIR, filename)
    folder = os.path.dirname(filename)
    if folder:
        os.makedirs(folder, exist_ok=True)

    filesize = 54 + W * H * 3
    hdr = bytearray(54)
    hdr[0:2] = b"BM"
    hdr[2:6] = struct.pack("<I", filesize)
    hdr[10] = 54
    hdr[14] = 40
    hdr[18:22] = struct.pack("<I", W)
    hdr[22:26] = struct.pack("<i", -H)
    hdr[26:28] = struct.pack("<H", 1)
    hdr[28:30] = struct.pack("<H", 24)
    with open(filename, "wb") as f:
        f.write(hdr)
        f.write(pixels_bgr24)


def open_serial_no_reset(port):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = BAUD
    ser.timeout = 0.25
    ser.write_timeout = 5
    ser.rtscts = False
    ser.dsrdtr = False

    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.setDTR(False)
    ser.setRTS(False)
    return ser


def capture(port, outfile):
    print(f"Opening {port} without reset...")
    ser = open_serial_no_reset(port)

    try:
        time.sleep(0.2)
        ser.reset_input_buffer()

        print("Checking device...")
        ser.write(b"R")
        ready, _ = wait_for_marker(ser, [b"READY"], timeout=2)
        if ready:
            print("Device ready.")
        else:
            print("No READY reply; continuing anyway...")

        ser.reset_input_buffer()
        print("Capturing screen...")

        ser.write(b"S")
        ser.flush()
        marker, _ = wait_for_marker(ser, [b"RGB332:", b"OOM:"], timeout=20)

        if marker is None:
            raise RuntimeError("No screenshot response from device.")

        if marker == b"OOM:":
            info = ser.readline().decode("ascii", errors="replace").strip()
            raise RuntimeError(f"Device ran out of RAM while capturing: {info}")

        start = time.time()
        total = W * H
        data = read_exact(ser, total)
        if len(data) < total:
            raise RuntimeError(f"Transfer stalled at {len(data)}/{total} bytes.")

        elapsed = time.time() - start
        print(f"  Done in {elapsed:.1f}s          ")
    finally:
        ser.close()

    pixels = bytearray(W * H * 3)
    for i, c in enumerate(data):
        r3 = (c >> 5) & 0x07
        g3 = (c >> 2) & 0x07
        b2 = c & 0x03
        r8 = (r3 << 5) | (r3 << 2) | (r3 >> 1)
        g8 = (g3 << 5) | (g3 << 2) | (g3 >> 1)
        b8 = (b2 << 6) | (b2 << 4) | (b2 << 2) | b2
        pixels[i * 3 + 0] = b8
        pixels[i * 3 + 1] = g8
        pixels[i * 3 + 2] = r8

    write_bmp(outfile, pixels)
    print(f"Saved {outfile}")


def main():
    interactive = len(sys.argv) == 1 and sys.stdin.isatty()
    try:
        if interactive:
            port, outfile = ask_interactive()
        else:
            port, outfile = parse_args(sys.argv[1:])
        capture(port, outfile)
    except Exception as exc:
        print(f"Error: {exc}")
        if not interactive:
            sys.exit(1)
    finally:
        if interactive:
            input("Press Enter to close...")


if __name__ == "__main__":
    main()
