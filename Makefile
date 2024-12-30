httpd: httpd.c
	gcc -o httpd -Wall -pedantic -std=c99 -g httpd.c -lpthread