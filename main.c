#include <MiniFB.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

// Scale up the screen by a factor of 16
#define WIDTH (64*16)
#define HEIGHT (32*16)

int main() {

	uint32_t fb[HEIGHT][WIDTH] = {};
	struct Window *window = mfb_open("Chip-8 Emulator", WIDTH, HEIGHT);
	assert(window);

	while (true) {

		for (int i = 0; i < WIDTH; ++i) {
			for (int j = 0; j < HEIGHT; ++j) {
				fb[j][i] = (i * j + 32) << 8;
			}
		}

		UpdateState state = mfb_update(window, fb);
		assert(state == STATE_OK);
	}

	return 0;
}
