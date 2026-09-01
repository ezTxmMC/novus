package main

import "fmt"

func isPrime(n int64) bool {
	if n < 2 {
		return false
	}
	for i := int64(2); i*i <= n; i++ {
		if n%i == 0 {
			return false
		}
	}
	return true
}

func main() {
	count := 0
	for n := int64(0); n < 200000; n++ {
		if isPrime(n) {
			count++
		}
	}
	fmt.Println(count)
}
