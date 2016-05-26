ramdisk: ramdisk.c hashmap.c hashmap.h
	gcc -g -Wall -w ramdisk.c hashmap.c hashmap.h `pkg-config fuse --cflags --libs` -o ramdisk
