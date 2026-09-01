counts = {} of String => Int64
i = 0_i64
while i < 300_000
  counts["key#{i}"] = i
  i += 1
end
sum = 0_i64
i = 0_i64
while i < 300_000
  sum += counts["key#{i}"]
  i += 1
end
puts "#{counts.size} #{sum}"
