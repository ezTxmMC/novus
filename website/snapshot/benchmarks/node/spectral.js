function a(i, j) {
  return 1 / (((i + j) * (i + j + 1)) / 2 + i + 1);
}

function mulAv(n, v, out) {
  for (let i = 0; i < n; i++) {
    let s = 0;
    for (let j = 0; j < n; j++) s = s + a(i, j) * v[j];
    out[i] = s;
  }
}

function mulAtv(n, v, out) {
  for (let i = 0; i < n; i++) {
    let s = 0;
    for (let j = 0; j < n; j++) s = s + a(j, i) * v[j];
    out[i] = s;
  }
}

function mulAtAv(n, v, out, tmp) {
  mulAv(n, v, tmp);
  mulAtv(n, tmp, out);
}

const n = 500;
const u = new Float64Array(n).fill(1);
const v = new Float64Array(n);
const tmp = new Float64Array(n);
for (let i = 0; i < 10; i++) {
  mulAtAv(n, u, v, tmp);
  mulAtAv(n, v, u, tmp);
}
let vBv = 0, vv = 0;
for (let i = 0; i < n; i++) {
  vBv = vBv + u[i] * v[i];
  vv = vv + v[i] * v[i];
}
console.log(Math.sqrt(vBv / vv).toFixed(9));
