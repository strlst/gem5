# gem5 flags
GEM5:=build/RISCV/gem5.opt
# set MEMSEC:=no-memsec for no memory security
MEMSEC:=memsec
GEM5_PIPEVIEW:=--debug-flags=O3PipeView --debug-start=0 --debug-file=trace.out
GEM5_CONFIG:=configs/aim/simple/simple.py --num-cores=1 --ooo --$(MEMSEC)
# sysroot is a simple riscv64 linux kernel build generated using buildroot,
# this is only needed for dynamically linked executables requiring an
# interpreter
GEM5_CONFIG_SE:=--interp-dir benchmark/sysroot --redirects /lib=benchmark/sysroot/lib --redirects /lib64=benchmark/sysroot/lib64 --redirects /usr/lib=benchmark/sysroot/usr/lib --redirects /usr/lib64=benchmark/sysroot/usr/lib64
GEM5_CONFIG_SPEC:=--maxinsts 1000000
GEM5_CONFIG_FULL:=configs/aim/full.py --num-cores=1 --ooo --$(MEMSEC)
GEM5_CONFIG_CACHE:=--l1i-size=1KiB --l1d-size=1KiB --l2-size=2KiB --no-l3
GEM5_CONFIG_AIM_SHAPE:=--aim-counter-bits=56 --aim-mac-bits=64 --aim-packing-factor=8 --aim-aes-block-bits=128
GEM5_CONFIG_AIM_PERF:=--aim-aes-units=1 --aim-aes-enc-cycles=80 --aim-aes-dec-cycles=80 --aim-aes-enc-ii=20 --aim-aes-dec-ii=20 --aim-mac-units=1 --aim-mac-cycles=40 --aim-mac-ii=10 --metadata-cache-size=1KiB --metadata-cache-assoc=8 --int-trb-size=1 --no-int-merge-req --no-int-defrag-req --no-int-parallel-dispatch --no-int-similar-dispatch

# running flags
# binary is used for bare metal tests
BINARY:=tests/test-progs/matmul/bin/riscv/linux/matmul
#BINARY:=tests/test-progs/memscan/bin/riscv/linux/memscan
#DEBUG_FLAGS:=AIMCtrl,IntTRB,MACUnit,AESUnit,Vma,SyscallVerbose,DRAMsim3,Cache#,O3CPUAll
DEBUG_FLAGS:=AIMCtrl,IntTRB,MACUnit,AESUnit

# tracediff flags
GEM5_TRACEDIFF=util/tracediff
GEM5_TRACEDIFF_DEBUG:=--debug-flags=Exec
GEM5_TRACEDIFF_CONFIG:=configs/aim/simple/simple.py --num-cores=1 --no-ooo '--memsec|--no-memsec'

# pipeview
PIPEVIEW:=util/o3-pipeview.py
PIPEVIEW_CONFIG:=-c 1000 -o m5out/pipeview.out --color m5out/trace.out

# ssh deployment flags
REMOTE_HOSTNAME:=tlml003
REMOTE_DIR:=~/gem5

run:
	$(GEM5) $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AIM_SHAPE) $(GEM5_CONFIG_AIM_PERF) $(OVERRIDE) --cmd $(BINARY)

spec:
	$(GEM5) -d $(SPECOUTDIR) $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_SPEC) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AIM_SHAPE) $(GEM5_CONFIG_AIM_PERF) --cmd $(SPECCMD) $(SPECOPTIONS) $(SPECSTDIN) $(SPECSTDOUT) $(SPECSTDERR)

linux:
	$(GEM5) $(GEM5_CONFIG_FULL) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AIM_SHAPE) $(GEM5_CONFIG_AIM_PERF)

tracediff:
	$(GEM5_TRACEDIFF) $(GEM5) $(GEM5_TRACEDIFF_DEBUG) $(GEM5_TRACEDIFF_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AIM_SHAPE) $(GEM5_CONFIG_AIM_PERF) --cmd $(BINARY)

