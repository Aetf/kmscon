TARGET?=kmscon
MODULES?=${TARGET:=.pp.bz2}
SHAREDIR?=/usr/share
SELINUXTYPE?=targeted

all: ${TARGET:=.pp.bz2}

%.pp.bz2: %.pp
	@echo Compressing $^ -\> $@
	bzip2 -9 $^

%.pp: %.te
	make -f ${SHAREDIR}/selinux/devel/Makefile $@

clean:
	rm -f *~  *.tc *.pp *.pp.bz2
	rm -rf tmp *.tar.gz

man: install-policy
	sepolicy manpage --path . --domain ${TARGET}_t

install-policy: all
	semodule -i ${TARGET}.pp.bz2

install: all
	install -D -m 644 ${TARGET}.pp.bz2 ${DESTDIR}${SHAREDIR}/selinux/packages/${SELINUXTYPE}/${TARGET}.pp.bz2
