package main

import (
	"fmt"
	"sort"
)

func main() {
	values := []int64{}
	seed := int64(12345)
	for i := 0; i < 300000; i++ {
		seed = seed * 48271 % 2147483647
		values = append(values, seed%1000000)
	}
	sort.Slice(values, func(a, b int) bool { return values[a] < values[b] })
	fmt.Println(values[0], values[len(values)/2], values[len(values)-1])
}
