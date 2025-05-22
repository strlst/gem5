OPTIONS:=
GEM5:=build/RISCV/gem5.opt
CONFIG:=configs/tlcpu/config.py --l1i-size=1KiB --l1d-size=1KiB --l2-size=2KiB
DEBUG:=--debug-flags=DRAMsim3,CryptoCtrl

run:
	$(GEM5) $(CONFIG)

build: build-release

build-release:
	python3 $$(which scons) build/RISCV/gem5.opt -j$$(nproc)

build-release-bear:
	bear -- python3 $$(which scons) build/RISCV/gem5.opt -j$$(nproc)

build-debug:
	python3 $$(which scons) --linker=mold build/RISCV/gem5.opt -j$$(nproc)

build-debug-bear:
	bear -- python3 $$(which scons) --linker=mold build/RISCV/gem5.opt -j$$(nproc)

debug:
	$(GEM5) $(DEBUG) $(CONFIG)

stream:
	$(GEM5) $(CONFIG) --binary=

clean:
	rm -rf m5out build


.PHONY: build build-release build-debug build-release-bear build-debug-bear run debug stream clean
