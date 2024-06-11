# ARM Microprocessor Simulator

A virtual interface for simulating the ARMv8 Instruction Set Architecture (ISA).

The ISA's specifications can be found here:

https://developer.arm.com/documentation/ddi0553/latest/

## Features
- 5-stage RISC pipeline model (IF, ID, EX, MEM, WB) with four registers in between each stage
- Control and Data Dependency handling
- Branch prediction supported by a 256-entry Global Pattern History Table (PHT) and a 1024-entry Branch Target Buffer (BTB)
- 4-way set associative LRU Instruction Cache with 64 sets of 32-byte blocks (total size: 8 KB)
- 8-way set associative LRU Data Cache with 256 sets of 32-byte blocks (total size: 64 KB)

## How to run
1. Navigate to the source directory
2. Compile with `make`
3. Run `./sim [inst.txt]`, where `[inst.txt]` is the file of ARM instructions converted to hex code you want to process
4. Run the simulator to completion with `go` or `g`, or run for a specific number of clock cycles with `r [x]`, where `[x]` is the number of clock cycles you want to process
5. View a full list of commands with `?` 
