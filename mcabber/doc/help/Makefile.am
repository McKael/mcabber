helpdir = $(datadir)/mcabber/help

install-data-local:
	$(INSTALL) -d $(DESTDIR)$(datadir)/mcabber/help
	for lang in en de fr pl uk; do \
		$(INSTALL) -d $(DESTDIR)$(datadir)/mcabber/help/$$lang; \
		$(INSTALL) -m 0644 $$lang/* \
		           $(DESTDIR)$(datadir)/mcabber/help/$$lang; \
	done
