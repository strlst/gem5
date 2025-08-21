# gem5 flags
GEM5:=build/RISCV/gem5.opt
GEM5_PIPEVIEW:=--debug-flags=O3PipeView --debug-start=0 --debug-file=trace.out
GEM5_CONFIG:=configs/tlcpu/simple/simple.py --num-cores=1 --ooo --memsec
# sysroot is a simple riscv64 linux kernel build generated using buildroot,
# this is only needed for dynamically linked executables requiring an
# interpreter
GEM5_CONFIG_SE:=--interp-dir benchmark/sysroot --redirects /lib=benchmark/sysroot/lib --redirects /lib64=benchmark/sysroot/lib64 --redirects /usr/lib=benchmark/sysroot/usr/lib --redirects /usr/lib64=benchmark/sysroot/usr/lib64
GEM5_CONFIG_FULL:=configs/tlcpu/full.py --num-cores=1 --ooo --memsec
GEM5_CONFIG_CACHE:=--l1i-size=1KiB --l1d-size=1KiB --l2-size=2KiB --no-l3
GEM5_CONFIG_AES:=--crypto-aes-block-bits=128 --crypto-aes-enc-cycles=80 --crypto-aes-dec-cycles=80 --crypto-aes-enc-ii=20 --crypto-aes-dec-ii=20
GEM5_CONFIG_MAC:=--crypto-mac-cycles=40 --crypto-mac-ii=10
GEM5_CONFIG_CRYPTO:=--crypto-counter-bits=56 --crypto-mac-bits=64 --crypto-packing-factor=8 --metadata-cache-size=16KiB

# running flags
# binary is used for bare metal tests
BINARY:=tests/test-progs/matmul/bin/riscv/linux/matmul
DEBUG_FLAGS:=CryptoCtrl,IntTRB,Vma,SyscallVerbose,DRAMsim3#,O3CPUAll,Cache,DRAMsim3

# tracediff flags
GEM5_TRACEDIFF=util/tracediff
GEM5_TRACEDIFF_DEBUG:=--debug-flags=Exec
GEM5_TRACEDIFF_CONFIG:=configs/tlcpu/simple/simple.py --num-cores=1 --ooo '--memsec|--no-memsec'

# pipeview
PIPEVIEW:=util/o3-pipeview.py
PIPEVIEW_CONFIG:=-c 1000 -o m5out/pipeview.out --color m5out/trace.out

# ssh deployment flags
REMOTE_HOSTNAME:=tlml003
REMOTE_DIR:=~/gem5

run:
	$(GEM5) $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO) --cmd $(BINARY)

spec:
	$(GEM5) -d $(SPECOUTDIR) $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO) --cmd $(SPECCMD) --options "$(SPECOPTIONS)"

linux:
	$(GEM5) $(GEM5_CONFIG_FULL) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO)

tracediff:
	$(GEM5_TRACEDIFF) $(GEM5) $(GEM5_TRACEDIFF_DEBUG) $(GEM5_TRACEDIFF_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO) --cmd $(BINARY)

debug:
	$(GEM5) --debug-flags=$(DEBUG_FLAGS) $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO) --cmd $(BINARY)
	# $(GEM5) --debug-flags=$(DEBUG_FLAGS) $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO) --cmd $(BINARY)

debug-help:
	$(GEM5) --debug-help

debug-branch:
	$(GEM5) --debug-flags=Branch $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO) --cmd $(BINARY)

debug-exec:
	$(GEM5) --debug-flags=Exec $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO) --cmd $(BINARY)

pipeview:
	$(GEM5) $(GEM5_PIPEVIEW) --debug-flags=$(DEBUG_FLAGS) $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO) --cmd $(BINARY)
	$(PIPEVIEW) $(PIPEVIEW_CONFIG)

gdb:
	# $(GEM5) --debug-flags=$(DEBUG_FLAGS) $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AES) $(GEM5_CONFIG_MAC) $(GEM5_CONFIG_CRYPTO) --cmd $(BINARY)
	gdb $(GEM5)

build:
	$(DEVELOP_PREFIX_CMD) python3 $$(which scons) $(DEVELOP_LINKER) build/RISCV/gem5.opt -j$$(nproc)

build-dev:
	bear -- python3 $$(which scons) --linker=mold build/RISCV/gem5.opt -j$$(nproc)

remote-run:
	ssh $(REMOTE_HOSTNAME) "cd $(REMOTE_DIR); $(GEM5) $(GEM5_CONFIG)"

remote-debug:
	ssh $(REMOTE_HOSTNAME) "cd $(REMOTE_DIR); $(GEM5) --debug-flags=$(DEBUG_FLAGS) $(GEM5_CONFIG)"

remote-build:
	rsync -av --exclude=build,m5out --delete . $(REMOTE_HOSTNAME):$(REMOTE_DIR)/
	ssh $(REMOTE_HOSTNAME) "cd $(REMOTE_DIR); make build"

clean:
	rm -rf m5out build

.PHONY: run linux build debug build build-dev remote-run remote-debug remote-build test-stream clean
