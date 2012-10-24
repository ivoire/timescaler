TEST_SUITES = perl-5.16.1
RUN_TEST_SUITES = $(addprefix run_, $(TEST_SUITES))

all: $(RUN_TEST_SUITES)

run_perl-5.16.1: perl-5.16.1
	(cd $< && ./Configure -de && make && http_proxy='' TIMESCALER_SCALE=2 LD_PRELOAD=$(PWD)/../timescaler.so make test)

perl-5.16.1: perl-5.16.1.tar.gz
	tar xf $<

perl-5.16.1.tar.gz:
	wget http://www.cpan.org/src/5.0/$@

clean:
	rm -rf $(TEST_SUITES)

distclean:
	rm -rf $(TEST_SUITES) $(addsuffix .tar.gz, $(TEST_SUITES))

.PHONY: all clean distclean
