# Top-level Makefile for Buildroot external tree
BR2_EXTERNAL ?= $(PWD)/br-external

all:
	make -C buildroot BR2_EXTERNAL=$(BR2_EXTERNAL)
