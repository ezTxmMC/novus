const PI = 3.141592653589793;
const SOLAR_MASS = 4 * PI * PI;
const DAYS_PER_YEAR = 365.24;

class Body {
  constructor(x, y, z, vx, vy, vz, mass) {
    this.x = x; this.y = y; this.z = z;
    this.vx = vx; this.vy = vy; this.vz = vz;
    this.mass = mass;
  }
}

function energy(bodies) {
  let e = 0;
  for (let i = 0; i < bodies.length; i++) {
    const b = bodies[i];
    e = e + 0.5 * b.mass * (b.vx * b.vx + b.vy * b.vy + b.vz * b.vz);
    for (let j = i + 1; j < bodies.length; j++) {
      const c = bodies[j];
      const dx = b.x - c.x, dy = b.y - c.y, dz = b.z - c.z;
      e = e - b.mass * c.mass / Math.sqrt(dx * dx + dy * dy + dz * dz);
    }
  }
  return e;
}

function advance(bodies, dt) {
  const n = bodies.length;
  for (let i = 0; i < n; i++) {
    const b = bodies[i];
    for (let j = i + 1; j < n; j++) {
      const c = bodies[j];
      const dx = b.x - c.x, dy = b.y - c.y, dz = b.z - c.z;
      const d2 = dx * dx + dy * dy + dz * dz;
      const mag = dt / (d2 * Math.sqrt(d2));
      b.vx = b.vx - dx * c.mass * mag;
      b.vy = b.vy - dy * c.mass * mag;
      b.vz = b.vz - dz * c.mass * mag;
      c.vx = c.vx + dx * b.mass * mag;
      c.vy = c.vy + dy * b.mass * mag;
      c.vz = c.vz + dz * b.mass * mag;
    }
  }
  for (const b of bodies) {
    b.x = b.x + dt * b.vx;
    b.y = b.y + dt * b.vy;
    b.z = b.z + dt * b.vz;
  }
}

const bodies = [
  new Body(0, 0, 0, 0, 0, 0, SOLAR_MASS),
  new Body(4.84143144246472090, -1.16032004402742839, -0.103622044471123109,
    0.00166007664274403694 * DAYS_PER_YEAR, 0.00769901118419740425 * DAYS_PER_YEAR,
    -0.0000690460016972063023 * DAYS_PER_YEAR, 0.000954791938424326609 * SOLAR_MASS),
  new Body(8.34336671824457987, 4.12479856412430479, -0.403523417114321381,
    -0.00276742510726862411 * DAYS_PER_YEAR, 0.00499852801234917238 * DAYS_PER_YEAR,
    0.0000230417297573763929 * DAYS_PER_YEAR, 0.000285885980666130812 * SOLAR_MASS),
  new Body(12.8943695621391310, -15.1111514016986312, -0.223307578892655734,
    0.00296460137564761618 * DAYS_PER_YEAR, 0.00237847173959480950 * DAYS_PER_YEAR,
    -0.0000296589568540237556 * DAYS_PER_YEAR, 0.0000436624404335156298 * SOLAR_MASS),
  new Body(15.3796971148509165, -25.9193146099879641, 0.179258772950371181,
    0.00268067772490389322 * DAYS_PER_YEAR, 0.00162824170038242295 * DAYS_PER_YEAR,
    -0.0000951592254519715870 * DAYS_PER_YEAR, 0.0000515138902046611451 * SOLAR_MASS),
];
let px = 0, py = 0, pz = 0;
for (const b of bodies) {
  px = px + b.vx * b.mass;
  py = py + b.vy * b.mass;
  pz = pz + b.vz * b.mass;
}
bodies[0].vx = -px / SOLAR_MASS;
bodies[0].vy = -py / SOLAR_MASS;
bodies[0].vz = -pz / SOLAR_MASS;
console.log(energy(bodies).toFixed(9));
for (let i = 0; i < 1000000; i++) advance(bodies, 0.01);
console.log(energy(bodies).toFixed(9));
