all: client server
client: client.c
	gcc -o client client.c
server: server.c mybuff.c mybuff.h
	gcc -o server server.c mybuff.c -DBETTER_PRINT
clean:
	@rm -f client server
