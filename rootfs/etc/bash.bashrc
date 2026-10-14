[ -z "$PS1" ] && return
PS1='\w \$ '
HISTSIZE=500
HISTFILESIZE=500
HISTCONTROL=ignoredups
shopt -s histappend checkwinsize
. /etc/shrc
