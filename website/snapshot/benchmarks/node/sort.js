const values = [];
let seed = 12345;
for (let i = 0; i < 300000; i++) {
  seed = seed * 48271 % 2147483647;
  values.push(seed % 1000000);
}
values.sort((a, b) => a - b);
console.log(values[0], values[values.length >> 1], values[values.length - 1]);
