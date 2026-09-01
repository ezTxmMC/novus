# Novus examples

250 small programs, from the first line of Novus to complete projects.
Every example is self contained, prints something and is checked against the
`.golden` file next to it.

```sh
novusc run examples/01-basics/001-hello-world.nv   # run a single example
make examples                                      # run all 250
test/run_examples.sh strings                       # only matching paths
```

| Chapter | Examples | Topic |
|---|---|---|
| [Basics](#basics-001-030) | 001-030 | Values, variables, printing and the shape of a program |
| [Control flow](#control-flow-031-060) | 031-060 | Conditions, loops and the patterns built from them |
| [Methods](#methods-061-080) | 061-080 | Parameters, return values, overloading and recursion |
| [Strings](#strings-081-100) | 081-100 | Text handling from single characters to whole documents |
| [Arrays](#arrays-101-130) | 101-130 | Ordered collections, transformations and classic sorts |
| [Maps](#maps-131-150) | 131-150 | Key/value data: counting, grouping, indexing, caching |
| [Classes and objects](#classes-and-objects-151-180) | 151-180 | Fields, constructors, inheritance, interfaces, enums |
| [Standard library](#standard-library-181-215) | 181-215 | One or two examples per module of std/ |
| [Algorithms](#algorithms-216-238) | 216-238 | Searching, sorting, dynamic programming, graphs |
| [Projects](#projects-239-250) | 239-250 | Longer programs that combine everything |

## Basics (001-030)

Values, variables, printing and the shape of a program.

| # | Example | Shows |
|---|---------|-------|
| 001 | [hello-world](01-basics/001-hello-world.nv) | The smallest Novus program: a main method that prints a line |
| 002 | [printing](01-basics/002-printing.nv) | println adds a line break, print does not, eprintln writes to stderr |
| 003 | [variables](01-basics/003-variables.nv) | `var` declares a variable; its type comes from the value |
| 004 | [integers](01-basics/004-integers.nv) | Integers are 64 bit |
| 005 | [floats](01-basics/005-floats.nv) | Floats are doubles and print with six decimals |
| 006 | [strings](01-basics/006-strings.nv) | Strings support + for concatenation and a set of methods |
| 007 | [booleans](01-basics/007-booleans.nv) | Booleans come from comparisons and the logical operators && \|\| ! |
| 008 | [arithmetic](01-basics/008-arithmetic.nv) | Operator precedence follows the usual rules; parentheses override it |
| 009 | [interpolation](01-basics/009-interpolation.nv) | "${expression}" inserts a value into a string |
| 010 | [escapes](01-basics/010-escapes.nv) | Backslash escapes inside string literals |
| 011 | [typed-variables](01-basics/011-typed-variables.nv) | A declared type converts the value: integers truncate, floats widen, strings take the text form |
| 012 | [type-of](01-basics/012-type-of.nv) | typeOf reports the runtime type of a value |
| 013 | [constants](01-basics/013-constants.nv) | Top level constants are visible in every method of the program |
| 014 | [comments](01-basics/014-comments.nv) | A line comment |
| 015 | [chr-ord](01-basics/015-chr-ord.nv) | chr turns a character code into a string, ord does the reverse |
| 016 | [parsing](01-basics/016-parsing.nv) | Text to numbers and back |
| 017 | [arguments](01-basics/017-arguments.nv) | The array parameter of main holds the command line arguments |
| 018 | [exit-code](01-basics/018-exit-code.nv) | The integer returned by main becomes the process exit code |
| 019 | [swap](01-basics/019-swap.nv) | Swapping two variables needs a temporary |
| 020 | [comparisons](01-basics/020-comparisons.nv) | Numbers compare numerically, strings lexicographically |
| 021 | [unary](01-basics/021-unary.nv) | Unary minus and the ! operator |
| 022 | [precision](01-basics/022-precision.nv) | Integer and float arithmetic behave differently |
| 023 | [string-numbers](01-basics/023-string-numbers.nv) | Adding a number to a string concatenates, adding two numbers adds |
| 024 | [blocks](01-basics/024-blocks.nv) | Variables live inside the block that declares them |
| 025 | [multiple-returns](01-basics/025-multiple-returns.nv) | A method can return early; the last value wins |
| 026 | [environment](01-basics/026-environment.nv) | env reads environment variables, platform names the system |
| 027 | [files](01-basics/027-files.nv) | The free builtins read and write files |
| 028 | [truthiness](01-basics/028-truthiness.nv) | Conditions need a boolean - there is no implicit truthiness |
| 029 | [chained-calls](01-basics/029-chained-calls.nv) | Method calls chain from left to right |
| 030 | [formatting-table](01-basics/030-formatting-table.nv) | Building aligned output by hand |

## Control flow (031-060)

Conditions, loops and the patterns built from them.

| # | Example | Shows |
|---|---------|-------|
| 031 | [if-else](02-control/031-if-else.nv) | if / else picks one of two branches |
| 032 | [else-if](02-control/032-else-if.nv) | else if chains test one condition after another |
| 033 | [early-return](02-control/033-early-return.nv) | Guard clauses keep nesting shallow: check, return, continue |
| 034 | [while](02-control/034-while.nv) | while repeats as long as its condition holds |
| 035 | [countdown](02-control/035-countdown.nv) | Counting down with a while loop |
| 036 | [for-in-array](02-control/036-for-in-array.nv) | for..in walks an array element by element |
| 037 | [for-in-map](02-control/037-for-in-map.nv) | Iterating a map yields its keys, always in sorted order |
| 038 | [for-in-string](02-control/038-for-in-string.nv) | Iterating a string yields one character at a time |
| 039 | [break](02-control/039-break.nv) | break leaves the innermost loop immediately |
| 040 | [continue](02-control/040-continue.nv) | continue skips to the next iteration |
| 041 | [nested-loops](02-control/041-nested-loops.nv) | A loop inside a loop; break only leaves the inner one |
| 042 | [counting-loop](02-control/042-counting-loop.nv) | arrays.range gives a numeric range to loop over |
| 043 | [fizzbuzz](02-control/043-fizzbuzz.nv) | The classic: multiples of 3 are Fizz, of 5 Buzz, of both FizzBuzz |
| 044 | [sum-and-average](02-control/044-sum-and-average.nv) | Summing a list and computing its average |
| 045 | [factorial](02-control/045-factorial.nv) | The factorial of a number, computed iteratively |
| 046 | [fibonacci](02-control/046-fibonacci.nv) | Building the Fibonacci sequence with two running values |
| 047 | [multiplication-table](02-control/047-multiplication-table.nv) | A formatted multiplication table |
| 048 | [triangle](02-control/048-triangle.nv) | Drawing triangles with repeated characters |
| 049 | [collatz](02-control/049-collatz.nv) | The Collatz sequence: halve even numbers, triple odd ones and add one |
| 050 | [leap-year](02-control/050-leap-year.nv) |  |
| 051 | [min-max](02-control/051-min-max.nv) | Finding the smallest and largest value in one pass |
| 052 | [digit-sum](02-control/052-digit-sum.nv) | Adding the digits of a number |
| 053 | [reverse-number](02-control/053-reverse-number.nv) | Reversing the digits of a number |
| 054 | [prime-check](02-control/054-prime-check.nv) | Testing a single number for primality |
| 055 | [primes-list](02-control/055-primes-list.nv) | Collecting every prime below a limit |
| 056 | [gcd-lcm](02-control/056-gcd-lcm.nv) | The greatest common divisor with Euclid's algorithm |
| 057 | [power](02-control/057-power.nv) | Fast exponentiation by squaring |
| 058 | [number-guess](02-control/058-number-guess.nv) | Binary search as a guessing game, without any input |
| 059 | [loop-patterns](02-control/059-loop-patterns.nv) | The three common shapes of a loop |
| 060 | [infinite-with-break](02-control/060-infinite-with-break.nv) | while (true) needs a break to end |

## Methods (061-080)

Parameters, return values, overloading and recursion.

| # | Example | Shows |
|---|---------|-------|
| 061 | [defining-methods](03-methods/061-defining-methods.nv) | A method has a name, parameters and an optional return type |
| 062 | [parameters](03-methods/062-parameters.nv) | Parameters are typed; values are converted to the declared type |
| 063 | [void-methods](03-methods/063-void-methods.nv) | A method without a return type performs an action |
| 064 | [overloading-arity](03-methods/064-overloading-arity.nv) | Methods with the same name but a different number of parameters |
| 065 | [overloading-types](03-methods/065-overloading-types.nv) | Overloads with the same arity are chosen by parameter type |
| 066 | [recursion](03-methods/066-recursion.nv) | A method that calls itself needs a case that stops |
| 067 | [recursive-fibonacci](03-methods/067-recursive-fibonacci.nv) | Straightforward but slow: every call branches twice |
| 068 | [mutual-recursion](03-methods/068-mutual-recursion.nv) | Two methods can call each other |
| 069 | [hanoi](03-methods/069-hanoi.nv) | Towers of Hanoi: the classic recursive puzzle |
| 070 | [arrays-are-references](03-methods/070-arrays-are-references.nv) | Arrays are passed by reference: the method changes the caller's array |
| 071 | [maps-are-references](03-methods/071-maps-are-references.nv) | Maps behave the same way |
| 072 | [returning-collections](03-methods/072-returning-collections.nv) | Methods can build and return arrays and maps |
| 073 | [decomposition](03-methods/073-decomposition.nv) | Splitting a task into small methods keeps main readable |
| 074 | [accumulator](03-methods/074-accumulator.nv) | Passing an accumulator through recursive calls |
| 075 | [optional-arguments](03-methods/075-optional-arguments.nv) | Novus has no default values - a shorter overload provides them |
| 076 | [validation](03-methods/076-validation.nv) | Validating input and reporting the problem on stderr |
| 077 | [pure-methods](03-methods/077-pure-methods.nv) | A pure method only depends on its arguments and changes nothing |
| 078 | [call-graph](03-methods/078-call-graph.nv) | Methods calling methods calling methods |
| 079 | [documentation](03-methods/079-documentation.nv) |  |
| 080 | [main-overloads](03-methods/080-main-overloads.nv) | Two entry points: with and without command line arguments |

## Strings (081-100)

Text handling from single characters to whole documents.

| # | Example | Shows |
|---|---------|-------|
| 081 | [length-and-chars](04-strings/081-length-and-chars.nv) | The length of a string and its individual characters |
| 082 | [substring](04-strings/082-substring.nv) | substring(start, end) - end is exclusive and clamped to the length |
| 083 | [searching](04-strings/083-searching.nv) |  |
| 084 | [split-and-join](04-strings/084-split-and-join.nv) | Splitting text into parts and joining it back together |
| 085 | [replace-and-trim](04-strings/085-replace-and-trim.nv) | Replacing substrings and removing surrounding whitespace |
| 086 | [case](04-strings/086-case.nv) | Upper case, lower case and capitalization |
| 087 | [reverse](04-strings/087-reverse.nv) |  |
| 088 | [palindrome](04-strings/088-palindrome.nv) |  |
| 089 | [counting](04-strings/089-counting.nv) |  |
| 090 | [char-frequency](04-strings/090-char-frequency.nv) |  |
| 091 | [word-count](04-strings/091-word-count.nv) |  |
| 092 | [title-case](04-strings/092-title-case.nv) |  |
| 093 | [padding](04-strings/093-padding.nv) | Padding and repeating text for aligned output |
| 094 | [truncate](04-strings/094-truncate.nv) | Shortening text and stripping prefixes or suffixes |
| 095 | [template](04-strings/095-template.nv) | Filling placeholders in a template |
| 096 | [caesar](04-strings/096-caesar.nv) | A Caesar cipher shifts every letter by a fixed amount |
| 097 | [anagram](04-strings/097-anagram.nv) |  |
| 098 | [vowels](04-strings/098-vowels.nv) |  |
| 099 | [key-value](04-strings/099-key-value.nv) | Parsing simple key=value configuration lines |
| 100 | [slugify](04-strings/100-slugify.nv) | Turning a title into a URL slug |

## Arrays (101-130)

Ordered collections, transformations and classic sorts.

| # | Example | Shows |
|---|---------|-------|
| 101 | [array-basics](05-arrays/101-array-basics.nv) | Creating an array, reading and replacing its elements |
| 102 | [append-and-pop](05-arrays/102-append-and-pop.nv) | Growing and shrinking an array at the end |
| 103 | [insert-and-remove](05-arrays/103-insert-and-remove.nv) | Inserting and removing elements at any position |
| 104 | [search](05-arrays/104-search.nv) | Looking for an element with contains and indexOf |
| 105 | [iterate](05-arrays/105-iterate.nv) | Two ways to walk an array: by element and by index |
| 106 | [sum-min-max](05-arrays/106-sum-min-max.nv) | Aggregating numbers with the arrays module |
| 107 | [sorting](05-arrays/107-sorting.nv) | Sorting numbers, floats and strings |
| 108 | [reverse-unique](05-arrays/108-reverse-unique.nv) | Reversing, deduplicating and counting elements |
| 109 | [slice-and-chunk](05-arrays/109-slice-and-chunk.nv) | Taking parts of an array and splitting it into chunks |
| 110 | [filter](05-arrays/110-filter.nv) | Filtering without lambdas: a loop and a helper method |
| 111 | [map-transform](05-arrays/111-map-transform.nv) |  |
| 112 | [reduce](05-arrays/112-reduce.nv) | Folding an array into a single value |
| 113 | [two-dimensional](05-arrays/113-two-dimensional.nv) | Arrays of arrays as a grid |
| 114 | [matrix-transpose](05-arrays/114-matrix-transpose.nv) | Swapping the rows and columns of a matrix |
| 115 | [matrix-multiply](05-arrays/115-matrix-multiply.nv) | Multiplying two matrices |
| 116 | [bubble-sort](05-arrays/116-bubble-sort.nv) | Bubble sort: repeatedly swap neighbours that are out of order |
| 117 | [selection-sort](05-arrays/117-selection-sort.nv) | Selection sort: repeatedly move the smallest element to the front |
| 118 | [insertion-sort](05-arrays/118-insertion-sort.nv) | Insertion sort: place every value where it belongs |
| 119 | [binary-search](05-arrays/119-binary-search.nv) | Binary search halves the range with every step |
| 120 | [merge-sorted](05-arrays/120-merge-sorted.nv) | Merging two sorted arrays into one |
| 121 | [stack](05-arrays/121-stack.nv) | An array used as a stack: append pushes, pop takes from the end |
| 122 | [queue](05-arrays/122-queue.nv) | A queue: append at the end, remove(0) from the front |
| 123 | [rotate](05-arrays/123-rotate.nv) |  |
| 124 | [zip](05-arrays/124-zip.nv) |  |
| 125 | [flatten](05-arrays/125-flatten.nv) |  |
| 126 | [partition](05-arrays/126-partition.nv) | Splitting values into two groups |
| 127 | [running-total](05-arrays/127-running-total.nv) | A running total alongside the original values |
| 128 | [deduplicate-keep-order](05-arrays/128-deduplicate-keep-order.nv) |  |
| 129 | [array-of-maps](05-arrays/129-array-of-maps.nv) |  |
| 130 | [copy-vs-reference](05-arrays/130-copy-vs-reference.nv) | Assigning an array shares it; arrays.copy makes an independent one |

## Maps (131-150)

Key/value data: counting, grouping, indexing, caching.

| # | Example | Shows |
|---|---------|-------|
| 131 | [map-basics](06-maps/131-map-basics.nv) | Creating a map, reading, adding and testing keys |
| 132 | [keys-and-values](06-maps/132-keys-and-values.nv) | The keys and values of a map, always in key order |
| 133 | [remove-and-default](06-maps/133-remove-and-default.nv) | Removing entries and reading with a fallback |
| 134 | [counting](06-maps/134-counting.nv) | Counting how often each word appears |
| 135 | [grouping](06-maps/135-grouping.nv) | Grouping values under a common key |
| 136 | [inverting](06-maps/136-inverting.nv) | Inverting, merging and listing map entries |
| 137 | [nested-maps](06-maps/137-nested-maps.nv) | Maps inside maps for structured configuration |
| 138 | [set](06-maps/138-set.nv) | A map with boolean values works as a set |
| 139 | [lookup-table](06-maps/139-lookup-table.nv) | Replacing an if chain with a lookup table |
| 140 | [memoization](06-maps/140-memoization.nv) | Caching results in a map turns exponential recursion into linear work |
| 141 | [index-building](06-maps/141-index-building.nv) | Building an index from words to the lines that contain them |
| 142 | [map-of-arrays](06-maps/142-map-of-arrays.nv) | A map whose values are arrays |
| 143 | [sorted-by-value](06-maps/143-sorted-by-value.nv) | Maps iterate by key; sorting by value needs a little work |
| 144 | [merging-defaults](06-maps/144-merging-defaults.nv) | User settings on top of defaults |
| 145 | [map-keys-are-strings](06-maps/145-map-keys-are-strings.nv) | Every key is stored as text, so 7 and "7" are the same key |
| 146 | [frequency-top](06-maps/146-frequency-top.nv) | Finding the most frequent element |
| 147 | [map-copy](06-maps/147-map-copy.nv) | Sharing a map versus copying it |
| 148 | [records](06-maps/148-records.nv) | Using maps as lightweight records |
| 149 | [from-pairs](06-maps/149-from-pairs.nv) | Building a map from two arrays |
| 150 | [switch-table](06-maps/150-switch-table.nv) | A dispatch table: command name to a description |

## Classes and objects (151-180)

Fields, constructors, inheritance, interfaces, enums.

| # | Example | Shows |
|---|---------|-------|
| 151 | [first-class](07-classes/151-first-class.nv) | A class groups data (fields) and behaviour (methods) |
| 152 | [fields-and-accessors](07-classes/152-fields-and-accessors.nv) | `: get` creates a reader, `: get, set` a reader and a writer |
| 153 | [methods-on-objects](07-classes/153-methods-on-objects.nv) | Methods that compute from the fields of an object |
| 154 | [implicit-this](07-classes/154-implicit-this.nv) | Inside a class, fields and methods can be used without `this.` |
| 155 | [object-literals](07-classes/155-object-literals.nv) | Name{field=value} builds an object without calling a constructor |
| 156 | [constructors](07-classes/156-constructors.nv) | The constructor runs when the object is created |
| 157 | [inheritance](07-classes/157-inheritance.nv) | A base class with subclasses that override a method |
| 158 | [polymorphism](07-classes/158-polymorphism.nv) | One loop, many types: every shape answers area() |
| 159 | [abstract-class](07-classes/159-abstract-class.nv) | An abstract class cannot be created; subclasses fill in the gaps |
| 160 | [interfaces](07-classes/160-interfaces.nv) | An interface lists methods a class must implement |
| 161 | [enums](07-classes/161-enums.nv) | An enum is a fixed set of named values |
| 162 | [enums-with-fields](07-classes/162-enums-with-fields.nv) | Enum constants can carry data and methods |
| 163 | [enum-state-machine](07-classes/163-enum-state-machine.nv) |  |
| 164 | [composition](07-classes/164-composition.nv) | An object can hold other objects |
| 165 | [objects-in-arrays](07-classes/165-objects-in-arrays.nv) | Keeping objects in an array and updating them |
| 166 | [objects-in-maps](07-classes/166-objects-in-maps.nv) | Looking objects up by key |
| 167 | [equality](07-classes/167-equality.nv) | Two objects are equal only when they are the same instance |
| 168 | [mutation](07-classes/168-mutation.nv) | Objects are references: changing one changes every name for it |
| 169 | [linked-list](07-classes/169-linked-list.nv) | A linked list built from nodes that point at each other |
| 170 | [stack-class](07-classes/170-stack-class.nv) | A stack with push, pop and peek |
| 171 | [queue-class](07-classes/171-queue-class.nv) | A queue: first in, first out |
| 172 | [binary-tree](07-classes/172-binary-tree.nv) | A binary search tree with in-order traversal |
| 173 | [annotations](07-classes/173-annotations.nv) | @Deprecated warns the first time the method is called |
| 174 | [interface-checks](07-classes/174-interface-checks.nv) | A class that implements an interface must define every method |
| 175 | [inheritance-chain](07-classes/175-inheritance-chain.nv) | Three levels of inheritance and method overriding |
| 176 | [fields-of-fields](07-classes/176-fields-of-fields.nv) | Reaching through one object into another |
| 177 | [builder](07-classes/177-builder.nv) | A builder collects values and creates the final object at the end |
| 178 | [bank-account](07-classes/178-bank-account.nv) | An account that validates deposits and withdrawals |
| 179 | [vector](07-classes/179-vector.nv) | A two dimensional vector with arithmetic |
| 180 | [inventory](07-classes/180-inventory.nv) | Objects stored in a map and summed up |

## Standard library (181-215)

One or two examples per module of std/.

| # | Example | Shows |
|---|---------|-------|
| 181 | [os-files](08-stdlib/181-os-files.nv) | Reading, writing, appending and deleting files |
| 182 | [os-directories](08-stdlib/182-os-directories.nv) | Creating and removing directory trees |
| 183 | [os-listing](08-stdlib/183-os-listing.nv) | Walking a directory tree recursively |
| 184 | [os-file-info](08-stdlib/184-os-file-info.nv) | Size and modification time of a file |
| 185 | [os-copy-rename](08-stdlib/185-os-copy-rename.nv) | Copying, renaming and deleting files |
| 186 | [os-exec](08-stdlib/186-os-exec.nv) | exec runs a command and returns its exit code |
| 187 | [os-output](08-stdlib/187-os-output.nv) | output captures what a command prints |
| 188 | [os-environment](08-stdlib/188-os-environment.nv) |  |
| 189 | [path-parts](08-stdlib/189-path-parts.nv) | Taking a path apart: directory, name, stem, extension |
| 190 | [path-join](08-stdlib/190-path-join.nv) | Building paths from segments |
| 191 | [path-normalize](08-stdlib/191-path-normalize.nv) | Collapsing  |
| 192 | [path-relative](08-stdlib/192-path-relative.nv) | The path from one directory to another |
| 193 | [json-stringify](08-stdlib/193-json-stringify.nv) | Turning values into JSON text |
| 194 | [json-parse](08-stdlib/194-json-parse.nv) | Reading JSON text into maps and arrays |
| 195 | [json-round-trip](08-stdlib/195-json-round-trip.nv) | Serializing and parsing back again |
| 196 | [json-file](08-stdlib/196-json-file.nv) | Saving and loading a JSON file |
| 197 | [http-request](08-stdlib/197-http-request.nv) | request never aborts: it reports transport problems in the result |
| 198 | [strings-module](08-stdlib/198-strings-module.nv) | The helpers of the strings module |
| 199 | [arrays-module](08-stdlib/199-arrays-module.nv) | The helpers of the arrays module |
| 200 | [maps-module](08-stdlib/200-maps-module.nv) | The helpers of the maps module |
| 201 | [math-functions](08-stdlib/201-math-functions.nv) | Square roots, powers and rounding |
| 202 | [math-integers](08-stdlib/202-math-integers.nv) | Divisors, primes and integer helpers |
| 203 | [math-rounding](08-stdlib/203-math-rounding.nv) | Rounding to decimals and converting numbers |
| 204 | [time-format](08-stdlib/204-time-format.nv) | Fixed timestamps keep the output stable |
| 205 | [time-duration](08-stdlib/205-time-duration.nv) | Human readable durations and measuring time |
| 206 | [random-seeded](08-stdlib/206-random-seeded.nv) | Seeding makes the sequence reproducible |
| 207 | [random-collections](08-stdlib/207-random-collections.nv) | Shuffling, picking and random text |
| 208 | [fmt-numbers](08-stdlib/208-fmt-numbers.nv) | Formatting numbers, sizes and percentages |
| 209 | [fmt-table](08-stdlib/209-fmt-table.nv) | A two column table |
| 210 | [log-levels](08-stdlib/210-log-levels.nv) | Log output goes to stderr; timestamps are off here so the output is stable |
| 211 | [cli-arguments](08-stdlib/211-cli-arguments.nv) | Parsing arguments, options and flags |
| 212 | [base64](08-stdlib/212-base64.nv) | Base64 encoding and decoding |
| 213 | [hashing](08-stdlib/213-hashing.nv) | Hashing text with FNV-1a and CRC32 |
| 214 | [csv](08-stdlib/214-csv.nv) | Reading and writing CSV with quoting |
| 215 | [io-and-test](08-stdlib/215-io-and-test.nv) | io.write prints without a line break; readLine/readLines take input |

## Algorithms (216-238)

Searching, sorting, dynamic programming, graphs.

| # | Example | Shows |
|---|---------|-------|
| 216 | [search-comparison](09-algorithms/216-search-comparison.nv) | Linear search versus binary search |
| 217 | [quicksort](09-algorithms/217-quicksort.nv) | Quicksort: partition around a pivot and recurse |
| 218 | [merge-sort](09-algorithms/218-merge-sort.nv) | Merge sort: split, sort the halves, merge them |
| 219 | [counting-sort](09-algorithms/219-counting-sort.nv) | Sorting small integers by counting how often each value occurs |
| 220 | [levenshtein](09-algorithms/220-levenshtein.nv) | Edit distance: how many changes turn one word into another |
| 221 | [common-prefix](09-algorithms/221-common-prefix.nv) | The longest prefix shared by a list of words |
| 222 | [run-length-encoding](09-algorithms/222-run-length-encoding.nv) | Compressing runs of repeated characters |
| 223 | [roman-numerals](09-algorithms/223-roman-numerals.nv) | Converting numbers to roman numerals and back |
| 224 | [base-conversion](09-algorithms/224-base-conversion.nv) | Numbers in binary, octal and hexadecimal |
| 225 | [sieve](09-algorithms/225-sieve.nv) | The sieve of Eratosthenes marks multiples instead of testing divisors |
| 226 | [perfect-numbers](09-algorithms/226-perfect-numbers.nv) | Numbers that equal the sum of their divisors |
| 227 | [pascal-triangle](09-algorithms/227-pascal-triangle.nv) | Pascal's triangle, row by row |
| 228 | [magic-square](09-algorithms/228-magic-square.nv) | Checking whether every row, column and diagonal sums to the same value |
| 229 | [n-queens](09-algorithms/229-n-queens.nv) | Counting the ways to place N queens without attacks (backtracking) |
| 230 | [knapsack](09-algorithms/230-knapsack.nv) | The 0/1 knapsack problem solved with dynamic programming |
| 231 | [coin-change](09-algorithms/231-coin-change.nv) | The fewest coins that make an amount |
| 232 | [breadth-first-search](09-algorithms/232-breadth-first-search.nv) | Shortest path in an unweighted graph |
| 233 | [depth-first-search](09-algorithms/233-depth-first-search.nv) | Walking a graph as deep as possible first |
| 234 | [topological-sort](09-algorithms/234-topological-sort.nv) | Ordering tasks so that every dependency comes first |
| 235 | [dijkstra](09-algorithms/235-dijkstra.nv) | Shortest path with weights: always expand the cheapest known node |
| 236 | [game-of-life](09-algorithms/236-game-of-life.nv) | Conway's game of life on a small fixed board |
| 237 | [lru-cache](09-algorithms/237-lru-cache.nv) | A least recently used cache built from a map and an order list |
| 238 | [frequency-ranking](09-algorithms/238-frequency-ranking.nv) | Ranking words by frequency, breaking ties alphabetically |

## Projects (239-250)

Longer programs that combine everything.

| # | Example | Shows |
|---|---------|-------|
| 239 | [rpn-calculator](10-projects/239-rpn-calculator.nv) | A calculator for reverse polish notation: "3 4 + 2 *" is (3 + 4) * 2 |
| 240 | [tokenizer](10-projects/240-tokenizer.nv) | Splitting source text into tokens - the first step of every compiler |
| 241 | [expression-parser](10-projects/241-expression-parser.nv) | A recursive descent parser and evaluator for + - * / and parentheses |
| 242 | [ini-parser](10-projects/242-ini-parser.nv) | Reading INI style configuration into nested maps |
| 243 | [markdown-to-html](10-projects/243-markdown-to-html.nv) | A small subset of markdown: headings, bullets, bold and paragraphs |
| 244 | [template-engine](10-projects/244-template-engine.nv) | A template engine with variables and a repeat block |
| 245 | [log-analyzer](10-projects/245-log-analyzer.nv) | Counting log levels and finding the slowest request |
| 246 | [text-statistics](10-projects/246-text-statistics.nv) |  |
| 247 | [todo-cli](10-projects/247-todo-cli.nv) | A small todo list stored as JSON - run it with: add "text" \| list \| done 0 |
| 248 | [bank-system](10-projects/248-bank-system.nv) |  |
| 249 | [tic-tac-toe](10-projects/249-tic-tac-toe.nv) | Playing a fixed sequence of moves and detecting the winner |
| 250 | [stack-machine](10-projects/250-stack-machine.nv) | A tiny virtual machine: the last example puts everything together |

## Showcase projects

Larger programs with a directory of their own:

| Project | Shows | Run with |
|---|---|---|
| [shapes](shapes/main.nv) | Interfaces, abstract classes, inheritance, polymorphism, enums | `novusc run examples/shapes/main.nv` |
| [todo](todo/main.nv) | CLI arguments and JSON persistence | `novusc run examples/todo/main.nv add "buy milk"` |
| [wordcount](wordcount/main.nv) | Files, maps and string processing | `novusc run examples/wordcount/main.nv file.txt` |
| [mccloud](mccloud/README.md) | A multi-file project: classes, `os`/`path`/`json`, `screen` and Windows consoles | `novusc build examples/mccloud/main.nv && ./mccloud init` |

## Notes

- Examples that write files clean up after themselves, and none of them needs a network.
- Output is deterministic: random numbers are seeded, timestamps are fixed.
- `${...}` cannot contain a string literal - put it in a variable first.
- `test/run_examples.sh --update` regenerates the golden files after a change.
- This file is generated: `novusc run tools/exampleindex.nv > examples/README.md`
