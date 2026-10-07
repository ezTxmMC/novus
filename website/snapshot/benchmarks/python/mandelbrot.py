inside = 0
for py in range(400):
    y = (py / 400.0) * 2.0 - 1.0
    for px in range(400):
        x = (px / 400.0) * 3.0 - 2.0
        zx = zy = 0.0
        i = 0
        while i < 50 and zx * zx + zy * zy <= 4.0:
            zx, zy = zx * zx - zy * zy + x, 2.0 * zx * zy + y
            i += 1
        if i == 50:
            inside += 1
print(inside)
