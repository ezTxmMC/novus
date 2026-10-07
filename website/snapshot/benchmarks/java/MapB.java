import java.util.*;
public class MapB {
    public static void main(String[] args) {
        HashMap<String, Long> counts = new HashMap<>();
        for (long i = 0; i < 300000; i++) counts.put("key" + i, i);
        long sum = 0;
        for (long i = 0; i < 300000; i++) sum += counts.get("key" + i);
        System.out.println(counts.size() + " " + sum);
    }
}
