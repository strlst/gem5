#CC:=riscv64-unknown-elf-gcc
CC:=riscv64-linux-gnu-gcc
OPTIONS:=-Wall

matmul: matmul.c
	$(CC) -g matmul.c -o ../bin/riscv/linux/matmul $(OPTIONS)

clean:
	rm -rf ../bin/riscv/linux/matmul
