
guide:
	cd $(srcdir) && \
	pdflatex -output-directory=$(abs_top_builddir)/doc/guide guide
	BIBINPUTS=$(srcdir) bibtex guide
	cd $(srcdir) && \
	pdflatex -output-directory=$(abs_top_builddir)/doc/guide guide && \
	pdflatex -output-directory=$(abs_top_builddir)/doc/guide guide

clean-guide:
	rm -f *.aux *.blg *.dvi *.toc *.out *.bbl *.log *.pdf
