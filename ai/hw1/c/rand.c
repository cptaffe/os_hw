#include <stdlib.h>
#include <stdint.h>

// Algorithmic constants
typedef struct {
	uint64_t w, // word size in bits
		n,        // degree of recurrence
		m,        // middle word
		r,        // separation point of one word
		a,        // coefficients of the rational normal form twist matrix
		b, c,     // TGFSR(R) tempering bitmasks
		s, t,     // TGFSR(R) tempering bit shitfs
		u, d, l,  // Additional Mersenne Twister tempering bit shifts/masks
		f;
} Rand;

// State of generator
typedef struct {
	Rand *r;
	uint64_t *mt;
	int i;
	uint64_t lmask, umask;
} RandState;

static uint64_t lowestBits(uint64_t w) {
	uint64_t m = 0;
	for (int i = 0; i < w; i++) {
		m |= 1 << i;
	}
	return m;
}

static void seed(RandState *s, uint64_t seed) {
	s->i = s->r->n;
	s->mt[0] = seed;
	for (int i = 1; i < s->r->n; i++) {
		s->mt[i] = (s->r->f * (s->mt[i-1] ^ (s->mt[i-1] >> (s->r->w-2))) + i) & lowestBits(s->r->w);
	}
}

static void twist(RandState *s) {
	for (int i = 0; i < s->r->n; i++) {
		uint64_t x = (s->mt[i] & s->umask) + (s->mt[(i+1) % s->r->n] & s->lmask);
		uint64_t xA = x >> 1;
		if (x % 2 != 0) {
			xA ^= s->r->a;
		}
		s->mt[i] = s->mt[(i + s->r->m) % s->r->n] ^ xA;
	}
	s->i = 0;
}

static uint64_t extract(RandState *s) {
	if (s->i >= s->r->n) {
		if (s->i > s->r->n) {
			seed(s, 5489);
		}
		twist(s);
	}
	uint64_t y = s->mt[s->i];
	y ^= (y >> s->r->u) & s->r->d;
	y ^= (y << s->r->s) & s->r->b;
	y ^= (y << s->r->t) & s->r->c;
	y ^= y >> s->r->l;

	s->i++;
	return y & lowestBits(s->r->w);
}

static RandState *makeRandState(Rand *r) {
	RandState *s = calloc(sizeof(RandState), 1);
	*s = (RandState) {
		.r = r,
		.mt = calloc(sizeof(uint64_t), r->n),
		.i = r->n+1,
		.lmask = (1 << r->r) - 1,
		.umask = ~((1 << r->r) - 1) & lowestBits(r->w),
	};
	return s;
}

static Rand *makeMT199364Random() {
	Rand *r = calloc(sizeof(Rand), 1);
	*r = (Rand){
		.w = 64, .n = 312, .m = 156, .r = 31,
		.a = 0xB5026F5AA96619E9,
		.u = 29, .d = 0x5555555555555555,
		.s = 17, .b = 0x71D67FFFEDA60000,
		.t = 37, .c = 0xFFF7EEE000000000,
		.l = 43,
		.f = 6364136223846793005
	};
	return r;
}

typedef Rand *(RandMakeFunc)();

RandState *gRandState;

static void checkInit(RandMakeFunc *f) {
	if (gRandState == NULL) {
		gRandState = makeRandState(f());
	}
}

static uint64_t randGen(RandMakeFunc *f) {
	checkInit(f);
	// lowest w bits will also be the largest generatable number.
	return extract(gRandState);
}

// Exported functions

void randSeed(uint64_t s) {
	checkInit(makeMT199364Random);
	seed(gRandState, s);
}

uint64_t randInt64() {
	return randGen(makeMT199364Random);
}

double randDouble() {
	return ((double) randGen(makeMT199364Random))/lowestBits(gRandState->r->w);
}
