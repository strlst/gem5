OPTIONS:=
GEM5:=build/RISCV/gem5.opt
GEM5_PIPEVIEW:=--debug-flags=O3PipeView --debug-start=0 --debug-file=trace.out
GEM5_CONFIG:=configs/tlcpu/config.py --l1i-size=1KiB --l1d-size=1KiB --l2-size=2KiB --memsec --ooo --num-cores=1
PIPEVIEW:=util/o3-pipeview.py
PIPEVIEW_CONFIG:=-c 1000 -o m5out/pipeview.out --color m5out/trace.out
DEBUG_FLAGS:=--debug-flags=DRAMsim3,CryptoCtrl
REMOTE_HOSTNAME:=tlml003
REMOTE_DIR:=~/gem5

run:
	$(GEM5) $(GEM5_CONFIG)

pipeview:
	$(GEM5) $(GEM5_PIPEVIEW) $(DEBUG_FLAGS) $(GEM5_CONFIG)
	$(PIPEVIEW) $(PIPEVIEW_CONFIG)

debug:
	$(GEM5) $(DEBUG_FLAGS) $(GEM5_CONFIG)

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
