objs := disk.o fs.o
CC = gcc
CFLAGS  = -g -Wall

ifneq ($(V),1)
Q=@
endif

deps := $(patsubst %.o,%.d,$(objs)) 
-include $(deps)
DEPFLAGS = -MMD -MF $(@:.o=.d)

libfs.a: $(objs)
	@echo "AR $@"
	$(Q)ar rcs $@ $^
	
%.o:%.c 	
	@echo "CC $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $< $(DEPFLAGS)
	
clean:
	@echo "clean"
	$(Q)rm -f $(objs) libfs.a $(deps)