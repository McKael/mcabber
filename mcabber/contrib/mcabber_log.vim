" Vim syntax file
" Language:	MCabber log file
" Maintainer:	Mikael BERTHE <mikael.berthe@lilotux.net>
" URL:		Included in mcabber source package <http://mcabber.com>
" Last Change:	2010-04-01

" Place this file as ~/.vim/syntax/mcabber_log.vim
" and add the following line to ~/.vimrc
"
" au BufRead  */.mcabber/histo/* setfiletype mcabber_log
"
" Logfile format:
" TT YYYYmmddTHH:MM:SSZ nnn Text (this line and the nnn following lines)
" TT is the data type
" 'YYYYmmddTHH:MM:SSZ' is a timestamp
"
" XXX Please help me to improve this syntax script!

if exists("b:current_syntax")
    finish
endif

syn cluster mcabberlogStatEntry contains=mcabberlogStatus,mcabberlogDate
syn cluster mcabberlogMsgEntry contains=mcabberlogMsgIn,mcabberlogMsgOut,mcabberlogDate

syn region mcabberlogStatusLine
    \ start="^S[OFDNAI_] \d\{8\}T\d\d:\d\d:\d\dZ \(\d\{3\}\) "
    \ end="\(\_^[MS][RSIOFDNAI_] \d\{8}T.\{8}Z \d\{3} \|\%$\)\@="
    \ contains=mcabberlogStatus,mcabberlogDate,mcabberlognlines
syn region mcabberlogMessageLineInfo
    \ start="^MI \d\{8\}T\d\d:\d\d:\d\dZ \(\d\{3\}\) "
    \ end="\(\_^[MS][RSIOFDNAI_] \d\{8}T.\{8}Z \d\{3} \|\%$\)\@="
    \ contains=mcabberlogMsgInfo,mcabberlogDate,mcabberlognlines
syn region mcabberlogMessageLineIn
    \ start="^MR \d\{8\}T\d\d:\d\d:\d\dZ \(\d\{3\}\) "
    \ end="\(\_^[MS][RSIOFDNAI_] \d\{8}T.\{8}Z \d\{3} \|\%$\)\@="
    \ contains=mcabberlogMsgIn,mcabberlogDate,mcabberlognlines

syn region mcabberlogMessageLineOut
    \ start="^MS \d\{8\}T\d\d:\d\d:\d\dZ \(\d\{3\}\) "
    \ end="\(\_^[MS][RSIOFDNAI_] \d\{8}T.\{8}Z \d\{3} \|\%$\)\@="
    \ contains=mcabberlogMsgOut,mcabberlogDate,mcabberlognlines

syn match mcabberlogDate /\d\{8\}T\d\d:\d\d:\d\dZ/ contained contains=mcabberlogDateChar nextgroup=mcabberlognlines
syn match mcabberlogDateChar /[TZ]/ contained

syn match mcabberlogStatus /^S[OFDNAI_]/ contained skipwhite nextgroup=@mcabberlogStatusLine
syn match mcabberlogMsgIn /^MR/ contained skipwhite nextgroup=@mcabberlogMessageLine
syn match mcabberlogMsgOut /^MS/ contained skipwhite nextgroup=@mcabberlogMessageLine
syn match mcabberlogMsgInfo /^MI/ contained skipwhite nextgroup=@mcabberlogMessageLine

syn match mcabberlognlines /\<\d\{3\}\>/ contained


command -nargs=+ HiLink hi def link <args>

HiLink mcabberlogStatus     PreProc

HiLink mcabberlogMessageLineIn      Keyword
HiLink mcabberlogMsgIn              Keyword

HiLink mcabberlogMessageLineOut     Function
HiLink mcabberlogMsgOut             Function

HiLink mcabberlogMsgInfo            String
HiLink mcabberlogMessageLineInfo    String

HiLink mcabberlogDate       SpecialChar
HiLink mcabberlogDateChar   Normal

HiLink mcabberlognlines     Normal

HiLink mcabberlogStatusLine Comment

delcommand HiLink

let b:current_syntax = "mcabber_log"
