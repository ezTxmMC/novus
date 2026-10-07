package main

import "fmt"

func main() {
	values := []int64{}
	for i := int64(0); i < 2000000; i++ {
		values = append(values, i)
	}
	var sum int64
	for _, v := range values {
		sum += v
	}
	fmt.Println(sum)
}
