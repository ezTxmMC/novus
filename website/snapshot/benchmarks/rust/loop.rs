fn main() {
    let mut sum: i64 = 0;
    for i in 0..10_000_000i64 { sum += i % 7; }
    println!("{}", sum);
}
