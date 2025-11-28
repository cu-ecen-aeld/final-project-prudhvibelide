#!/bin/sh
KERNEL_VER=$(ls ${TARGET_DIR}/lib/modules/)
depmod -a -b ${TARGET_DIR} ${KERNEL_VER}
