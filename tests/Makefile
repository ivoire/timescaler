TEST_SUITES = perl-5.16.1 coreutils-8.21
RUN_TEST_SUITES = $(addprefix run_, $(TEST_SUITES))

check: $(RUN_TEST_SUITES)

run_perl-5.16.1: perl-5.16.1
	(cd $< && ./Configure -de && make && http_proxy='' TIMESCALER_SCALE=2 LD_PRELOAD=`pwd`/../../timescaler.so make test)

perl-5.16.1: perl-5.16.1.tar.gz
	tar xf $<

perl-5.16.1.tar.gz:
	wget http://www.cpan.org/src/5.0/$@

run_coreutils-8.21: coreutils-8.21
	(cd $< && ./configure && make && TIMESCALER_SCALE=2 LD_PRELOAD=`pwd`/../../timescaler.so RUN_VERY_EXPENSIVE_TESTS=yes make check)

coreutils-8.21: coreutils-8.21.tar.xz
	tar xf $<

coreutils-8.21.tar.xz:
	wget http://ftp.gnu.org/gnu/coreutils/coreutils-8.21.tar.xz

clean:
	rm -rf $(TEST_SUITES)

distclean:
	rm -rf $(TEST_SUITES) $(addsuffix .tar.gz, $(TEST_SUITES))

.PHONY: all clean distclean
