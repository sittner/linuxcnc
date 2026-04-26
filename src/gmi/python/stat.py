"""gmi.stat — Drop-in replacement for linuxcnc.stat().

Subscribes to the emcstat WebSocket watch channel and maintains a local
cache of all stat fields. Attribute access (stat.task_mode) reads from
the cache. poll() is a no-op (data is pushed automatically at 50ms).

Usage:
    s = gmi.Stat()
    s.poll()  # no-op, data is already current
    print(s.task_mode, s.task_state)
"""

from __future__ import annotations

import asyncio
import json
import threading
from typing import Any, Optional

try:
    import websockets
except ImportError:
    websockets = None

from gmi import ws_url


class Stat:
    """Drop-in replacement for linuxcnc.stat().

    Connects to the emcstat watch channel and caches the latest StatFull.
    All attributes from the stat struct are accessible as properties.
    """

    def __init__(self):
        self._data = {}
        self._lock = threading.Lock()
        self._connected = threading.Event()
        self._loop = None
        self._thread = None
        self._ws = None
        self._start_watch()

    def _start_watch(self):
        """Start background thread for WebSocket watch."""
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        self._connected.wait(timeout=5)

    def _run(self):
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        self._loop.run_until_complete(self._connect_and_subscribe())
        self._connected.set()
        self._loop.run_forever()
        self._loop.close()

    async def _connect_and_subscribe(self):
        url = ws_url()
        self._ws = await websockets.connect(url)
        # Subscribe to emcstat.get_stat at 50ms
        msg = {
            "action": "subscribe",
            "api": "emcstat",
            "instance": "emcstat",
            "func": "get_stat",
            "rate_ms": 50,
        }
        await self._ws.send(json.dumps(msg))
        asyncio.get_event_loop().create_task(self._recv_loop())

    async def _recv_loop(self):
        try:
            async for raw in self._ws:
                msg = json.loads(raw)
                if msg.get("type") == "update" and msg.get("func") == "get_stat":
                    data = msg.get("data", {})
                    with self._lock:
                        self._data = data
        except asyncio.CancelledError:
            pass
        except Exception:
            pass

    def poll(self):
        """No-op. Data is pushed by the watch channel automatically."""
        pass

    # ─── Flat attribute access (matching linuxcnc.stat() API) ───

    def __getattr__(self, name):
        if name.startswith("_"):
            raise AttributeError(name)

        with self._lock:
            data = self._data

        # Direct top-level fields
        if name in data:
            return data[name]

        # Task fields (s.task_mode → data["task"]["mode"])
        task = data.get("task", {})
        _TASK_MAP = {
            "task_mode": ("mode", 0),
            "task_state": ("state", 0),
            "interp_state": ("interp_state", 0),
            "exec_state": ("exec_state", 0),
            "file": ("file", ""),
            "command": ("command", ""),
            "motion_line": ("motion_line", 0),
            "current_line": ("current_line", 0),
            "read_line": ("read_line", 0),
            "queued_mdi_commands": ("queued_mdi_commands", 0),
            "optional_stop": ("optional_stop", 0),
            "block_delete": ("block_delete", 0),
            "task_paused": ("task_paused", 0),
            "g5x_index": ("g5x_index", 0),
        }
        if name in _TASK_MAP:
            key, default = _TASK_MAP[name]
            return task.get(key, default)

        # Motion fields (s.motion_mode → data["motion"]["mode"])
        motion = data.get("motion", {})
        _MOTION_MAP = {
            "motion_mode": ("mode", 0),
            "enabled": ("enabled", False),
            "inpos": ("in_position", False),
            "paused": ("paused", False),
            "feedrate": ("feedrate", 0.0),
            "rapidrate": ("rapidrate", 0.0),
            "max_velocity": ("max_velocity", 0.0),
            "velocity": ("velocity", 0.0),
            "distance_to_go": ("distance_to_go", 0.0),
            "dtg": ("dtg", 0.0),
            "current_vel": ("current_vel", 0.0),
            "motion_id": ("motion_id", 0),
        }
        if name in _MOTION_MAP:
            key, default = _MOTION_MAP[name]
            return motion.get(key, default)

        # Position fields (return as 9-tuple for linuxcnc.stat() compat)
        _POS_FIELDS = {
            "position", "actual_position", "probed_position",
            "g5x_offset", "g92_offset", "tool_offset",
        }
        if name in _POS_FIELDS:
            pos = data.get(name, {})
            return _pos_to_tuple(pos)

        # joint_actual_position — array of 16 floats
        if name == "joint_actual_position":
            return tuple(data.get("joint_actual_position", [0.0] * 16))

        # joints (count) → data["joints_count"]
        if name == "joints":
            return data.get("joints_count", 0)

        # joint (array of dicts) — data["joints"]
        if name == "joint":
            return tuple(data.get("joints", []))

        # spindle (array of dicts) — data["spindle"]
        if name == "spindle":
            return tuple(data.get("spindle", []))

        # axis (array of dicts) — data["axis"]
        if name == "axis":
            return tuple(data.get("axis", []))

        # gcodes, mcodes, settings
        if name == "gcodes":
            return tuple(data.get("active_gcodes", []))
        if name == "mcodes":
            return tuple(data.get("active_mcodes", []))
        if name == "settings":
            return tuple(data.get("active_settings", []))

        # homed — tuple of booleans per joint
        if name == "homed":
            return tuple(data.get("homed", [False] * 16))

        # limit — tuple of bitmasks per joint
        if name == "limit":
            return tuple(data.get("limit", [0] * 16))

        # Remaining scalars — (json_key, default) so we never return None
        _SCALAR_MAP = {
            "kinematics_type": ("kinematics_type", 0),
            "num_extrajoints": ("num_extrajoints", 0),
            "axis_mask": ("axis_mask", 0),
            "flood": ("flood", 0),
            "mist": ("mist", 0),
            "tool_in_spindle": ("tool_in_spindle", 0),
            "pocket_prepped": ("pocket_prepped", -1),
            "linear_units": ("linear_units", 1.0),
            "state": ("state", 0),
            "rotation_xy": ("rotation_xy", 0.0),
        }
        if name in _SCALAR_MAP:
            key, default = _SCALAR_MAP[name]
            return data.get(key, default)

        raise AttributeError(f"Stat has no attribute {name!r}")

    def stop(self):
        """Stop the background WebSocket thread."""
        if self._loop:
            self._loop.call_soon_threadsafe(self._loop.stop)
        if self._thread:
            self._thread.join(timeout=2)


def _pos_to_tuple(pos):
    """Convert a position dict to a 9-tuple (x, y, z, a, b, c, u, v, w)."""
    if isinstance(pos, dict):
        return (
            pos.get("x", 0.0), pos.get("y", 0.0), pos.get("z", 0.0),
            pos.get("a", 0.0), pos.get("b", 0.0), pos.get("c", 0.0),
            pos.get("u", 0.0), pos.get("v", 0.0), pos.get("w", 0.0),
        )
    return (0.0,) * 9
