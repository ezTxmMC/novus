import os, sys, random

def method_block(cls, k, helper):
    return f'''
    // Computes step {k} of {cls}.
    method step{k}(integer limit): integer {{
        var total = 0
        var names = []
        for (i in arrays.range(0, limit)) {{
            if (i % 2 == 0) {{
                total = total + i * {k + 1}
            }} else {{
                total = total - 1
            }}
            names.push("item" + i)
        }}
        var table = {{}}
        table["count"] = names.length()
        var label = "${{this.name}}-${{total}}"
        if (label.length() > 100) {{
            return 0
        }}
        return total + table["count"] + {helper}(total)
    }}
'''

def free_method(cls, k):
    return f'''
// Free helper {k} of {cls}.
method {cls[0].lower()}{cls[1:]}Help{k}(integer value): integer {{
    var result = value
    while (result > 1000) {{
        result = result / 2
    }}
    return result + {k}
}}
'''

def gen_file(pkg, cls, lines, imports):
    out = [f'package {pkg}', '', 'import arrays']
    for im in imports: out.append(f'import {im}')
    out.append('')
    out.append(f'// The class {cls}.')
    out.append(f'define class {cls} {{')
    out.append('    string name: get')
    out.append('    integer size: get, set')
    out.append('')
    out.append(f'    construct(string name, integer size) {{')
    out.append('        this.name = name')
    out.append('        this.size = size')
    out.append('    }')
    helper = f'{cls[0].lower()}{cls[1:]}Help0'
    body = []
    k = 0
    while sum(len(b.split('\n')) for b in body) + 10 < lines - 12:
        body.append(method_block(cls, k, helper)); k += 1
    out.append(''.join(body))
    out.append('}')
    out.append(free_method(cls, 0))
    return '\n'.join(out) + '\n'

def gen_project(root, nfiles, per_pkg, lines_per_file, cross=True, seed=1):
    os.makedirs(root, exist_ok=True)
    npkg = (nfiles + per_pkg - 1)//per_pkg
    files = []
    for p in range(npkg):
        pkg = f'pkg{p:03d}'
        d = os.path.join(root, pkg); os.makedirs(d, exist_ok=True)
        for j in range(per_pkg):
            if len(files) >= nfiles: break
            cls = f'Thing{p:03d}x{j:02d}'
            imports = []
            if cross and p > 0: imports = [f'pkg{p-1:03d}']
            txt = gen_file(pkg, cls, lines_per_file, imports)
            fp = os.path.join(d, f'f{j:02d}.nv')
            open(fp, 'w').write(txt); files.append(fp)
    main = 'package main\n\nimport pkg%03d\n\nmethod main() {\n    var t = Thing%03dx00("a", 1)\n    println(t.step0(10))\n}\n' % (npkg-1, npkg-1)
    open(os.path.join(root, 'main.nv'), 'w').write(main)
    open(os.path.join(root, 'project.nv'), 'w').write('project "synthetic"\nversion "0.1.0"\nmain "main.nv"\n')
    return files

if __name__ == '__main__':
    root, nfiles, per_pkg, lines = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
    gen_project(root, nfiles, per_pkg, lines)
