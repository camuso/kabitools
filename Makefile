
hostprogs-y	:= kabi
always		:= $(hostprogs-y)

kabi-objs	:= kabi.o

HOST_EXTRACFLAGS += "-I/usr/include/sparse"
HOSTLOADLIBES_kabi := "-lsparse"

targets += kabi.c

kabi.o : kabi.c
	echo "hostprogs-y: $(hostprogs-y)"
	cc $(HOST_EXTRACFLAGS) -o kabi kabi.c -lsparse


