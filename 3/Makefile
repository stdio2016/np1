all: client server
client: client.o mypack.o queue.o
client.o: client.c mypack.h queue.h
server: server.o mypack.o queue.o connection.o
server.o: server.c connection.h queue.h mypack.h
mypack.o: mypack.c mypack.h
queue.o: queue.c queue.h
connection.o: connection.c connection.h queue.h mypack.h
clean:
	@rm -f client server
	@rm -f *.o
