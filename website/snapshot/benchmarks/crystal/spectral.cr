def a(i, j)
  1.0 / ((i + j) * (i + j + 1) // 2 + i + 1)
end

def mul_av(n, v, res)
  i = 0
  while i < n
    s = 0.0
    j = 0
    while j < n
      s = s + a(i, j) * v[j]
      j += 1
    end
    res[i] = s
    i += 1
  end
end

def mul_atv(n, v, res)
  i = 0
  while i < n
    s = 0.0
    j = 0
    while j < n
      s = s + a(j, i) * v[j]
      j += 1
    end
    res[i] = s
    i += 1
  end
end

def mul_atav(n, v, res, tmp)
  mul_av(n, v, tmp)
  mul_atv(n, tmp, res)
end

n = 500
u = Array.new(n, 1.0)
v = Array.new(n, 0.0)
tmp = Array.new(n, 0.0)
10.times do
  mul_atav(n, u, v, tmp)
  mul_atav(n, v, u, tmp)
end
vbv = 0.0
vv = 0.0
i = 0
while i < n
  vbv = vbv + u[i] * v[i]
  vv = vv + v[i] * v[i]
  i += 1
end
puts "%.9f" % Math.sqrt(vbv / vv)