debug:
	$(GEM5) --debug-flags=$(DEBUG_FLAGS) $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AIM_SHAPE) $(GEM5_CONFIG_AIM_PERF) $(OVERRIDE) --cmd $(BINARY)
	# $(GEM5) --debug-flags=$(DEBUG_FLAGS) $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AIM_SHAPE) $(GEM5_CONFIG_AIM_PERF) $(OVERRIDE) --cmd $(BINARY)

debug-help:
	$(GEM5) --debug-help

debug-branch:
	$(GEM5) --debug-flags=Branch $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AIM_SHAPE) $(GEM5_CONFIG_AIM_PERF) --cmd $(BINARY)

debug-exec:
	$(GEM5) --debug-flags=Exec $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AIM_SHAPE) $(GEM5_CONFIG_AIM_PERF) --cmd $(BINARY)

pipeview:
	$(GEM5) $(GEM5_PIPEVIEW) --debug-flags=$(DEBUG_FLAGS) $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AIM_SHAPE) $(GEM5_CONFIG_AIM_PERF) --cmd $(BINARY)
	$(PIPEVIEW) $(PIPEVIEW_CONFIG)

gdb:
	# $(GEM5) --debug-flags=$(DEBUG_FLAGS) $(GEM5_CONFIG) $(GEM5_CONFIG_SE) $(GEM5_CONFIG_CACHE) $(GEM5_CONFIG_AIM_SHAPE) $(GEM5_CONFIG_AIM_PERF) --cmd $(BINARY)
	gdb $(GEM5)

build:
	python3 $$(which scons) --linker=mold build/RISCV/gem5.opt -j$$(nproc)

build-dev:
	bear -- python3 $$(which scons) --linker=mold build/RISCV/gem5.opt -j$$(nproc)

remote-run:
	ssh $(REMOTE_HOSTNAME) "cd $(REMOTE_DIR); $(GEM5) $(GEM5_CONFIG)"

remote-debug:
	ssh $(REMOTE_HOSTNAME) "cd $(REMOTE_DIR); $(GEM5) --debug-flags=$(DEBUG_FLAGS) $(GEM5_CONFIG)"

remote-build:
	rsync -av --exclude=build,m5out --delete . $(REMOTE_HOSTNAME):$(REMOTE_DIR)/
	ssh $(REMOTE_HOSTNAME) "cd $(REMOTE_DIR); make build"

benchmark: tidy
	python3 benchmark/gen_run_script.py
	./run-spec-benchmarks.sh

summary:
	python3 benchmark/gen_summary.py

detailed-summary:
	python3 benchmark/gen_summary.py -f memsec_none,memsec_full,memsec_basic -p plots_general
	python3 benchmark/gen_summary.py -f memsec_none,memsec_no_delay,memsec_basic -p plots_pre_optimization
	python3 benchmark/gen_summary.py -f memsec_basic,memsec_buff,memsec_merge,memsec_defrag,memsec_similar -p plots_buffered
	python3 benchmark/gen_summary.py -f memsec_none,memsec_basic,memsec_no_delay,memsec_aes_units -p plots_delay

summaries: summary detailed-summary

measure-int-trb-latencies:
	./benchmark/benchmark_int_trb_latencies.sh
	python3 benchmark/plot_int_trb_bench.py

measure-spec-int-trb-latencies:
	./benchmark/benchmark_spec_int_trb_latencies.sh
	python3 benchmark/plot_int_trb_bench.py

force-clean:
	rm -rf m5out build

tidy:
	rm -rf result plots run-spec-benchmarks.sh

clean:
	@echo -n "Are you sure? [y/N] " && read ans && if [ $${ans:-'N'} = 'y' ]; then make clean; fi


.PHONY: run linux build debug build build-dev remote-run remote-debug remote-build test-stream clean
