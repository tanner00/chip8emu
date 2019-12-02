// @TODO: redo opcode d{x,y,n}
// @TODO: redo keyboard input code
// @TODO: beep when reg_st = 0

#include <SFML/Graphics.h>
#include <SFML/Window.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

// The Chip-8 display is 64x32
#define CHIP8_WIDTH 64
#define CHIP8_HEIGHT 32
// Scale up the screen by a factor of 16
#define SCALE 16
#define SCREEN_WIDTH ((CHIP8_WIDTH) * (SCALE))
#define SCREEN_HEIGHT ((CHIP8_HEIGHT) * (SCALE))

// Foreground color
#define FG 0xffffffff
// Background color
#define BG 0x00000000

static const u8 digits[5 * 16] = {
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
	long rom_size = ftell(rom_file);
	if (rom_size > 0x1000 - 0x200) {
		printf("ROM too large for system!\n");
		return -1;
	}
	rewind(rom_file);
	u8 *rom = malloc(rom_size);
	assert(rom);
	long got_size = fread(rom, sizeof(u8), rom_size, rom_file);
	assert(got_size == rom_size);
	fclose(rom_file);

	// digits are defined to be stored in the reserved area of RAM
	memcpy(ram + 0x050, digits, sizeof(digits));
	memcpy(ram + 0x200, rom, rom_size);

	sfVideoMode mode = {SCREEN_WIDTH, SCREEN_HEIGHT, 32};
	sfRenderWindow *window = sfRenderWindow_create(
		mode, "Chip-8 Emulator", sfResize | sfClose, NULL);
	// Most games seem to run well at 400HZ
	// The Chip-8 has no defined processor speed
	sfRenderWindow_setFramerateLimit(window, 400);

	u32 fb[CHIP8_HEIGHT][CHIP8_WIDTH] = {};
	sfTexture *screen_texture = sfTexture_create(CHIP8_WIDTH, CHIP8_HEIGHT);
	sfSprite *screen_drawable = sfSprite_create();
	sfSprite_setTexture(screen_drawable, screen_texture, true);
	sfSprite_setScale(screen_drawable, (sfVector2f){SCALE, SCALE});

	sfClock *timers = sfClock_create();
	double last_time = 0;

	bool new_keys[0x10] = {};
	while (sfRenderWindow_isOpen(window)) {
		sfEvent event;
		while (sfRenderWindow_pollEvent(window, &event)) {
			if (event.type == sfEvtClosed) {
				sfRenderWindow_close(window);
			}
		}

		// @NOTE: I can't just assume these change atomically.
		// If I could, like an event system, this could be done in O(1)
		// space. Same complexity though and it's such a small n that
		// the worst part about it is that it's uglier imo
		memset(new_keys, 0, sizeof(new_keys));

		new_keys[0x1] = sfKeyboard_isKeyPressed(sfKeyNum2);
		new_keys[0x2] = sfKeyboard_isKeyPressed(sfKeyNum3);
		new_keys[0x3] = sfKeyboard_isKeyPressed(sfKeyNum4);
		new_keys[0x4] = sfKeyboard_isKeyPressed(sfKeyQ);
		new_keys[0x5] = sfKeyboard_isKeyPressed(sfKeyW);
		new_keys[0x6] = sfKeyboard_isKeyPressed(sfKeyE);
		new_keys[0x7] = sfKeyboard_isKeyPressed(sfKeyR);
		new_keys[0x8] = sfKeyboard_isKeyPressed(sfKeyA);
		new_keys[0x9] = sfKeyboard_isKeyPressed(sfKeyS);
		new_keys[0xa] = sfKeyboard_isKeyPressed(sfKeyD);
		new_keys[0xb] = sfKeyboard_isKeyPressed(sfKeyF);
		new_keys[0xc] = sfKeyboard_isKeyPressed(sfKeyZ);
		new_keys[0xd] = sfKeyboard_isKeyPressed(sfKeyX);
		new_keys[0xe] = sfKeyboard_isKeyPressed(sfKeyC);
		new_keys[0xf] = sfKeyboard_isKeyPressed(sfKeyV);
		for (size_t i = 0; i < sizeof(new_keys) / sizeof(*new_keys);
		     ++i) {
			if (new_keys[i] != key_pressed[i]) {
				new_key = true;
			}
			key_pressed[i] = new_keys[i];
		}

		const u16 instr = (ram[reg_pc] << 8) | ram[reg_pc + 1];
		const u8 high = (instr & 0xf000) >> 12;
		const u8 highmid = (instr & 0x0f00) >> 8;
		const u8 lowmid = (instr & 0x00f0) >> 4;
		const u8 low = (instr & 0x000f);

		bool jumped = false;
		bool draw = false;

		switch (high) {
		case 0x0: {
			// Clear display
			if (instr == 0x00e0) {
				memset(fb, 0, CHIP8_WIDTH * CHIP8_WIDTH);
				draw = true;
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
			// Skip next instruction if gp_regs[x] is equal
			// to byte
			const u8 byte = (lowmid << 4) | low;
			reg_pc += (gp_regs[highmid] == byte) ? 2 : 0;
			break;
		}
		case 0x4: {
			// Skip next instruction if gp_regs[x] is not
			// equal to byte
			const u8 byte = (lowmid << 4) | low;
			reg_pc += (gp_regs[highmid] != byte) ? 2 : 0;
			break;
		}
		case 0x5: {
			// Skip next instruction if gp_regs[x] is equal
			// to gp_regs[y]
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
			// Subtract register from another register,
			// order reversed
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
			// Generate a random byte and AND it with a
			// constant
			const u8 c = (lowmid << 4) | low;
			gp_regs[highmid] = (rand() % 255) & c;
			break;
		}
		case 0xd: {
			// Draw 8x{height} sprite at position (x, y)
			// from RAM Sets flag if XORed pixels are erased
			// (collision) Sprite wraps around screen if it
			// goes out of bounds
			const u8 x = gp_regs[highmid];
			const u8 y = gp_regs[lowmid];
			const u8 height = low;

			gp_regs[0xf] = 0;
			for (size_t py = 0; py < height; ++py) {
				const u8 cy = y + py;
				const u8 pixel = ram[reg_i + py];

				for (size_t px = 0; px < 8; ++px) {
					const u8 cx = x + px;
					if (pixel & (0x80 >> px)) {
						if (fb[cy][cx]) {
							gp_regs[0xf] = 1;
						}
						fb[cy][cx] ^= FG;
					}
				}
			}

			draw = true;

			break;
		}
		case 0xe: {
			const u8 low_byte = (lowmid << 4) | low;
			const u8 key_index = gp_regs[highmid];
			// Skip the next instruction if a specified key
			// is pressed
			if (low_byte == 0x9e) {
				if (key_pressed[key_index]) {
					reg_pc += 2;
				}
			}
			// Skip the next instruction if a specified key
			// is not pressed
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
			// Wait for a key to be pressed and store it
			// into a register
			if (low_byte == 0x0a) {
				// Acts as a halt if no key has been
				// pressed
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
			// Increment memory address register by another
			// register
			if (low_byte == 0x1e) {
				reg_i += gp_regs[highmid];
			}
			// Point memory address register to digit sprite
			// from register index
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
			// Load an amount of register from RAM
			if (low_byte == 0x65) {
				for (size_t i = 0; i < highmid; ++i) {
					gp_regs[i] = ram[reg_i + i];
				}
			}
			break;
		}
		}

		// Decrement timers if active
		double current_time =
			sfTime_asMilliseconds(sfClock_getElapsedTime(timers));
		if (current_time - last_time >= 1 / 60.0) {
			if (reg_dt) {
				reg_dt -= 1;
			}
			if (reg_st) {
				reg_st -= 1;
			}
			sfClock_restart(timers);
			last_time = current_time;
		}

		if (!jumped) {
			reg_pc += 2;
		}

		if (draw) {
			sfTexture_updateFromPixels(screen_texture, fb,
						   CHIP8_WIDTH, CHIP8_HEIGHT, 0,
						   0);
		}

		sfRenderWindow_clear(window, sfColor_fromInteger(BG));

		sfRenderWindow_drawSprite(window, screen_drawable, NULL);

		sfRenderWindow_display(window);
	}

	sfSprite_destroy(screen_drawable);
	sfTexture_destroy(screen_texture);
	sfRenderWindow_destroy(window);

	return 0;
}
