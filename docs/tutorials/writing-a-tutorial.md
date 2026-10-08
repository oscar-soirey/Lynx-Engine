This is a **sample tutorial**. It shows how a tutorial is structured; remove it from `tutorials/index.json` when you publish your own.

## What you will learn

- how a tutorial is split into steps
- how to show code, images and videos

## Step 1: one idea per step

Give every step its own `##` heading and keep it focused on a single goal. Explain *why* before showing *how*.

## Step 2: show the code

Fenced code blocks are highlighted and labelled with their language:

```js
function Update(dt) {
  const x = Input.axis("move_x");
  parent.position.x += x * 8 * dt;

  if (Input.pressed("jump"))
    parent.velocity.y = 12;
}
```

## Step 3: show the result

Drop or paste a screenshot or a GIF in the editor and it is inserted for you. To embed a video, put a YouTube link alone on a line.

> Single line breaks are kept, so you can write the way you talk.

## Going further

Pick the **level** (Beginner, Intermediate, Advanced) and a few **tags** in the editor so readers can filter tutorials.
