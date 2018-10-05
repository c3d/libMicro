#
# CDDL HEADER START
#
# The contents of this file are subject to the terms
# of the Common Development and Distribution License
# (the "License").  You may not use this file except
# in compliance with the License.
#
# You can obtain a copy of the license at
# src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing
# permissions and limitations under the License.
#
# When distributing Covered Code, include this CDDL
# HEADER in each file and include the License file at
# usr/src/OPENSOLARIS.LICENSE.  If applicable,
# add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your
# own identifying information: Portions Copyright [yyyy]
# [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Modifications by Red Hat, Inc.
#

include Makefile.benchmarks

BINS=		$(ALL:%=bin/%) bin/tattle bin/pm_qos

TARBALL_CONTENTS = 	\
	Makefile.benchmarks \
	Makefile.SunOS 	\
	Makefile.Linux 	\
	Makefile.Aix 	\
	Makefile.com 	\
	Makefile	\
	$(ALL:%=%.c)	\
	elided.c	\
	exec_bin.c	\
	libmicro.c	\
	libmicro_main.c	\
	libmicro.h	\
	recurse2.c	\
	benchmark_finibatch.c 	\
	benchmark_initbatch.c	\
	benchmark_optswitch.c	\
	benchmark_fini.c	\
	benchmark_init.c	\
	benchmark_result.c	\
	benchmark_finirun.c	\
	benchmark_initrun.c	\
	benchmark_initworker.c	\
	benchmark_finiworker.c	\
	bench.sh	\
	mk_tarball	\
	multiview.sh	\
	multiview.awk	\
	numactl.awk	\
	OPENSOLARIS.LICENSE	\
	tattle.c	\
	pm_qos.c	\
	wrapper.sh	\
	README

default $(ALL) run cstyle lint tattle pm_qos: $(BINS) bench multiview wrapper
	@mkdir -p bin-`uname -m`; cd bin-`uname -m`; MACH=`uname -m` $(MAKE) -f ../Makefile.`uname -s` UNAME_RELEASE=`uname -r | sed 's/\./_/g'` $@

bench: bench.sh suites
	cp bench.sh bench
	chmod +x bench

.PHONY:	suites
suites:
	$(MAKE) -C suites

multiview: multiview.sh
	cp multiview.sh multiview
	chmod +x multiview

wrapper: wrapper.sh
	cp wrapper.sh wrapper
	chmod +x wrapper

clean:
	$(MAKE) -C suites clean
	rm -rf bin bin-* wrapper multiview bench *~ cscope.*

bin:	
	@mkdir -p bin

$(BINS): bin wrapper
	@ln -sf ../wrapper $@

libMicro.tar:	FORCE
	@chmod +x ./mk_tarball
	@./mk_tarball $(TARBALL_CONTENTS)

$(ALL): config.h
config.h: recorder/config.h
	@cp $< $@

recorder/config.h:
	@(cd recorder && make)

FORCE:

