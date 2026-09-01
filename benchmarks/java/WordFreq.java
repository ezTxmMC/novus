import java.nio.file.*;
import java.util.*;
public class WordFreq {
    public static void main(String[] args) throws Exception {
        String text = Files.readString(Path.of("input.txt"));
        HashMap<String, Long> counts = new HashMap<>();
        for (String word : text.split("\\s+")) {
            if (word.isEmpty()) continue;
            counts.merge(word, 1L, Long::sum);
        }
        ArrayList<String> ranked = new ArrayList<>();
        for (Map.Entry<String, Long> entry : counts.entrySet())
            ranked.add(String.format("%8d %s", entry.getValue(), entry.getKey()));
        ranked.sort(Comparator.reverseOrder());
        for (int i = 0; i < 3; i++) System.out.println(ranked.get(i).trim());
        System.out.println(counts.size() + " unique");
    }
}
