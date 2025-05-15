OPTIONS:=

run:
	./build/RISCV/gem5.opt configs/tlcpu/config.py --l1i-size=1KiB --l1d-size=1KiB --l2-size=2KiB

clean:
	rm -rf m5out

.PHONY: run
