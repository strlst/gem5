OPTIONS:=
GEM5:=build/RISCV/gem5.opt
CONFIG:=configs/tlcpu/config.py --l1i-size=1KiB --l1d-size=1KiB --l2-size=2KiB
DEBUG:=--debug-flags=DRAMsim3,CryptoCtrl

run:
	$(GEM5) $(CONFIG)

build:
	python3 $$(which scons) build/RISCV/gem5.opt -j$$(nproc)

build-bear:
	bear -- python3 $$(which scons) build/RISCV/gem5.opt -j$$(nproc)

debug:
	$(GEM5) $(DEBUG) $(CONFIG)

stream:
	$(GEM5) $(CONFIG) --binary=

clean:
	rm -rf m5out

.PHONY: build run debug stream clean
