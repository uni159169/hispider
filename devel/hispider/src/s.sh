gcc -o tlink -D_DEBUG_LINKTABLE -D_FILE_OFFSET_BITS=64 link.c http.c utils/*.c -I utils/ -DHAVE_PTHREAD -lpthread -lz -D_DEBUG && ./tlink www.sina.com.cn / 64
