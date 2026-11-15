// Example AnvilOS Loop Program -> /bin/calc.anv

int main() {
    print("Squares table (1..8):\n");
    for (int x = 1; x <= 8; x += 1) {
        print("  ");
        print_int(x);
        print("^2 = ");
        print_int(x * x);
        print("\n");
    }
    return 42;
}
