filesys: filesys.c filesys.h
	cc -o filesys -g filesys.c filesys.h
clean:
	rm filesys
