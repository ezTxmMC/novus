const values = [];
for (let i = 0; i < 2000000; i++) values.push(i);
let sum = 0;
for (const v of values) sum += v;
console.log(sum);
