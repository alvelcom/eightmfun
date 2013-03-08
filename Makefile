CFLAGS  := -std=c99 -Wall -O2

SRC 	:= main.c
ODIR	:= obj
OBJ		:= $(patsubst %.c,$(ODIR)/%.o,$(SRC))

BIN		:= eightmfun


all: $(BIN)

clean:
	$(RM) $(BIN) $(ODIR)/*

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(OBJ): $(ODIR)

$(ODIR):
	@mkdir $@

$(ODIR)/%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: all clean
.SUFFIXES: .c .o

vpath %.c src
vpath %.h src
