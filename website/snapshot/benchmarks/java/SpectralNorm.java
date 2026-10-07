public class SpectralNorm {
    static double a(int i, int j) { return 1.0 / ((i + j) * (i + j + 1) / 2 + i + 1); }

    static void mulAv(int n, double[] v, double[] out) {
        for (int i = 0; i < n; i++) {
            double s = 0.0;
            for (int j = 0; j < n; j++) s = s + a(i, j) * v[j];
            out[i] = s;
        }
    }

    static void mulAtv(int n, double[] v, double[] out) {
        for (int i = 0; i < n; i++) {
            double s = 0.0;
            for (int j = 0; j < n; j++) s = s + a(j, i) * v[j];
            out[i] = s;
        }
    }

    static void mulAtAv(int n, double[] v, double[] out, double[] tmp) {
        mulAv(n, v, tmp);
        mulAtv(n, tmp, out);
    }

    public static void main(String[] args) {
        int n = 500;
        double[] u = new double[n], v = new double[n], tmp = new double[n];
        java.util.Arrays.fill(u, 1.0);
        for (int i = 0; i < 10; i++) {
            mulAtAv(n, u, v, tmp);
            mulAtAv(n, v, u, tmp);
        }
        double vBv = 0.0, vv = 0.0;
        for (int i = 0; i < n; i++) {
            vBv = vBv + u[i] * v[i];
            vv = vv + v[i] * v[i];
        }
        System.out.println(String.format(java.util.Locale.ROOT, "%.9f", Math.sqrt(vBv / vv)));
    }
}
