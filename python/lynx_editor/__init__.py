"""
lynx_editor : drive the Lynx editor from Python.

The editor runs a local command server (127.0.0.1). This module connects to it
and sends commands (see the "Reference" tab of the Commands window, or
``lynx.help()``).

    import lynx_editor as lynx

    for enemy in lynx.actors(cls="Enemy"):
        enemy.move(0, 2)                      # relative move

    box = lynx.spawn("Crate", location=(4, 10, 0), properties={"health": 3})
    box["health"] = 5                          # property
    box.delete()

    lynx.voxels.fill(0, 0, 40, 2, lynx.voxels.type_id("Grass"))
    lynx.assets.write("scripts/hello.js", "console.log('hi');")

    with lynx.undo_group("Build the level"):   # ONE Ctrl+Z in the editor
        ...

    lynx.save()

Connection : scripts started from the editor are connected automatically
(LYNX_EDITOR_PORT / LYNX_EDITOR_TOKEN). Elsewhere the module reads
<project>/.lynx/editor.json (from the current folder, LYNX_PROJECT, or
connect(project=...)), then the file of the last started editor.
"""

from __future__ import annotations

import base64
import contextlib
import itertools
import json
import os
import pathlib
import socket
import tempfile
from typing import Any, Iterable, Optional, Sequence

__all__ = [
    "LynxError", "ConnectionLost", "Connection", "Actor", "Assets", "Voxels",
    "connect", "connection", "call", "help",
    "info", "save", "undo", "play", "stop", "wait_frames", "select", "camera",
    "screenshot", "log", "compile", "reload_scripts", "eval_js",
    "classes", "actors", "actor", "spawn", "delete", "undo_group", "batch",
]


class LynxError(Exception):
    """Error returned by the editor (unknown actor, bad parameter...) or connection error."""


class ConnectionLost(LynxError):
    """The editor is not running / the connection was closed."""


# =============================================================================
# Connection discovery
# =============================================================================

def _last_editor_file() -> pathlib.Path:
    return pathlib.Path(tempfile.gettempdir()) / "LynxEditor" / "last_editor.json"


def _read_connection_file(path: pathlib.Path) -> Optional[dict]:
    try:
        with open(path, "r", encoding="utf-8") as file:
            data = json.load(file)
        if isinstance(data, dict) and "port" in data and "token" in data:
            return data
    except (OSError, ValueError):
        pass
    return None


def _find_connection(project: Optional[str]) -> Optional[dict]:
    # 1. Explicit project.
    if project:
        return _read_connection_file(pathlib.Path(project) / ".lynx" / "editor.json")

    # 2. Started by the editor.
    port = os.environ.get("LYNX_EDITOR_PORT")
    token = os.environ.get("LYNX_EDITOR_TOKEN")
    if port and token:
        return {"port": int(port), "token": token, "project": os.environ.get("LYNX_PROJECT", "")}

    # 3. LYNX_PROJECT, then the current folder and its parents.
    candidates = []
    if os.environ.get("LYNX_PROJECT"):
        candidates.append(pathlib.Path(os.environ["LYNX_PROJECT"]))
    cwd = pathlib.Path.cwd()
    candidates += [cwd, *cwd.parents]
    for folder in candidates:
        data = _read_connection_file(folder / ".lynx" / "editor.json")
        if data:
            return data

    # 4. The last started editor.
    return _read_connection_file(_last_editor_file())


# =============================================================================
# Connection
# =============================================================================

