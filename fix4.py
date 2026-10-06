import codecs

with codecs.open('src/ui/Toolbar.ts', 'r', 'utf-8') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if "const dotSrc =" in line:
        lines[i] = "    const dotSrc = \\`\\${dotString.replace(/\\`/g, '\\\\\\\\\\\\`')}\\`;\n"

with codecs.open('src/ui/Toolbar.ts', 'w', 'utf-8') as f:
    f.writelines(lines)
