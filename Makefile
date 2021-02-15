all : shell 

shell: myshell.c
	gcc -o myshell myshell.c

clean:
	rm -rf myshell