OPTIONS:=
DEBUG:=--debug-flags=DRAMsim3
CONFIG:=configs/tlcpu/config.py --l1i-size=1KiB --l1d-size=1KiB --l2-size=2KiB
GEM5:=./build/RISCV/gem5.opt

run:
	$(GEM5) $(CONFIG)

debug:
	$(GEM5) $(DEBUG) $(CONFIG)

clean:
	rm -rf m5out

.PHONY: run
