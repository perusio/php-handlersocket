
srcdir=`pwd`
PHP_EXECUTABLE=/usr/bin/php

all:
	$(MAKE) test

test:
	TEST_PHP_EXECUTABLE=$(PHP_EXECUTABLE) \
	TEST_PHP_SRCDIR=$(srcdir) \
	$(PHP_EXECUTABLE) -n -q $(srcdir)/common/run-tests.php;
