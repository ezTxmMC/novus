sum = 0_i64
i = 0_i64
while i < 10_000_000
  sum += i % 7
  i += 1
end
puts sum
