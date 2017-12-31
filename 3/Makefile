all: client server
client: client.c
	gcc -o client client.c
server: server.o mypack.o
server.o: server.c mypack.h
mypack.o: mypack.c mypack.h
clean:
	@rm -f client server
	@rm -f *.o
