#include <MiniFB.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

// The Chip-8 display is 64x32
#define CHIP8_WIDTH 64
#define CHIP8_HEIGHT 32
// Scale up the screen by a factor of 16
#define SCREEN_WIDTH (CHIP8_WIDTH * 16)
#define SCREEN_HEIGHT (CHIP8_HEIGHT * 16)

#define LENGTH_OF(a) (sizeof((a)) / sizeof(*(a)))

const u8 digits[5 * 16] = {
	0xF0, 0x90, 0x90, 0x90, 0xF0, 0x20, 0x60, 0x20, 0x20, 0x70, 0xF0, 0x10,
	0xF0, 0x80, 0xF0, 0xF0, 0x10, 0xF0, 0x10, 0xF0, 0x90, 0x90, 0xF0, 0x10,
	0x10, 0xF0, 0x80, 0xF0, 0x10, 0xF0, 0xF0, 0x80, 0xF0, 0x90, 0xF0, 0xF0,
	0x10, 0x20, 0x40, 0x40, 0xF0, 0x90, 0xF0, 0x90, 0xF0, 0xF0, 0x90, 0xF0,
	0x10, 0xF0, 0xF0, 0x90, 0xF0, 0x90, 0x90, 0xE0, 0x90, 0xE0, 0x90, 0xE0,
	0xF0, 0x80, 0x80, 0x80, 0xF0, 0xE0, 0x90, 0x90, 0x90, 0xE0, 0xF0, 0x80,
	0xF0, 0x80, 0xF0, 0xF0, 0x80, 0xF0, 0x80, 0x80,
};

bool key_pressed[0x10] = {};
bool new_key = false;

void keyboard_event(struct Window *window, Key key, KeyMod mod, bool pressed) {
	if (key == KB_KEY_ESCAPE) {
		mfb_close(window);
		exit(0);
	}

	int before_num_pressed = 0;
	for (size_t i = 0; i < LENGTH_OF(key_pressed); ++i) {
		before_num_pressed += key_pressed[i];
	}

	key_pressed[0x0] = key == '1' && pressed;
	key_pressed[0x1] = key == '2' && pressed;
	key_pressed[0x2] = key == '3' && pressed;
	key_pressed[0x3] = key == '4' && pressed;
	key_pressed[0x4] = key == 'q' && pressed;
	key_pressed[0x5] = key == 'w' && pressed;
	key_pressed[0x6] = key == 'e' && pressed;
	key_pressed[0x7] = key == 'r' && pressed;
	key_pressed[0x8] = key == 'a' && pressed;
	key_pressed[0x9] = key == 's' && pressed;
	key_pressed[0xa] = key == 'd' && pressed;
	key_pressed[0xb] = key == 'f' && pressed;
	key_pressed[0xc] = key == 'z' && pressed;
	key_pressed[0xd] = key == 'x' && pressed;
	key_pressed[0xe] = key == 'c' && pressed;
	key_pressed[0xf] = key == 'v' && pressed;

	int after_num_pressed = 0;
	for (size_t i = 0; i < LENGTH_OF(key_pressed); ++i) {
		after_num_pressed += key_pressed[i];
	}
	new_key = after_num_pressed > before_num_pressed;
}

