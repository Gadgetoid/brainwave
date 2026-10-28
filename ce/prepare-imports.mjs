// Turns the ROM export lists in ce-imports/*.def into the name lists mkimplib.py builds import libraries from.
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const [source, destination] = process.argv.slice(2);
mkdirSync(destination, { recursive: true });
for (const library of ['coredll', 'commctrl']) {
    const text = readFileSync(join(source, `${library}.def`), 'utf8');
    const exports = text.split(/\bEXPORTS\b/)[1];
    if (!exports) throw new Error(`Missing export table: ${library}`);
    const names = exports.split(/\r?\n/).map(line => line.replace(/;.*/, '').trim().split(/\s+/)[0])
        .filter(name => /^[A-Za-z_][A-Za-z0-9_]*$/.test(name));
    writeFileSync(join(destination, `${library}.txt`), [...new Set(names)].join('\n') + '\n');
}