class Connection:
    """One connection to a running editor. Thread : one at a time."""

    def __init__(self, port: Optional[int] = None, token: Optional[str] = None,
                 project: Optional[str] = None, host: str = "127.0.0.1",
                 timeout: Optional[float] = 600.0):
        if port is None or token is None:
            found = _find_connection(project)
            if not found:
                raise ConnectionLost(
                    "no running Lynx editor found (open the project in the editor ; "
                    "the editor writes <project>/.lynx/editor.json)")
            port = port if port is not None else int(found["port"])
            token = token if token is not None else str(found["token"])
            self.project = found.get("project", "")
        else:
            self.project = project or ""

        self.port = port
        self._ids = itertools.count(1)
        try:
            self._socket = socket.create_connection((host, port), timeout=10.0)
        except OSError as error:
            raise ConnectionLost(
                f"cannot connect to the editor on {host}:{port} ({error}). "
                "Is the editor still running ?") from None
        self._socket.settimeout(timeout)
        self._socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._reader = self._socket.makefile("rb")

        self._request("auth", {"token": token})

        self.assets = Assets(self, "assets")
        self.project_files = Assets(self, "project")
        self.voxels = Voxels(self)

    # --- Low level ------------------------------------------------------------

    def _request(self, command: str, params: Optional[dict]) -> Any:
        request_id = next(self._ids)
        line = json.dumps({"id": request_id, "command": command, "params": params or {}},
                          ensure_ascii=False) + "\n"
        try:
            self._socket.sendall(line.encode("utf-8"))
            while True:
                raw = self._reader.readline()
                if not raw:
                    raise ConnectionLost("the editor closed the connection")
                answer = json.loads(raw.decode("utf-8"))
                if answer.get("id") == request_id:
                    break
        except OSError as error:
            raise ConnectionLost(f"connection to the editor lost ({error})") from None

        if not answer.get("ok"):
            raise LynxError(f"{command} : {answer.get('error')}")
        return answer.get("result")

    def call(self, command: str, params: Optional[dict] = None, **kwargs: Any) -> Any:
        """Runs a command : call("actor.spawn", {"class": "Enemy"}) or call("voxel.get", x=1, y=2)."""
        merged = dict(params or {})
        merged.update({k: v for k, v in kwargs.items() if v is not None})
        return self._request(command, merged)

    def close(self) -> None:
        with contextlib.suppress(Exception):
            self._request("bye", {})
        with contextlib.suppress(Exception):
            self._reader.close()
            self._socket.close()

    def __enter__(self) -> "Connection":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()

    # --- Editor ---------------------------------------------------------------

    def help(self, name: Optional[str] = None) -> Any:
        """Command list (or one command) with their parameters."""
        return self.call("help", name=name)

    def info(self) -> dict:
        return self.call("editor.info")

    def save(self) -> None:
        self.call("editor.save")

    def undo(self, count: int = 1) -> dict:
        return self.call("editor.undo", count=count)

    def play(self) -> None:
        self.call("editor.play")

    def stop(self) -> None:
        self.call("editor.stop")

    def wait_frames(self, count: int = 1) -> None:
        self.call("editor.wait_frames", count=count)

    def select(self, actor: "Actor | str | None") -> None:
        self.call("editor.select", id=_actor_id(actor) if actor is not None else None)

    def camera(self, x: Optional[float] = None, y: Optional[float] = None,
               z: Optional[float] = None, focus: "Actor | str | None" = None) -> dict:
        return self.call("editor.camera", x=x, y=y, z=z,
                         focus=_actor_id(focus) if focus is not None else None)

    def screenshot(self, path: Optional[str] = None, target: str = "viewport",
                   max_size: int = 1024) -> bytes:
        """PNG of the scene ("viewport") or of the editor window ("window"). Saved to `path` if given."""
        result = self.call("editor.screenshot", target=target, max_size=max_size)
        data = base64.b64decode(result["data"])
        if path:
            pathlib.Path(path).parent.mkdir(parents=True, exist_ok=True)
            pathlib.Path(path).write_bytes(data)
        return data

    def log(self, message: str) -> None:
        """Message in the editor console."""
        self.call("editor.log", message=str(message))

    def compile(self, wait: bool = True) -> dict:
        """Compiles the game ; {success, output, reload}."""
        return self.call("editor.compile", wait=wait)

    def reload_scripts(self) -> None:
        self.call("editor.reload_scripts")

    def js_api(self, query: str = "") -> list:
        """Engine JavaScript API entries matching `query` (object / component / function name).

        Each entry : {"owner", "name", "signature", "doc", "function"}. Empty query : the objects.
        """
        return self.call("js.api", query=query)["entries"]

    def eval_js(self, code: str) -> Any:
        """Runs JavaScript in the engine, returns the last expression."""
        return self.call("script.eval", code=code)["result"]

    @contextlib.contextmanager
    def undo_group(self, name: str = "script"):
        """Every change inside the block is ONE Ctrl+Z in the editor."""
        self.call("undo.begin_group", name=name)
        try:
            yield self
        finally:
            self.call("undo.end_group")

    def batch(self, stop_on_error: bool = True) -> "Batch":
        """Groups many commands in one request (fast, one undo step) : see Batch."""
        return Batch(self, stop_on_error)

    # --- Actors -----------------------------------------------------------------

    def classes(self) -> list:
        """Spawnable actor classes."""
        return self.call("level.list_classes")

    def actors(self, cls: Optional[str] = None, id: Optional[str] = None,
               near: Optional[Sequence[float]] = None, radius: Optional[float] = None,
               limit: Optional[int] = None) -> list:
        """Actors of the level (filters : class, id pattern with * ?, near=(x, y) + radius)."""
        result = self.call("level.list_actors", **{
            "class": cls, "id": id,
            "near": list(near) if near is not None else None,
            "radius": radius, "limit": limit,
        })
        return [Actor(self, data) for data in result]

    def actor(self, actor_id: str) -> "Actor":
        """One actor by id (LynxError if it does not exist)."""
        return Actor(self, self.call("actor.get", id=actor_id))

    def find(self, actor_id: str) -> "Optional[Actor]":
        """One actor by id, None if it does not exist."""
        try:
            return self.actor(actor_id)
        except LynxError:
            return None

    def spawn(self, cls: str, location: Optional[Sequence[float]] = None, *,
              id: Optional[str] = None, rotation: Optional[Sequence[float]] = None,
              scale: Optional[Sequence[float]] = None, voxel: Optional[Sequence[int]] = None,
              properties: Optional[dict] = None, select: bool = False) -> "Actor":
        """Creates an actor of class `cls`."""
        result = self.call("actor.spawn", **{
            "class": cls, "id": id, "select": select or None,
            "location": _vec(location), "rotation": _vec(rotation), "scale": _vec(scale),
            "voxel": _vec(voxel), "properties": properties,
        })
        return Actor(self, result)

    def delete(self, *actors: "Actor | str") -> None:
        """Deletes actors (ids or Actor objects)."""
        ids = [_actor_id(a) for a in _flatten(actors)]
        if ids:
            self.call("actor.delete", ids=ids)


