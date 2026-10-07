text = File.read("input.txt")
counts = Hash(String, Int64).new(0_i64)
text.split { |word| counts[word] += 1 }
ranked = counts.map { |word, count| "%8d %s" % [count, word] }
ranked.sort!
ranked.reverse!
3.times { |i| puts ranked[i].strip }
puts "#{counts.size} unique"
