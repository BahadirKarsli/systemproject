CC = gcc
CFLAGS = -Wall -g `pkg-config --cflags gtk+-3.0 glib-2.0`
LIBS = -lrt -pthread `pkg-config --libs gtk+-3.0 glib-2.0`

all: terminal

terminal: model.o view.o controller.o
	$(CC) -o terminal model.o view.o controller.o $(LIBS)

model.o: model.c model.h
	$(CC) $(CFLAGS) -c model.c

view.o: view.c view.h
	$(CC) $(CFLAGS) -c view.c

controller.o: controller.c controller.h
	$(CC) $(CFLAGS) -c controller.c

clean:
	rm -f *.o terminal