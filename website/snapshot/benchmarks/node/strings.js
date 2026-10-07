const parts = [];
for (let i = 0; i < 200000; i++) parts.push("word" + (i % 10) + " ");
const text = parts.join("").trim();
const words = text.split(" ");
const joined = words.join("-");
console.log(text.length, words.length, joined.length);
