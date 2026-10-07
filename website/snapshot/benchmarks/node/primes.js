function isPrime(n) {
  if (n < 2) return false;
  for (let i = 2; i * i <= n; i++) if (n % i === 0) return false;
  return true;
}
let count = 0;
for (let n = 0; n < 200000; n++) if (isPrime(n)) count++;
console.log(count);
