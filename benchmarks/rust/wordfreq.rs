use std::collections::HashMap;
use std::fs;
fn main() {
    let text = fs::read_to_string("input.txt").unwrap();
    let mut counts: HashMap<&str, i64> = HashMap::new();
    for word in text.split_whitespace() { *counts.entry(word).or_insert(0) += 1; }
    let mut ranked: Vec<String> = counts.iter().map(|(w, c)| format!("{:>8} {}", c, w)).collect();
    ranked.sort();
    ranked.reverse();
    for line in ranked.iter().take(3) { println!("{}", line.trim()); }
    println!("{} unique", counts.len());
}
