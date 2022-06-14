Chrome EC I2C Tunnel Driver

* NOTE: This driver requires the https://github.com/coolstar/crosecbus driver to be present, as well as the correct ACPI tables set up by coreboot

Implements Windows's SPB protocol so most existing I2C drivers should attach and work as child devices off this driver

Tested operations:
* Connect
* Read
* Write

Implemented but untested:
* Sequence

Do note that EC I2C tunnelling is going to be slower than a proper I2C bus. I2C drivers attaching off this should minimize the number of transactions if possible

Requires Windows 10 1607 or higher (Uses ACPI `_DSD` method)

Tested with rt5682 driver on HP Chromebook 14b (Ryzen 3 3250C)