use std::collections::HashMap;
fn main() {
    let mut counts: HashMap<String, i64> = HashMap::new();
    for i in 0..300_000i64 { counts.insert(format!("key{}", i), i); }
    let mut sum: i64 = 0;
    for i in 0..300_000i64 { sum += counts[&format!("key{}", i)]; }
    println!("{} {}", counts.len(), sum);
}
