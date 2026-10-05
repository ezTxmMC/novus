"""A realistic fake dependency cache ($NOVUS_DEPS) and a consuming project for the dependency e2e checks."""
import os

from lspclient import make_project

GEOMETRY_LIB = '''package geometry

// Distance between two points.
method distance(float x1, float y1, float x2, float y2): float {
    return 0.0
}

/// A point of the plane.
define class Point {
    float x
    float y
    string label: get, set

    construct(float x, float y) {
        this.x = x
        this.y = y
    }

    // Move the point by an offset.
    method shift(float dx, float dy): Point {
        return Point(x + dx, y + dy)
    }
}

define enum Axis {
    HORIZONTAL,
    VERTICAL
}

private final ORIGIN_NAME = "origin"
'''

FILES_GEOMETRY = {
    "project.nv": 'project "github.com/acme/geometry"\nversion "1.2.0"\nlib "lib.nv"\n\nrequire "github.com/acme/mathx" "v0.3.1"\n',
    "lib.nv": GEOMETRY_LIB,
    "shapes/circle.nv": 'package shapes\n\n// A circle with a radius.\ndefine class Circle {\n    float radius\n\n    method area(): float {\n        return radius * radius * 3.14\n    }\n}\n\nmethod unitCircle(): Circle {\n    return Circle{radius=1.0}\n}\n',
    "shapes/square.nv": 'package shapes\n\ndefine class Square {\n    float side\n}\n',
    "util/text.nv": 'package util\n\n// Shout a text.\nmethod shout(string text): string {\n    return text.toUpper()\n}\n',
    "internal/.hidden": "",
}
FILES_MATHX = {
    "project.nv": 'project "github.com/acme/mathx"\nversion "0.3.1"\nlib "mathx.nv"\n',
    "mathx.nv": 'package mathx\n\n// Square of a number.\nmethod sq(float x): float {\n    return x * x\n}\n\ndefine class Matrix {\n    integer rows\n}\n',
}
FILES_LATEST = {"lib.nv": 'package latestlib\n\n// Latest of everything.\nmethod newest(): string {\n    return "latest"\n}\n'}
FILES_TAGGED_V2 = {"lib.nv": 'package tagged\n\nmethod versionTwo(): integer {\n    return 2\n}\n'}
FILES_TAGGED_V1 = {"lib.nv": 'package tagged\n\nmethod versionOne(): integer {\n    return 1\n}\n'}
FILES_REPLACED_LOCAL = {
    "project.nv": 'project "github.com/acme/localdep"\nversion "9.9.9"\nlib "lib.nv"\n\nrequire "github.com/acme/nested" "v1.0.0"\nreplace "github.com/acme/nested" "../nested"\n',
    "lib.nv": 'package localdep\n\n// From the replaced local directory.\nmethod fromLocal(): string {\n    return "local"\n}\n',
}
FILES_NESTED_LOCAL = {"lib.nv": 'package nested\n\nmethod deepHelper(): integer {\n    return 3\n}\n'}

CONSUMER_MANIFEST = '''project "demo"
version "0.1.0"
main "main.nv"

require "github.com/acme/geometry" "v1.2.0"
require "github.com/acme/latestlib"
require "github.com/acme/tagged" "v2"
require "github.com/acme/localdep" "v9.9.9"
require "github.com/acme/missing" "v1.0.0"

replace "github.com/acme/localdep" "../localdep"
'''


def write_tree(base, files):
    for name, content in files.items():
        full = os.path.join(base, name)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w") as handle:
            handle.write(content)


def build(base=None):
    """Returns (project root, deps cache). The local replace target sits next to the project as ../localdep."""
    top = make_project({"placeholder": ""}, base)
    deps = os.path.join(top, "deps")
    write_tree(os.path.join(deps, "github.com/acme/geometry@v1.2.0"), FILES_GEOMETRY)
    write_tree(os.path.join(deps, "github.com/acme/mathx@v0.3.1"), FILES_MATHX)
    write_tree(os.path.join(deps, "github.com/acme/latestlib@latest"), FILES_LATEST)
    write_tree(os.path.join(deps, "github.com/acme/tagged@v2"), FILES_TAGGED_V2)
    write_tree(os.path.join(deps, "github.com/acme/tagged@v1"), FILES_TAGGED_V1)
    write_tree(os.path.join(top, "localdep"), FILES_REPLACED_LOCAL)
    write_tree(os.path.join(top, "nested"), FILES_NESTED_LOCAL)
    project = os.path.join(top, "app")
    write_tree(project, {"project.nv": CONSUMER_MANIFEST, "main.nv": "package main\n\nmethod main() {\n}\n"})
    return project, deps
