PYTHON = /usr/bin/python

.PHONY : test 

build: netuse.c
	(cd ..; $(PYTHON) setup.py build)

test: build
	$(PYTHON) tests.py

