
FSDB2RTLSO := ../../fsdb2rtl.so

verilog.fsdb: build_pp
	./build_pp +fsdb2rtl+fsdb=stim.fsdb +fsdb2rtl+test_xor1.rx1=test_xor1.rx1 +fsdbfile+$@

stim.fsdb: $(STIM_SOURCES)
	\rm -rf csrc simv*
	vcs -full64 -debug_pp -sverilog $+ -P $(VERDI_HOME)/share/PLI/VCS/LINUXAMD64/novas.tab $(VERDI_HOME)/share/PLI/VCS/LINUXAMD64/pli.a +define+FSDB_DUMP
	./simv +vcs+finish+20000 +fsdbfile+$@

build_pp: $(PP_SOURCES) $(FSDB2RTLSO) stim.fsdb
	\rm -rf csrc build_pp*
	vcs -full64 -debug_pp -sverilog -o build_pp $(PP_SOURCES) -load $(FSDB2RTLSO):Fsdb2RtlElabCb $(VERDI_HOME)/share/FsdbReader/LINUXAMD64/libnffr.so $(VERDI_HOME)/share/FsdbReader/LINUXAMD64/libnsys.so +define+FSDB_DUMP -P $(VERDI_HOME)/share/PLI/VCS/LINUXAMD64/novas.tab $(VERDI_HOME)/share/PLI/VCS/LINUXAMD64/pli.a -lz

$(FSDB2RTLSO):
	$(MAKE) -C../..

clean:
	\rm -rf csrc simv* build_pp* novas_dump.log ucli.key