# =============================================================================
# Batch
# =============================================================================

class Batch:
    """
    Sends many commands in ONE request (much faster for thousands of changes)
    and makes them ONE undo step :

        with lynx.batch() as b:
            for x in range(100):
                b.call("actor.spawn", {"class": "Coin", "location": [x, 5, 0]})
        print(b.results)
    """

    def __init__(self, connection: Connection, stop_on_error: bool = True):
        self._connection = connection
        self._stop_on_error = stop_on_error
        self._commands: list = []
        self.results: list = []

    def call(self, command: str, params: Optional[dict] = None, **kwargs: Any) -> None:
        merged = dict(params or {})
        merged.update({k: v for k, v in kwargs.items() if v is not None})
        self._commands.append({"command": command, "params": merged})

    def send(self) -> list:
        if not self._commands:
            return []
        result = self._connection.call("batch", commands=self._commands,
                                       stop_on_error=self._stop_on_error)
        self._commands = []
        self.results = result["results"]
        errors = [r["error"] for r in self.results if not r["ok"]]
        if errors:
            raise LynxError("batch : " + "; ".join(str(e) for e in errors))
        return [r.get("result") for r in self.results]

    def __enter__(self) -> "Batch":
        return self

    def __exit__(self, exc_type: Any, *exc: Any) -> None:
        if exc_type is None:
            self.send()


# =============================================================================
# Actor
# =============================================================================

def _vec(value: Any) -> Any:
    if value is None:
        return None
    return [float(v) for v in value]


