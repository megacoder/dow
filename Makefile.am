SUBDIRS=src

EXTRA_DIST	=						\
	AUTHORS							\
	COPYING							\
	ChangeLog						\
	INSTALL							\
	LICENSE							\
	NEWS							\
	README.md						\
	dow.1.in

README:	README.md
	markdown2 $< | lynx -dump -stdin >$@
