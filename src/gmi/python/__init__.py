"""GMI client package — generated REST and WebSocket clients for LinuxCNC."""

import os

_DEFAULT_REST_URL = "http://127.0.0.1:5080"
_ENV_VAR = "GMC_REST_URL"

# Version string (matches linuxcnc.version).
version = os.environ.get("LINUXCNCVERSION", "unknown")


def rest_url() -> str:
    """Return the REST base URL (from GMC_REST_URL or default)."""
    return os.environ.get(_ENV_VAR, _DEFAULT_REST_URL).rstrip("/")


def ws_url() -> str:
    """Return the WebSocket watch URL derived from the REST URL."""
    base = rest_url()
    base = base.replace("https://", "wss://").replace("http://", "ws://")
    return base + "/api/v1/watch"


# Re-export wrapper classes for convenience.
# These are lazy-imported to avoid pulling in websockets at module load
# (not all callers need stat/error channels).
def Stat():
    """Create a gmi.Stat instance (drop-in for linuxcnc.stat())."""
    from gmi.stat import Stat as _Stat
    return _Stat()


def Command():
    """Create a gmi.Command instance (drop-in for linuxcnc.command())."""
    from gmi.command import Command as _Command
    return _Command()


def ErrorChannel():
    """Create a gmi.ErrorChannel instance (drop-in for linuxcnc.error_channel())."""
    from gmi.error import ErrorChannel as _ErrorChannel
    return _ErrorChannel()


def positionlogger(stat_unused, c0, c1, c2, c3, c4, c5, geometry, is_xyuv=0):
    """Create a gmi.PositionLogger (drop-in for linuxcnc.positionlogger())."""
    from gmi.positionlogger import PositionLogger
    return PositionLogger(stat_unused, c0, c1, c2, c3, c4, c5, geometry, is_xyuv)


def ToolTable():
    """Create a gmi.ToolTable instance for REST tool table access."""
    from gmi.tools import ToolTable as _ToolTable
    return _ToolTable()


def component_exists(name: str) -> bool:
    """Check if a HAL component exists via the halcmd REST API."""
    import json
    import urllib.request
    url = rest_url() + "/api/v1/halcmd/components?pattern=" + name
    try:
        with urllib.request.urlopen(url, timeout=2) as resp:
            data = json.loads(resp.read())
            return len(data) > 0
    except Exception:
        return False


def pin_has_writer(name: str) -> bool:
    """Check if a HAL pin's signal has any writers via the halcmd REST API."""
    import json
    import urllib.request
    url = rest_url() + "/api/v1/halcmd/pins?pattern=" + name
    try:
        with urllib.request.urlopen(url, timeout=2) as resp:
            data = json.loads(resp.read())
            if data:
                return data[0].get("has_writer", False)
            return False
    except Exception:
        return False


class IniFile:
    """Drop-in replacement for linuxcnc.ini() that fetches values via REST.

    Matches the linuxcnc.ini API:
      - find(section, key) -> str | None
      - findall(section, key) -> list[str]
    """

    def __init__(self):
        self._cache = {}  # (section, key) -> str or None (find)
        self._cache_all = {}  # (section, key) -> list[str] (findall)

    def find(self, section, key):
        """Return the first value for section/key, or None if not found."""
        cache_key = (section, key)
        if cache_key in self._cache:
            return self._cache[cache_key]
        result = self._query([{"section": section, "key": key}])
        if result and len(result) == 1:
            val = result[0].get("value")
            self._cache[cache_key] = val
            return val
        self._cache[cache_key] = None
        return None

    def findall(self, section, key):
        """Return all values for section/key as a list."""
        cache_key = (section, key)
        if cache_key in self._cache_all:
            return self._cache_all[cache_key]
        result = self._query([{"section": section, "key": key, "all": True}])
        if result and len(result) == 1:
            vals = result[0].get("values", [])
            self._cache_all[cache_key] = vals
            return vals
        self._cache_all[cache_key] = []
        return []

    def _query(self, items):
        """Issue a bulk query to the INI REST endpoint."""
        import json
        import urllib.request
        url = rest_url() + "/api/v1/ini/query"
        data = json.dumps(items).encode("utf-8")
        req = urllib.request.Request(
            url, data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())
