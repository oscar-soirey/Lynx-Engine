# -*- coding: utf-8 -*-
"""Lynxie evaluation : plays French requests against the open Lynx editor and
scores the result.

    python run_eval.py --gold                       # checks the cases themselves (no model)
    python run_eval.py --model qwen3-coder:30b      # scores a model (Ollama)
    python run_eval.py --model my-ft --api openai --base-url http://localhost:8000/v1

For each case : the scene is built (undoable), the editor gives the EXACT
prompt Lynxie would send (command ai.context), the model answers, its script
runs like in the editor (with the same auto-fix loop : error -> undo -> the
model corrects), then the checks look at the scene. Everything is undone at
the end of the case. Run it on a test project, with the level saved.
"""

import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from lynx_client import CommandError, LynxClient  # noqa: E402
from cases import CASES, SCENES  # noqa: E402

EVAL_FOLDER = "_lynxie_eval"
EVAL_CLASSES_FILE = EVAL_FOLDER + "/EvalActors.js"
EVAL_CLASSES = ["EvalHero", "EvalEnemy", "EvalCoin", "EvalTorch", "EvalDoor", "EvalCrate", "EvalPlatform"]
SCENE_CLEAR_RECT = [0, 0, 24, 14]   # cleared before each scene


# -----------------------------------------------------------------------------
# Small helpers
# -----------------------------------------------------------------------------

def extract_python(answer):
    """Same rule as the editor : first ```python block, else first ``` block."""
    match = re.search(r"```python[^\n]*\n(.*?)```", answer, re.S)
    if not match:
        match = re.search(r"```[^\n]*\n(.*?)```", answer, re.S)
    return match.group(1) if match else ""


def looks_like_python(code):
    for line in code.splitlines():
        line = line.lstrip()
        if line.startswith(("//", "let ", "const ", "function ", "var ")):
            return False
    return True


def strip_code(answer):
    return re.sub(r"```.*?(```|$)", "", answer, flags=re.S)


def tail(text, max_lines=60, max_chars=6000):
    lines = [l for l in text.splitlines() if l.strip()]
    out = "\n".join(lines[-max_lines:])
    if len(out) > max_chars:
        out = "...\n" + out[-max_chars:]
    return out or "(no output)"


def same_value(a, b, tolerance=1e-3):
    """Booleans must be booleans ; numbers compared with a tolerance (int / float)."""
    if isinstance(a, bool) or isinstance(b, bool):
        return isinstance(a, bool) and isinstance(b, bool) and a == b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return abs(float(a) - float(b)) <= tolerance
    return a == b


# -----------------------------------------------------------------------------
# Editor helpers
# -----------------------------------------------------------------------------

