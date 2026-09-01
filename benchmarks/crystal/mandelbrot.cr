inside = 0
py = 0
while py < 400
  y = (py / 400.0) * 2.0 - 1.0
  px = 0
  while px < 400
    x = (px / 400.0) * 3.0 - 2.0
    zx = 0.0
    zy = 0.0
    i = 0
    while i < 50 && zx * zx + zy * zy <= 4.0
      t = zx * zx - zy * zy + x
      zy = 2.0 * zx * zy + y
      zx = t
      i += 1
    end
    inside += 1 if i == 50
    px += 1
  end
  py += 1
end
puts inside
