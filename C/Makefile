CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
LDFLAGS = -lpthread

SERVER = server
CLIENT = client

all: $(SERVER) $(CLIENT)

$(SERVER): server.c common.c common.h parson.c parson.h chart.c chart.h
	$(CC) $(CFLAGS) -o $(SERVER) server.c common.c parson.c chart.c $(LDFLAGS)
$(CLIENT): client.c common.c common.h parson.c parson.h 
	$(CC) $(CFLAGS) -o $(CLIENT) client.c common.c parson.c $(LDFLAGS)

static: server.c client.c common.c common.h parson.c parson.h chart.c chart.h
	$(CC) $(CFLAGS) -static -o $(SERVER) server.c common.c parson.c chart.c -static-libgcc -Wl,-Bstatic $(LDFLAGS)
	$(CC) $(CFLAGS) -static -o $(CLIENT) client.c common.c parson.c -static-libgcc -Wl,-Bstatic $(LDFLAGS)

clean:
	rm -f $(SERVER) $(CLIENT)

.PHONY: all clean
