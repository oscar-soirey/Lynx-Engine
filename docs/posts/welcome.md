This is the first post of the Lynx devlog. It is a **sample**: remove it from `posts/index.json` when you publish your own.

## Writing a post

Posts are plain Markdown files stored in `posts/`. The list of posts lives in `posts/index.json`. Use the local editor (`devlog-admin.html`) to write, preview and export a post package.

### Code

Fenced code blocks are highlighted:

```cpp
void Update(float dt) {
    position += velocity * dt;
}
```

### Images, GIFs and videos

Drop or paste an image in the editor and it is inserted for you. To embed a YouTube video, put its link alone on a line:

```
https://youtu.be/xxxxxxxxxxx
```

> Single line breaks are kept, so you can write the way you talk.

- bold: `**text**`
- italic: `*text*`
- links: `[label](https://example.com)`
