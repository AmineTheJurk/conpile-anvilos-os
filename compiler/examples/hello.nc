// Example AnvilOS C Program compiled by agcc -> /bin/hello.anv

int add(int a, int b) {
    return a + b;
}

int main() {
    print("Hello from a REAL compiled .anv executable!\n");
    print("Running inside AnvilOS via INT 0x80 syscalls.\n");
    print("Process PID: ");
    print_int(getpid());
    print("\nKernel uptime (ms): ");
    print_int(uptime());
    print("\nadd(17, 25) = ");
    print_int(add(17, 25));
    print("\n");
    return 0;
}