def _actor_id(actor: Any) -> str:
    return actor.id if isinstance(actor, Actor) else str(actor)


def _flatten(items: Iterable) -> list:
    out = []
    for item in items:
        if isinstance(item, (list, tuple, set)):
            out += _flatten(item)
        else:
            out.append(item)
    return out


class Actor:
    """
    An actor of the level. `location`, `rotation`, `scale` are the values known
    when the object was made (call refresh() to read them again) ; setting them
    moves the actor. Properties : actor["name"] (read from the editor each time).
    """

    def __init__(self, connection: Connection, data: dict):
        self._connection = connection
        self._data = dict(data)

    def __repr__(self) -> str:
        return f"<Actor {self.id} ({self.cls}) at {self.location}>"

    def __eq__(self, other: Any) -> bool:
        return isinstance(other, Actor) and other.id == self.id

    def __hash__(self) -> int:
        return hash(self.id)

    @property
    def id(self) -> str:
        return self._data["id"]

    @property
    def cls(self) -> str:
        return self._data.get("class", "")

    @property
    def location(self) -> tuple:
        return tuple(self._data.get("location", (0, 0, 0)))

    @location.setter
    def location(self, value: Sequence[float]) -> None:
        self.set_transform(location=value)

    @property
    def rotation(self) -> tuple:
        return tuple(self._data.get("rotation", (0, 0, 0)))

    @rotation.setter
    def rotation(self, value: Sequence[float]) -> None:
        self.set_transform(rotation=value)

    @property
    def scale(self) -> tuple:
        return tuple(self._data.get("scale", (1, 1, 1)))

    @scale.setter
    def scale(self, value: Sequence[float]) -> None:
        self.set_transform(scale=value)

    def refresh(self) -> "Actor":
        """Reads everything again from the editor."""
        self._data = self._connection.call("actor.get", id=self.id)
        return self

    def details(self) -> dict:
        """{id, class, location..., properties: {name: {type, value}}, functions: [...]}"""
        return self.refresh()._data

    @property
    def properties(self) -> dict:
        """{name: value} (read now)."""
        return {name: p["value"] for name, p in self.details()["properties"].items()}

    def __getitem__(self, name: str) -> Any:
        properties = self.details()["properties"]
        if name not in properties:
            raise KeyError(f"actor '{self.id}' has no property '{name}'")
        return properties[name]["value"]

    def __setitem__(self, name: str, value: Any) -> None:
        self.set(**{name: value})

    def set(self, **properties: Any) -> dict:
        """Sets properties : actor.set(health=3, speed=2.5)."""
        return self._connection.call("actor.set_property", id=self.id, properties=properties)

    def set_transform(self, location: Optional[Sequence[float]] = None,
                      rotation: Optional[Sequence[float]] = None,
                      scale: Optional[Sequence[float]] = None, relative: bool = False) -> "Actor":
        result = self._connection.call("actor.set_transform", id=self.id, relative=relative or None,
                                       location=_vec(location), rotation=_vec(rotation), scale=_vec(scale))
        self._data.update(result)
        return self

    def move(self, dx: float = 0.0, dy: float = 0.0, dz: float = 0.0) -> "Actor":
        """Relative move."""
        return self.set_transform(location=(dx, dy, dz), relative=True)

    def move_to(self, x: float, y: float, z: Optional[float] = None) -> "Actor":
        return self.set_transform(location=(x, y) if z is None else (x, y, z))

    def call(self, function: str, *args: Any) -> Any:
        """Calls a HFUNCTION / script method : actor.call("Jump")."""
        return self._connection.call("actor.call", id=self.id, function=function,
                                     args=list(args))["result"]

    def rename(self, new_id: str) -> "Actor":
        self._connection.call("actor.rename", id=self.id, new_id=new_id)
        self._data["id"] = new_id
        return self

    def duplicate(self, offset: Optional[Sequence[float]] = None, new_id: Optional[str] = None) -> "Actor":
        result = self._connection.call("actor.duplicate", id=self.id, offset=_vec(offset), new_id=new_id)
        return Actor(self._connection, result)

    def select(self) -> None:
        self._connection.call("editor.select", id=self.id)

    def delete(self) -> None:
        self._connection.call("actor.delete", id=self.id)

    @property
    def exists(self) -> bool:
        return self._connection.find(self.id) is not None


