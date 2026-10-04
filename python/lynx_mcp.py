#!/usr/bin/env python3
"""
MCP server for the Lynx editor : lets an AI model (Claude Desktop, Claude Code,
any MCP client) act on the running editor. Every editor command becomes a tool
(actor.spawn -> actor_spawn, voxel.fill -> voxel_fill...).

Configuration (Claude Desktop, claude_desktop_config.json) :

    {
      "mcpServers": {
        "lynx": {
          "command": "python",
          "args": ["C:/.../LynxEditor/python/lynx_mcp.py"]
        }
      }
    }

Claude Code :  claude mcp add lynx -- python C:/.../python/lynx_mcp.py

Options : --project <folder>   connect to the editor of this project (default :
                               the editor started last)

Only the Python standard library is used. Protocol : JSON-RPC 2.0, one message
per line on stdin / stdout (MCP stdio transport). Logs go to stderr.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys
import tempfile
import threading
import traceback
from typing import Any, Optional

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import lynx_editor  # noqa: E402

SERVER_NAME = "lynx-editor"
SERVER_VERSION = "1.0.0"
PROTOCOL_VERSIONS = ["2025-06-18", "2025-03-26", "2024-11-05"]

INSTRUCTIONS = """\
Tools to act on the Lynx game engine editor (a 2D voxel game editor) while it runs.

Start with editor_info, level_list_classes, voxel_types and level_list_actors to see
the project. actor_get shows the properties (with their types) and functions of an actor.

- Positions of actors are world units (location [x, y, z], z usually 0). Voxels use
  integer cells ; voxel_cell_to_world / voxel_world_to_cell convert. Voxel type 0 = empty.
- Every change can be undone (editor_undo). Use batch (one undo step) for many changes.
- Changes are NOT saved to disk until editor_save.
- asset_* tools read / write the files of assets/ (root "project" : the whole project,
  e.g. the C++ sources in src/ ; after editing C++ call editor_compile to rebuild and
  reload the game, it returns the compiler errors). Replaced files are kept in .lynx/trash/.
