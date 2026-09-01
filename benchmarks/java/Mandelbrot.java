public class Mandelbrot {
    public static void main(String[] args) {
        int inside = 0;
        for (int py = 0; py < 400; py++) {
            double y = (py / 400.0) * 2.0 - 1.0;
            for (int px = 0; px < 400; px++) {
                double x = (px / 400.0) * 3.0 - 2.0;
                double zx = 0.0, zy = 0.0;
                int i = 0;
                while (i < 50 && zx * zx + zy * zy <= 4.0) {
                    double t = zx * zx - zy * zy + x;
                    zy = 2.0 * zx * zy + y;
                    zx = t;
                    i++;
                }
                if (i == 50) inside++;
            }
        }
        System.out.println(inside);
    }
}
