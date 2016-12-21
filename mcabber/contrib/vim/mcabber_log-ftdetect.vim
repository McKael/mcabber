"
" Save this file in your ~/.vim/ftdetect/ folder

function! MCabber_log_ftdetect()
    if getline(1) =~ '^\u. \d\{8}T\d\d:\d\d:\d\dZ \d\{3} '
      setlocal filetype=mcabber_log
    endif
endfunction

autocmd BufRead */*mcabber/histo/* call MCabber_log_ftdetect()
