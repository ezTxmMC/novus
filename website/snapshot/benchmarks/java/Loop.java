public class Loop {
    public static void main(String[] args) {
        long sum = 0;
        for (long i = 0; i < 10000000L; i++) sum += i % 7;
        System.out.println(sum);
    }
}