class Editor:
    def __init__(self, client):
        self.client = client
        c0 = client.call("voxel.cell_to_world", x=0, y=0)
        c1 = client.call("voxel.cell_to_world", x=1, y=1)
        self.cell_w = c1["x"] - c0["x"]
        self.cell_h = c1["y"] - c0["y"]
        self.types = client.call("voxel.types") or []

    def undo_steps(self):
        return int(self.client.call("editor.info").get("undo_steps", 0))

    def undo_since(self, before):
        now = self.undo_steps()
        if now > before:
            self.client.call("editor.undo", count=now - before)
        return max(0, now - before)

    def type_id(self, ref):
        """'$type1' -> id of the first voxel type ; numbers stay numbers."""
        if isinstance(ref, str) and ref.startswith("$type"):
            index = int(ref[5:]) - 1
            if index >= len(self.types):
                raise RuntimeError("the project has only %d voxel type(s)" % len(self.types))
            return int(self.types[index]["type"])
        return int(ref)

    def type_name(self, index):
        return self.types[index - 1]["name"] if index <= len(self.types) else "?"

    def resolve(self, value):
        """Replaces the placeholders of the gold commands ($cells, $cell, $typeN)."""
        if isinstance(value, dict):
            if "$cells" in value:
                dx, dy = value["$cells"]
                return [dx * self.cell_w, dy * self.cell_h, 0]
            if "$cell" in value:
                x, y = value["$cell"]
                world = self.client.call("voxel.cell_to_world", x=x, y=y)
                return [world["x"], world["y"], 0]
            return {k: self.resolve(v) for k, v in value.items()}
        if isinstance(value, list):
            return [self.resolve(v) for v in value]
        if isinstance(value, str) and value.startswith("$type"):
            return self.type_id(value)
        return value

    def snapshot(self):
        """{id: {class, location, cell}} of every actor."""
        state = {}
        for a in self.client.call("level.list_actors", limit=100000) or []:
            location = a.get("location") or [0, 0, 0]
            cell = self.client.try_call("voxel.world_to_cell", x=location[0], y=location[1])
            state[a["id"]] = {
                "class": a.get("class"),
                "location": location,
                "cell": [cell["x"], cell["y"]] if cell else None,
            }
        return state

    def properties(self, actor_id):
        details = self.client.try_call("actor.get", id=actor_id) or {}
        return {name: (p or {}).get("value") for name, p in (details.get("properties") or {}).items()}

    def asset_files(self):
        result = self.client.try_call("asset.list", path=".", recursive=True, limit=100000) or {}
        # The editor answers {"entries": [...], "truncated": bool}.
        entries = result.get("entries", []) if isinstance(result, dict) else result
        return {f["path"] for f in entries
                if isinstance(f, dict) and f.get("type") == "file" and not f["path"].startswith(EVAL_FOLDER)}

    def build_scene(self, name):
        scene = SCENES[name]
        commands = [{"command": "voxel.fill", "params": {
            "x0": SCENE_CLEAR_RECT[0], "y0": SCENE_CLEAR_RECT[1],
            "x1": SCENE_CLEAR_RECT[2], "y1": SCENE_CLEAR_RECT[3], "type": 0}}]
        for v in scene["voxels"]:
            x0, y0, x1, y1 = v["rect"]
            commands.append({"command": "voxel.fill", "params": {
                "x0": x0, "y0": y0, "x1": x1, "y1": y1, "type": self.type_id(v["voxel"])}})
        for a in scene["actors"]:
            params = {"class": a["class"], "id": a["id"], "voxel": a["cell"]}
            if a["properties"]:
                params["properties"] = a["properties"]
            commands.append({"command": "actor.spawn", "params": params})
        result = self.client.call("batch", commands=commands, undo_name="lynxie eval scene")
        errors = [r.get("error") for r in result.get("results", []) if not r.get("ok")]
        if errors:
            raise RuntimeError("scene '%s' : %s" % (name, errors[0]))


# -----------------------------------------------------------------------------
# Checks
# -----------------------------------------------------------------------------

