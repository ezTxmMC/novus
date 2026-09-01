public class Str {
    public static void main(String[] args) {
        StringBuilder builder = new StringBuilder();
        for (int i = 0; i < 200000; i++) builder.append("word").append(i % 10).append(" ");
        String text = builder.toString().trim();
        String[] words = text.split(" ");
        String joined = String.join("-", words);
        System.out.println(text.length() + " " + words.length + " " + joined.length());
    }
}
