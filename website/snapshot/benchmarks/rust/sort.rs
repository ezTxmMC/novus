fn main() {
    let mut values: Vec<i64> = Vec::new();
    let mut seed: i64 = 12345;
    for _ in 0..300_000 {
        seed = seed * 48271 % 2147483647;
        values.push(seed % 1_000_000);
    }
    values.sort();
    println!("{} {} {}", values[0], values[values.len() / 2], values[values.len() - 1]);
}
