#!/usr/bin/env python3
"""Creates the workloads of the performance scenarios below a work directory (no spaces in the path):
syn500 (500 files, 55k lines), size200/size2000/size10000 (one file each), huge10k (10000 files), deps3 + depproj
(a fake $NOVUS_DEPS with three dependencies of 50 files each and a project that requires them), fake-novusc.sh.
Usage: python3 prepare.py WORKDIR"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen import gen_file, gen_project

FAKE_NOVUSC = '#!/bin/sh\nif [ "$1" = "deps" ]; then echo fetched; exit 0; fi\necho "ok: $2"\n'
MANIFEST = 'project "%s"\nversion "0.1.0"\nmain "main.nv"\n'


def prepare_sizes():
    for lines in (200, 2000, 10000):
        os.makedirs('size%d' % lines, exist_ok=True)
        text = gen_file('main', 'Big', lines, []) + '\nmethod main() {\n    var b = Big("x", 1)\n    println(b.step0(3))\n}\n'
        open('size%d/main.nv' % lines, 'w').write(text)
        open('size%d/project.nv' % lines, 'w').write(MANIFEST % 'size')


def prepare_dependencies():
    deps = os.path.abspath('deps3')
    os.makedirs('depproj', exist_ok=True)
    requires = []
    for k in range(3):
        base = '%s/github.com/acme/lib%d@v1.0.0' % (deps, k)
        os.makedirs(base + '/util', exist_ok=True)
        for j in range(40):
            open('%s/f%02d.nv' % (base, j), 'w').write(gen_file('lib%d' % k, 'Lib%dThing%02d' % (k, j), 150, []))
        for j in range(10):
            open('%s/util/u%d.nv' % (base, j), 'w').write(gen_file('util', 'Lib%dUtil%d' % (k, j), 150, []))
        entry = 'package lib%d\n\n' % k + ''.join('import @f%02d\n' % j for j in range(40))
        open(base + '/main.nv', 'w').write(entry + '\nmethod libVersion%d(): string {\n    return "1.0"\n}\n' % k)
        requires.append('require "github.com/acme/lib%d" "v1.0.0"' % k)
    open('depproj/project.nv', 'w').write(MANIFEST % 'withdeps' + '\n' + '\n'.join(requires) + '\n')
    imports = ''.join('import "github.com/acme/lib%d"\n' % k for k in range(3)) + 'import "github.com/acme/lib0/util"\n'
    open('depproj/main.nv', 'w').write('package main\n\n' + imports + '\nmethod main() {\n    var t = Lib0Thing00("a", 1)\n    var u = Lib1Thing05("b", 2)\n    println(t.step0(10))\n    println(u.name)\n}\n')


def prepare_huge():
    os.makedirs('huge10k', exist_ok=True)
    for d in range(200):
        folder = 'huge10k/dir%03d' % d
        os.makedirs(folder, exist_ok=True)
        for j in range(50):
            open('%s/m%02d.nv' % (folder, j), 'w').write('package dir%03d\n\nmethod fn%dx%d(integer a): integer {\n    return a + %d\n}\n' % (d, d, j, j))
    open('huge10k/project.nv', 'w').write(MANIFEST % 'huge')
    open('huge10k/main.nv', 'w').write('package main\n\nimport dir000\n\nmethod main() {\n    var x = fn0x1(2)\n    println(x)\n}\n')


def main():
    work = os.path.abspath(sys.argv[1])
    os.makedirs(work, exist_ok=True)
    os.chdir(work)
    gen_project('syn500', 500, 10, 100)
    prepare_sizes()
    prepare_dependencies()
    prepare_huge()
    open('fake-novusc.sh', 'w').write(FAKE_NOVUSC)
    os.chmod('fake-novusc.sh', 0o755)
    print('workloads ready in', work)


main()
