### sysroot

To run system-call emulation gem5 simulations, the linker needs to be provided compiled with the appropriate ISA and ABI.
For this project, `riscv64-linux-gnu` is used as a cross compilation toolchain.
By using `buildroot` and compiling a basic Linux kernel with library support for Fortran and C++, the resulting image can be extracted and copied to `sysroot` in this folder.
