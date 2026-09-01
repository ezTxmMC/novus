package main

import "fmt"

func main() {
	inside := 0
	for py := 0; py < 400; py++ {
		y := (float64(py)/400.0)*2.0 - 1.0
		for px := 0; px < 400; px++ {
			x := (float64(px)/400.0)*3.0 - 2.0
			zx, zy := 0.0, 0.0
			i := 0
			for i < 50 && zx*zx+zy*zy <= 4.0 {
				t := zx*zx - zy*zy + x
				zy = 2.0*zx*zy + y
				zx = t
				i++
			}
			if i == 50 {
				inside++
			}
		}
	}
	fmt.Println(inside)
}
