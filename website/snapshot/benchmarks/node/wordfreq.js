const fs = require("fs");
const text = fs.readFileSync("input.txt", "utf8");
const counts = new Map();
for (const word of text.split(/\s+/)) {
  if (!word) continue;
  counts.set(word, (counts.get(word) ?? 0) + 1);
}
const ranked = [];
for (const [word, count] of counts) ranked.push(String(count).padStart(8) + " " + word);
ranked.sort().reverse();
for (let i = 0; i < 3; i++) console.log(ranked[i].trim());
console.log(counts.size + " unique");
