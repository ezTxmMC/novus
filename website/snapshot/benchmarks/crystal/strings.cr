builder = String::Builder.new
200_000.times do |i|
  builder << "word" << (i % 10) << " "
end
text = builder.to_s.strip
words = text.split(" ")
joined = words.join("-")
puts "#{text.size} #{words.size} #{joined.size}"