# =============================================================================
# Voxels
# =============================================================================

class Voxels:
    """Voxel world (cells : integers ; type 0 = empty)."""

    def __init__(self, connection: Connection):
        self._connection = connection
        self._types: Optional[list] = None

    def types(self, refresh: bool = False) -> list:
        """[{type, name, color, indestructible, on_destroyed}]"""
        if self._types is None or refresh:
            self._types = self._connection.call("voxel.types")
        return self._types

    def type_id(self, name: str) -> int:
        """Type number from its name in voxels.json (case insensitive)."""
        for voxel_type in self.types():
            if voxel_type["name"].lower() == name.lower():
                return voxel_type["type"]
        for voxel_type in self.types(refresh=True):
            if voxel_type["name"].lower() == name.lower():
                return voxel_type["type"]
        raise LynxError(f"unknown voxel type '{name}' : {[t['name'] for t in self.types()]}")

    def _type(self, value: "int | str") -> int:
        return self.type_id(value) if isinstance(value, str) else int(value)

    def get(self, x: int, y: int) -> int:
        return self._connection.call("voxel.get", x=x, y=y)["type"]

    def set(self, x: int, y: int, voxel_type: "int | str") -> int:
        return self._connection.call("voxel.set", x=x, y=y, type=self._type(voxel_type))["changed"]

    def set_many(self, cells: Iterable[Sequence]) -> int:
        """cells : [(x, y, type), ...] (type : number or name)."""
        voxels = [[int(c[0]), int(c[1]), self._type(c[2])] for c in cells]
        return self._connection.call("voxel.set", voxels=voxels)["changed"]

    def fill(self, x0: int, y0: int, x1: int, y1: int, voxel_type: "int | str",
             outline: bool = False) -> int:
        return self._connection.call("voxel.fill", x0=x0, y0=y0, x1=x1, y1=y1,
                                     type=self._type(voxel_type), outline=outline or None)["changed"]

    def clear(self, x0: int, y0: int, x1: int, y1: int) -> int:
        return self.fill(x0, y0, x1, y1, 0)

    def region(self, x0: int, y0: int, x1: int, y1: int) -> list:
        """rows[i][j] = type of cell (x0 + j, y0 + i)."""
        return self._connection.call("voxel.get_region", x0=x0, y0=y0, x1=x1, y1=y1)["rows"]

    def world_to_cell(self, x: float, y: float) -> tuple:
        r = self._connection.call("voxel.world_to_cell", x=x, y=y)
        return r["x"], r["y"]

    def cell_to_world(self, x: int, y: int) -> tuple:
        r = self._connection.call("voxel.cell_to_world", x=x, y=y)
        return r["x"], r["y"]


# =============================================================================
# Files
# =============================================================================