double time_diff_seconds(struct timespec t1, struct timespec t2) {
	return ((double)t2.tv_sec + 1e-9 * t2.tv_nsec) -
	       ((double)t1.tv_sec + 1e-9 * t1.tv_nsec);
}

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Supply a ROM path!\n");
		return -1;
	}

	srand(time(NULL));

	// 0x000-0x1ff is reserved
	// 0x200 onward is used by ROMs
	u8 ram[0x1000] = {};
	// gp_regs[0xf] should not be written to by a ROM because it is used as
	// a flag
	u8 gp_regs[0x10] = {};
	u8 stack[0x10] = {};

	// used to store memory addresses
	u16 reg_i = 0;
	// delay timer which is decremented by 1 (if nonzero) every 1/60th of a
	// second
	u8 reg_dt = 0;
	// sound timer which is decremented by 1 (if nonzero) every 1/60th of a
	// second
	u8 reg_st = 0;

	u16 reg_pc = 0x200;
	u8 reg_sp = 0;

	FILE *rom_file = fopen(argv[1], "rb");
	assert(rom_file);
	fseek(rom_file, 0, SEEK_END);
	const long rom_size = ftell(rom_file);
	fseek(rom_file, 0, SEEK_SET);
	uint8_t *rom = malloc(rom_size);
	fread(rom, 1, rom_size, rom_file);
	fclose(rom_file);
	if (rom_size > 0x1000 - 0x200) {
		printf("ROM too large for system!\n");
		return -1;
	}

	// digits are defined to be stored in the reserved area of RAM
	memcpy(ram + 0x050, digits, sizeof(digits));
	memcpy(ram + 0x200, rom, rom_size);

	u8 fb_bitmap[CHIP8_HEIGHT][CHIP8_WIDTH] = {};
	u32 fb_final[SCREEN_HEIGHT][SCREEN_WIDTH] = {};
	struct Window *window =
		mfb_open("Chip-8 Emulator", SCREEN_WIDTH, SCREEN_HEIGHT);
	assert(window);
	mfb_set_keyboard_callback(window, keyboard_event);

	struct timespec last_time = {0, 0};
	while (true) {
		// Run at 60HZ
		struct timespec now = {0, 0};
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (time_diff_seconds(last_time, now) > 1 / 60.0) {
			last_time = now;
		} else {
			continue;
		}

		const u16 instr = (ram[reg_pc] << 8) | ram[reg_pc + 1];
		const u8 high = (instr & 0xf000) >> 12;
		const u8 highmid = (instr & 0x0f00) >> 8;
		const u8 lowmid = (instr & 0x00f0) >> 4;
		const u8 low = (instr & 0x000f);

		bool jumped = false;

		switch (high) {
		case 0x0: {
			// Clear display
			if (instr == 0x00e0) {
				memset(fb_bitmap, 0, sizeof(fb_bitmap));
			}
			// Return from subroutine
			if (instr == 0x00ee) {
				reg_pc = stack[reg_sp];
				reg_sp -= 1;
				jumped = true;
			}

			break;
		}
		case 0x1: {
			// Jump to address
			reg_pc = (highmid << 8) | (lowmid << 4) | low;
			jumped = true;
			break;
		}
		case 0x2: {
			// Call subroutine
			reg_sp += 1;
			stack[reg_sp] = reg_pc;
			reg_pc = (highmid << 8) | (lowmid << 4) | low;
			break;
		}
		case 0x3: {
			// Skip next instruction if gp_regs[x] is equal to byte
			const u8 byte = (lowmid << 4) | low;
			reg_pc += (gp_regs[highmid] == byte) ? 2 : 0;
			break;
		}
		case 0x4: {
			// Skip next instruction if gp_regs[x] is not equal to
			// byte
			const u8 byte = (lowmid << 4) | low;
			reg_pc += (gp_regs[highmid] != byte) ? 2 : 0;
			break;
		}
		case 0x5: {
			// Skip next instruction if gp_regs[x] is equal to
			// gp_regs[y]
			const u8 rega = gp_regs[highmid];
			const u8 regb = gp_regs[lowmid];
			reg_pc += (rega == regb) ? 2 : 0;
			break;
		}
		case 0x6: {
			// Load byte into register
			gp_regs[highmid] = (lowmid << 4) | low;
			break;
		}
		case 0x7: {
			// Add byte to register
			gp_regs[highmid] += (lowmid << 4) | low;
			break;
		}
		case 0x8: {
			// Load register from another register
			if (low == 0x0) {
				gp_regs[highmid] = gp_regs[lowmid];
			}
			// OR register with another register
			if (low == 0x1) {
				gp_regs[highmid] |= gp_regs[lowmid];
			}
			// AND register with another register
			if (low == 0x2) {
				gp_regs[highmid] &= gp_regs[lowmid];
			}
			// XOR register with another register
			if (low == 0x3) {
				gp_regs[highmid] ^= gp_regs[lowmid];
			}
			// Add register with another register
			if (low == 0x4) {
				// Set flag if the result cannot be
				// contained in 8 bits
				gp_regs[0xf] = ((u32)gp_regs[highmid] +
						gp_regs[lowmid]) > 255;
				gp_regs[highmid] += gp_regs[lowmid];
			}
			// Subtract register from another register
			if (low == 0x5) {
				// Set flag if no borrow is performed
				gp_regs[0xf] =
					gp_regs[highmid] > gp_regs[lowmid];

				gp_regs[highmid] -= gp_regs[lowmid];
			}
			// Shift register right by 1
			if (low == 0x6) {
				// Set flag if a 1 bit is shifted out
				gp_regs[0xf] = gp_regs[highmid] & 0b1;
				gp_regs[highmid] >>= 1;
			}
			// Subtract register from another register, order
			// reversed
			if (low == 0x7) {
				// Set flag if no borrow is performed
				gp_regs[0xf] =
					gp_regs[lowmid] > gp_regs[highmid];

				gp_regs[highmid] =
					gp_regs[lowmid] - gp_regs[highmid];
			}
			// Shift register left by 1
			if (low == 0xe) {
				// Set flag if a 1 bit is shifted out
				gp_regs[0xf] = gp_regs[highmid] & 0b10000000;
				gp_regs[0xf] <<= 1;
			}
			break;
		}
		case 0x9: {
			// Skip next instruction if registers not equal
			reg_pc += (gp_regs[highmid] != gp_regs[lowmid]) ? 2 : 0;
			break;
		}
		case 0xa: {
			// Load value into memory address register
			reg_i = (highmid << 8) | (lowmid << 4) | low;
			break;
		}
		case 0xb: {
			// Jump with base + register offset
			const u8 base = (highmid << 8) | (lowmid << 4) | low;
			reg_pc = base + gp_regs[0];
			jumped = true;
			break;
		}
		case 0xc: {
			// Generate a random byte and AND it with a constant
			const u8 c = (lowmid << 4) | low;
			gp_regs[highmid] = (rand() % 255) & c;
			break;
		}
		case 0xd: {
			// Draw 8x{height} sprite at position (x, y) from RAM
			// Sets flag if XORed pixels are erased (collision)
			// Sprite wraps around screen if it goes out of bounds
			const u8 x = highmid;
			const u8 y = lowmid;
			const u8 height = low;

			// write into fb_bitmap
			// write without collision (cube8 demo shouldn't need
			// that?)

			break;
		}
		case 0xe: {
			const u8 low_byte = (lowmid << 4) | low;
			const u8 key_index = gp_regs[highmid];
			// Skip the next instruction if a specified key is
			// pressed
			if (low_byte == 0x9e) {
				if (key_pressed[key_index]) {
					reg_pc += 2;
				}
			}
			// Skip the next instruction if a specified key is not
			// pressed
			if (low_byte == 0xa1) {
				if (!key_pressed[key_index]) {
					reg_pc += 2;
				}
			}
			break;
		}
		case 0xf: {
			const u8 low_byte = (lowmid << 4) | low;
			// Load value of delay timer in a register
			if (low_byte == 0x07) {
				gp_regs[highmid] = reg_dt;
			}
			// Wait for a key to be pressed and store it into a
			// register
			if (low_byte == 0x0a) {
				// Acts as a halt if no key has been pressed
				jumped = !new_key;
			}
			// Set delay timer to a value from register
			if (low_byte == 0x15) {
				reg_dt = gp_regs[highmid];
			}
			// Set sound timer to a value from register
			if (low_byte == 0x18) {
				reg_st = gp_regs[highmid];
			}
			// Increment memory address register by another register
			if (low_byte == 0x1e) {
				reg_i += gp_regs[highmid];
			}
			// Point memory address register to digit sprite from
			// register index
			if (low_byte == 0x29) {
				// digits are 5 pixels tall
				reg_i = 0x50 + gp_regs[highmid] * 5;
			}
			// Store BCD of byte into RAM
			if (low_byte == 0x33) {
				ram[reg_i] = gp_regs[highmid] / 100;
				ram[reg_i + 1] = (gp_regs[highmid] / 10) % 10;
				ram[reg_i + 2] = (gp_regs[highmid] % 100) % 10;
			}
			// Store an amount of register into RAM
			if (low_byte == 0x55) {
				for (size_t i = 0; i < highmid; ++i) {
					ram[reg_i + i] = gp_regs[i];
				}
			}
			// Load an amount of reigster from RAM
			if (low_byte == 0x65) {
				for (size_t i = 0; i < highmid; ++i) {
					gp_regs[i] = ram[reg_i + i];
				}
			}
			break;
		}
		}

		// Decrement timers if active
		if (reg_dt) {
			reg_dt -= 1;
		}
		if (reg_st) {
			reg_st -= 1;
			// @TODO: play a tone when reg_st = 0
		}

		if (!jumped) {
			reg_pc += 2;
		}

		// Process the bitmap into the final fb
		for (size_t x = 0; x < CHIP8_WIDTH; ++x) {
			for (size_t y = 0; y < CHIP8_HEIGHT; ++y) {
			}
		}

		const UpdateState state = mfb_update(window, fb_bitmap);
		assert(state == STATE_OK);
	}

	return 0;
}
