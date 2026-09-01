def fib(n : Int64) : Int64
  n < 2 ? n : fib(n - 1) + fib(n - 2)
end

puts fib(30_i64)
