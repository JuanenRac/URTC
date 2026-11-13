# Changes for the next board revision

Notes for the next revision of the board. They need a new board, so no firmware change covers them.

1. **TMC2209 ENN apart from the laser interlock.** The enable pin of the stepper driver (ENN) and the laser interlock should be on separate pins of the microcontroller, so that releasing one cannot enable or disable the other.
2. **Pull-downs on the MOSFET gates.** Every MOSFET gate that a microcontroller pin drives should have a pull-down resistor, so that the output stays off while the pin is floating (reset, bootloader, flashing).
