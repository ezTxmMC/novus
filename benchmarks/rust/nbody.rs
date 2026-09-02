struct Body { x: f64, y: f64, z: f64, vx: f64, vy: f64, vz: f64, mass: f64 }

const PI: f64 = 3.141592653589793;
const SOLAR_MASS: f64 = 4.0 * PI * PI;
const DAYS_PER_YEAR: f64 = 365.24;

fn energy(bodies: &[Body]) -> f64 {
    let mut e = 0.0;
    for i in 0..bodies.len() {
        let b = &bodies[i];
        e = e + 0.5 * b.mass * (b.vx * b.vx + b.vy * b.vy + b.vz * b.vz);
        for c in &bodies[i + 1..] {
            let (dx, dy, dz) = (b.x - c.x, b.y - c.y, b.z - c.z);
            e = e - b.mass * c.mass / (dx * dx + dy * dy + dz * dz).sqrt();
        }
    }
    e
}

fn advance(bodies: &mut [Body], dt: f64) {
    let n = bodies.len();
    for i in 0..n {
        for j in i + 1..n {
            let (dx, dy, dz) = (bodies[i].x - bodies[j].x, bodies[i].y - bodies[j].y, bodies[i].z - bodies[j].z);
            let d2 = dx * dx + dy * dy + dz * dz;
            let mag = dt / (d2 * d2.sqrt());
            let (bm, cm) = (bodies[i].mass, bodies[j].mass);
            bodies[i].vx = bodies[i].vx - dx * cm * mag;
            bodies[i].vy = bodies[i].vy - dy * cm * mag;
            bodies[i].vz = bodies[i].vz - dz * cm * mag;
            bodies[j].vx = bodies[j].vx + dx * bm * mag;
            bodies[j].vy = bodies[j].vy + dy * bm * mag;
            bodies[j].vz = bodies[j].vz + dz * bm * mag;
        }
    }
    for b in bodies.iter_mut() {
        b.x = b.x + dt * b.vx;
        b.y = b.y + dt * b.vy;
        b.z = b.z + dt * b.vz;
    }
}

fn main() {
    let mut bodies = vec![
        Body { x: 0.0, y: 0.0, z: 0.0, vx: 0.0, vy: 0.0, vz: 0.0, mass: SOLAR_MASS },
        Body { x: 4.84143144246472090, y: -1.16032004402742839, z: -0.103622044471123109,
               vx: 0.00166007664274403694 * DAYS_PER_YEAR, vy: 0.00769901118419740425 * DAYS_PER_YEAR,
               vz: -0.0000690460016972063023 * DAYS_PER_YEAR, mass: 0.000954791938424326609 * SOLAR_MASS },
        Body { x: 8.34336671824457987, y: 4.12479856412430479, z: -0.403523417114321381,
               vx: -0.00276742510726862411 * DAYS_PER_YEAR, vy: 0.00499852801234917238 * DAYS_PER_YEAR,
               vz: 0.0000230417297573763929 * DAYS_PER_YEAR, mass: 0.000285885980666130812 * SOLAR_MASS },
        Body { x: 12.8943695621391310, y: -15.1111514016986312, z: -0.223307578892655734,
               vx: 0.00296460137564761618 * DAYS_PER_YEAR, vy: 0.00237847173959480950 * DAYS_PER_YEAR,
               vz: -0.0000296589568540237556 * DAYS_PER_YEAR, mass: 0.0000436624404335156298 * SOLAR_MASS },
        Body { x: 15.3796971148509165, y: -25.9193146099879641, z: 0.179258772950371181,
               vx: 0.00268067772490389322 * DAYS_PER_YEAR, vy: 0.00162824170038242295 * DAYS_PER_YEAR,
               vz: -0.0000951592254519715870 * DAYS_PER_YEAR, mass: 0.0000515138902046611451 * SOLAR_MASS },
    ];
    let (mut px, mut py, mut pz) = (0.0, 0.0, 0.0);
    for b in &bodies {
        px = px + b.vx * b.mass;
        py = py + b.vy * b.mass;
        pz = pz + b.vz * b.mass;
    }
    bodies[0].vx = -px / SOLAR_MASS;
    bodies[0].vy = -py / SOLAR_MASS;
    bodies[0].vz = -pz / SOLAR_MASS;
    println!("{:.9}", energy(&bodies));
    for _ in 0..1000000 { advance(&mut bodies, 0.01); }
    println!("{:.9}", energy(&bodies));
}
