"""Creates a JavaScript actor class in assets/classes/. Argument : ClassName"""

import sys

import lynx_editor as lynx

TEMPLATE = """class {name} extends Actor {{
    static properties = {{
        speed: 2.0,
    }};

    BeginPlay() {{
        console.log("{name} : BeginPlay", this.id);
    }}

    Update(dt) {{
    }}
}}
"""


def main():
    if len(sys.argv) < 2:
        print("Usage : new_js_class.py ClassName")
        return

    name = sys.argv[1]
    path = f"classes/{name}.js"

    if lynx.assets.exists(path):
        print(path, "already exists")
        return

    # .js files are reloaded by the editor after the write.
    result = lynx.assets.write(path, TEMPLATE.format(name=name))
    print("created", result["path"], "- reloaded :", result["reloaded"])
    print("classes :", lynx.classes())


if __name__ == "__main__":
    main()
