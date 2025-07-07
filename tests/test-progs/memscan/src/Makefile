CC:=riscv64-unknown-elf-gcc
OPTIONS:=-Wall

memscan: memscan.c
	$(CC) -g memscan.c -o ../bin/riscv/linux/memscan $(OPTIONS)

clean:
	rm -rf ../bin/riscv/linux/memscan
