class Point:
    __slots__ = ("x", "y")

    def __init__(self, x, y):
        self.x = x
        self.y = y

    def sum(self):
        return self.x + self.y


points = [Point(i, i * 2) for i in range(1_000_000)]
print(sum(p.sum() for p in points))
