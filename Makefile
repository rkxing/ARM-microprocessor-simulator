sim: shell.c pipe.c bp.c cache.c utils.c
	@gcc -g -O2 $^ -o $@

.PHONY: clean
clean:
	rm -rf *.o *~ sim