class Assets:
    """Files of assets/ (root "assets") or of the project (root "project")."""

    def __init__(self, connection: Connection, root: str):
        self._connection = connection
        self._root = root

    def _call(self, command: str, **params: Any) -> Any:
        return self._connection.call(command, root=self._root, **params)

    def list(self, path: str = ".", recursive: bool = False, pattern: Optional[str] = None,
             limit: Optional[int] = None) -> list:
        """[{path, type: file|dir, size, modified}]"""
        return self._call("asset.list", path=path, recursive=recursive or None,
                          pattern=pattern, limit=limit)["entries"]

    def read(self, path: str) -> str:
        return self._call("asset.read", path=path)["content"]

    def read_bytes(self, path: str) -> bytes:
        return base64.b64decode(self._call("asset.read", path=path, encoding="base64")["content"])

    def read_json(self, path: str) -> Any:
        return json.loads(self.read(path))

    def write(self, path: str, text: str, overwrite: bool = True) -> dict:
        return self._call("asset.write", path=path, content=text,
                          overwrite=None if overwrite else False)

    def write_bytes(self, path: str, data: bytes, overwrite: bool = True) -> dict:
        return self._call("asset.write", path=path, encoding="base64",
                          content=base64.b64encode(data).decode("ascii"),
                          overwrite=None if overwrite else False)

    def write_json(self, path: str, value: Any, indent: int = 4) -> dict:
        return self.write(path, json.dumps(value, indent=indent, ensure_ascii=False) + "\n")

    def append(self, path: str, text: str) -> dict:
        return self._call("asset.write", path=path, content=text, append=True)

    def delete(self, path: str) -> dict:
        """Moved to .lynx/trash/ (recoverable)."""
        return self._call("asset.delete", path=path)

    def move(self, source: str, destination: str, overwrite: bool = False) -> dict:
        return self._call("asset.move", **{"from": source, "to": destination,
                                           "overwrite": overwrite or None})

    def mkdir(self, path: str) -> dict:
        return self._call("asset.mkdir", path=path)

    def exists(self, path: str) -> bool:
        return self._call("asset.exists", path=path)["exists"]


# =============================================================================
# Module level : one default connection
# =============================================================================

_default: Optional[Connection] = None


def connect(port: Optional[int] = None, token: Optional[str] = None,
            project: Optional[str] = None) -> Connection:
    """Connects (again) and makes it the default connection of the module functions."""
    global _default
    if _default is not None:
        _default.close()
    _default = Connection(port=port, token=token, project=project)
    return _default


def connection() -> Connection:
    """The default connection (made on first use)."""
    global _default
    if _default is None:
        _default = Connection()
    return _default


def __getattr__(name: str) -> Any:
    # lynx.assets / lynx.project_files / lynx.voxels
    if name in ("assets", "project_files", "voxels"):
        return getattr(connection(), name)
    raise AttributeError(name)


def call(command: str, params: Optional[dict] = None, **kwargs: Any) -> Any:
    return connection().call(command, params, **kwargs)


def help(name: Optional[str] = None) -> Any:  # noqa: A001 - mirrors the command
    return connection().help(name)


def info() -> dict: return connection().info()
def save() -> None: connection().save()
def undo(count: int = 1) -> dict: return connection().undo(count)
def play() -> None: connection().play()
def stop() -> None: connection().stop()
def wait_frames(count: int = 1) -> None: connection().wait_frames(count)
def select(actor: "Actor | str | None") -> None: connection().select(actor)
def log(message: str) -> None: connection().log(message)
def compile(wait: bool = True) -> dict: return connection().compile(wait)  # noqa: A001
def reload_scripts() -> None: connection().reload_scripts()
def eval_js(code: str) -> Any: return connection().eval_js(code)
def js_api(query: str = "") -> list: return connection().js_api(query)
def classes() -> list: return connection().classes()
def actor(actor_id: str) -> Actor: return connection().actor(actor_id)
def find(actor_id: str) -> Optional[Actor]: return connection().find(actor_id)
def delete(*actors: "Actor | str") -> None: connection().delete(*actors)
def batch(stop_on_error: bool = True) -> Batch: return connection().batch(stop_on_error)
def undo_group(name: str = "script"): return connection().undo_group(name)


def camera(x: Optional[float] = None, y: Optional[float] = None, z: Optional[float] = None,
           focus: "Actor | str | None" = None) -> dict:
    return connection().camera(x, y, z, focus)


def screenshot(path: Optional[str] = None, target: str = "viewport", max_size: int = 1024) -> bytes:
    return connection().screenshot(path, target, max_size)


def actors(cls: Optional[str] = None, id: Optional[str] = None,  # noqa: A002
           near: Optional[Sequence[float]] = None, radius: Optional[float] = None,
           limit: Optional[int] = None) -> list:
    return connection().actors(cls, id, near, radius, limit)


def spawn(cls: str, location: Optional[Sequence[float]] = None, **kwargs: Any) -> Actor:
    return connection().spawn(cls, location, **kwargs)
