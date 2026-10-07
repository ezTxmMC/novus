fn main() {
    let mut inside = 0;
    for py in 0..400 {
        let y = (py as f64 / 400.0) * 2.0 - 1.0;
        for px in 0..400 {
            let x = (px as f64 / 400.0) * 3.0 - 2.0;
            let (mut zx, mut zy) = (0.0f64, 0.0f64);
            let mut i = 0;
            while i < 50 && zx * zx + zy * zy <= 4.0 {
                let t = zx * zx - zy * zy + x;
                zy = 2.0 * zx * zy + y;
                zx = t;
                i += 1;
            }
            if i == 50 { inside += 1; }
        }
    }
    println!("{}", inside);
}
