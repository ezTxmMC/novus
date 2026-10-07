struct Point { x: i64, y: i64 }
impl Point { fn sum(&self) -> i64 { self.x + self.y } }
fn main() {
    let mut points: Vec<Box<Point>> = Vec::new();
    for i in 0..1_000_000i64 { points.push(Box::new(Point { x: i, y: i * 2 })); }
    let total: i64 = points.iter().map(|p| p.sum()).sum();
    println!("{}", total);
}
