"""
corelib.py — VernisOS AI Engine Core Library (Phase 9)

Provides:
  - VernisConnection   : socket connection to kernel bridge
  - MessageFrame       : parsed protocol message
  - KernelLogger       : event log with history
  - EngineContext      : shared runtime context for all modules
"""

from __future__ import annotations

import socket
import threading
import time
import os
from dataclasses import dataclass, field
from datetime import datetime
from enum import IntEnum
from typing import Callable, Optional


# =============================================================================
# Protocol
# =============================================================================

PROTOCOL_SEP   = "|"
PROTOCOL_EOL   = "\n"
MSG_REQ        = "REQ"
MSG_RESP       = "RESP"
MSG_EVT        = "EVT"
MSG_CMD        = "CMD"


@dataclass
class MessageFrame:
    """A single decoded message from the kernel bridge."""
    msg_type: str       # REQ | RESP | EVT | CMD
    seq:      int       # sequence number (0 for EVT)
    payload:  str       # main content
    raw:      str = ""  # original raw line

    @staticmethod
    def parse(line: str) -> Optional["MessageFrame"]:
        """Parse 'TYPE|seq|payload' into a MessageFrame. Returns None on error."""
        parts = line.split(PROTOCOL_SEP, 2)
        if len(parts) < 3:
            return None
        msg_type, seq_str, payload = parts
        try:
            seq = int(seq_str)
        except ValueError:
            seq = 0
        return MessageFrame(msg_type=msg_type, seq=seq, payload=payload, raw=line)

    def encode(self) -> str:
        """Encode back to wire format."""
        return f"{self.msg_type}{PROTOCOL_SEP}{self.seq}{PROTOCOL_SEP}{self.payload}{PROTOCOL_EOL}"


# =============================================================================
# Logger
# =============================================================================

class LogLevel(IntEnum):
    DEBUG   = 0
    INFO    = 1
    WARNING = 2
    ERROR   = 3


@dataclass
class LogEntry:
    timestamp: datetime
    level:     LogLevel
    source:    str
    message:   str

    def __str__(self) -> str:
        ts  = self.timestamp.strftime("%H:%M:%S.%f")[:-3]
        lvl = self.level.name[:4]
        return f"[{ts}] {lvl:4s} [{self.source}] {self.message}"


class KernelLogger:
    """Thread-safe logger that keeps an in-memory history of kernel events."""

    MAX_HISTORY = 500

    def __init__(self, min_level: LogLevel = LogLevel.DEBUG) -> None:
        self._history: list[LogEntry] = []
        self._lock     = threading.Lock()
        self.min_level = min_level

    def _write(self, level: LogLevel, source: str, msg: str) -> None:
        if level < self.min_level:
            return
        entry = LogEntry(datetime.now(), level, source, msg)
        with self._lock:
            self._history.append(entry)
            if len(self._history) > self.MAX_HISTORY:
                self._history.pop(0)
        print(str(entry), flush=True)

    def debug(self,   src: str, msg: str) -> None: self._write(LogLevel.DEBUG,   src, msg)
    def info(self,    src: str, msg: str) -> None: self._write(LogLevel.INFO,    src, msg)
    def warning(self, src: str, msg: str) -> None: self._write(LogLevel.WARNING, src, msg)
    def error(self,   src: str, msg: str) -> None: self._write(LogLevel.ERROR,   src, msg)

    def history(self, last_n: int = 50) -> list[LogEntry]:
        with self._lock:
            return list(self._history[-last_n:])

    def events_by_source(self, source: str) -> list[LogEntry]:
        with self._lock:
            return [e for e in self._history if e.source == source]


# =============================================================================
# Connection
# =============================================================================

class ConnectionState(IntEnum):
    DISCONNECTED = 0
    CONNECTING   = 1
    CONNECTED    = 2
    ERROR        = 3


