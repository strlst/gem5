OPTIONS:=
GEM5:=build/RISCV/gem5.opt
BINARY:=tests/test-progs/matmul/bin/riscv/linux/matmul
#BINARY:=tests/test-progs/memsec/bin/riscv/linux/secmatmul
#BINARY:=tests/test-progs/memscan/bin/riscv/linux/memscan
GEM5_PIPEVIEW:=--debug-flags=O3PipeView --debug-start=0 --debug-file=trace.out
GEM5_CONFIG:=configs/tlcpu/config.py --l1i-size=1KiB --l1d-size=1KiB --l2-size=2KiB --no-l3 --ooo --num-cores=1 --memsec
GEM5_CONFIG_AES:=--crypto-aes-block-bits=128 --crypto-aes-enc-cycles=80 --crypto-aes-dec-cycles=80 --crypto-aes-enc-ii=20 --crypto-aes-dec-ii=20
GEM5_CONFIG_MAC:=--crypto-mac-cycles=40 --crypto-mac-ii=10
GEM5_CONFIG_CRYPTO:=--crypto-counter-bits=56 --crypto-mac-bits=64 --crypto-packing-factor=8 --metadata-cache-size=16KiB
PIPEVIEW:=util/o3-pipeview.py
PIPEVIEW_CONFIG:=-c 1000 -o m5out/pipeview.out --color m5out/trace.out
DEBUG_FLAGS:=--debug-flags=CryptoCtrl,IntTRB#,O3CPUAll,Cache,DRAMsim3
REMOTE_HOSTNAME:=tlml003
REMOTE_DIR:=~/gem5

run:
	$(GEM5) $(GEM5_CONFIG) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO) $(BINARY)

pipeview:
	$(GEM5) $(GEM5_PIPEVIEW) $(DEBUG_FLAGS) $(GEM5_CONFIG) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO) $(BINARY)
	$(PIPEVIEW) $(PIPEVIEW_CONFIG)

debug:
	$(GEM5) $(DEBUG_FLAGS) $(GEM5_CONFIG) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO) $(BINARY)
	# $(GEM5) $(DEBUG_FLAGS) $(GEM5_CONFIG) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO) $(BINARY)

gdb:
	# $(GEM5_CONFIG) $(BINARY)
	gdb $(GEM5)

build:
	$(DEVELOP_PREFIX_CMD) python3 $$(which scons) $(DEVELOP_LINKER) build/RISCV/gem5.opt -j$$(nproc)

build-dev:
	bear -- python3 $$(which scons) --linker=mold build/RISCV/gem5.opt -j$$(nproc)

remote-run:
	ssh $(REMOTE_HOSTNAME) "cd $(REMOTE_DIR); $(GEM5) $(GEM5_CONFIG)"

remote-debug:
	ssh $(REMOTE_HOSTNAME) "cd $(REMOTE_DIR); $(GEM5) $(DEBUG_FLAGS) $(GEM5_CONFIG)"

remote-build:
	rsync -av --exclude=build,m5out --delete . $(REMOTE_HOSTNAME):$(REMOTE_DIR)/
	ssh $(REMOTE_HOSTNAME) "cd $(REMOTE_DIR); make build"

test-stream:
# TODO: insert target for stream test
	$(GEM5) $(GEM5_CONFIG) --binary=

clean:
	rm -rf m5out build

.PHONY: build debug build build-dev remote-run remote-debug remote-build test-stream clean
