// Example AnvilOS System Check Program -> /bin/sysinfo.anv

int fib(int n) {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main() {
    print("=== AnvilOS Userland Syscall Benchmark ===\n");
    print("PID: ");
    print_int(getpid());
    print(" | Uptime: ");
    print_int(uptime());
    print(" ms\n");

    int i = 1;
    while (i <= 10) {
        print("fib(");
        print_int(i);
        print(") = ");
        print_int(fib(i));
        print("\n");
        i += 1;
    }
    return 0;
}
