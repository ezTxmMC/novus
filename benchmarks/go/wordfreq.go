package main

import (
	"fmt"
	"os"
	"sort"
	"strings"
)

func main() {
	data, _ := os.ReadFile("input.txt")
	counts := map[string]int64{}
	for _, word := range strings.Fields(string(data)) {
		counts[word]++
	}
	ranked := make([]string, 0, len(counts))
	for word, count := range counts {
		ranked = append(ranked, fmt.Sprintf("%8d %s", count, word))
	}
	sort.Sort(sort.Reverse(sort.StringSlice(ranked)))
	for i := 0; i < 3; i++ {
		fmt.Println(strings.TrimSpace(ranked[i]))
	}
	fmt.Println(len(counts), "unique")
}
