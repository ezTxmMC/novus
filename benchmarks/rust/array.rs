fn main() {
    let mut values: Vec<i64> = Vec::new();
    for i in 0..2_000_000i64 { values.push(i); }
    let sum: i64 = values.iter().sum();
    println!("{}", sum);
}
