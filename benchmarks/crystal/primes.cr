def prime?(n : Int64) : Bool
  return false if n < 2
  i = 2_i64
  while i * i <= n
    return false if n % i == 0
    i += 1
  end
  true
end

count = 0
n = 0_i64
while n < 200_000
  count += 1 if prime?(n)
  n += 1
end
puts count
