"""A multi-package project for the project-suggestion e2e checks (packages, root files, hyphenated folder, nested package)."""

FILES = {
 "project.nv": 'project "demo"\nversion "0.1.0"\nmain "main.nv"\n',
 "main.nv": 'package main\n\nimport os\n\nmethod main() {\n}\n',
 "geo/shape.nv": '''package geo

// A figure with an area.
define interface Shape {
    area(): float
}

/// A round figure.
define class Circle based Shape {
    float radius
    string label: get, set

    construct(float radius) {
        this.radius = radius
    }

    // The area of the circle.
    method area(): float {
        return radius * radius * 3.14
    }

    method grow(float by): Circle {
        return Circle(radius + by)
    }
}

// Describe a shape in words.
method describe(Shape s): string {
    return "shape"
}

private final PI = 3.14

define enum Kind {
    ROUND,
    SQUARE
}
''',
 "geo/shapes/round.nv": 'package shapes\n\ndefine class Ring {\n    float inner\n}\n\nmethod ringArea(Ring r): float {\n    return 1.0\n}\n',
 "models/user.nv": 'package models\n\ndefine class User {\n    string name\n    integer age: get, set\n\n    construct(string name) {\n        this.name = name\n    }\n\n    method greet(): string {\n        return "hi " + name\n    }\n}\n\nmethod makeUser(string name): User {\n    return User(name)\n}\n',
 "helper.nv": 'package main\n\nmethod helperFn(): integer {\n    return 1\n}\n\ndefine class HelperBox {\n    integer size\n}\n',
 "tools/tools.nv": 'package tools\n\nmethod toolRun() {\n}\n',
 "my-lib/x.nv": 'package mylib\n\nmethod hyph() {\n}\n',
}
