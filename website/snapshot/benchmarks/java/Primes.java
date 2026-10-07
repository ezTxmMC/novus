public class Primes {
    static boolean isPrime(long n) {
        if (n < 2) return false;
        for (long i = 2; i * i <= n; i++) if (n % i == 0) return false;
        return true;
    }
    public static void main(String[] args) {
        int count = 0;
        for (long n = 0; n < 200000; n++) if (isPrime(n)) count++;
        System.out.println(count);
    }
}
