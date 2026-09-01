package main

import (
	"fmt"
	"strconv"
	"strings"
)

func main() {
	var builder strings.Builder
	for i := 0; i < 200000; i++ {
		builder.WriteString("word")
		builder.WriteString(strconv.Itoa(i % 10))
		builder.WriteString(" ")
	}
	text := strings.TrimSpace(builder.String())
	words := strings.Split(text, " ")
	joined := strings.Join(words, "-")
	fmt.Println(len(text), len(words), len(joined))
}
