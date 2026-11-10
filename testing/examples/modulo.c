void main() {
    int num = 17;
    int is_even;
    int adjusted;
    int mod;
    mod = num % 2;

    if (mod == 0) {
        is_even = 1;
        adjusted = num / 2;
    } else {
        is_even = 0;
        adjusted = (num * 3) + 1;
    }

    assert(adjusted == 52); 
}
