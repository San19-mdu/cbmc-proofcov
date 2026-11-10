int main() {
    int a;
    int b;
    int c;
    int d = 0;

    a = 1;
    b = 1;
    c = 1;

    if ((a == 1 && b == 1) || c == 1) {
        d = 1;
    }

    assert(d == 1);
}
