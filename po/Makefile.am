localedir = $(datadir)/locale

POFILES = zh_CN.po
MOFILES = zh_CN.gmo
GMSGFMT = msgfmt

all-local: $(MOFILES)
	@for f in $(MOFILES); do \
	  lang=$$(basename $$f .gmo); \
	  dir=locale/$$lang/LC_MESSAGES; \
	  $(MKDIR_P) $$dir; \
	  cp $$f $$dir/$(DOMAIN).mo; \
	done

%.gmo: %.po
	$(GMSGFMT) -c -o $@ $<

install-data-local: $(MOFILES)
	@for f in $(MOFILES); do \
	  lang=$$(basename $$f .gmo); \
	  dir=$(DESTDIR)$(localedir)/$$lang/LC_MESSAGES; \
	  $(MKDIR_P) $$dir; \
	  $(INSTALL_DATA) $$f $$dir/$(DOMAIN).mo; \
	done

uninstall-local:
	@for f in $(MOFILES); do \
	  lang=$$(basename $$f .gmo); \
	  rm -f $(DESTDIR)$(localedir)/$$lang/LC_MESSAGES/$(DOMAIN).mo; \
	done

clean-local:
	rm -f *.gmo
	rm -rf locale

EXTRA_DIST = $(POFILES) LINGUAS POTFILES.in Makevars

DOMAIN = lfspkg