- script_eval runs JavaScript inside the engine.
- editor_screenshot returns an image of the scene : use it to check your work.
- editor_play / editor_stop run the game ; the level comes back to its previous state at Stop.
"""

# --------------------------------------------------------------------------
# Logging (stderr only : stdout is the protocol)
# --------------------------------------------------------------------------


def log(*args: Any) -> None:
    print("[lynx-mcp]", *args, file=sys.stderr, flush=True)


# --------------------------------------------------------------------------
# Editor connection
# --------------------------------------------------------------------------

class Editor:
    def __init__(self, project: Optional[str]):
        self.project = project
        self.connection: Optional[lynx_editor.Connection] = None
        self.lock = threading.Lock()

    def connect(self) -> lynx_editor.Connection:
        if self.connection is None:
            self.connection = lynx_editor.Connection(project=self.project)
            log("connected to the editor on port", self.connection.port,
                "-", self.connection.project)
        return self.connection

    def call(self, command: str, params: dict) -> Any:
        with self.lock:
            for attempt in range(2):
                try:
                    return self.connect().call(command, params)
                except lynx_editor.ConnectionLost:
                    # Editor restarted (new port / token) : once more.
                    self.connection = None
                    if attempt == 1:
                        raise


# --------------------------------------------------------------------------
# Tools
# --------------------------------------------------------------------------

TYPE_SCHEMAS = {
    "string": {"type": "string"},
    "integer": {"type": "integer"},
    "number": {"type": "number"},
    "boolean": {"type": "boolean"},
    "vec2": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2},
    "vec3": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 3},
    "array": {"type": "array"},
    "object": {"type": "object"},
    "any": {},
}

CACHE_FILE = pathlib.Path(tempfile.gettempdir()) / "LynxEditor" / "mcp_tools_cache.json"


def tool_name(command: str) -> str:
    return command.replace(".", "_")


def make_tool(command: dict) -> dict:
    properties = {}
    required = []

    for param in command.get("params", []):
        schema = dict(TYPE_SCHEMAS.get(param.get("type", "any"), {}))
        if param.get("description"):
            schema["description"] = param["description"]
        properties[param["name"]] = schema
        if param.get("required"):
            required.append(param["name"])

    if command["name"] == "batch":
        properties["commands"] = {
            "type": "array",
            "description": "List of {\"command\": \"actor.spawn\", \"params\": {...}} "
                           "(command names with dots, as in the editor).",
            "items": {
                "type": "object",
                "properties": {"command": {"type": "string"}, "params": {"type": "object"}},
                "required": ["command"],
            },
        }

    schema = {"type": "object", "properties": properties}
    if required:
        schema["required"] = required

    return {
        "name": tool_name(command["name"]),
        "description": command.get("description", ""),
        "inputSchema": schema,
    }


STATUS_TOOL = {
    "name": "editor_connection",
    "description": "Tells whether the Lynx editor is running and reachable (and reconnects).",
    "inputSchema": {"type": "object", "properties": {}},
}


class Tools:
    def __init__(self, editor: Editor):
        self.editor = editor
        self.commands: dict = {}      # tool name -> command name
        self.tools: list = []

    def load(self) -> bool:
        """Command list from the editor (or the cache of the last run). True if it changed."""
        previous = [t["name"] for t in self.tools]
        commands = None

        try:
            commands = self.editor.call("help", {})
            CACHE_FILE.parent.mkdir(parents=True, exist_ok=True)
            CACHE_FILE.write_text(json.dumps(commands), encoding="utf-8")
        except lynx_editor.LynxError as error:
            log("editor not reachable :", error)
            try:
                commands = json.loads(CACHE_FILE.read_text(encoding="utf-8"))
                log("using the cached command list")
            except (OSError, ValueError):
                commands = []

        self.commands = {}
        self.tools = [STATUS_TOOL]

        for command in commands:
            if command["name"] in ("undo.begin_group", "undo.end_group"):
                continue   # a group must not stay open between tool calls : use batch
            tool = make_tool(command)
            self.commands[tool["name"]] = command["name"]
            self.tools.append(tool)

        return previous != [t["name"] for t in self.tools]

    def call(self, name: str, arguments: dict) -> dict:
        if name == STATUS_TOOL["name"]:
            try:
                info = self.editor.call("editor.info", {})
                return text_result(json.dumps({"connected": True, **info}, indent=1))
            except lynx_editor.LynxError as error:
                return text_result(f"Editor not reachable : {error}", error=True)

        command = self.commands.get(name)

        if command is None:
            # Maybe a new command (editor updated) : reload once.
            self.load()
            command = self.commands.get(name, name.replace("_", ".", 1))

        try:
            result = self.editor.call(command, arguments or {})
        except lynx_editor.LynxError as error:
            return text_result(str(error), error=True)

        if command == "editor.screenshot" and isinstance(result, dict) and "data" in result:
            data = result.pop("data")
            return {
                "content": [
                    {"type": "image", "data": data, "mimeType": "image/png"},
                    {"type": "text", "text": json.dumps(result)},
                ],
                "isError": False,
            }

        text = json.dumps(result, indent=1, ensure_ascii=False)

        if len(text) > 200_000:
            text = text[:200_000] + f"\n... (truncated, {len(text)} characters)"

        return text_result(text)


def text_result(text: str, error: bool = False) -> dict:
    return {"content": [{"type": "text", "text": text}], "isError": error}


# --------------------------------------------------------------------------
# JSON-RPC over stdio
# --------------------------------------------------------------------------

class Server:
    def __init__(self, project: Optional[str]):
        self.editor = Editor(project)
        self.tools = Tools(self.editor)
        self.loaded = False
        self.out = sys.stdout
        self.out_lock = threading.Lock()

    def send(self, message: dict) -> None:
        with self.out_lock:
            self.out.write(json.dumps(message, ensure_ascii=False) + "\n")
            self.out.flush()

    def reply(self, request_id: Any, result: Any) -> None:
        self.send({"jsonrpc": "2.0", "id": request_id, "result": result})

    def error(self, request_id: Any, code: int, message: str) -> None:
        self.send({"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}})

    def handle(self, message: dict) -> None:
        method = message.get("method")
        request_id = message.get("id")
        params = message.get("params") or {}
        is_request = "id" in message

        if method == "initialize":
            wanted = params.get("protocolVersion", PROTOCOL_VERSIONS[0])
            version = wanted if wanted in PROTOCOL_VERSIONS else PROTOCOL_VERSIONS[0]
            self.reply(request_id, {
                "protocolVersion": version,
                "capabilities": {"tools": {"listChanged": True}},
                "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
                "instructions": INSTRUCTIONS,
            })
            return

        if method == "ping":
            self.reply(request_id, {})
            return

        if method == "tools/list":
            if not self.loaded:
                self.tools.load()
                self.loaded = True
            self.reply(request_id, {"tools": self.tools.tools})
            return

        if method == "tools/call":
            name = params.get("name", "")
            arguments = params.get("arguments") or {}

            if not self.loaded:
                self.tools.load()
                self.loaded = True

            result = self.tools.call(name, arguments)
            self.reply(request_id, result)

            # The editor was not running at tools/list time : tell the client
            # the real list is available now.
            if len(self.tools.tools) <= 1 and not result.get("isError"):
                if self.tools.load():
                    self.send({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"})
            return

        if method in ("resources/list",):
            self.reply(request_id, {"resources": []})
            return

        if method in ("prompts/list",):
            self.reply(request_id, {"prompts": []})
            return

        if not is_request:
            return   # notifications (initialized, cancelled...)

        self.error(request_id, -32601, f"method not found : {method}")

    def run(self) -> None:
        log("started (pid", os.getpid(), ")")

        for raw in sys.stdin:
            raw = raw.strip()

            if not raw:
                continue

            try:
                message = json.loads(raw)
            except ValueError:
                self.error(None, -32700, "parse error")
                continue

            messages = message if isinstance(message, list) else [message]

            for item in messages:
                try:
                    self.handle(item)
                except Exception as error:  # never die on one request
                    log("error :", error)
                    traceback.print_exc(file=sys.stderr)
                    if isinstance(item, dict) and "id" in item:
                        self.error(item.get("id"), -32603, f"internal error : {error}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--project", help="project folder (default : the editor started last)")
    args = parser.parse_args()

    # Windows : no CRLF / ANSI code page on the protocol stream.
    try:
        sys.stdin.reconfigure(encoding="utf-8")
        sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    except AttributeError:
        pass

    Server(args.project).run()


if __name__ == "__main__":
    main()
