#!/usr/bin/env python3
"""Serve Pixel Mirror and bridge the M5Stack on USB serial.

The board speaks newline-delimited lines that start with "pm ".
Browser clients subscribe at /m5/events and push changes back with POST /m5.
"""

from __future__ import annotations

import json
import queue
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import serial
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parent
HOST = "127.0.0.1"
PORT = 8765

clients: list[queue.Queue] = []
clients_lock = threading.Lock()
ser_lock = threading.Lock()
ser: serial.Serial | None = None
last: dict[str, str] = {}
state = {"connected": False, "port": None, "error": "starting"}


def find_port() -> str | None:
    candidates = []
    for p in list_ports.comports():
        blob = " ".join(
            str(x or "")
            for x in (p.device, p.description, p.manufacturer, p.product, p.serial_number)
        ).lower()
        if p.vid == 0x0403 and p.pid == 0x6001:
            candidates.append((0 if ("m5" in blob or "hades" in blob) else 1, p.device))
        elif "m5stack" in blob or "m5stack" in blob:
            candidates.append((0, p.device))
    if not candidates:
        return None
    candidates.sort()
    return candidates[0][1]


def broadcast(line: str) -> None:
    if line.startswith("pm set "):
        parts = line.split()
        if len(parts) >= 4:
            last[parts[2]] = line
    with clients_lock:
        for q in clients:
            q.put(line)


FRAME_BYTES = 16 * 12


def write_serial(data: bytes) -> None:
    if not data.endswith(b"\n"):
        data += b"\n"
    with ser_lock:
        port = ser
        if port is None or not port.is_open:
            return
        port.write(data)
        port.flush()


def write_frame(data: bytes) -> None:
    if len(data) != FRAME_BYTES:
        return
    with ser_lock:
        port = ser
        if port is None or not port.is_open:
            return
        port.write(b"pm px\n")
        port.write(data)
        port.flush()


def open_serial() -> serial.Serial | None:
    port_name = find_port()
    if not port_name:
        state.update(connected=False, port=None, error="M5Stack not found on USB")
        return None
    port = serial.Serial()
    port.port = port_name
    port.baudrate = 115200
    port.timeout = 0.2
    port.dsrdtr = False
    port.rtscts = False
    port.dtr = False
    port.rts = False
    port.open()
    state.update(connected=True, port=port_name, error=None)
    print(f"m5 on {port_name}")
    return port


def reader() -> None:
    global ser
    buf = b""
    while True:
        with ser_lock:
            port = ser
        if port is None or not port.is_open:
            try:
                port = open_serial()
            except serial.SerialException as exc:
                state.update(connected=False, error=str(exc))
                time.sleep(2)
                continue
            if port is None:
                time.sleep(2)
                continue
            with ser_lock:
                ser = port
            buf = b""
            time.sleep(0.4)
            write_serial(b"pm dump\n")
        try:
            chunk = port.read(port.in_waiting or 1)
        except (serial.SerialException, AttributeError) as exc:
            print(f"m5 dropped: {exc}")
            state.update(connected=False, error=str(exc))
            with ser_lock:
                try:
                    port.close()
                except serial.SerialException:
                    pass
                ser = None
            time.sleep(1)
            continue
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("pm "):
                continue
            print(f"m5 << {line}")
            broadcast(line)


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_GET(self) -> None:
        path = self.path.split("?", 1)[0]
        if path == "/stage":
            self.path = "/stage.html"
        if path == "/m5/status":
            body = json.dumps(state).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path == "/m5/events":
            self._events()
            return
        super().do_GET()

    def do_POST(self) -> None:
        path = self.path.split("?", 1)[0]
        length = int(self.headers.get("Content-Length", "0") or 0)
        body = self.rfile.read(length)
        if path == "/m5/frame":
            write_frame(body)
            self.send_response(204)
            self.end_headers()
            return
        if path != "/m5":
            self.send_error(404)
            return
        text = body.decode("utf-8", "replace").strip()
        if text.startswith("pm "):
            print(f"m5 >> {text}")
            write_serial(body)
        self.send_response(204)
        self.end_headers()

    def _events(self) -> None:
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        q: queue.Queue = queue.Queue()
        with clients_lock:
            clients.append(q)
            snapshot = list(last.values())
        try:
            for line in snapshot:
                self.wfile.write(f"data: {line}\n\n".encode())
            self.wfile.flush()
            write_serial(b"pm dump\n")
            while True:
                try:
                    line = q.get(timeout=15)
                except queue.Empty:
                    self.wfile.write(b": ping\n\n")
                    self.wfile.flush()
                    continue
                self.wfile.write(f"data: {line}\n\n".encode())
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            pass
        finally:
            with clients_lock:
                if q in clients:
                    clients.remove(q)

    def log_message(self, fmt: str, *args) -> None:
        if args and isinstance(args[0], str) and args[0].startswith("GET /m5/events"):
            return
        super().log_message(fmt, *args)


def main() -> None:
    threading.Thread(target=reader, daemon=True).start()
    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"open http://{HOST}:{PORT}/")
    httpd.serve_forever()


if __name__ == "__main__":
    main()
