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
