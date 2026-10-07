class Point {
  constructor(x, y) { this.x = x; this.y = y; }
  sum() { return this.x + this.y; }
}
const points = [];
for (let i = 0; i < 1000000; i++) points.push(new Point(i, i * 2));
let total = 0;
for (const p of points) total += p.sum();
console.log(total);
