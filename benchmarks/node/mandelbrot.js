let inside = 0;
for (let py = 0; py < 400; py++) {
  const y = (py / 400) * 2 - 1;
  for (let px = 0; px < 400; px++) {
    const x = (px / 400) * 3 - 2;
    let zx = 0, zy = 0, i = 0;
    while (i < 50 && zx * zx + zy * zy <= 4) {
      const t = zx * zx - zy * zy + x;
      zy = 2 * zx * zy + y;
      zx = t;
      i++;
    }
    if (i === 50) inside++;
  }
}
console.log(inside);
