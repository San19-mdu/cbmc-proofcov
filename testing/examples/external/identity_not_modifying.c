void main() {
	int p = 0;
	int x, y;
	y = p;
	x = p;

	if (p > 0) {
		x = y;
	} else {
		p = p;
	}

	assert(x == y);
}