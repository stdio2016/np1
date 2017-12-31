all: client server
client: client.o mypack.o queue.o
client.o: client.c mypack.h queue.h
server: server.o mypack.o queue.o
server.o: server.c mypack.h queue.h
mypack.o: mypack.c mypack.h
queue.o: queue.c queue.h
clean:
	@rm -f client server
	@rm -f *.o
