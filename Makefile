CC=cc
CFLAGS=-ansi -g -std=gnu99 -Wall

LINKER=cc -o
LFLAGS=-Wall -I. -lm

SRCDIR=src
BINDIR=bin
OBJDIR=obj

TARGET=xripd

SOURCES  := $(wildcard $(SRCDIR)/*.c)
INCLUDES := $(wildcard $(SRCDIR)/*.h)
OBJECTS := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
DEPS := $(OBJECTS:%.o=%.d)

$(BINDIR)/$(TARGET): $(OBJECTS)
	    @$(LINKER) $@ $(LFLAGS) $(OBJECTS)
	        @echo "Linking complete!"

-include $(DEPS)

$(OBJECTS): $(OBJDIR)/%.o : $(SRCDIR)/%.c
	    @$(CC) -MMD $(CFLAGS) -c $< -o $@
	    @echo "Compiled "$<" successfully!"


.PHONY : clean
clean:
	@- $(RM) $(OBJECTS) $(DEPS)
	@- $(RM) $(BINDIR)/$(TARGET)
	@echo "Cleanup Complete!"
