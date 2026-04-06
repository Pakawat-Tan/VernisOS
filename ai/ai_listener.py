#!/usr/bin/env python3
"""
ai_listener.py — VernisOS AI Engine Main Listener (Phase 9)

Brings together:
  - corelib        : VernisConnection + KernelLogger + EngineContext
  - policy_manager : PolicyManager (access control)
  - modules        : AIModule dispatcher

QEMU setup — add these flags when running VernisOS:

  TCP (recommended for development):
    -chardev socket,id=com2,host=localhost,port=4444,server=off
    -device isa-serial,chardev=com2

  Unix socket:
    -chardev socket,id=com2,path=/tmp/vernisai.sock,server=off
    -device isa-serial,chardev=com2

Usage:
    # Start listener first, then start QEMU
    python3 ai/ai_listener.py [--port 4444]
    python3 ai/ai_listener.py --unix /tmp/vernisai.sock
    python3 ai/ai_listener.py --priv root      # default privilege level
"""

from __future__ import annotations

import argparse
import sys
import os

# Allow running from project root: python3 ai/ai_listener.py
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ai.corelib import (
    VernisConnection, KernelLogger, EngineContext,
    MessageFrame, MSG_REQ, MSG_EVT, LogLevel,
)
from ai.policy_manager import PolicyManager, PrivilegeLevel
from ai.modules import SystemMonitorModule, HelpModule, BehaviorMonitorModule
from ai.modules.auto_tuner_module import AutoTunerModule
from ai.config_loader import load_config, apply_config


# =============================================================================
# AI Engine Dispatcher
# =============================================================================

class AIEngine:
    """
    Dispatches incoming kernel messages to the appropriate AIModule,
    enforcing PolicyManager rules before processing.
    """

    def __init__(self, ctx: EngineContext, policy: PolicyManager,
                 caller_priv: PrivilegeLevel = PrivilegeLevel.ROOT,
                 config_path: str | None = None) -> None:
        self.ctx         = ctx
        self.policy      = policy
        self.caller_priv = caller_priv

        # Load anomaly rules from config (or use defaults)
        config = {"rules_path": config_path} if config_path else {}

        # Register modules sorted by priority (lowest number = first)
        self._modules = sorted(
            [
                BehaviorMonitorModule(ctx, config=config),  # priority=5  — Phase 10
                AutoTunerModule(ctx),           # priority=8  — Phase 11
                SystemMonitorModule(ctx),       # priority=10 — Phase 9
                HelpModule(ctx),                # priority=200
            ],
            key=lambda m: m.priority,
        )

    def handle_request(self, frame: MessageFrame) -> str:
        """Process a REQ frame and return a response payload."""
        query = frame.payload.strip()
        self.ctx.query_count += 1

        # Policy check
        topic = query.split()[0] if query else ""
        decision = self.policy.evaluate(topic, self.caller_priv)
        if not decision.allowed:
            self.ctx.logger.warning(
                "engine", f"Policy DENY seq={frame.seq}: {decision.reason}"
            )
            return f"[DENIED] {decision.reason}"

        # Dispatch to first matching module
        for module in self._modules:
            if module.can_handle(query):
                self.ctx.logger.debug(
                    "engine", f"seq={frame.seq} → {module.name}"
                )
                return module.process(query)

        # Fallback: echo with acknowledgment
        return f"Query received: '{query}'. No module matched — expand engine in Phase 10."

    def handle_event(self, frame: MessageFrame) -> None:
        """Process an EVT frame (fire-and-forget)."""
        self.ctx.event_count += 1
        parts = frame.payload.split("|", 1)
        event_type = parts[0]
        data       = parts[1] if len(parts) > 1 else ""

        self.ctx.logger.info("event", f"EVT/{event_type}: {data}")

        for module in self._modules:
            module.on_event(event_type, data)


# =============================================================================
# Main Session — reads lines, dispatches, sends responses
# =============================================================================

def run_session(conn: VernisConnection, engine: AIEngine) -> None:
    logger = engine.ctx.logger
    logger.info("session", "Session started — waiting for kernel messages.")

    for raw_line in conn.recv_lines():
        frame = MessageFrame.parse(raw_line)
        if frame is None:
            logger.warning("session", f"Unparseable line: {raw_line!r}")
            continue

        if frame.msg_type == MSG_REQ:
            logger.info("session", f"← REQ  seq={frame.seq}: {frame.payload[:80]}")
            response = engine.handle_request(frame)
            conn.send_response(frame.seq, response)
            logger.info("session", f"→ RESP seq={frame.seq}: {response[:80]}")

        elif frame.msg_type == MSG_EVT:
            engine.handle_event(frame)

        else:
            logger.warning("session", f"Unknown message type '{frame.msg_type}'")

    logger.info("session", "Session ended — kernel disconnected.")


# =============================================================================
# Entry Point
# =============================================================================

def main() -> None:
    parser = argparse.ArgumentParser(
        description="VernisOS AI Engine Listener (Phase 9)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--host",  default="localhost",
                        help="TCP host to listen on (default: localhost)")
    parser.add_argument("--port",  type=int, default=4444,
                        help="TCP port to listen on (default: 4444)")
    parser.add_argument("--unix",  default=None,
                        help="Unix socket path (overrides --host/--port)")
    parser.add_argument("--priv",  default="root",
                        choices=["root", "admin", "user"],
                        help="Privilege level to assume for kernel sessions (default: root)")
    parser.add_argument("--log-level", default="info",
                        choices=["debug", "info", "warning", "error"],
                        help="Minimum log level (default: info)")
    parser.add_argument("--config", default=None,
                        help="Path to anomaly_rules.yaml (default: ai/config/anomaly_rules.yaml)")
    args = parser.parse_args()

    # --- Setup ---
    log_level = LogLevel[args.log_level.upper()]
    priv_map  = {"root": PrivilegeLevel.ROOT,
                 "admin": PrivilegeLevel.ADMIN,
                 "user":  PrivilegeLevel.USER}
    caller_priv = priv_map[args.priv]

    logger = KernelLogger(min_level=log_level)
    policy = PolicyManager()

    logger.info("main", "VernisOS AI Engine starting (Phase 10)")
    logger.info("main", f"Privilege: {caller_priv.name}  Log level: {log_level.name}")
    logger.info("main", "Active policy rules:")
    for line in policy.describe():
        logger.info("main", line)

    # --- Listen loop (reconnects on kernel restart) ---
    while True:
        try:
            if args.unix:
                conn = VernisConnection.unix(args.unix, logger)
            else:
                conn = VernisConnection.tcp(args.host, args.port, logger)

            ctx    = EngineContext(conn, logger)
            engine = AIEngine(ctx, policy, caller_priv, config_path=args.config)

            run_session(conn, engine)
            conn.close()
            logger.info("main", "Waiting for next kernel connection...")

        except KeyboardInterrupt:
            logger.info("main", "Shutting down AI engine.")
            break
        except OSError as e:
            logger.error("main", f"Connection error: {e}")
            import time
            time.sleep(1)


if __name__ == "__main__":
    main()
