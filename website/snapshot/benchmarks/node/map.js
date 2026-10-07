const counts = new Map();
for (let i = 0; i < 300000; i++) counts.set("key" + i, i);
let sum = 0;
for (let i = 0; i < 300000; i++) sum += counts.get("key" + i);
console.log(counts.size, sum);
