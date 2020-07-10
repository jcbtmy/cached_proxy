README.txt For cache_proxy.c

Complie:
	gcc cache_proxy.c -o server -pthread

Usage:
	./server <port> <time_to_live>

Description:

	Simple cached proxy server
	==========================
	-<port> is port to connect to client
	-<time_to_live> is expire time in seconds of cache page
	-must include a blacklist.txt
	

