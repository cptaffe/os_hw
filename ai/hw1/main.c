
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <strings.h>
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

// PCG random numbers
#include "pcg_variants.h"

typedef struct {
	long double x, y, z, T;
	uint64_t bits;
} HyperSpace;

HyperSpace hyperSpaceFromBits(uint64_t b) {
	return (HyperSpace){
		.x = (long double) ((b & 0xffff0000000000) >> 40) / 0xffff * 40 - 20,
		.y = (long double) ((b & 0xffff000000) >> 24) / 0xffff * 60 - 30,
		.z = (long double) ((b & 0xffff00) >> 8) / 0xffff * 50,
		.T = (long double) (b & 0xff) / 0xff * 5,
		.bits = b
	};
}

long double simX(long double x, long double y, long double z) {
	const long double o = 10;
	return o * (y-x);
}

long double simY(long double x, long double y, long double z) {
	const long double r = 28;
	return r*x - y - x*z;
}

long double simZ(long double x, long double y, long double z) {
	const long double b = 8.0/3.0;
	return x*y - b*z;
}

HyperSpace hyperSpaceSimulateForwardEuler(HyperSpace *h) {
	const long double dt = 0.01, o = 10, r = 28, b = 8.0/3.0;
	HyperSpace n = *h;
	for (long double t = 0; t < h->T; t += dt) {
		HyperSpace m = n;
		// Forward Euler
		n.x += dt * simX(m.x, m.y, m.z);
		n.y += dt * simY(m.x, m.y, m.z);
		n.z += dt * simZ(m.x, m.y, m.z);
	}
	return n;
}

HyperSpace hyperSpaceSimulateRK4(HyperSpace *h) {
	const long double dt = 0.01, o = 10, r = 28, b = 8.0/3.0;
	HyperSpace n = *h;
	for (long double t = 0; t < h->T; t += dt) {
		HyperSpace m = n;
		long double
			k1x = simX(m.x, m.y, m.z),
			k1y = simY(m.x, m.y, m.z),
			k1z = simZ(m.x, m.y, m.z),

			k2x = simX(m.x + dt/2*k1x, m.y + dt/2*k1y, m.z + dt/2*k1z),
			k2y = simY(m.x + dt/2*k1x, m.y + dt/2*k1y, m.z + dt/2*k1z),
			k2z = simZ(m.x + dt/2*k1x, m.y + dt/2*k1y, m.z + dt/2*k1z),

			k3x = simX(m.x + dt/2*k2x, m.y + dt/2*k2y, m.z + dt/2*k2z),
			k3y = simY(m.x + dt/2*k2x, m.y + dt/2*k2y, m.z + dt/2*k2z),
			k3z = simZ(m.x + dt/2*k2x, m.y + dt/2*k2y, m.z + dt/2*k2z),

			k4x = simX(m.x + dt*k3x, m.y + dt*k3y, m.z + dt*k3z),
			k4y = simY(m.x + dt*k3x, m.y + dt*k3y, m.z + dt*k3z),
			k4z = simZ(m.x + dt*k3x, m.y + dt*k3y, m.z + dt*k3z);

		n.x += dt/6*(k1x + 2*k2x + 2*k3x + k4x);
		n.y += dt/6*(k1y + 2*k2y + 2*k3y + k4y);
		n.z += dt/6*(k1z + 2*k2z + 2*k3z + k4z);
	}
	return n;
}

uint64_t randomPCG() {
	return pcg64_boundedrand(0x100000000000000);
}

// Global program configuration
struct {
	HyperSpace (*simulate)(HyperSpace *h);
	uint64_t (*random)();
} Config = {
	.simulate = hyperSpaceSimulateForwardEuler,
	.random = randomPCG
};

long double hyperSpaceCost(HyperSpace *h) {
	const long double tx = 18, ty = 20, tz = 45;
	HyperSpace n = Config.simulate(h);
	return sqrtl(powl(h->x, 2) + powl(h->y, 2) + powl(h->z, 2)) + sqrtl(powl(n.x-tx, 2) + powl(n.y-ty, 2) + powl(n.z-tz, 2));
}

