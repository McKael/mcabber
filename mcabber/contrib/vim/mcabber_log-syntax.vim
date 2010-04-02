" Vim syntax file
" Language:	MCabber log file
" Maintainer:	Mikael BERTHE <mikael.berthe@lilotux.net>
" URL:		Included in mcabber source package <http://mcabber.com>
" Last Change:	2010-04-02

" Save this file as ~/.vim/syntax/mcabber_log.vim
" (and copy the ftdetect file as well)
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

" All lines (except text continuation lines) contain the date and nnn
syn cluster mcabberlogEntry contains=mcabberlogDate,mcabberlognlines

syn region mcabberlogStatusLine
    \ start="^S[OFDNAI_] \d\{8\}T\d\d:\d\d:\d\dZ \d\d\d "
    \ end="\(\_^[MS][RSIOFDNAI_] \d\{8}T.\{8}Z \d\d\d \|\%$\)\@="
    \ contains=mcabberlogStatus,@mcabberlogEntry

syn region mcabberlogMessageLineInfo
    \ start="^MI \d\{8\}T\d\d:\d\d:\d\dZ \d\d\d "
    \ end="\(\_^[MS][RSIOFDNAI_] \d\{8}T.\{8}Z \d\d\d \|\%$\)\@="
    \ contains=mcabberlogMsgInfo,@mcabberlogEntry
syn region mcabberlogMessageLineIn
    \ start="^MR \d\{8\}T\d\d:\d\d:\d\dZ \d\d\d "
    \ end="\(\_^[MS][RSIOFDNAI_] \d\{8}T.\{8}Z \d\d\d \|\%$\)\@="
    \ contains=mcabberlogMsgIn,@mcabberlogEntry
syn region mcabberlogMessageLineOut
    \ start="^MS \d\{8\}T\d\d:\d\d:\d\dZ \d\d\d "
    \ end="\(\_^[MS][RSIOFDNAI_] \d\{8}T.\{8}Z \d\d\d \|\%$\)\@="
    \ contains=mcabberlogMsgOut,@mcabberlogEntry

syn match mcabberlogDate "\d\{8\}T\d\d:\d\d:\d\dZ" contained
    \ contains=mcabberlogDateChar nextgroup=mcabberlognlines
syn match mcabberlogDateChar /[TZ]/ contained

syn match mcabberlogStatus "^S[OFDNAI_]"
    \ contained skipwhite nextgroup=@mcabberlogStatusLine
syn match mcabberlogMsgIn "^MR" contained skipwhite
    \ nextgroup=@mcabberlogMessageLine
syn match mcabberlogMsgOut "^MS" contained skipwhite
    \ nextgroup=@mcabberlogMessageLine
syn match mcabberlogMsgInfo "^MI" contained skipwhite
    \ nextgroup=@mcabberlogMessageLine

syn match mcabberlognlines "\<\d\{3\}\>" contained


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
