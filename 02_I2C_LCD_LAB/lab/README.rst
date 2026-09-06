.. zephyr:code-sample:: i2c-pcf8574-hd44780-lcd
   :name: I2C character LCD (PCF8574 + HD44780)
   :relevant-api: i2c_interface

   Drive a 16x2 HD44780 character LCD through a PCF8574/PCF8574A I2C
   GPIO-expander backpack, using raw I2C writes instead of a Zephyr
   display driver.

Overview
********

This sample implements a minimal "LiquidCrystal_I2C"-style driver for
the very common PCF8574 I2C-to-parallel backpack boards sold together
with HD44780-compatible 16x2 character LCDs ("LCM1602 IIC" modules).

The backpack exposes the LCD's 4-bit parallel interface (RS, RW, EN,
4 data lines) plus a backlight transistor through the 8 I/O pins of a
PCF8574 (or PCF8574A) I2C GPIO expander. This sample talks to the
expander directly with :c:func:`i2c_write`, bit-banging the HD44780's
4-bit initialization and write protocol, rather than going through
Zephyr's Display/CFB subsystem.

At startup the sample scans the I2C bus for both of the addresses
these backpacks commonly ship at (0x27 for a PCF8574, 0x3F for a
PCF8574A) and uses whichever one responds.

Requirements
************

* A Synaptics SR110 board (``sr100_rdk/sr100/m55``)
* A 16x2 HD44780-compatible character LCD with a PCF8574/PCF8574A I2C
  backpack

Wiring
******

* Backpack VCC -> see the power/signal-level note in the lab's Korean
  doc before choosing 3.3V or 5V
* Backpack GND -> board GND
* Backpack SDA -> board I2C0 SDA (pin group ``i2c0_ms_sda``)
* Backpack SCL -> board I2C0 SCL (pin group ``i2c0_ms_scl``)

Building and Running
*********************

.. zephyr-app-commands::
   :zephyr-app: samples/boards/sr100_rdk/i2c_lcd_lab
   :board: sr100_rdk/sr100/m55
   :goals: build flash

Sample Output
=============

.. code-block:: console

   I2C LCD (PCF8574 + HD44780) lab starting
   Scanning I2C0 bus (0x08-0x77)...
     found device at 0x3f
   Using LCD backpack address 0x3f
   LCD initialized and "Hello World!" / "SR110 Zephyr" written

The LCD itself should show ``Hello World!`` on the first line and
``SR110 Zephyr`` on the second.