class VernisConnection:
    """
    Manages the socket connection to the QEMU-mapped COM2 port.

    Supports:
      - TCP:  VernisConnection.tcp("localhost", 4444)
      - Unix: VernisConnection.unix("/tmp/vernisai.sock")
    """

    RECV_BUF = 1024

    def __init__(self, logger: KernelLogger) -> None:
        self._sock:   Optional[socket.socket] = None
        self._buf:    bytes  = b""
        self._state:  ConnectionState = ConnectionState.DISCONNECTED
        self._lock    = threading.Lock()
        self.logger   = logger
        self._on_frame: Optional[Callable[[MessageFrame], None]] = None

    # ---- Factory methods ----

    @classmethod
    def tcp(cls, host: str, port: int, logger: KernelLogger) -> "VernisConnection":
        c = cls(logger)
        c._connect_tcp(host, port)
        return c

    @classmethod
    def unix(cls, path: str, logger: KernelLogger) -> "VernisConnection":
        c = cls(logger)
        c._connect_unix(path)
        return c

    # ---- Connection ----

    def _connect_tcp(self, host: str, port: int) -> None:
        self._state = ConnectionState.CONNECTING
        self.logger.info("conn", f"Connecting TCP {host}:{port} ...")
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind((host, port))
            s.listen(1)
            self.logger.info("conn", f"Waiting for kernel on {host}:{port} ...")
            conn, addr = s.accept()
            self._sock  = conn
            self._state = ConnectionState.CONNECTED
            self.logger.info("conn", f"Kernel connected from {addr}")
        except OSError as e:
            self._state = ConnectionState.ERROR
            self.logger.error("conn", f"TCP error: {e}")
            raise

    def _connect_unix(self, path: str) -> None:
        self._state = ConnectionState.CONNECTING
        if os.path.exists(path):
            os.remove(path)
        self.logger.info("conn", f"Waiting on Unix socket: {path} ...")
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.bind(path)
            s.listen(1)
            conn, _ = s.accept()
            self._sock  = conn
            self._state = ConnectionState.CONNECTED
            self.logger.info("conn", "Kernel connected via Unix socket.")
        except OSError as e:
            self._state = ConnectionState.ERROR
            self.logger.error("conn", f"Unix error: {e}")
            raise

    def close(self) -> None:
        if self._sock:
            self._sock.close()
            self._sock  = None
        self._state = ConnectionState.DISCONNECTED

    @property
    def connected(self) -> bool:
        return self._state == ConnectionState.CONNECTED

    # ---- Send ----

    def send_frame(self, frame: MessageFrame) -> bool:
        if not self._sock:
            return False
        try:
            with self._lock:
                self._sock.sendall(frame.encode().encode("ascii", errors="replace"))
            return True
        except OSError as e:
            self.logger.error("conn", f"Send error: {e}")
            self._state = ConnectionState.ERROR
            return False

    def send_response(self, seq: int, payload: str) -> bool:
        return self.send_frame(MessageFrame(MSG_RESP, seq, payload))

    # ---- Receive (blocking iterator) ----

    def recv_lines(self):
        """Generator: yields decoded lines as they arrive from the kernel."""
        if not self._sock:
            return
        while self.connected:
            try:
                chunk = self._sock.recv(self.RECV_BUF)
                if not chunk:
                    break
                self._buf += chunk
                while b"\n" in self._buf:
                    line_b, self._buf = self._buf.split(b"\n", 1)
                    line = line_b.decode("ascii", errors="replace").strip()
                    if line:
                        yield line
            except OSError:
                break
        self._state = ConnectionState.DISCONNECTED


# =============================================================================
# Engine Context  (shared state passed to all modules)
# =============================================================================

class EngineContext:
    """
    Shared runtime context for all AI modules.
    Holds the connection, logger, and kernel stats.
    """

    def __init__(self, conn: VernisConnection, logger: KernelLogger) -> None:
        self.conn         = conn
        self.logger       = logger
        self.start_time   = datetime.now()
        self.query_count  = 0
        self.event_count  = 0
        self._stats: dict = {}

    def uptime_seconds(self) -> int:
        return int((datetime.now() - self.start_time).total_seconds())

    def record_stat(self, key: str, value) -> None:
        self._stats[key] = value

    def get_stat(self, key: str, default=None):
        return self._stats.get(key, default)

    def all_stats(self) -> dict:
        return dict(self._stats)
