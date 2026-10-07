import java.util.*;
public class Obj {
    static class Point {
        final long x, y;
        Point(long x, long y) { this.x = x; this.y = y; }
        long sum() { return x + y; }
    }
    public static void main(String[] args) {
        ArrayList<Point> points = new ArrayList<>();
        for (long i = 0; i < 1000000; i++) points.add(new Point(i, i * 2));
        long total = 0;
        for (Point p : points) total += p.sum();
        System.out.println(total);
    }
}
