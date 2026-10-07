import java.util.*;
public class Arr {
    public static void main(String[] args) {
        ArrayList<Long> values = new ArrayList<>();
        for (long i = 0; i < 2000000; i++) values.add(i);
        long sum = 0;
        for (long v : values) sum += v;
        System.out.println(sum);
    }
}
