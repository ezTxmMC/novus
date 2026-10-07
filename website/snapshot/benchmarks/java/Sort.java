import java.util.*;
public class Sort {
    public static void main(String[] args) {
        long[] values = new long[300000];
        long seed = 12345;
        for (int i = 0; i < values.length; i++) {
            seed = seed * 48271 % 2147483647;
            values[i] = seed % 1000000;
        }
        Arrays.sort(values);
        System.out.println(values[0] + " " + values[values.length / 2] + " " + values[values.length - 1]);
    }
}
