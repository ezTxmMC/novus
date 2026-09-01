package main

import "fmt"

type Point struct{ x, y int64 }

func (p *Point) Sum() int64 { return p.x + p.y }

func main() {
	points := []*Point{}
	for i := int64(0); i < 1000000; i++ {
		points = append(points, &Point{x: i, y: i * 2})
	}
	var total int64
	for _, p := range points {
		total += p.Sum()
	}
	fmt.Println(total)
}
