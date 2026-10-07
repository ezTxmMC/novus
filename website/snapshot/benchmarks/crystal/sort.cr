values = [] of Int64
seed = 12345_i64
300_000.times do
  seed = seed &* 48271 % 2147483647
  values << seed % 1_000_000
end
sorted = values.sort
puts "#{sorted[0]} #{sorted[sorted.size // 2]} #{sorted[sorted.size - 1]}"
