class Point
  getter x : Int64
  getter y : Int64

  def initialize(@x : Int64, @y : Int64)
  end

  def sum : Int64
    @x + @y
  end
end

points = [] of Point
i = 0_i64
while i < 1_000_000
  points << Point.new(i, i * 2)
  i += 1
end
total = 0_i64
points.each { |p| total += p.sum }
puts total
