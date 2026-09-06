/*
 * I2C Bus Scanner (Zephyr, Synaptics SR110 / sr100_rdk/sr100/m55)
 *
 * Runs a one-shot scan of I2C0 in a dedicated thread at boot and
 * prints an `i2cdetect`-style grid. Useful for checking what address
 * a newly-attached sensor or display module shows up at.
 *
 * Probe method: a 1-BYTE i2c_read() per address - the OPPOSITE choice
 * from the original ESP32-S3 version of this lab.
 *
 * The ESP32-S3 version deliberately used a zero-length i2c_write()
 * instead, to work around Zephyr issue #45008 ("esp32: i2c_read()
 * error was returned successfully at the bus nack") on ESP32's I2C
 * driver. On SR110's snps,designware-i2c driver the situation is
 * reversed and confirmed on real hardware: a zero-length (or 1-byte
 * dummy) i2c_write() probe fails to detect devices that are genuinely
 * present on the bus, while i2c_read() correctly reports NACKs and
 * finds them. If you port this scanner to yet another SoC, re-verify
 * which probe style actually works there - don't assume either one
 * transfers over.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <stdint.h>
#include <stdbool.h>

#define I2C0_NODE DT_NODELABEL(i2c0)

#define I2C_SCAN_ADDR_MIN 0x08U   /* addresses below this are reserved */
#define I2C_SCAN_ADDR_MAX 0x77U   /* addresses above this are reserved */

#define SCAN_THREAD_STACK_SIZE 2048
#define SCAN_THREAD_PRIORITY   5

/* Probe a single 7-bit address with a 1-byte read (confirmed reliable
 * on this platform's I2C driver - see the file header comment).
 */
static bool i2c_probe_addr(const struct device *bus, uint8_t addr)
{
	uint8_t dummy;
	int ret = i2c_read(bus, &dummy, 1, addr);

	return (ret == 0);
}

static void scan_bus(const struct device *bus, const char *bus_name)
{
	if (bus == NULL || !device_is_ready(bus)) {
		printk("[%s] device not ready - check devicetree status/overlay\n",
		       bus_name);
		return;
	}

	printk("\nScanning %s...\n", bus_name);
	printk("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

	int found = 0;

	for (uint8_t row = 0; row <= 0x70; row += 0x10) {
		printk("%02x: ", row);

		for (uint8_t col = 0; col < 16; col++) {
			uint8_t addr = row + col;

			if (addr < I2C_SCAN_ADDR_MIN || addr > I2C_SCAN_ADDR_MAX) {
				printk("   ");
				continue;
			}

			if (i2c_probe_addr(bus, addr)) {
				printk("%02x ", addr);
				found++;
			} else {
				printk("-- ");
			}
		}
		printk("\n");
	}

	printk("Scan complete on %s: %d device(s) found\n", bus_name, found);
}

/* One-shot scan thread: runs once at boot, then exits. */
static void scan_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct device *i2c0 = DEVICE_DT_GET(I2C0_NODE);

	printk("\n=== I2C Bus Scanner (SR110) ===\n");
	scan_bus(i2c0, "I2C0");

	/* If you've also enabled a second I2C controller in your overlay,
	 * add it the same way, e.g.:
	 *
	 *   const struct device *i2c1 = DEVICE_DT_GET(DT_NODELABEL(i2c1));
	 *   scan_bus(i2c1, "I2C1");
	 */
}

K_THREAD_DEFINE(scan_tid, SCAN_THREAD_STACK_SIZE, scan_thread_entry,
		 NULL, NULL, NULL, SCAN_THREAD_PRIORITY, 0, 0);

int main(void)
{
	/* All work happens in scan_tid, defined above via K_THREAD_DEFINE.
	 * Nothing left for the main thread to do.
	 */
	return 0;
}