def run_checks(case, editor, before, after, answer, new_files):
    """Returns the list of failure messages (empty = pass)."""
    failures = []
    has_code = bool(extract_python(answer)) or "```" in answer
    text = strip_code(answer).lower()

    allowed_new_classes = {c["class"] for c in case["checks"] if c["type"] == "new_actors"}
    allowed_new_ids = {c["id"] for c in case["checks"] if c["type"] == "exists" and c["id"] not in before}
    new_ids = [i for i in after if i not in before]

    for check in case["checks"]:
        kind = check["type"]

        if kind == "moved":
            b, a = before.get(check["id"]), after.get(check["id"])
            if not a:
                failures.append("%s was deleted" % check["id"])
                continue
            expected = [b["cell"][0] + check["delta"][0], b["cell"][1] + check["delta"][1]]
            if a["cell"] != expected:
                failures.append("%s : cell %s, expected %s (moved by %s)" % (
                    check["id"], a["cell"], expected, check["delta"]))

        elif kind == "at_cell":
            a = after.get(check["id"])
            if not a or a["cell"] != check["cell"]:
                failures.append("%s : cell %s, expected %s" % (check["id"], a and a["cell"], check["cell"]))

        elif kind == "others_unchanged":
            for actor_id, b in before.items():
                if actor_id in check["except"]:
                    continue
                a = after.get(actor_id)
                if not a:
                    failures.append("%s was deleted (should not change)" % actor_id)
                elif a["cell"] != b["cell"]:
                    failures.append("%s moved %s -> %s (should not change)" % (actor_id, b["cell"], a["cell"]))
            unexpected = [i for i in new_ids
                          if after[i]["class"] not in allowed_new_classes and i not in allowed_new_ids]
            if unexpected:
                failures.append("unexpected new actors : %s" % ", ".join(unexpected))

        elif kind == "exists":
            a = after.get(check["id"])
            if not a:
                failures.append("%s does not exist" % check["id"])
            elif check.get("class") and a["class"] != check["class"]:
                failures.append("%s : class %s, expected %s" % (check["id"], a["class"], check["class"]))
            elif check.get("cell") and a["cell"] != check["cell"]:
                failures.append("%s : cell %s, expected %s" % (check["id"], a["cell"], check["cell"]))

        elif kind == "absent":
            if check["id"] in after:
                failures.append("%s still exists" % check["id"])

        elif kind == "count":
            n = sum(1 for a in after.values() if a["class"] == check["class"])
            if n != check["count"]:
                failures.append("%d %s, expected %d" % (n, check["class"], check["count"]))

        elif kind == "new_actors":
            created = [i for i in new_ids if after[i]["class"] == check["class"]]
            got = sorted(after[i]["cell"] or [None, None] for i in created)
            expected = sorted(check["cells"])
            if got != expected:
                failures.append("new %s at %s, expected %s" % (check["class"], got, expected))
            for actor_id in created:
                props = editor.properties(actor_id)
                for name, value in check.get("properties", {}).items():
                    if not same_value(props.get(name), value):
                        failures.append("%s.%s = %r, expected %r" % (actor_id, name, props.get(name), value))

        elif kind == "property":
            props = editor.properties(check["id"])
            if check["name"] not in props:
                failures.append("%s has no property %s" % (check["id"], check["name"]))
            elif not same_value(props[check["name"]], check["value"]):
                failures.append("%s.%s = %r, expected %r" % (
                    check["id"], check["name"], props[check["name"]], check["value"]))

        elif kind == "property_contains":
            value = editor.properties(check["id"]).get(check["name"])
            if not isinstance(value, str) or check["text"] not in value:
                failures.append("%s.%s = %r, should contain %r" % (check["id"], check["name"], value, check["text"]))

        elif kind == "voxels":
            x0, y0, x1, y1 = check["rect"]
            expected = editor.type_id(check["voxel"])
            region = editor.client.call("voxel.get_region", x0=x0, y0=y0, x1=x1, y1=y1)
            wrong = []
            for j, row in enumerate(region["rows"]):
                for i, t in enumerate(row):
                    if t != expected:
                        wrong.append((x0 + i, y0 + j, t))
            if wrong:
                failures.append("voxels %s : %d cell(s) not %d, ex. %s" % (check["rect"], len(wrong), expected, wrong[:3]))

        elif kind == "no_code":
            if has_code:
                failures.append("wrote code, expected a text answer")

        elif kind == "asks":
            if has_code:
                failures.append("wrote code, expected a question")
            elif "?" not in answer:
                failures.append("no question in the answer")

        elif kind == "mentions":
            if not any(t.lower() in text for t in check["any"]):
                failures.append("answer does not mention any of %s" % check["any"])

        elif kind == "mentions_all":
            missing = [t for t in check["all"] if t.lower() not in text]
            if missing:
                failures.append("answer does not mention %s" % missing)

        elif kind == "js_class":
            classes = editor.client.call("level.list_classes") or []
            if check["name"] not in classes:
                failures.append("class %s is not registered (JS error ? see the editor console)" % check["name"])
            elif check.get("properties"):
                steps = editor.undo_steps()
                try:
                    spawned = editor.client.call("actor.spawn", **{"class": check["name"]})
                    props = editor.properties(spawned["id"])
                    for name, value in check["properties"].items():
                        if not same_value(props.get(name), value):
                            failures.append("%s.%s = %r, expected %r" % (check["name"], name, props.get(name), value))
                except CommandError as error:
                    failures.append("cannot spawn %s : %s" % (check["name"], error))
                finally:
                    editor.undo_since(steps)

        elif kind == "new_file":
            matching = [f for f in new_files if f.startswith(check["prefix"]) and f.endswith(check["ext"])]
            if not matching:
                failures.append("no new file %s*%s in assets/" % (check["prefix"], check["ext"]))

        else:
            failures.append("unknown check %s" % kind)

    return failures


# -----------------------------------------------------------------------------
# Models
# -----------------------------------------------------------------------------

