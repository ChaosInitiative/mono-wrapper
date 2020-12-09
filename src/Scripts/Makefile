# Builds a simple assembly
SRCS=$(wildcard *.cs)
MCS?=mcs

all:
	$(MCS) -target:library -out:"$(OUTDIR)/TestAssembly.dll" -o+ -langversion:6 -debug+ $(SRCS)

clean:
	rm "$(OUTDIR)/TestAssembly.dll"