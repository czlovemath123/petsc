IPETSCDIR = .

CFLAGS   = $(OPT) -I$(IPETSCDIR)/include -I.. -I$(IPETSCDIR) $(CONF)
SOURCEC  =
SOURCEF  =
WSOURCEC = 
SOURCEH  = 
OBJSC    =
WOBJS    = 
OBJSF    =
LIBBASE  = libpetscvec
LINCLUDE = $(SOURCEH)
DIRS     = src include pinclude

include $(IPETSCDIR)/bmake/$(PETSC_ARCH)/$(PETSC_ARCH)

all: chkpetsc_dir
	-@if [ ! -d $(LDIR) ]; then \
          echo $(LDIR) ; mkdir -p $(LDIR) ; fi
	-$(RM) -f $(LDIR)/*.a
	-@$(OMAKE) BOPT=$(BOPT) PARCH=$(PETSC_ARCH) COMPLEX=$(COMPLEX) \
           ACTION=libfast  tree 
	$(RANLIB) $(LDIR)/*.a

ranlib:
	$(RANLIB) $(LDIR)/*.a

deletelibs:
	-$(RM) -f $(LDIR)/*.a $(LDIR)/complex/*

deletemanpages:
	$(RM) -f $(PETSC_DIR)/Keywords $(PETSC_DIR)/docs/man/man*/*

deletewwwpages:
	$(RM) -f $(PETSC_DIR)/docs/www/man*/* $(PETSC_DIR)/docs/www/www.cit

deletelatexpages:
	$(RM) -f $(PETSC_DIR)/docs/tex/rsum/*sum*.tex

#  to access the tags in emacs type esc-x visit-tags-table 
#  then esc . to find a function
etags:
	$(RM) -f TAGS
	etags -f TAGS    src/*/impls/*/*.h src/*/impls/*/*/*.h src/*/examples/*.c
	etags -a -f TAGS src/*/*.h src/*/*.c src/*/src/*.c src/*/impls/*/*.c 
	etags -a -f TAGS src/*/impls/*/*/*.c src/*/utils/*.c
	etags -a -f TAGS docs/tex/manual.tex src/sys/error/*.c
	etags -a -f TAGS include/*.h pinclude/*.h
	etags -a -f TAGS src/*/impls/*.c src/sys/*.c
	etags -a -f TAGS makefile src/*/src/makefile src/makefile 
	etags -a -f TAGS src/*/impls/makefile src/*/impls/*/makefile
	etags -a -f TAGS bmake/common 
	chmod g+w TAGS

runexamples:
