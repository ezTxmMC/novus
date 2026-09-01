// Copies the repository's std/ into the extension so the language server can
// read the real standard library sources (completions, hover, signatures).
const fs = require('fs');
const path = require('path');
const source = path.resolve(__dirname, '..', '..', 'std');
const target = path.resolve(__dirname, '..', 'std');
if (!fs.existsSync(source)) {
  console.error('std/ not found at ' + source);
  process.exit(1);
}
fs.rmSync(target, { recursive: true, force: true });
fs.mkdirSync(target, { recursive: true });
for (const file of fs.readdirSync(source)) {
  if (file.endsWith('.nv')) fs.copyFileSync(path.join(source, file), path.join(target, file));
}
console.log('copied std/ (' + fs.readdirSync(target).length + ' modules)');
