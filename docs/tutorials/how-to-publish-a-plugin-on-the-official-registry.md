# Publishing a Plugin for Lynx Engine

Plugins allow you to add new features to Lynx Engine without modifying the engine itself. Each plugin is distributed through its own GitHub repository and can be installed directly from the Lynx launcher.

This guide explains how to structure your plugin, create a release, and submit it to the official Lynx plugin registry.

## 1. Create a GitHub repository

Each plugin must have its **own GitHub repository**.

For example:

```text
https://github.com/oscar-soirey/Lynx-Weather
```

The repository name is up to you, but using a name such as `Lynx-PluginName` is recommended.

The repository must be public so that the launcher can access its releases.

---

## 2. Plugin structure

Your plugin must contain a `plugin.json` file.

A typical plugin repository could look like this:

```text
Lynx-Weather/
├── plugin.json
├── src/
│   ├── Weather.cpp
│   └── Weather.h
├── include/
│   └── ...
├── bin/
│   └── ...
└── ...
```

The `plugin.json` file is required.

When distributing the plugin, `plugin.json` and the `bin/` directory must be included in the `.zip` archive.

They can be placed directly at the root:

```text
Weather.zip
├── plugin.json
└── bin/
    └── Weather.dll
```

or inside a subdirectory:

```text
Weather.zip
└── Weather/
    ├── plugin.json
    └── bin/
        └── Weather.dll
```

Both layouts are supported by the launcher.

---

## 3. The `plugin.json` file

The `plugin.json` file contains the information Lynx uses to identify your plugin.

The `name` field is especially important. It must **exactly match** the name you submit to the plugin registry.

For example:

```json
{
  "name": "Weather",
  "version": "1.2.4"
}
```

If your registry entry is:

```json
{
  "name": "Weather",
  "repo": "oscar-soirey/Lynx-Weather",
  "description": "Rain, snow and day/night cycle.",
  "category": "Gameplay",
  "author": "Oscar"
}
```

then the `plugin.json` must use:

```json
"name": "Weather"
```

The name is case-sensitive and is used by the launcher to recognize an already-installed plugin.

---

## 4. Prepare a plugin version

Every plugin release targets a specific version of Lynx.

For example, if you are publishing version `1.2.4` of your plugin for Lynx `26.0.6`, the GitHub release tag must be:

```text
v1.2.4-26.0.6
```

The format is:

```text
v<plugin-version>-<lynx-version>
```

Examples:

```text
v1.0.0-26.0.1
v1.2.4-26.0.6
v2.0.0-27.0.0
```

Tags using another format are ignored by the launcher.

---

## 5. Create the plugin archive

Each release must contain a `.zip` archive with the files required to install the plugin.

For example:

```text
Weather.zip
├── plugin.json
└── bin/
    └── Weather.dll
```

or:

```text
Weather.zip
└── Weather/
    ├── plugin.json
    └── bin/
        └── Weather.dll
```

### Important

The launcher uses the **first `.zip` file found in the release**.

For this reason, it is recommended to include only one `.zip` archive in each release.

---

## 6. Create a GitHub release

Once your plugin is ready, create a new release on GitHub.

1. Open your plugin repository.
2. Go to **Releases**.
3. Click **Create a new release**.
4. Create a tag using the required format.

For example:

```text
v1.2.4-26.0.6
```

5. Upload your plugin `.zip` archive.
6. Optionally add release notes.
7. Publish the release.

Your release should look similar to:

```text
v1.2.4-26.0.6

Weather.zip
```

---

## 7. Beta releases

You can also publish **pre-releases** when you want to distribute a version for testing.

Pre-releases are only offered to users who have enabled:

**Beta Versions**

in the launcher.

This allows you to test new versions without immediately making them available to everyone.

---

## 8. Submit your plugin to the registry

Creating a GitHub release does not automatically add your plugin to the Lynx launcher.

You also need to submit it to the official plugin registry.

The registry is the catalog used by the launcher's **Plugins** tab.

To submit your plugin, use the **plugin submission form on the Lynx Engine website**.

The form will ask you for information such as:

### Name

The name of your plugin.

It must **exactly match** the `name` field in your `plugin.json`.

For example:

```text
Weather
```

### GitHub repository

Enter the repository containing your plugin.

You can use either:

```text
oscar-soirey/Lynx-Weather
```

or:

```text
https://github.com/oscar-soirey/Lynx-Weather
```

Both formats are supported.

### Description

Give a short description of what your plugin does.

For example:

```text
Rain, snow and day/night cycle.
```

Try to keep it short and clear.

### Category

Choose the category that best describes your plugin.

For example:

```text
Gameplay
```

### Author

Enter the name you want to display as the plugin author.

For example:

```text
Oscar
```

---

## 9. Before submitting

Make sure everything is ready before submitting the form:

- [ ] The GitHub repository is public.
- [ ] The repository contains a `plugin.json`.
- [ ] The `name` in `plugin.json` exactly matches the name submitted in the form.
- [ ] The plugin works with the targeted version of Lynx.
- [ ] A GitHub release has been created.
- [ ] The release tag follows `v<plugin-version>-<lynx-version>`.
- [ ] The release contains a `.zip` archive.
- [ ] The `.zip` contains `plugin.json`.
- [ ] The `.zip` contains all files required by the plugin.
- [ ] The `bin/` directory is included if required by the plugin.
- [ ] Only one `.zip` archive is included in the release.

---

## 10. How plugin versions are selected

When a user selects a version of Lynx, the launcher automatically looks for the **highest plugin version compatible with that engine version**.

For example, suppose a plugin has these releases:

```text
v1.0.0-26.0.1
v1.1.0-26.0.4
v1.2.0-26.0.6
v2.0.0-27.0.0
```

If the user is running Lynx `26.0.6`, the launcher will select:

```text
v1.2.0-26.0.6
```

If the user is running Lynx `26.0.5`, it will select:

```text
v1.1.0-26.0.4
```

A plugin built for an older version of Lynx can therefore be used with a newer compatible engine version.

If the selected plugin version is older than the engine version, Lynx will display a warning when the user activates the plugin from:

**Options → Plugins**

---

## Complete example

Here is a complete example of a plugin called `Weather`.

### GitHub repository

```text
https://github.com/oscar-soirey/Lynx-Weather
```

### `plugin.json`

```json
{
  "name": "Weather",
  "version": "1.2.4"
}
```

### Plugin archive

```text
Weather.zip
├── plugin.json
└── bin/
    └── Weather.dll
```

### GitHub release

```text
v1.2.4-26.0.6
```

with:

```text
Weather.zip
```

### Registry entry

```json
{
  "name": "Weather",
  "repo": "oscar-soirey/Lynx-Weather",
  "description": "Rain, snow and day/night cycle.",
  "category": "Gameplay",
  "author": "Oscar"
}
```

After submitting the plugin through the website form, it can be added to the Lynx plugin registry and made available through the launcher's **Plugins** tab.
