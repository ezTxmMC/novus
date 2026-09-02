package main

import (
	"fmt"
	"math"
)

type Body struct{ x, y, z, vx, vy, vz, mass float64 }

const pi = 3.141592653589793
const solarMass = 4.0 * pi * pi
const daysPerYear = 365.24

func energy(bodies []Body) float64 {
	e := 0.0
	for i := range bodies {
		b := &bodies[i]
		e = e + 0.5*b.mass*(b.vx*b.vx+b.vy*b.vy+b.vz*b.vz)
		for j := i + 1; j < len(bodies); j++ {
			c := &bodies[j]
			dx, dy, dz := b.x-c.x, b.y-c.y, b.z-c.z
			e = e - b.mass*c.mass/math.Sqrt(dx*dx+dy*dy+dz*dz)
		}
	}
	return e
}

func advance(bodies []Body, dt float64) {
	n := len(bodies)
	for i := 0; i < n; i++ {
		b := &bodies[i]
		for j := i + 1; j < n; j++ {
			c := &bodies[j]
			dx, dy, dz := b.x-c.x, b.y-c.y, b.z-c.z
			d2 := dx*dx + dy*dy + dz*dz
			mag := dt / (d2 * math.Sqrt(d2))
			b.vx = b.vx - dx*c.mass*mag
			b.vy = b.vy - dy*c.mass*mag
			b.vz = b.vz - dz*c.mass*mag
			c.vx = c.vx + dx*b.mass*mag
			c.vy = c.vy + dy*b.mass*mag
			c.vz = c.vz + dz*b.mass*mag
		}
	}
	for i := range bodies {
		b := &bodies[i]
		b.x = b.x + dt*b.vx
		b.y = b.y + dt*b.vy
		b.z = b.z + dt*b.vz
	}
}

func main() {
	bodies := []Body{
		{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, solarMass},
		{4.84143144246472090, -1.16032004402742839, -0.103622044471123109,
			0.00166007664274403694 * daysPerYear, 0.00769901118419740425 * daysPerYear,
			-0.0000690460016972063023 * daysPerYear, 0.000954791938424326609 * solarMass},
		{8.34336671824457987, 4.12479856412430479, -0.403523417114321381,
			-0.00276742510726862411 * daysPerYear, 0.00499852801234917238 * daysPerYear,
			0.0000230417297573763929 * daysPerYear, 0.000285885980666130812 * solarMass},
		{12.8943695621391310, -15.1111514016986312, -0.223307578892655734,
			0.00296460137564761618 * daysPerYear, 0.00237847173959480950 * daysPerYear,
			-0.0000296589568540237556 * daysPerYear, 0.0000436624404335156298 * solarMass},
		{15.3796971148509165, -25.9193146099879641, 0.179258772950371181,
			0.00268067772490389322 * daysPerYear, 0.00162824170038242295 * daysPerYear,
			-0.0000951592254519715870 * daysPerYear, 0.0000515138902046611451 * solarMass},
	}
	px, py, pz := 0.0, 0.0, 0.0
	for i := range bodies {
		b := &bodies[i]
		px = px + b.vx*b.mass
		py = py + b.vy*b.mass
		pz = pz + b.vz*b.mass
	}
	bodies[0].vx = -px / solarMass
	bodies[0].vy = -py / solarMass
	bodies[0].vz = -pz / solarMass
	fmt.Printf("%.9f\n", energy(bodies))
	for i := 0; i < 1000000; i++ {
		advance(bodies, 0.01)
	}
	fmt.Printf("%.9f\n", energy(bodies))
}