typedef struct {
	HyperSpace h;
	long double cost;
	uint64_t evals;
} Result;

void resultPprint(Result *r) {
	printf("%llu, %Lf, %Lf, %Lf, %Lf, %Lf\n", r->evals, r->cost, r->h.x, r->h.y, r->h.z, r->h.T);
}

// Generates all single-hamming-distance solutions
// and selects the best one.
// Slight optimization combines neighborhood generation
// and greedy descent selection for O(1) vs O(n) space.
bool hyperSpaceNext(Result *r) {
	Result t = *r;
	const int blen = 56;
	uint64_t bits = t.h.bits, evals = t.evals; // Original bits
	for (int i = 0; i < blen; i++) {
		Result lt = {
			.h = hyperSpaceFromBits(bits ^ (1 << i)),
			.cost = hyperSpaceCost(&lt.h)
		};
		if (lt.cost < t.cost) {
			t = lt;
		}
	}
	bool haveAdvanced = r->h.bits != t.h.bits;
	*r = (Result){
		.h = t.h,
		.cost = t.cost,
		// Accumulate evals
		.evals = r->evals + blen
	};
	return haveAdvanced;
}

// Search thread parameters
struct SearchThreadParams {
	size_t S;
	Result *r;
};

// search thread function
// for use with pthread_create
// void* cast to a SearchThreadParams*
void *searchThread(void *stp) {
	struct SearchThreadParams *p = (struct SearchThreadParams *) stp;
	for (int s = 0; s < p->S; s++) {
		Result l = {
			// PCG bounded rand is on the range [0, bound).
			.h = hyperSpaceFromBits(Config.random()),
			.cost = hyperSpaceCost(&l.h),
			.evals = 1
		};
		// Keeps a running total of evals
		while (hyperSpaceNext(&l)) {}
		p->r[s] = l;
	}
	return NULL;
}

// Returns an array of S result tuples
Result *search(size_t S, const int threads) {
	Result *r = calloc(sizeof(Result), S);
	pthread_t ts[threads];
	struct SearchThreadParams params[threads];
	bool spawnThreads = S/threads > 0;
	if (spawnThreads) {
		for (int i = 0; i < threads; i++) {
			params[i] = (struct SearchThreadParams){
				.S = S/threads,
				.r = &r[i*(S/threads)]
			};
			if (pthread_create(
				&ts[i],
				NULL,
				searchThread,
				(void *) &params[i]
			) != 0) {
				fprintf(stderr, "Failed to spawn thread %d!\n", i);
				exit(1);
			}
		}
	}

	// Possible that S did not divide evenly
	// Run the remaining ones in this thread.
	if (S%threads > 0) {
		searchThread(&(struct SearchThreadParams){
			.S = S%threads,
			.r = &r[S-S%threads]
		});
	}

	if (spawnThreads) {
		for (int i = 0; i < threads; i++) {
			pthread_join(ts[i], NULL);
		}
	}
	return r;
}

void usage() {
	printf("main [-s|--simulate algorithm(rc4|euler)] [-r|--random function(pcg)].\n");
	exit(1);
}

int main(int argc, char *argv[]) {
	const int max = 1000, threads = sysconf(_SC_NPROCESSORS_ONLN);

	// Parse command line options
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--simulate") == 0) {
			if ((i + 1) < argc) {
				i++;
				if (strcmp(argv[i], "rc4") == 0) {
					Config.simulate = hyperSpaceSimulateRK4;
				} else if (strcmp(argv[i], "euler") == 0) {
					Config.simulate = hyperSpaceSimulateForwardEuler;
				} else {
					usage();
				}
			} else {
				usage();
			}
		} else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--random") == 0) {
			if ((i + 1) < argc) {
				i++;
				if (strcmp(argv[i], "pcg") == 0) {
					Config.random = randomPCG;
				} else {
					usage();
				}
			} else {
				usage();
			}
		} else {
			usage();
		}
	}

	// printf("Running with %d threads.\n", threads);
	Result *t = search(max, threads);
	for (int i = 0; i < max; i++) {
		resultPprint(&t[i]);
	}
}
