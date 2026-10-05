# Lynxie eval

Measures how well a model drives the Lynx editor, on 61 French requests with
automatic checks. Run it before and after each change (prompt, model,
fine-tune) : the score says if it is better.

```
python run_eval.py --gold                                   # 1. validates the cases (no model)
python run_eval.py --model qwen3-coder:30b --lynx-python <engine>/python
python run_eval.py --model my-ft --api openai --base-url http://localhost:8000/v1 ...
```

**Run it on a test project** (a copy of Blocky...), editor open, level saved.

## What it does, for each case

1. Builds the case scene : ground voxels + actors of test classes
   (`EvalHero`, `EvalEnemy`, `EvalCoin`, `EvalTorch`, `EvalDoor`, `EvalCrate`...),
   written to `assets/_lynxie_eval/` (JS classes can be anywhere in assets/).
2. Asks the editor for the **exact prompt Lynxie sends** (command `ai.context`) :
   rules, live scene state, target resolution, references. Nothing is copied
   in Python, so the evaluation always measures the real Lynxie.
3. Sends it to the model, runs its script like the editor (same
   `lynx_editor` module), with the **same auto-fix loop** : error -> the
   changes are undone -> `ai.fix_message` -> the model corrects (`--attempts`, 3).
4. Checks the scene : moved actors (in cells), others unchanged, new actors,
   properties, voxels, "asked a question instead of guessing", answer text, JS class
   registered...
5. Undoes everything (Ctrl+Z steps) and deletes the new files.

The actors already in the level are removed for the run and restored at the end
(one undo step). The test classes are removed at the end.

## Results

```
category      cases   passed    1st try
move             14      93%        86%
...
TOTAL            61      93%        90%
```

*passed* = correct after the auto-fix loop ; *1st try* = correct without any
fix. Each run writes `results/<date>_<model>.jsonl` : the request, the system
prompt, every answer, script output, and the reason of each failure.

Options : `--case move_` / `--category voxel` (subset), `--repeat 3` (variance,
temperature 0.2), `--attempts 1` (no auto-fix), `--temperature`, `--num-ctx`.

## Fine-tune data

```
python run_eval.py --model <big model> --export-finetune data.jsonl [--export-with-fixes]
```

Writes the **passed** conversations as chat JSONL
(`{"messages": [system, user, assistant]}`), with the same system prompt as in
the editor (live scene state included) : the model learns to read the context,
not to memorize the API. `--export-with-fixes` keeps the error / correction
turns (teaches self-correction). Use a better model (or several runs) to produce
the data, and keep the cases of the evaluation itself OUT of the training set
(write new requests / scenes for training, or the score means nothing).

## Cases (`cases.py`)

A case = scene + request + checks + gold solution :

```python
case("move_left_3", "move", "basic",
     "déplace le joueur de 3 cases vers la gauche",
     [moved("Hero", -3, 0), others_unchanged("Hero")],
     [move("Hero", -3, 0)]),
```

`--gold` plays the gold solutions instead of a model : **every case must
pass**. If one fails on your editor, the case (or the editor) is wrong, not the
model. Run it after adding cases or changing the commands.

Categories : move, place, target (which actor), spawn, delete, property,
duplicate, voxel, question (answer without code), ask (must ask, no clear
target), javascript.

## Files

| File | |
|---|---|
| `run_eval.py` | runner, checks, report, export |
| `cases.py` | scenes and cases |
| `eval_actors.js` | test actor classes |
| `lynx_client.py` | client of the editor command server (protocol : JSON lines) |
