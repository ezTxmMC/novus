package main

import (
	"fmt"
	"strconv"
)

func main() {
	counts := map[string]int64{}
	for i := int64(0); i < 300000; i++ {
		counts["key"+strconv.FormatInt(i, 10)] = i
	}
	var sum int64
	for i := int64(0); i < 300000; i++ {
		sum += counts["key"+strconv.FormatInt(i, 10)]
	}
	fmt.Println(len(counts), sum)
}
