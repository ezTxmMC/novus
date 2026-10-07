package main

import "fmt"

func main() {
	var sum int64
	for i := int64(0); i < 10000000; i++ {
		sum += i % 7
	}
	fmt.Println(sum)
}
