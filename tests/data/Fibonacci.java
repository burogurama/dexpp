class Fibonacci {

    /** Iterative Fibonacci with a loop, branch, and local array accumulator.
     *  Returns -1 for negative n, 0 for n==0, and fib(n) otherwise.
     *  Also accumulates the sum of the sequence and stores it in a field. */
    public long sumFib;

    public long compute(int n) {
        if (n < 0) return -1L;
        if (n == 0) return 0L;

        long prev = 0L;
        long curr = 1L;
        long sum  = 1L;

        for (int i = 2; i <= n; i++) {
            long next = prev + curr;
            prev = curr;
            curr = next;
            sum += next;
        }

        sumFib = sum;
        return curr;
    }

    public static String classify(long value) {
        if (value < 0)   return "negative";
        if (value == 0)  return "zero";
        if (value < 10)  return "small";
        if (value < 100) return "medium";
        return "large";
    }
}
