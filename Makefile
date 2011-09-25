PROG=	jstatus
NO_MAN=	yes

WARNS=	6
CFLAGS+=-I/usr/local/include
LDADD+=-L/usr/local/lib -lxosd

.include <bsd.prog.mk>
