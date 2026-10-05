"""Minimal client of the Lynx editor command server (no dependency).

Protocol : one JSON object per line over TCP 127.0.0.1:<port>.
    -> {"id": 1, "command": "auth", "params": {"token": "..."}}
    <- {"id": 1, "ok": true, "result": {...}}
The port and the token are in <project>/.lynx/editor.json, or in
<temp>/LynxEditor/last_editor.json for the editor started last.
"""

import json
import os
import socket
import tempfile


class CommandError(Exception):
    """The editor answered ok = false."""


def find_connection(project=None):
    """Returns the connection info dict (port, token, project...)."""
    candidates = []
    if project:
        candidates.append(os.path.join(project, ".lynx", "editor.json"))
    candidates.append(os.path.join(tempfile.gettempdir(), "LynxEditor", "last_editor.json"))

    for path in candidates:
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8") as f:
                return json.load(f)

    raise RuntimeError(
        "No running Lynx editor found (looked for: %s). Open the project in the editor first."
        % ", ".join(candidates))


class LynxClient:
    def __init__(self, project=None, timeout=120.0):
        info = find_connection(project)
        self.info = info
        self.sock = socket.create_connection(("127.0.0.1", int(info["port"])), timeout=timeout)
        self.buffer = b""
        self.next_id = 1
        self.call("auth", token=info["token"])

    def close(self):
        try:
            self.call("bye")
        except Exception:
            pass
        self.sock.close()

    def _read_line(self):
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(1 << 16)
            if not chunk:
                raise ConnectionError("the editor closed the connection")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line

    def call(self, command, params=None, **kwargs):
        """Runs an editor command. Raises CommandError on failure."""
        if params is None:
            params = {}
        params = dict(params, **kwargs)
        request_id = self.next_id
        self.next_id += 1
        payload = json.dumps({"id": request_id, "command": command, "params": params}, ensure_ascii=False)
        self.sock.sendall(payload.encode("utf-8") + b"\n")

        while True:
            message = json.loads(self._read_line().decode("utf-8", errors="replace"))
            if message.get("id") != request_id:
                continue   # an answer to something else (should not happen)
            if not message.get("ok"):
                raise CommandError("%s : %s" % (command, message.get("error")))
            return message.get("result")

    def try_call(self, command, params=None, **kwargs):
        """Like call(), returns None instead of raising."""
        try:
            return self.call(command, params, **kwargs)
        except CommandError:
            return None