def chat(args, messages, options):
    if args.api == "ollama":
        url = args.base_url.rstrip("/") + "/api/chat"
        body = {"model": args.model, "messages": messages, "stream": False,
                "options": {"num_ctx": options["num_ctx"], "temperature": options["temperature"]}}
    else:
        url = args.base_url.rstrip("/") + "/chat/completions"
        body = {"model": args.model, "messages": messages, "temperature": options["temperature"]}

    request = urllib.request.Request(url, data=json.dumps(body).encode("utf-8"),
                                     headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=args.model_timeout) as response:
        root = json.loads(response.read().decode("utf-8"))

    if args.api == "ollama":
        return (root.get("message") or {}).get("content") or ""
    return ((root.get("choices") or [{}])[0].get("message") or {}).get("content") or ""


def run_script(args, connection, project_root, code):
    """Runs a generated script like the editor does. Returns (exit_code, output)."""
    folder = os.path.join(project_root, ".lynx", "lynxie_eval")
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, "attempt.py")
    with open(path, "w", encoding="utf-8") as f:
        f.write(code)

    env = dict(os.environ)
    env["LYNX_EDITOR_PORT"] = str(connection["port"])
    env["LYNX_EDITOR_TOKEN"] = connection["token"]
    env["LYNX_PROJECT"] = project_root
    env["PYTHONPATH"] = args.lynx_python + (os.pathsep + env["PYTHONPATH"] if env.get("PYTHONPATH") else "")
    env["PYTHONIOENCODING"] = "utf-8"
    env["PYTHONUNBUFFERED"] = "1"

    try:
        done = subprocess.run([sys.executable, path], cwd=project_root, env=env, capture_output=True,
                              text=True, encoding="utf-8", errors="replace", timeout=args.script_timeout)
        return done.returncode, (done.stdout or "") + (done.stderr or "")
    except subprocess.TimeoutExpired as error:
        out = (error.stdout or b"")
        out = out.decode("utf-8", "replace") if isinstance(out, bytes) else out
        return -1, out + "\nTIMEOUT : the script ran more than %d s (infinite loop ?)" % args.script_timeout


# -----------------------------------------------------------------------------
# One case
# -----------------------------------------------------------------------------

def play_case(args, case, editor, connection, project_root):
    client = editor.client
    record = {"id": case["id"], "category": case["category"], "request": case["request"]}

    request = case["request"]
    for i in (1, 2):
        request = request.replace("{type%d}" % i, editor.type_name(i))
    record["request"] = request

    files_before = editor.asset_files()
    steps_start = editor.undo_steps()
    started = time.time()

    try:
        editor.build_scene(case["scene"])
        if case.get("select"):
            client.call("editor.select", id=case["select"])
        else:
            client.call("editor.select")
        steps_scene = editor.undo_steps()
        before = editor.snapshot()

        transcript = []
        attempts = 0
        answer = ""

        if args.gold:
            gold = case["gold"]
            if isinstance(gold, dict):
                answer = gold["answer"]
            else:
                commands = [{"command": g["command"], "params": editor.resolve(g["params"])} for g in gold]
                result = client.call("batch", commands=commands, undo_name="lynxie eval gold")
                errors = [r.get("error") for r in result.get("results", []) if not r.get("ok")]
                if errors:
                    raise RuntimeError("gold command failed : %s" % errors[0])
                answer = "(gold commands)\n```python\n# %d command(s)\n```" % len(commands)
                attempts = 1
        else:
            context = client.call("ai.context", prompt=request)
            options = {"num_ctx": args.num_ctx or context["num_ctx"],
                       "temperature": args.temperature if args.temperature is not None else context["temperature"]}
            history = [{"role": "user", "content": request}]

            while True:
                messages = [{"role": "system", "content": context["system"]}] + history[-12:]
                answer = chat(args, messages, options)
                history.append({"role": "assistant", "content": answer})
                transcript.append({"answer": answer})

                code = extract_python(answer)
                if not code:
                    break

                attempts += 1
                steps_script = editor.undo_steps()

                if looks_like_python(code):
                    exit_code, output = run_script(args, connection, project_root, code)
                else:
                    exit_code, output = 1, "The answer is not Python (JavaScript syntax : // comments or let/const/var/function)."

                transcript[-1].update({"exit_code": exit_code, "output": tail(output, 30, 3000)})

                if exit_code == 0:
                    break

                editor.undo_since(steps_script)

                if attempts >= args.attempts:
                    break

                fix = client.call("ai.fix_message", exit_code=exit_code, output=tail(output))["message"]
                history.append({"role": "user", "content": fix})
                transcript[-1]["fix"] = fix
                # Same as the editor : the context is rebuilt for the ORIGINAL request.
                context = client.call("ai.context", prompt=request)

            record["transcript"] = transcript
            record["system"] = context["system"]
            record["final_answer"] = answer

        after = editor.snapshot()
        new_files = sorted(editor.asset_files() - files_before)
        failures = run_checks(case, editor, before, after, answer, new_files)

        last_exit = transcript[-1].get("exit_code") if transcript else 0
        if last_exit not in (0, None) and not failures:
            failures.append("the last script failed (exit code %s)" % last_exit)

        record.update({"passed": not failures, "failures": failures, "attempts": attempts})

    except Exception as error:   # setup, model or connection error
        record.update({"passed": False, "failures": ["ERROR : %s" % error], "attempts": 0, "error": True})

    finally:
        # Back to the state before the case : scene, script, new files.
        try:
            editor.undo_since(steps_start)
            created = sorted(editor.asset_files() - files_before)
            for path in created:
                client.try_call("asset.delete", path=path)
            if any(p.endswith(".js") for p in created):
                client.try_call("editor.reload_scripts")
        except Exception as error:
            record.setdefault("failures", []).append("CLEANUP ERROR : %s" % error)

    record["seconds"] = round(time.time() - started, 1)
    return record


# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------

def find_lynx_python(given):
    if given:
        return os.path.abspath(given)
    try:
        import lynx_editor  # noqa: F401
        return os.path.dirname(os.path.dirname(os.path.abspath(lynx_editor.__file__)))
    except ImportError:
        return ""


def main():
    parser = argparse.ArgumentParser(description="Lynxie evaluation (run it on a test project, level saved).")
    parser.add_argument("--project", help="project folder (default : the editor started last)")
    parser.add_argument("--gold", action="store_true", help="play the gold solutions (validates the cases, no model)")
    parser.add_argument("--model", help="model name (Ollama tag, or name for --api openai)")
    parser.add_argument("--api", choices=["ollama", "openai"], default="ollama")
    parser.add_argument("--base-url", default=None, help="default http://localhost:11434 (ollama) / http://localhost:8000/v1")
    parser.add_argument("--attempts", type=int, default=3, help="script runs per case (auto-fix), like the editor")
    parser.add_argument("--temperature", type=float, default=None)
    parser.add_argument("--num-ctx", type=int, default=None)
    parser.add_argument("--lynx-python", default=None, help="folder that contains the lynx_editor package (<engine>/python)")
    parser.add_argument("--case", action="append", default=[], help="only the cases whose id contains this (repeatable)")
    parser.add_argument("--category", action="append", default=[], help="only these categories")
    parser.add_argument("--repeat", type=int, default=1, help="run each case N times (variance)")
    parser.add_argument("--script-timeout", type=int, default=60)
    parser.add_argument("--model-timeout", type=int, default=900)
    parser.add_argument("--out", default=None, help="results file (.jsonl)")
    parser.add_argument("--export-finetune", default=None,
                        help="write the PASSED conversations as chat JSONL (system with live scene, user, assistant)")
    parser.add_argument("--export-with-fixes", action="store_true",
                        help="keep the error / correction turns in the exported conversations")
    parser.add_argument("--force", action="store_true", help="run even if the level has unsaved changes")
    args = parser.parse_args()

    if not args.gold and not args.model:
        parser.error("give --model, or --gold to validate the cases")
    if args.base_url is None:
        args.base_url = "http://localhost:11434" if args.api == "ollama" else "http://localhost:8000/v1"

    args.lynx_python = find_lynx_python(args.lynx_python)
    if not args.gold and not os.path.isdir(os.path.join(args.lynx_python, "lynx_editor")):
        parser.error("lynx_editor not found : give --lynx-python <engine>/python (the folder that contains lynx_editor/)")

    client = LynxClient(args.project)
    connection = client.info
    info = client.call("editor.info")
    project_root = info["project_root"]

    if info.get("playing"):
        sys.exit("The editor is in Play mode : press Stop first.")
    if info.get("unsaved_changes") and not args.force:
        sys.exit("The level has unsaved changes : save it (Ctrl+S) first, or use --force.\n"
                 "(Everything is undone after each case, but a saved level is the safety net.)")

    print("Project : %s" % project_root)

    # --- Evaluation classes ---------------------------------------------------
    with open(os.path.join(HERE, "eval_actors.js"), "r", encoding="utf-8") as f:
        client.call("asset.write", path=EVAL_CLASSES_FILE, content=f.read())
    classes = client.call("level.list_classes") or []
    missing = [c for c in EVAL_CLASSES if c not in classes]
    if missing:
        client.try_call("asset.delete", path=EVAL_FOLDER)
        client.try_call("editor.reload_scripts")
        sys.exit("The evaluation classes are not registered (%s). A C++ class with the same name ? "
                 "See the editor console." % ", ".join(missing))

    # --- Existing actors : removed for the run (one undo step, restored) -------
    editor = Editor(client)
    steps_clear = editor.undo_steps()
    existing = [a["id"] for a in client.call("level.list_actors", limit=100000) or []]
    if existing:
        client.call("actor.delete", ids=existing)
        print("Removed %d actor(s) of the level for the run (restored at the end)." % len(existing))

    cases = [c for c in CASES
             if (not args.case or any(s in c["id"] for s in args.case))
             and (not args.category or c["category"] in args.category)]

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_path = args.out or os.path.join(HERE, "results", "%s_%s.jsonl" % (stamp, "gold" if args.gold else
                                                                          re.sub(r"[^\w.-]", "_", args.model)))
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)

    records = []
    try:
        with open(out_path, "w", encoding="utf-8") as out:
            for index, case in enumerate(cases):
                if case["needs_types"] > len(editor.types):
                    print("SKIP  %-32s (needs %d voxel types)" % (case["id"], case["needs_types"]))
                    continue
                for run in range(args.repeat):
                    record = play_case(args, case, editor, connection, project_root)
                    record["run"] = run
                    records.append(record)
                    out.write(json.dumps(record, ensure_ascii=False) + "\n")
                    out.flush()
                    status = "PASS" if record["passed"] else "FAIL"
                    tries = (" (%d tries)" % record["attempts"]) if record["attempts"] > 1 else ""
                    print("%s  [%d/%d] %-32s %s%s" % (status, index + 1, len(cases), case["id"],
                                                     "" if record["passed"] else "; ".join(record["failures"])[:200],
                                                     tries))
    finally:
        # --- Restore the level and remove the evaluation classes -------------
        editor.undo_since(steps_clear)
        client.try_call("asset.delete", path=EVAL_FOLDER)
        client.try_call("editor.reload_scripts")
        client.close()

    # --- Report ---------------------------------------------------------------
    if not records:
        print("No case run.")
        return

    print("\n%-12s %6s %8s %10s" % ("category", "cases", "passed", "1st try"))
    categories = sorted({r["category"] for r in records})
    for category in categories + ["TOTAL"]:
        rs = [r for r in records if category == "TOTAL" or r["category"] == category]
        passed = sum(r["passed"] for r in rs)
        first = sum(r["passed"] and r["attempts"] <= 1 for r in rs)
        print("%-12s %6d %7.0f%% %9.0f%%" % (category, len(rs), 100.0 * passed / len(rs), 100.0 * first / len(rs)))
    print("\nResults : %s" % out_path)

    if args.export_finetune and not args.gold:
        count = 0
        with open(args.export_finetune, "w", encoding="utf-8") as f:
            for r in records:
                if not r["passed"] or "system" not in r:
                    continue
                messages = [{"role": "system", "content": r["system"]}, {"role": "user", "content": r["request"]}]
                if args.export_with_fixes and len(r["transcript"]) > 1:
                    for step in r["transcript"][:-1]:
                        messages.append({"role": "assistant", "content": step["answer"]})
                        messages.append({"role": "user", "content": step.get("fix") or step.get("output", "")})
                messages.append({"role": "assistant", "content": r["final_answer"]})
                f.write(json.dumps({"messages": messages, "case": r["id"]}, ensure_ascii=False) + "\n")
                count += 1
        print("Fine-tune data : %d conversation(s) -> %s" % (count, args.export_finetune))


if __name__ == "__main__":
    main()
