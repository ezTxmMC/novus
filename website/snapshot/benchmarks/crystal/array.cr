values = [] of Int64
i = 0_i64
while i < 2_000_000
  values << i
  i += 1
end
sum = 0_i64
values.each { |v| sum += v }
puts sum
