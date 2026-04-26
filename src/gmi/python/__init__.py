"""GMI client package — generated REST and WebSocket clients for LinuxCNC."""

import os

_DEFAULT_REST_URL = "http://127.0.0.1:5080"
_ENV_VAR = "GMC_REST_URL"


def rest_url() -> str:
    """Return the REST base URL (from GMC_REST_URL or default)."""
    return os.environ.get(_ENV_VAR, _DEFAULT_REST_URL).rstrip("/")


def ws_url() -> str:
    """Return the WebSocket watch URL derived from the REST URL."""
    base = rest_url()
    base = base.replace("https://", "wss://").replace("http://", "ws://")
    return base + "/api/v1/watch"


def component_exists(name: str) -> bool:
    """Check if a HAL component exists via the halcmd REST API."""
    import json
    import urllib.request
    url = rest_url() + "/api/v1/halcmd0/components?pattern=" + name
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
    url = rest_url() + "/api/v1/halcmd0/pins?pattern=" + name
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
        url = rest_url() + "/api/v1/ini0/query"
        data = json.dumps(items).encode("utf-8")
        req = urllib.request.Request(
            url, data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())
