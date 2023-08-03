CFLAGS = -g -I. -O3 -std=c89 -Wall -pedantic

OBJ=mathc/mathc.o sdf.o sdfvm.o

default: demo vmdemo

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

libsdf2d.a: $(OBJ)
	$(AR) rcs $@ $(OBJ)

demo: demo.c libsdf2d.a
	$(CC) $(CFLAGS) $< -o $@ -L. -lsdf2d

vmdemo: vmdemo.c libsdf2d.a
	$(CC) $(CFLAGS) $< -o $@ -L. -lsdf2d

clean:
	$(RM) $(OBJ)
	$(RM) demo
	$(RM) -r demo.dSYM
	$(RM) libsdf2d.a
