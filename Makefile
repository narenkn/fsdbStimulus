fsdb2rtl.so: fsdb2rtl.cc
	g++ -g -w -shared -std=c++11 -pipe -fPIC $+ -I$(VERDI_HOME)/share/FsdbReader -L$(VERDI_HOME)/share/FsdbReader/LINUXAMD64 -I.. -O -I$(VCS_HOME)/include -I$(BOOST_HOME)/include -L$(BOOST_HOME)/lib -o $@

%.s: %.cc
	g++ -g -w -shared -std=c++11 -pipe -fPIC $+ -O -I$(VERDI_HOME)/include -I$(BOOST_HOME)/include -S -o $@

%.so : %.s
	g++ -o $@ -shared -fPIC -pipe $<
