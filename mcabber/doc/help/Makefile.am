helpdir = $(datadir)/mcabber/help

install-data-local:
	$(INSTALL) -d $(DESTDIR)$(datadir)/mcabber/help
	for lang in en cs de fr it nl pl ru uk; do \
		$(INSTALL) -d $(DESTDIR)$(datadir)/mcabber/help/$$lang; \
		$(INSTALL) -m 0644 $(srcdir)/$$lang/* \
		           $(DESTDIR)$(datadir)/mcabber/help/$$lang; \
	done
