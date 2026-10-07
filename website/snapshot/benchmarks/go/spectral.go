package main

import (
	"fmt"
	"math"
)

func a(i, j int) float64 { return 1.0 / float64((i+j)*(i+j+1)/2+i+1) }

func mulAv(n int, v, out []float64) {
	for i := 0; i < n; i++ {
		s := 0.0
		for j := 0; j < n; j++ {
			s = s + a(i, j)*v[j]
		}
		out[i] = s
	}
}

func mulAtv(n int, v, out []float64) {
	for i := 0; i < n; i++ {
		s := 0.0
		for j := 0; j < n; j++ {
			s = s + a(j, i)*v[j]
		}
		out[i] = s
	}
}

func mulAtAv(n int, v, out, tmp []float64) {
	mulAv(n, v, tmp)
	mulAtv(n, tmp, out)
}

func main() {
	n := 500
	u := make([]float64, n)
	v := make([]float64, n)
	tmp := make([]float64, n)
	for i := range u {
		u[i] = 1.0
	}
	for i := 0; i < 10; i++ {
		mulAtAv(n, u, v, tmp)
		mulAtAv(n, v, u, tmp)
	}
	vBv, vv := 0.0, 0.0
	for i := 0; i < n; i++ {
		vBv = vBv + u[i]*v[i]
		vv = vv + v[i]*v[i]
	}
	fmt.Printf("%.9f\n", math.Sqrt(vBv/vv))
}
