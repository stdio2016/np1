all: client server
client: client.o mypack.o
client.o: client.c mypack.h
server: server.o mypack.o
server.o: server.c mypack.h
mypack.o: mypack.c mypack.h
clean:
	@rm -f client server
	@rm -f *.o
