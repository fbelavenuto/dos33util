# Makefile

CC = gcc
LD = gcc
CP = cp
RM = rm -f
MD = mkdir

SDIR = src
ODIR = obj
IDIR = inc

CFLAGS = -g -Wall -I$(IDIR)
LDFLAGS = 

_OBJS = dos33util.o utils.o
OBJS = $(addprefix $(ODIR)/, $(_OBJS))

all: $(ODIR) dos33util

dos33util: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(ODIR):
	$(MD) $(ODIR)

.PHONY: clean install

clean:
	$(RM) *.exe dos33util $(ODIR)/*


$(ODIR)/%.o: $(SDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
