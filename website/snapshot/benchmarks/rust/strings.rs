fn main() {
    let mut text = String::new();
    for i in 0..200_000 {
        text.push_str("word");
        text.push_str(&(i % 10).to_string());
        text.push(' ');
    }
    let trimmed = text.trim().to_string();
    let words: Vec<&str> = trimmed.split(' ').collect();
    let joined = words.join("-");
    println!("{} {} {}", trimmed.len(), words.len(), joined.len());
}
