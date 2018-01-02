all: client server
client: client.o mypack.o queue.o connection.o MyHash.o
client.o: client.c mypack.h queue.h connection.h MyHash.h
server: server.o mypack.o queue.o connection.o MyHash.o
server.o: server.c connection.h queue.h mypack.h MyHash.h
mypack.o: mypack.c mypack.h
queue.o: queue.c queue.h
connection.o: connection.c connection.h queue.h mypack.h MyHash.h
MyHash.o: MyHash.c MyHash.h
clean:
	@rm -f client server
	@rm -f *.o
	@rm -f *.tmp *.dat
